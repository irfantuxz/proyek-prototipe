/*
  Data Logger v3 — Kalibrasi Suhu & Kelembapan Perhitungan
  - Calibrated Temp (Dry/Wet)
  - Calculated Humidity (psychrometric)
  - Keypad MDPL input (+ auto-OK 15s, EEPROM persist)
  - Watchdog 4s for auto-reset
  - LCD glyphs for 100..169
  - displayRtcBlink DS1307
  - SD card data logging
*/

#include <Wire.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "SdFat.h"
#include "sdios.h"
#include <EEPROM.h>
#include <RtcDS1307.h>
#ifdef ARDUINO_ARCH_AVR
  #include <avr/wdt.h>
#endif

// Delay sebelum mulai menulis Data Logger CSV (ms).
const unsigned long STARTUP_LOG_DELAY_MS = 3000UL;
unsigned long startupLogTime = 0;

// ======= PINs Configuration ========
#define DHTTYPE DHT22
const uint8_t DHT_PINS[4] = {49, 47, 45, 43};
const uint8_t DS_PINS[12] = {28, 32, 40, 26, 30, 34, 46, 36, 42, 44, 48, 38};

#define LCD_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);
bool lcdReady = true;

// SD/SoftSPI pins
const uint8_t SD_CS_PIN     = 10;
const uint8_t SOFT_MISO_PIN = 12;
const uint8_t SOFT_MOSI_PIN = 11;
const uint8_t SOFT_SCK_PIN  = 13;
SoftSpiDriver<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> softSpi;
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(0), &softSpi)

// RTC instance
RtcDS1307<TwoWire> Rtc(Wire);

// ========== Tunables / constants ==========
volatile bool oneSecondTick = false;
const unsigned long DS_CONV_DELAY_MS = 375UL; // 10-bit conversion
const float OFF_VALUE = -127.0f;
const uint16_t FLUSH_EVERY = 10;
const uint8_t LED_TOGGLE_PIN = 2;  // toggle each saved line
const uint8_t LED_SDERR_PIN  = 3;

// ========== Globals ==========
DHT* dht[4];

#include <new>
static uint8_t oneWireBuf[12][sizeof(OneWire)];
static uint8_t dsBuf[12][sizeof(DallasTemperature)];
OneWire* oneWireArr[12] = { nullptr };
DallasTemperature* dsArr[12] = { nullptr };

float dsTemps[12];
float dhtT[4], dhtH[4];

SdFat sd;
SdFile logFile;
bool sdAvailable = false;
bool ledToggleState = false;
uint16_t linesSinceFlush = 0;

char logFileNameBuf[64] = {0};
char logFileTimestampBuf[32] = {0};

// custom glyphs 10..16 (7 glyph)
byte customChar[7][8] = {
  {0x17,0x15,0x15,0x15,0x15,0x15,0x17,0x00},  // "10"
  {0x12,0x12,0x12,0x12,0x12,0x12,0x12,0x00},  // "11"
  {0x17,0x15,0x11,0x11,0x12,0x14,0x17,0x00},  // "12"
  {0x17,0x15,0x11,0x13,0x11,0x15,0x17,0x00},  // "13"
  {0x15,0x15,0x15,0x17,0x11,0x11,0x11,0x00},  // "14"
  {0x17,0x14,0x14,0x17,0x11,0x15,0x17,0x00},  // "15"
  {0x17,0x14,0x14,0x17,0x15,0x15,0x17,0x00}   // "16"
};

// ---- Keypad (3x4) ----
const byte KP_ROWS = 4, KP_COLS = 3;
char KEYS[KP_ROWS][KP_COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte KP_ROW_PINS[KP_ROWS] = {39, 37, 35, 33}; // R0..R3
byte KP_COL_PINS[KP_COLS] = {31, 29, 27};     // C0..C2
Keypad keypad = Keypad(makeKeymap(KEYS), KP_ROW_PINS, KP_COL_PINS, KP_ROWS, KP_COLS);

// ---- MDPL / Pressure state ----
bool setupKetinggian = true;   // true -> tampilkan input MDPL
bool reviewScreen = false;     // '*' toggle review
bool showRawScreen = false;    // '#' toggle RAW (default calc = false)
String mdplInput = "";         // input altitude
unsigned long blinkMillis = 0; // blinking caret
bool blinkOn = false;
unsigned long mdplAutoDeadlineMs = 0; // auto-OK 15s

float valMDPL = 0.0f;
float valPressureKPa = 101.325f;
float valPressureMBar = 1013.250f;

bool logHeaderWrittenV2 = false;

// ========== Forward declarations ==========
// Helpers
bool parseTimestampInput(const char* s, RtcDateTime &out);
void makeFilenameFromRtcBuf(const RtcDateTime &dt, char *buf, size_t bufsize);
void makeTimestampStrBuf(const RtcDateTime &dt, char *buf, size_t bufsize);

// LCD
bool tryInitLCD();
void displayRtcBlink(const RtcDateTime &now, bool diatas = false);

// SD / RTC
void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10);

