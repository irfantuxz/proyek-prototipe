/* Power Latch Controller — tampilan 4 baris tetap sampai detik 0
   - LATCH_PIN: D7 (kendali latch / BC547 basis)
   - BTN_PIN:   D6 (input dari BC547_level, active LOW)
   - OLED: U8G2 SSD1306 128x64 @ 0x3C (HW I2C)
   - LED_BUILTIN toggled on short press
   - Idle total: IDLE_TOTAL_MS
     - After LATCH_RELEASE_MS -> latch released (but UI stays)
     - Last SHUTDOWN_FINAL_MS seconds -> inputs ignored, UI still 4 lines
     - At 0 -> gracefulShutdown() -> show 3..1 then release latch and block
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// Pins
const uint8_t LATCH_PIN = D7; // controls BC547 latch base via resistor
const uint8_t BTN_PIN   = D6; // button input (active LOW from BC547_level)
const uint8_t LED_PIN   = LED_BUILTIN;

// Timing (ms)
const unsigned long DEBOUNCE_MS        = 50UL;
const unsigned long IDLE_TOTAL_MS      = 30000UL; // keep your chosen total
const unsigned long LATCH_RELEASE_MS   = 5000UL;  // after 5s idle -> release latch
const unsigned long SHUTDOWN_FINAL_MS  = 3000UL;  // last 3s -> inputs ignored

// OLED (HW I2C: SDA=D2, SCL=D1)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Button debounce state
int btnReading = HIGH;
int btnLastStable = HIGH;
unsigned long lastDebounceMillis = 0;

// Activity tracking
unsigned long lastActivityMillis = 0;

// Logical states
bool ledState = false;
const int LED_ON  = LOW;  // NodeMCU built-in LED usually active LOW
const int LED_OFF = HIGH;
bool latchHeld = false;   // true if we currently hold latch (LATCH_PIN HIGH)

// Helper
void applyLed() { digitalWrite(LED_PIN, ledState ? LED_ON : LED_OFF); }
void displayStatus(bool buttonDown, unsigned long remaining_ms);
void gracefulShutdown();

void setup() {
  // Ensure latch ON as early as possible
  pinMode(LATCH_PIN, OUTPUT);
  digitalWrite(LATCH_PIN, HIGH);
  latchHeld = true;

  // Button and LED
  pinMode(BTN_PIN, INPUT_PULLUP); // safe even if external pull-up present
  pinMode(LED_PIN, OUTPUT);
  ledState = false;
  applyLed();

  // I2C & OLED
  Wire.begin();
  u8g2.begin();

  // Splash
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(4, 16, "Power Latch Controller");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(4, 36, "Booting... latch=ON");
  u8g2.sendBuffer();
  delay(200);

  // init states
  btnLastStable = digitalRead(BTN_PIN);
  lastActivityMillis = millis();
}

void loop() {
  unsigned long now = millis();

  // --- Read & debounce button (non-blocking) ---
  int raw = digitalRead(BTN_PIN);
  if (raw != btnReading) {
    btnReading = raw;
    lastDebounceMillis = now;
  }
  if (now - lastDebounceMillis > DEBOUNCE_MS) {
    if (btnReading != btnLastStable) {
      // stable change happened
      btnLastStable = btnReading;
      // register activity for both press and release
      lastActivityMillis = now;
    }
  }

  // Compute idle elapsed and remaining
  unsigned long elapsedSinceActivity = now - lastActivityMillis;
  unsigned long remaining_ms = 0;
  if (elapsedSinceActivity >= IDLE_TOTAL_MS) remaining_ms = 0;
  else remaining_ms = IDLE_TOTAL_MS - elapsedSinceActivity;

  // Latch auto-release after LATCH_RELEASE_MS of idle elapsed
  if (elapsedSinceActivity >= LATCH_RELEASE_MS && latchHeld) {
    // release latch
    digitalWrite(LATCH_PIN, LOW);
    latchHeld = false;
    // module may take more time to actually power off
  }

  // Detect press edge (HIGH -> LOW) when allowed (ignored during final 3s)
  static int prevStable = HIGH;
  if (btnLastStable != prevStable) {
    if (prevStable == HIGH && btnLastStable == LOW) {
      // falling edge: button pressed
      if (remaining_ms > SHUTDOWN_FINAL_MS) {
        // toggle LED
        ledState = !ledState;
        applyLed();
        // ensure latch is held (if it was released, re-hold)
        if (!latchHeld) {
          digitalWrite(LATCH_PIN, HIGH);
          latchHeld = true;
        }
        // reset idle timer (as user interacted)
        lastActivityMillis = now;
      }
      // else: ignore button in final 3s
    }
    prevStable = btnLastStable;
  }

  // Display always shows 4-line status until remaining_ms == 0
  if (remaining_ms == 0) {
    // time's up -> graceful shutdown (shows 3..1 then releases latch and blocks)
    gracefulShutdown();
    // should not return
  } else {
    // normal 4-line status (even during last 3s)
    displayStatus((btnLastStable == LOW), remaining_ms);
  }

  delay(30);
}

// displayStatus: always show 4 lines:
// line2: Btn: UP/DOWN   LED: ON/OFF
// line3: Latch: HELD/RELEASE
// line4: Idle: XXs (marker) — shown down to 0
void displayStatus(bool buttonDown, unsigned long remaining_ms) {
  u8g2.clearBuffer();

  // Title
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(4, 12, "Power Latch");

  // Line 2: Btn and LED in one line
  u8g2.setFont(u8g2_font_6x10_tr);
  char line2[40];
  sprintf(line2, "Btn: %s   LED: %s", (buttonDown ? "DOWN" : "UP  "), (ledState ? "ON" : "OFF"));
  u8g2.drawStr(4, 30, line2);

  // Line 3: Latch status
  char line3[24];
  sprintf(line3, "Latch: %s", (latchHeld ? "HELD" : "RELEASED"));
  u8g2.drawStr(4, 44, line3);

  // Line 4: Idle timer marker (seconds ceil)
  unsigned long secRem = (remaining_ms + 999) / 1000;
  char line4[32];
  sprintf(line4, "Idle: %lus", secRem);
  u8g2.drawStr(4, 58, line4);

  u8g2.sendBuffer();
}

void gracefulShutdown() {
  // Show final 3-second countdown (3..1) visually — during this time inputs are ignored
  for (int s = 3; s >= 1; --s) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_fub25_tr);
    char buf[8];
    sprintf(buf, "%d", s);
    int tw = u8g2.getStrWidth(buf);
    u8g2.drawStr((128 - tw) / 2, 48, buf);
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(4, 12, "Shutting down...");
    u8g2.sendBuffer();
    delay(1000);
  }

  // Ensure latch released
  digitalWrite(LATCH_PIN, LOW);
  latchHeld = false;

  // Final message then block until power off
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(4, 20, "Powering off...");
  u8g2.sendBuffer();

  delay(200);

  // Block until power off
  while (true) {
    delay(1000);
  }
}
