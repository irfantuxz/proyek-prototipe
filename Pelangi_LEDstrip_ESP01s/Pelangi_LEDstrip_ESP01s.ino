#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>

// ====================== SETTINGS ======================
#define PIN_PB       0          // Push Button pada GPIO0
#define PIN_LED      2          // ESP-01S: GPIO2
#define JUMLAH_LED   20
#define BRIGHTNESS   80 
#define SPEED_MS     10 
#define STEP_SHIFT   1

// Durasi tekan lama (ms)
#define LONG_PRESS_MS 2000

// ====================== WIFI ======================
#define WIFI_SSID1 "Lab-Elektro.USD"
#define WIFI_PASS1 "kendali2022"
#define WIFI_SSID2 "Wifi Retta"
#define WIFI_PASS2 "kosongan"

const char* ssidList[] = { WIFI_SSID1, WIFI_SSID2 };
const char* passList[] = { WIFI_PASS1, WIFI_PASS2 };
const int ssidCount = 2;

// ====================== OBJECTS & VARS ======================
Adafruit_NeoPixel strip(JUMLAH_LED, PIN_LED, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(80);

enum Mode : uint8_t {
  MODE_RANDOM_PIXELS = 1,
  MODE_RAINBOW       = 2,
  MODE_RW            = 3,
  MODE_GY            = 4,
  MODE_CBP           = 5,
  MODE_SEQ4          = 6
};

volatile uint8_t currentMode = MODE_RANDOM_PIXELS;
volatile bool autoRandom = true;

uint32_t lastRandomPick = 0;
const uint32_t RANDOM_INTERVAL_MS = 15000;
uint32_t lastFrame = 0;
uint8_t offset = 0;

// Button Vars
unsigned long buttonPressStart = 0;
bool lastButtonState = HIGH;

// Mode specific vars
uint32_t seqLast = 0;
uint8_t  seqPhase = 0;
bool     seqBlinkOn = true;
uint8_t  seqShift = 0;
uint32_t rndPixLast = 0;
const uint32_t RNDPIX_INTERVAL_MS = 180;

// ====================== PROTOTYPES ======================
void pickRandomMode();
void resetAnimState();

// ====================== BUTTON LOGIC ======================
void handleButton() {
  bool currentButtonState = digitalRead(PIN_PB);
  uint32_t now = millis();

  // Deteksi Transisi: Ditekan (HIGH ke LOW)
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    buttonPressStart = now;
  }
  
  // Deteksi Transisi: Dilepas (LOW ke HIGH)
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    uint32_t pressDuration = now - buttonPressStart;

    if (pressDuration >= LONG_PRESS_MS) {
      // --- TEKAN LAMA (Toggle AutoRandom) ---
      autoRandom = !autoRandom;
      
      // Feedback Visual
      if (autoRandom) {
        // Biru jika True
        for(int i=0; i<JUMLAH_LED; i++) strip.setPixelColor(i, strip.Color(0,0,255));
      } else {
        // Merah jika False
        for(int i=0; i<JUMLAH_LED; i++) strip.setPixelColor(i, strip.Color(255,0,0));
      }
      strip.show();
      delay(1000); // Tahan warna feedback 1 detik
      
      lastRandomPick = now; // Reset timer agar tidak langsung ganti
    } 
    else if (pressDuration > 50) { // Debounce minimal 50ms
      // --- TEKAN SINGKAT (Langsung Random) ---
      lastRandomPick = now;
      pickRandomMode();
      resetAnimState();
    }
  }

  lastButtonState = currentButtonState;
}

// ====================== UTIL COLOR ======================
static inline uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
  uint16_t x = (uint16_t)a * (uint16_t)(255 - t) + (uint16_t)b * (uint16_t)t;
  return (uint8_t)(x / 255);
}

static inline uint32_t lerpColor(uint32_t c1, uint32_t c2, uint8_t t) {
  uint8_t r1 = (uint8_t)(c1 >> 16);
  uint8_t g1 = (uint8_t)(c1 >>  8);
  uint8_t b1 = (uint8_t)(c1 >>  0);
  uint8_t r2 = (uint8_t)(c2 >> 16);
  uint8_t g2 = (uint8_t)(c2 >>  8);
  uint8_t b2 = (uint8_t)(c2 >>  0);
  return strip.Color(lerp8(r1, r2, t), lerp8(g1, g2, t), lerp8(b1, b2, t));
}

uint32_t wheel(byte pos) {
  pos = 255 - pos;
  if (pos < 85) return strip.Color(255 - pos * 3, 0, pos * 3);
  if (pos < 170) { pos -= 85; return strip.Color(0, pos * 3, 255 - pos * 3); }
  pos -= 170; return strip.Color(pos * 3, 255 - pos * 3, 0);
}

// ====================== RENDERERS ======================
void renderRandomPixels() {
  uint32_t now = millis();
  if (now - rndPixLast < RNDPIX_INTERVAL_MS) return;
  rndPixLast = now;
  for (int i = 0; i < JUMLAH_LED; i++) {
    strip.setPixelColor(i, strip.Color(random(256), random(256), random(256)));
  }
  strip.show();
}

void renderRainbow() {
  for (int i = 0; i < JUMLAH_LED; i++) {
    uint8_t hue = (uint8_t)((i * 256 / JUMLAH_LED + offset) & 0xFF);
    strip.setPixelColor(i, wheel(hue));
  }
  strip.show();
  offset += STEP_SHIFT;
}