// Sensor
void initSensors();
void readAllDHTs();
void probeAndReadDS();

// Power / shutdown
void safeShutdown();

// Timer1
void setupTimer1_1Hz();
ISR(TIMER1_COMPA_vect);

// Keypad / MDPL
void handleMdplInputAndReview();
void drawMdplInputScreen();
void drawReviewScreen();
void writeHeaderWithMdpl();
void replaceDotWithComma(char *s);

// Calculations
inline float calSen(float x, int index);
inline float calDry(float x, int index);
inline float calWet(float x, int index);
inline float satVP(float T);
inline float psychroA(float Tw);
bool writeCsvLineAndToggleV2(const char* fileTimestamp,
                             float suhuRaw[4], float dryRaw[4], float wetRaw[4], float dhtHumRaw[4],
                             float calSuhu[4], float calDryV[4], float calWetV[4], float hrV[4]);

// ========== EEPROM (2 bytes value + 1 byte checksum) ==========
static const int EEPROM_MDPL_ADDR = 0; // [0]=lo, [1]=hi, [2]=cs
bool eepromLoadMDPL(uint16_t &out) {
  byte lo = EEPROM.read(EEPROM_MDPL_ADDR + 0);
  byte hi = EEPROM.read(EEPROM_MDPL_ADDR + 1);
  byte cs = EEPROM.read(EEPROM_MDPL_ADDR + 2);
  byte calc = (byte)(lo ^ hi ^ 0xA5);
  if (cs == calc) { out = (uint16_t)lo | ((uint16_t)hi << 8); return true; }
  return false;
}
void eepromSaveMDPLIfChanged(uint16_t v) {
  uint16_t cur;
  if (eepromLoadMDPL(cur) && cur == v) return;
  byte lo = (byte)(v & 0xFF);
  byte hi = (byte)(v >> 8);
  byte cs = (byte)(lo ^ hi ^ 0xA5);
  EEPROM.update(EEPROM_MDPL_ADDR + 0, lo);
  EEPROM.update(EEPROM_MDPL_ADDR + 1, hi);
  EEPROM.update(EEPROM_MDPL_ADDR + 2, cs);
}

// ========== Helpers ==========
bool parseTimestampInput(const char* s, RtcDateTime &out) {
  unsigned int Y, M, D, h, m, s2;
  if (sscanf(s, "%u_%u_%u_%u_%u_%u", &Y, &M, &D, &h, &m, &s2) != 6) return false;
  if (Y < 2000 || M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || s2 > 59) return false;
  out = RtcDateTime((uint16_t)Y, (uint8_t)M, (uint8_t)D, (uint8_t)h, (uint8_t)m, (uint8_t)s2);
  return true;
}

void makeFilenameFromRtcBuf(const RtcDateTime &dt, char *buf, size_t bufsize) {
  snprintf(buf, bufsize, "sensor_log_%04u_%02u_%02u_%02u_%02u_%02u.csv",
           dt.Year(), dt.Month(), dt.Day(), dt.Hour(), dt.Minute(), dt.Second());
}
void makeTimestampStrBuf(const RtcDateTime &dt, char *buf, size_t bufsize) {
  snprintf(buf, bufsize, "%04u-%02u-%02u;%02u:%02u:%02u",
           dt.Year(), dt.Month(), dt.Day(), dt.Hour(), dt.Minute(), dt.Second());
}

// ========== LCD helpers ==========
bool tryInitLCD() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  for (int i = 0; i < 7; i++ ) lcd.createChar(i, customChar[i]);
  return true;
}

