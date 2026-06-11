/*************** SERVER: ESP32-S2 LOLIN S2 Mini = USB HID + ESP-NOW ***************/
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"

USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;

// ===================== CONFIG MAC =====================
static uint8_t REMOTE_MAC[6] = { 0x44,0x1D,0x64,0xBC,0x1E,0x80 }; // ESP32 remote

// ===================== PINOUT ========================
#ifndef LED_BUILTIN
#define LED_BUILTIN 15  // LOLIN S2 Mini typical
#endif
#define BTN_TOGGLE 39   // eksternal pull-up 10k ke 3V3, tombol ke GND

// ===================== TIMING ========================
const uint32_t HEARTBEAT_PERIOD_MS = 200;  // kirim status ke remote
const uint32_t REMOTE_TIMEOUT_MS   = 1000; // bila tak ada paket
const uint32_t LED_BLINK_FAST_MS   = 100;  // disconnect
const uint32_t LED_BLINK_SLOW_MS   = 500;  // enabled

// ===================== PROTOKOL ======================
struct MouseMsg {
  int16_t dx;
  int16_t dy;
  uint8_t buttons;  // bit0=Left
  uint8_t back;     // 1=Back
  uint8_t seq;
};

struct StatusMsg {
  uint8_t enabled;
  uint8_t reserved[3];
};

// === Gap-fill smoothing ===
static uint8_t  expectedSeq = 0;
static bool     seqInit = false;

static int32_t  smoothResX = 0, smoothResY = 0; // reservoir delta yang akan didistribusi
static uint8_t  smoothLeft = 0;                 // berapa frame lagi micro-step
static const uint8_t MAX_SMOOTH_FRAMES = 6;     // batasi smoothing supaya tidak terlalu panjang
static inline uint8_t seqGap(uint8_t got, uint8_t expect) {
  // selisih modulo 256: berapa banyak frame terlewati
  return (uint8_t)(got - expect); // wrap-around otomatis di uint8
}

static void feedSmoothing(int16_t dx, int16_t dy, uint8_t rxSeq) {
  if (!seqInit) {
    expectedSeq = rxSeq + 1;
    seqInit = true;
    // langsung commit tanpa smoothing pertama kali
    smoothResX += dx; smoothResY += dy;
    smoothLeft = 1;
    return;
  }

  uint8_t gap = seqGap(rxSeq, expectedSeq);
  expectedSeq = rxSeq + 1;

  // tambahkan delta baru ke reservoir
  smoothResX += dx;
  smoothResY += dy;

  // total frame yang akan dipakai untuk membagi delta:
  // minimal 1 (frame saat ini), tambah gap (paket hilang)
  uint8_t frames = 1 + gap;
  if (frames > MAX_SMOOTH_FRAMES) frames = MAX_SMOOTH_FRAMES;

  smoothLeft = frames;
}

static void getSmoothedDelta(int16_t& outDx, int16_t& outDy) {
  if (smoothLeft == 0) {
    outDx = 0; outDy = 0;
    return;
  }
  // bagi rata sisa reservoir
  int32_t stepX = smoothResX / (int32_t)smoothLeft;
  int32_t stepY = smoothResY / (int32_t)smoothLeft;

  // jaga minimal step supaya tidak hilang karena pembulatan
  if (stepX == 0 && smoothResX != 0) stepX = (smoothResX > 0) ? 1 : -1;
  if (stepY == 0 && smoothResY != 0) stepY = (smoothResY > 0) ? 1 : -1;

  outDx = (int16_t)stepX;
  outDy = (int16_t)stepY;

  smoothResX -= stepX;
  smoothResY -= stepY;
  smoothLeft--;
}

// ===================== STATE =========================
volatile uint32_t lastRemoteMs = 0;
bool remoteConnected() {
  return (millis() - lastRemoteMs) < REMOTE_TIMEOUT_MS;
}

volatile bool enabledControl = true;   // toggle via BTN_TOGGLE

// LED pattern
uint32_t ledBlinkMs = 0;
bool ledOn = false;