void renderGradient(uint32_t cStart, uint32_t cEnd) {
  for (int i = 0; i < JUMLAH_LED; i++) {
    uint8_t p = (uint8_t)((i * 255) / (JUMLAH_LED - 1));
    uint8_t t = (uint8_t)(p + offset);
    strip.setPixelColor(i, lerpColor(cStart, cEnd, t));
  }
  strip.show();
  offset += STEP_SHIFT;
}

void renderSeq4() {
  const uint32_t RED = strip.Color(255, 0, 0), GREEN = strip.Color(0, 255, 0);
  const uint32_t YELLOW = strip.Color(255, 120, 0), BLUE = strip.Color(0, 0, 255), OFF = 0;
  uint32_t now = millis();
  if (seqPhase == 0) { if (now - seqLast >= 600) { seqLast = now; seqPhase = 1; seqBlinkOn = true; } }
  else if (seqPhase == 1) {
    if (now - seqLast >= 120) { seqLast = now; seqBlinkOn = !seqBlinkOn; }
    static uint32_t phase1Start = 0; if (phase1Start == 0) phase1Start = now;
    if (now - phase1Start >= 800) { phase1Start = 0; seqPhase = 2; seqLast = now; }
  } else {
    if (now - seqLast >= 120) { seqLast = now; seqShift = (seqShift + 1) & 0x03;
      static uint8_t shiftCount = 0; if (++shiftCount >= 6) { shiftCount = 0; seqPhase = 0; }
    }
  }
  for (int i = 0; i < JUMLAH_LED; i++) {
    uint8_t idx = (uint8_t)((i + seqShift) & 0x03);
    uint32_t c = OFF;
    if (seqPhase == 0) c = (idx==0)?RED:(idx==2?YELLOW:OFF);
    else if (seqPhase == 1) c = (seqBlinkOn)?((idx==1?GREEN:(idx==3?BLUE:OFF))):OFF;
    else c = (idx==0?RED:(idx==1?GREEN:(idx==2?YELLOW:BLUE)));
    strip.setPixelColor(i, c);
  }
  strip.show();
}

void pickRandomMode() { currentMode = (uint8_t)random(1, 7); }

void renderCurrentMode() {
  switch (currentMode) {
    case MODE_RANDOM_PIXELS: renderRandomPixels(); break;
    case MODE_RAINBOW:       renderRainbow(); break;
    case MODE_RW:            renderGradient(strip.Color(255,0,0), strip.Color(255,255,255)); break;
    case MODE_GY:            renderGradient(strip.Color(0,255,0), strip.Color(255,160,0)); break;
    case MODE_CBP:           renderGradient(strip.Color(0,180,255), strip.Color(160,0,255)); break;
    case MODE_SEQ4:          renderSeq4(); break;
    default: currentMode = MODE_RANDOM_PIXELS; break;
  }
}

// ====================== WEB HANDLERS ======================
void handleRoot() {
  String html = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP Control</title>");
  html += F("<style>body{font-family:Arial;margin:18px}.btn{display:block;width:100%;max-width:400px;padding:12px;margin:8px 0;border:1px solid #333;border-radius:10px;text-decoration:none;color:#111}.on{background:#d8ecff}</style></head><body>");
  html += F("<h2>ESP-01S NeoPixel</h2><p>IP: "); html += WiFi.localIP().toString();
  html += F("<br>Auto Random: "); html += autoRandom ? "ON" : "OFF";
  html += F("</p><a class='btn' href='/set?rand="); html += autoRandom?"0":"1"; html += "'>Toggle Auto Random</a><hr>";
  for (uint8_t m = 1; m <= 6; m++) {
    html += "<a class='btn " + String(m == currentMode ? "on" : "") + "' href='/set?mode=" + String(m) + "'>Mode " + String(m) + "</a>";
  }
  html += F("</body></html>");
  server.send(200, "text/html", html);
}

void resetAnimState() {
  offset = 0; seqPhase = 0; seqShift = 0; seqBlinkOn = true; seqLast = millis(); rndPixLast = 0;
}

void handleSet() {
  if (server.hasArg("rand")) { autoRandom = (server.arg("rand").toInt() != 0); lastRandomPick = millis(); }
  if (server.hasArg("mode")) { int m = server.arg("mode").toInt(); if (m >= 1 && m <= 6) { currentMode = (uint8_t)m; resetAnimState(); } }
  server.sendHeader("Location", "/"); server.send(302, "text/plain", "OK");
}

// ====================== WIFI & SYSTEM ======================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_PB, INPUT_PULLUP);
  randomSeed(ESP.getChipId());
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssidList[0], passList[0]); // Simple connect for brevity
}

void loop() {
  uint32_t now = millis();

  handleButton(); // <--- Cek Push Button

  // Auto-random logic
  if (autoRandom && (now - lastRandomPick >= RANDOM_INTERVAL_MS)) {
    lastRandomPick = now;
    pickRandomMode();
    resetAnimState();
  }

  // Animasi
  if (now - lastFrame >= SPEED_MS) {
    lastFrame = now;
    renderCurrentMode();
  }

  // Web & Services
  if (WiFi.status() == WL_CONNECTED) {
    if (millis() > 5000 && !server.client()) { // Simple check to init server
       static bool serverStarted = false;
       if(!serverStarted){ server.on("/", handleRoot); server.on("/set", handleSet); server.begin(); serverStarted=true; }
    }
    server.handleClient();
    ArduinoOTA.handle();
  }
}