// H:MM blink; diatas=true -> row0, else row3 (col 16..19)
void displayRtcBlink(const RtcDateTime &now, bool diatas) {
  uint8_t hour24 = now.Hour();
  uint8_t hour12 = now.Hour() % 12; if (hour24 == 12) hour12 = 12;
  uint8_t minute = now.Minute();
  uint8_t second = now.Second();
  char colon = (second & 1) ? ' ' : ':';
  char minTens  = '0' + (minute / 10);
  char minUnits = '0' + (minute % 10);
  if (diatas) lcd.setCursor(16, 0); else lcd.setCursor(16, 3);
  if (hour12 >= 10 && hour12 <= 12) { lcd.write(hour12 - 10); } else { lcd.print(hour12); }
  lcd.print(colon); lcd.print(minTens); lcd.print(minUnits);
}

// ===== LCD hotplug probe =====
unsigned long lastLcdProbeMs = 0;
const unsigned long LCD_PROBE_INTERVAL_MS = 500; // setengah detik cek

bool lcdDevicePresent() {
  Wire.beginTransmission(LCD_ADDR);
  return (Wire.endTransmission() == 0);
}

void pollLcdHotplug() {
  unsigned long ms = millis();
  if (ms - lastLcdProbeMs < LCD_PROBE_INTERVAL_MS) return;
  lastLcdProbeMs = ms;

  bool present = lcdDevicePresent();
  if (!present && lcdReady) {
    // Dicabut
    lcdReady = false;
  } else if (present && !lcdReady) {
    // Dipasang kembali → re-init
    tryInitLCD();
    // Kembalikan layar terakhir yang relevan tanpa mengubah gaya tampil:
    if (setupKetinggian) {
      drawMdplInputScreen();
    } else if (reviewScreen) {
      drawReviewScreen();
    } else {
      // biarkan loop tampilan rutin menggambar pada tick berikutnya
    }
    lcdReady = true;
  }
}

// SD timestamp callback
void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10) {
  RtcDateTime now = Rtc.GetDateTime();
  if (date) *date = FAT_DATE(now.Year(), now.Month(), now.Day());
  if (time) *time = FAT_TIME(now.Hour(), now.Minute(), now.Second());
  if (ms10) *ms10 = (now.Second() & 1) ? 100 : 0;
}

// ========== Sensors ==========
void initSensors() {
  // DHT init (safe heap allocation)
  for (int i = 0; i < 4; i++) { dht[i] = new DHT(DHT_PINS[i], DHTTYPE); dht[i]->begin(); }
  // OneWire + Dallas init
  for (int i = 0; i < 12; i++) {
    oneWireArr[i] = new ((void*)oneWireBuf[i]) OneWire(DS_PINS[i]);
    dsArr[i]      = new ((void*)dsBuf[i]) DallasTemperature(oneWireArr[i]);
    dsArr[i]->begin(); dsArr[i]->setWaitForConversion(false);
    dsTemps[i] = OFF_VALUE;
  }
}

void readAllDHTs() {
  for (int i = 0; i < 4; i++) {
    float t = dht[i]->readTemperature();
    float h = dht[i]->readHumidity();
    dhtT[i] = isnan(t) ? OFF_VALUE : t;
    dhtH[i] = isnan(h) ? OFF_VALUE : h;
  }
}

unsigned long lastDsConvMs = 0; bool dsConvInProgress = false;
void probeAndReadDS() {
  unsigned long nowMs = millis();
  if (!dsConvInProgress) { for (int i = 0; i < 12; i++) dsArr[i]->requestTemperatures(); dsConvInProgress = true; lastDsConvMs = nowMs; }
  if (dsConvInProgress && (nowMs - lastDsConvMs >= DS_CONV_DELAY_MS)) {
    for (int i = 0; i < 12; i++) {
      float v = dsArr[i]->getTempCByIndex(0);
      dsTemps[i] = (v <= -120.0f || v >= 150.0f || isnan(v)) ? OFF_VALUE : v;
    }
    dsConvInProgress = false;
  }
}

void safeShutdown() {
  if (sdAvailable && logFile.isOpen()) {
    logFile.flush();
    logFile.sync();
    logFile.close();
  }
  if (lcdReady) {
    lcd.clear();
    lcd.setCursor(0,1);
    lcd.print("Saving & Reset...");
  }
  delay(300);
}