void driveStatusLED() {
  uint32_t now = millis();

  if (!remoteConnected()) {
    // fast blink
    if (now - ledBlinkMs >= LED_BLINK_FAST_MS) {
      ledBlinkMs = now; ledOn = !ledOn;
      digitalWrite(LED_BUILTIN, ledOn);
    }
    return;
  }

  if (!enabledControl) {
    // solid ON
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    // slow blink
    if (now - ledBlinkMs >= LED_BLINK_SLOW_MS) {
      ledBlinkMs = now; ledOn = !ledOn;
      digitalWrite(LED_BUILTIN, ledOn);
    }
  }
}

// ===================== BTN TOGGLE ====================
const uint32_t BTN_DEBOUNCE_MS = 30;
bool lastBtn = true; // pull-up, aktif low
bool curBtn = true;
uint32_t btnChangeMs = 0;

void pollToggleButton() {
  bool raw = digitalRead(BTN_TOGGLE);
  if (raw != lastBtn) {
    lastBtn = raw;
    btnChangeMs = millis();
  } else if ((millis() - btnChangeMs) > BTN_DEBOUNCE_MS) {
    if (curBtn != raw) {
      curBtn = raw;
      if (curBtn == LOW) {
        enabledControl = !enabledControl;
        // Lepas semua tombol agar tidak ada yang tertahan
        if (!enabledControl) {
        Keyboard.releaseAll();
        Mouse.release();
        }
      }
    }
  }
}

// ===================== ESPNOW ========================
void sendStatus() {
  StatusMsg st{};
  st.enabled = enabledControl ? 1 : 0;
  esp_now_send(REMOTE_MAC, (uint8_t*)&st, sizeof(st));
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // optional: debug
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  lastRemoteMs = millis();

  if (len < (int)sizeof(MouseMsg)) return;
  MouseMsg m;
  memcpy(&m, data, sizeof(m));

  // kirim heartbeat balik
  sendStatus();

  if (!enabledControl) return;

  // --- Terapkan ke HID ---
  // Buttons
  bool leftPressed = (m.buttons & 0x01);

  // Mouse.move(dx, dy, wheel)
  Mouse.move((int8_t)constrain(m.dx, -400, 400),
             (int8_t)constrain(m.dy, -400, 400), 0);

  static bool prevLeft = false;
  if (leftPressed != prevLeft) {
    if (leftPressed) Mouse.press(MOUSE_LEFT);
    else             Mouse.release(MOUSE_LEFT);
    prevLeft = leftPressed;
  }

  // Tombol "Back" (contoh: Alt+Left)
  if (m.back) {
    Keyboard.press(KEY_LEFT_ALT);
    Keyboard.press(KEY_LEFT_ARROW);
    delay(5);
    Keyboard.release(KEY_LEFT_ARROW);
    delay(2);
    Keyboard.release(KEY_LEFT_ALT);
    delay(2);
    Keyboard.releaseAll();
  }
}

// ===================== SETUP =========================
uint32_t lastHbMs = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(BTN_TOGGLE, INPUT_PULLUP); // eksternal pull-up 10k

  Serial.begin(115200);

  // USB HID
  USB.begin();
  Keyboard.begin();
  Mouse.begin();

  // WiFi + ESPNOW
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  // Maksimalkan daya pancar (sekitar 19.5 dBm pada ESP32)
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Aktifkan 802.11 LR PHY (keduanya: pengirim & penerima harus ON)
  WiFi.enableLongRange(true);
  
  //lock CH 11
  esp_err_t err = esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (1) { delay(100); }
  }
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, REMOTE_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Add peer failed");
  }

  lastRemoteMs = 0;
  enabledControl = true; // default enable
}

// ===================== LOOP ==========================
void loop() {
  pollToggleButton();
  driveStatusLED();

  // heartbeat berkala walau tidak ada paket
  if (millis() - lastHbMs >= HEARTBEAT_PERIOD_MS) {
    lastHbMs = millis();
    sendStatus();
  }
}