// Timer1 1 Hz
void setupTimer1_1Hz() {
  cli(); TCCR1A = 0; TCCR1B = 0; TCNT1 = 0; OCR1A = 15487;
  TCCR1B |= (1 << WGM12); TCCR1B |= (1 << CS12) | (1 << CS10); TIMSK1 |= (1 << OCIE1A); sei();
}
ISR(TIMER1_COMPA_vect) { oneSecondTick = true; }

// ========== Calibration & Psychrometrics ==========
// Sensor
inline float calSen(float x, int index) {
  if (index == 0) return 1.1504f * x - 5.4902f;  // sen1
  if (index == 1) return 1.0853f * x - 2.8467f;  // sen2
  if (index == 2) return 1.0925f * x - 3.4868f;  // sen3
  if (index == 3) return 1.0417f * x - 2.1235f;  // sen4
  return 0.0f; // default jika index salah
}

// Dry bulb
inline float calDry(float x, int index) {
  if (index == 0) return 0.9515f * x + 0.6574f;  // dry1
  if (index == 1) return 0.9701f * x + 0.6796f;  // dry2
  if (index == 2) return 0.8963f * x + 2.8042f;  // dry3
  if (index == 3) return 0.9644f * x + 0.9133f;  // dry4
  return 0.0f;
}

// Wet bulb
inline float calWet(float x, int index) {
  if (index == 0) return 0.8249f * x + 2.6510f;  // wet1
  if (index == 1) return 0.7895f * x + 3.6936f;  // wet2
  if (index == 2) return 0.7869f * x + 3.6150f;  // wet3
  if (index == 3) return 0.7351f * x + 4.7307f;  // wet4
  return 0.0f;
}

inline float psychroA(float Tw) { return 0.00066f * (1.0f + 0.00115f * Tw); }
inline float satVP(float T) { return 6.112f * expf((17.502f * T) / (240.97f + T)); }


void replaceDotWithComma(char *s) { for (char *p = s; *p; ++p) if (*p == '.') *p = ','; }
// ========== LCD glyph helpers (100..169) ==========

void lcdPrintWithHundredsGlyph(float v, uint8_t decimals){
  if (v == OFF_VALUE || isnan(v)) { lcd.print("OFF "); return; }
  if (v >= 100.0f && v < 170.0f && decimals <= 1) {
    char full[12]; dtostrf(v, 0, decimals, full); int flen = strlen(full);
    const char* tail = (decimals ? (flen >= 3 ? (full + flen - 3) : full) : (flen >= 1 ? (full + flen - 1) : full));
    int intpart = (int)floor(v + 0.5f); int prefix = intpart / 10; int glyphIndex = prefix - 10;
    if (glyphIndex >=0 && glyphIndex < 7) { lcd.write(glyphIndex); lcd.print(tail); lcd.print(' '); return; }
  }
  lcd.print(String(v, decimals)); lcd.print(' ');
}

void lcdPrintPercentWithHundredsGlyph(float v){
  if (v == OFF_VALUE || isnan(v)) { lcd.print("OFF "); return; }
  float vv = v; if (vv < 0) vv = 0; if (vv > 200) vv = 200;
  if (vv >= 100.0f && vv < 170.0f) {
    int intpart = (int)floor(vv + 0.5f); int prefix = intpart / 10; int glyphIndex = prefix - 10; int last = intpart % 10;
    if (glyphIndex >=0 && glyphIndex < 7) { lcd.write(glyphIndex); lcd.print(last); lcd.print('%'); lcd.print(' '); return; }
  }
  lcd.print((int)round(vv)); lcd.print('%'); lcd.print(' ');
}

// ========== MDPL input & review screens ==========
void drawMdplInputScreen() {
  lcd.setCursor(0,0); lcd.print("Input ketinggian:");
  lcd.setCursor(0,1); lcd.print("     ");
  String disp = mdplInput; if (blinkOn) disp += (char)255; else disp += (char)0x5F; lcd.print(disp); lcd.print(" MDPL ");
  lcd.setCursor(0,3); lcd.print("* = backsp | # = OK");
}

void drawReviewScreen() {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("MDPL = "); lcd.print((long)valMDPL); lcd.print(" m ");
  char kpa[24], mbar[24]; dtostrf(valPressureKPa, 0, 3, kpa); dtostrf(valPressureMBar, 0, 3, mbar); replaceDotWithComma(kpa); replaceDotWithComma(mbar);
  lcd.setCursor(0,1); lcd.print("Press=  "); lcd.print(kpa); lcd.print(" kPa");
  lcd.setCursor(0,2); lcd.print("     = "); lcd.print(mbar); lcd.print(" mBar");
  lcd.setCursor(0,3); lcd.print("*=review | #=reset");
}

void handleMdplInputAndReview() {
  if (millis() - blinkMillis >= 500) { blinkMillis = millis(); blinkOn = !blinkOn; }
  char key = keypad.getKey();
  if (setupKetinggian) {
    if (mdplAutoDeadlineMs == 0) mdplAutoDeadlineMs = millis() + 30000UL;
    if (key) {
      mdplAutoDeadlineMs = millis() + 30000UL;
      if (key >= '0' && key <= '9') {
        if (mdplInput.length() < 5)
        mdplInput += key;
      }
      else if (key == '*') {
        if (mdplInput.length() > 0)
        mdplInput.remove(mdplInput.length()-1);
      }
      else if (key == '#') {
        valMDPL = mdplInput.length() ? mdplInput.toInt() : valMDPL;
        float t = 1.0f - 2.25577e-5f * valMDPL; valPressureKPa = 101.325f * powf(t, 5.2559f);
        valPressureMBar = valPressureKPa * 10.0f;

        eepromSaveMDPLIfChanged((uint16_t)(valMDPL + 0.5f));
        writeHeaderWithMdpl();
        drawReviewScreen();
        delay(3000);
        setupKetinggian = false;
        reviewScreen = false;
        lcd.clear();
        return;
      }
    }
    if (mdplAutoDeadlineMs && (long)(millis() - mdplAutoDeadlineMs) >= 0) {
      valMDPL = mdplInput.length() ? mdplInput.toInt() : valMDPL;
      float t = 1.0f - 2.25577e-5f * valMDPL; valPressureKPa = 101.325f * powf(t, 5.2559f);
      valPressureMBar = valPressureKPa * 10.0f;

      eepromSaveMDPLIfChanged((uint16_t)(valMDPL + 0.5f));
      writeHeaderWithMdpl();
      drawReviewScreen();
      delay(3000);
      setupKetinggian = false;
      reviewScreen = false;
      lcd.clear();
      return;
    }
    drawMdplInputScreen(); return;
  } else {
    if (key == '*') { reviewScreen = !reviewScreen; if (reviewScreen) { drawReviewScreen(); } }
    else if (key == '#' && reviewScreen) {
      safeShutdown();
      #ifdef ARDUINO_ARCH_AVR
        wdt_enable(WDTO_15MS); while (1) {}
      #else
        setupKetinggian = true;
        reviewScreen = false; showRawScreen = false;
        mdplInput = ""; logHeaderWrittenV2 = false;
        lcd.clear(); drawMdplInputScreen();
      #endif
    } else if (key == '#' && !reviewScreen) { showRawScreen = !showRawScreen; }
  }
}

void writeHeaderWithMdpl() {
  if (!sdAvailable) return; if (!logFile.isOpen()) return;
  logFile.rewind(); logFile.truncate(0);
  logFile.println(F("sep=;"));
  char kpa[24], mbar[24]; dtostrf(valPressureKPa, 0, 3, kpa); dtostrf(valPressureMBar, 0, 3, mbar);
  replaceDotWithComma(kpa); replaceDotWithComma(mbar);
  char mdplLine[128]; snprintf(mdplLine, sizeof(mdplLine),
       "MDPL = %ld; kPa = %s; mBar = %s;", (long)valMDPL, kpa, mbar);
  logFile.println(mdplLine);
  char header[512]; int p = 0; p += snprintf(header + p, sizeof(header) - p, "logDate;logTime");
  for (int i=0;i<4;i++) p += snprintf(header + p, sizeof(header) - p, ";RAW_Suhu_%d", i+1);
  for (int i=0;i<4;i++) p += snprintf(header + p, sizeof(header) - p, ";RAW_Dry_%d",  i+1);
  for (int i=0;i<4;i++) p += snprintf(header + p, sizeof(header) - p, ";RAW_Wet_%d",  i+1);
  for (int i=0;i<4;i++) p += snprintf(header + p, sizeof(header) - p, ";RAW_Humi_%d", i+1);
  for (int i=0;i<4;i++) p += snprintf(header + p, sizeof(header) - p, ";Cal_Suhu_%d",  i+1);
  for (int i=0;i<4;i++) p += snprintf(header + p, sizeof(header) - p, ";Cal_Dry_%d",  i+1);
  for (int i=0;i<4;i++) p += snprintf(header + p, sizeof(header) - p, ";Cal_Wet_%d",  i+1);
  for (int i=0;i<4;i++) p += snprintf(header + p, sizeof(header) - p, ";Cal_Hr_%d",   i+1);
  p += snprintf(header + p, sizeof(header) - p, "\n");
  logFile.write(header, p);
  logFile.flush();
  logHeaderWrittenV2 = true;
}

bool writeCsvLineAndToggleV2(const char* fileTimestamp,
                             float suhuRaw[4], float dryRaw[4], float wetRaw[4], float dhtHumRaw[4],
                             float calSuhu[4], float calDryV[4], float calWetV[4], float hrV[4]) {
  if (!sdAvailable) return false; if (!logFile.isOpen()) return false; if (setupKetinggian) return true;
  char line[700]; int p = 0; p += snprintf(line + p, sizeof(line) - p, "%s", fileTimestamp);
  auto outVal = [&](float v) {
    if (v == OFF_VALUE || isnan(v)) { p += snprintf(line + p, sizeof(line) - p, ";OFF"); }
    else { char tbuf[16]; dtostrf(v, 0, 2, tbuf); p += snprintf(line + p, sizeof(line) - p, ";%s", tbuf); }
  };
  for (int i=0;i<4;i++) outVal(suhuRaw[i]);
  for (int i=0;i<4;i++) outVal(dryRaw[i]);
  for (int i=0;i<4;i++) outVal(wetRaw[i]);
  for (int i=0;i<4;i++) outVal(dhtHumRaw[i]);
  for (int i=0;i<4;i++) outVal(calSuhu[i]);
  for (int i=0;i<4;i++) outVal(calDryV[i]);
  for (int i=0;i<4;i++) outVal(calWetV[i]);
  for (int i=0;i<4;i++) outVal(hrV[i]);
  p += snprintf(line + p, sizeof(line) - p, "\n");
  if (logFile.write(line, p) != p) return false;
  linesSinceFlush++; if (linesSinceFlush >= FLUSH_EVERY) { logFile.flush(); linesSinceFlush = 0; }
  ledToggleState = !ledToggleState; digitalWrite(LED_TOGGLE_PIN, ledToggleState ? HIGH : LOW);
  return true;
}

// ===== Serial command & RTC logging flag =====
volatile bool cekLog = false;

static char serialBuf[48];
static uint8_t serialPos = 0;

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      serialBuf[serialPos] = '\0';
      if (serialPos > 0) {
        // Normalisasi huruf kecil untuk perintah pendek
        for (uint8_t i = 0; i < serialPos; i++) {
          if (serialBuf[i] >= 'A' && serialBuf[i] <= 'Z') serialBuf[i] += 32;
        }

        // Perintah "rtc" → toggle cekLog
        if (strcmp(serialBuf, "rtc") == 0) {
          cekLog = !cekLog;
          Serial.print(F("[RTC LOG] ")); Serial.println(cekLog ? F("ON") : F("OFF"));
        }
        // Perintah settime=YYYY_MM_DD_hh_mm_ss
        else if (strncmp(serialBuf, "settime=", 8) == 0) {
          RtcDateTime dt;
          if (parseTimestampInput(serialBuf + 8, dt)) {
            Rtc.SetDateTime(dt);
            Serial.print(F("[RTC SET] OK ")); 
            char ts[32]; makeTimestampStrBuf(dt, ts, sizeof(ts));
            Serial.println(ts);
          } else {
            Serial.println(F("[RTC SET] ERROR format. Gunakan settime=yyyy_mm_dd_hh_mm_ss"));
          }
        }
        else {
          Serial.print(F("[UNKNOWN] ")); Serial.println(serialBuf);
        }
      }
      serialPos = 0;
    } else {
      if (serialPos < sizeof(serialBuf) - 1) serialBuf[serialPos++] = c;
      // abaikan overflow
    }
  }
}

// ========== Setup / Loop ==========
void setup() {
  Serial.begin(115200); while (!Serial && millis() < 1000) ;
#ifdef ARDUINO_ARCH_AVR
  wdt_enable(WDTO_4S); // watchdog 4s aktif
#endif

  // LCD & glyphs
  tryInitLCD();
  lcd.setCursor(0,0); lcd.print(" Data Logger Sensor ");
  lcd.setCursor(0,1); lcd.print(" Suhu & Kelembapan  ");
  lcd.setCursor(0,2); lcd.print("----Start System----");
  lcd.setCursor(0,3); lcd.print("Initializing sensors");

  // LEDs
  pinMode(LED_TOGGLE_PIN, OUTPUT); pinMode(LED_SDERR_PIN, OUTPUT);

  // Init protocol
  Wire.begin(); Rtc.Begin();

  // Load MDPL from EEPROM (prefill input)
  uint16_t saved; if (eepromLoadMDPL(saved)) { valMDPL = saved; }
  mdplInput = String((long)valMDPL);

  initSensors();
  // Start timer 1Hz
  setupTimer1_1Hz(); startupLogTime = millis() + STARTUP_LOG_DELAY_MS;

  if (!Rtc.IsDateTimeValid()) {
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    Rtc.SetDateTime(compiled);
  }

  lcd.setCursor(0,3); lcd.print("Preparing SDcard... ");
  SdFile::dateTimeCallback(dateTime);
  if (!sd.begin(SD_CONFIG)) {
    sdAvailable = false;
    lcd.setCursor(0,3); lcd.print("SD init FAILED      ");
    digitalWrite(LED_SDERR_PIN, HIGH);
  }
  else {
    sdAvailable = true; digitalWrite(LED_SDERR_PIN, LOW);
    RtcDateTime now = Rtc.GetDateTime();
    if (Rtc.IsDateTimeValid()) {
      makeFilenameFromRtcBuf(now, logFileNameBuf, sizeof(logFileNameBuf));
      makeTimestampStrBuf(now, logFileTimestampBuf, sizeof(logFileTimestampBuf));
    }
    else {
      unsigned long t = millis()/1000UL;
      snprintf(logFileNameBuf, sizeof(logFileNameBuf), "sensor_log_millis_%lu.csv", t);
      snprintf(logFileTimestampBuf, sizeof(logFileTimestampBuf), "millis_%lu", t);
    }
    // Open append; header ditulis setelah MDPL OK
    // Open aman: create jika belum ada, posisi di akhir jika sudah ada
    if (!logFile.open(logFileNameBuf, O_RDWR | O_CREAT | O_AT_END)) {
      sdAvailable = false;
      digitalWrite(LED_SDERR_PIN, HIGH);
    }

  }

  lcd.clear(); drawMdplInputScreen(); probeAndReadDS();
}

void loop() {
  pollLcdHotplug();
  handleSerialCommands();      // --- Serial Command
  handleMdplInputAndReview();  // --- Keypad INPUT (sebelum 1Hz clock) ---
#ifdef ARDUINO_ARCH_AVR
  wdt_reset();
#endif
  if (!oneSecondTick) return; oneSecondTick = false;

  RtcDateTime now = Rtc.GetDateTime(); if (setupKetinggian) return;
  if (cekLog) {
    char tsNow[32];
    makeTimestampStrBuf(now, tsNow, sizeof(tsNow));
    Serial.println(tsNow);
  }

  // sensor ops
  readAllDHTs(); probeAndReadDS();

  // kalkulasi
  float suhuRaw[4], dryRaw[4], wetRaw[4], dhtHumRaw[4];
  for (int i=0;i<4;i++) { suhuRaw[i] = dsTemps[i]; dryRaw[i]  = dsTemps[4+i]; wetRaw[i]  = dsTemps[8+i]; dhtHumRaw[i] = dhtH[i]; }
  float calSuhu[4], calDryV[4], calWetV[4], hrV[4];
  for (int i=0;i<4;i++) {
    calSuhu[i] = (suhuRaw[i] != OFF_VALUE) ? calSen(suhuRaw[i], i) : OFF_VALUE;
    calDryV[i] = (dryRaw[i] != OFF_VALUE) ? calDry(dryRaw[i], i) : OFF_VALUE;
    calWetV[i] = (wetRaw[i] != OFF_VALUE) ? calWet(wetRaw[i], i) : OFF_VALUE;
    if (calDryV[i] == OFF_VALUE || calWetV[i] == OFF_VALUE) { hrV[i] = OFF_VALUE; }
    else {
      float Ew = satVP(calWetV[i]); float Ed = satVP(calDryV[i]); float A  = psychroA(calWetV[i]);
      float hr = ((Ew - (A * valPressureMBar * (calDryV[i] - calWetV[i])))/Ed) * 100.0f;
      if (hr < 0) hr = 0; hrV[i] = hr;
    }
  }

  // Tampilan
  if (lcdReady) {
    if (reviewScreen) {
      displayRtcBlink(now, true); // di atas saat review
    } else if (!showRawScreen) {
      lcd.setCursor(0,0); for (int i=0;i<4;i++){ if (calSuhu[i]==OFF_VALUE) { lcd.print("OFF "); } else { lcdPrintWithHundredsGlyph(calSuhu[i],1); } }
      lcd.setCursor(0,1); for (int i=0;i<4;i++){ if (calDryV[i]==OFF_VALUE) { lcd.print("OFF "); } else { lcdPrintWithHundredsGlyph(calDryV[i],1); } }
      lcd.setCursor(0,2); for (int i=0;i<4;i++){ if (calWetV[i]==OFF_VALUE) { lcd.print("OFF "); } else { lcdPrintWithHundredsGlyph(calWetV[i],1); } }
      lcd.setCursor(0,3); for (int i=0;i<4;i++){ lcdPrintPercentWithHundredsGlyph(hrV[i]); }
      lcd.setCursor(19,0); lcd.print("C");
      displayRtcBlink(now, false);
    } else {
      lcd.setCursor(0,0); for (int i=0;i<4;i++){ if (suhuRaw[i]==OFF_VALUE) { lcd.print("OFF "); } else { lcdPrintWithHundredsGlyph(suhuRaw[i],1); } }
      lcd.setCursor(0,1); for (int i=0;i<4;i++){ if (dryRaw[i]==OFF_VALUE) { lcd.print("OFF "); } else { lcdPrintWithHundredsGlyph(dryRaw[i],1); } }
      lcd.setCursor(0,2); for (int i=0;i<4;i++){ if (wetRaw[i]==OFF_VALUE) { lcd.print("OFF "); } else { lcdPrintWithHundredsGlyph(wetRaw[i],1); } }
      lcd.setCursor(0,3); for (int i=0;i<4;i++){ if (dhtHumRaw[i]==OFF_VALUE) { lcd.print("OFF "); } else { lcd.print((int)round(dhtHumRaw[i])); lcd.print('%'); lcd.print(' '); } }
      lcd.setCursor(19,0); lcd.print("R");
      displayRtcBlink(now, false);
    }
  }


#ifdef ARDUINO_ARCH_AVR
  wdt_reset();
#endif
  // Logging
  if (sdAvailable && logFile.isOpen()) {
    makeTimestampStrBuf(now, logFileTimestampBuf, sizeof(logFileTimestampBuf));
    if (!logHeaderWrittenV2) writeHeaderWithMdpl();
    bool ok = writeCsvLineAndToggleV2(logFileTimestampBuf, suhuRaw, dryRaw, wetRaw, dhtHumRaw, calSuhu, calDryV, calWetV, hrV);
    // ulang sdcard jika fail
    if (!ok) {
      // Recovery ringan: flush/sync/close → re-init SD → reopen file
      digitalWrite(LED_SDERR_PIN, HIGH);
      logFile.flush();
      logFile.sync();
      logFile.close();

      sd.begin(SD_CONFIG);
      if (logFile.open(logFileNameBuf, O_RDWR | O_CREAT | O_AT_END)) {
        sdAvailable = true;
        digitalWrite(LED_SDERR_PIN, LOW);
      } else {
        sdAvailable = false;
      }
    }
  } else { digitalWrite(LED_SDERR_PIN, HIGH); }  // Blink LED indikator
}

// ========== End ==========
