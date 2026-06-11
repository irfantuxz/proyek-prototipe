#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <time.h>
#include <U8g2lib.h>

// Konfigurasi Pin & Alamat I2C
#define SDA_PIN        0
#define SCL_PIN        1
#define BTN_SYNC_TIME  9     // Built-in Boot Button ESP32-C3
#define LED_SYNC_INFO  8     // Built-in Blue LED ESP32-C3 super mini
#define DS3231_ADDR    0x68
#define OLED_ADDR      0x3C

// Inisialisasi Objek Global
WiFiMulti wifiMulti;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Variabel Sistem & Status
bool ntpOk = false;
bool dsReady = false;
bool isSynced = false;

struct tm timeinfo; // Struct untuk menyimpan waktu NTP
const char* bulanNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
// Cache waktu DS3231
uint8_t rtc_sec, rtc_min, rtc_hour, rtc_day, rtc_month, rtc_year;
static const unsigned char PROGMEM image_wifi_bits[24] = { 0x00,0x00, 0x3F,0x00, 0xC0,0x00, 0x00,0x01, 0x0F,0x02, 0x30,0x02, 0x40,0x04, 0x47,0x04, 0x88,0x04, 0x90,0x04, 0x93,0x04, 0x93,0x04};

// Variabel Interval (Non-blocking loop)
unsigned long lastDisplayUpdate = 0;
unsigned long lastBlinkUpdate = 0;
bool ledState = false;
bool lastBtnState = HIGH;

// Fungsi Bantuan Modular (Konversi BCD <-> DEC)
uint8_t decToBcd(uint8_t val) { return ((val / 10 * 16) + (val % 10)); }
uint8_t bcdToDec(uint8_t val) { return ((val / 16 * 10) + (val % 16)); }

// Modul DS3231 (Deteksi & Komunikasi I2C)
bool checkI2CDevice(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

void readDS3231() {
  if (!dsReady) return;
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00); // Set pointer ke register detik
  if (Wire.endTransmission() != 0) {
    dsReady = false; // Drop koneksi jika gagal komunikasi
    return;
  }
  
  Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)7);
  if (Wire.available() >= 7) {
    rtc_sec   = bcdToDec(Wire.read() & 0x7F);
    rtc_min   = bcdToDec(Wire.read());
    rtc_hour  = bcdToDec(Wire.read() & 0x3F);
    Wire.read(); // Skip hari dalam minggu
    rtc_day   = bcdToDec(Wire.read());
    rtc_month = bcdToDec(Wire.read() & 0x7F);
    rtc_year  = bcdToDec(Wire.read());
  }
}

void syncDS3231WithNTP() {
  if (!dsReady || !ntpOk) return;
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00); // Register mulai dari detik
  Wire.write(decToBcd(timeinfo.tm_sec));
  Wire.write(decToBcd(timeinfo.tm_min));
  Wire.write(decToBcd(timeinfo.tm_hour));
  Wire.write(decToBcd(0)); // Day of week (diabaikan)
  Wire.write(decToBcd(timeinfo.tm_mday));
  Wire.write(decToBcd(timeinfo.tm_mon + 1)); // tm_mon dari 0, RTC dari 1
  Wire.write(decToBcd(timeinfo.tm_year - 100)); // tm_year dari 1900, RTC dari 2000
  Wire.endTransmission();
}

// Modul Jaringan & NTP
void serviceNTP(bool forceInit = false) {
  if (forceInit) configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov", "time.cloudflare.com");
  if (wifiMulti.run() == WL_CONNECTED) {
    if (!ntpOk) {
      u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_bytesize_tr);
        u8g2.setCursor(0, 25);
        u8g2.print("WiFi connected:");
        u8g2.setCursor(0, 40);
        u8g2.print(WiFi.SSID());
      u8g2.sendBuffer();
      delay(2000);
    }
    ntpOk = (getLocalTime(&timeinfo, 10));
  }
}

// Evaluasi Keselarasan Waktu (Sync Status)
void evaluateSync() {
  if (!dsReady || !ntpOk) {
    isSynced = false;
    return;
  }
  
  // Konversi waktu RTC ke epoch untuk perbandingan
  struct tm rtc_tm;
  rtc_tm.tm_year = rtc_year + 100; 
  rtc_tm.tm_mon  = rtc_month - 1;
  rtc_tm.tm_mday = rtc_day;
  rtc_tm.tm_hour = rtc_hour;
  rtc_tm.tm_min  = rtc_min;
  rtc_tm.tm_sec  = rtc_sec;
  
  time_t rtc_epoch = mktime(&rtc_tm);
  time_t ntp_epoch = mktime(&timeinfo);
  
  // Toleransi deviasi waktu maksimum 2 detik
  isSynced = (abs(difftime(ntp_epoch, rtc_epoch)) <= 2);
}

// Kontrol UI / Display OLED
void updateDisplay() {
  u8g2.clearBuffer();
    //u8g2.setFont(u8g2_font_NokiaLargeBold_tr); 
    u8g2.setFont(u8g2_font_bytesize_tr);
    uint8_t baris1 = u8g2.getFontAscent();
    uint8_t baris2 = 16 + u8g2.getFontAscent();
    uint8_t baris3 = 32 + u8g2.getFontAscent();
    uint8_t baris4 = 48 + u8g2.getFontAscent();
    char buffer[32];
    u8g2_uint_t width;

    // Baris 1 & 2: NTP Time
    if (!ntpOk) {
      u8g2.setCursor(0, baris1);
      u8g2.print("Connecting to");
      u8g2.setCursor(0, baris2);
      u8g2.print("NTP server...");
    } else {
      u8g2.drawXBMP(0, 0, 12, 12, image_wifi_bits);
      u8g2.setCursor(14, baris1);
      u8g2.print("NTP >");
      snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      //width = u8g2.getStrWidth(buffer);
      u8g2.setCursor(75, baris1);
      u8g2.print(buffer);
      
      snprintf(buffer, sizeof(buffer), "%02d-%s-%04d", timeinfo.tm_mday, bulanNames[timeinfo.tm_mon], timeinfo.tm_year + 1900);
      width = u8g2.getStrWidth(buffer);
      u8g2.setCursor(127-width, baris2);
      u8g2.print(buffer);
    }
    
    // Baris 3 & 4: RTC Time
    if (!dsReady) {
      //u8g2.setDisplayRotation(U8G2_R3);
      u8g2.setCursor(0, baris3);
      u8g2.print("detecting...");
      u8g2.setCursor(0, baris4);
      u8g2.print("DS3231 RTC");
      u8g2.setDisplayRotation(U8G2_R0);
    } else {
      u8g2.setCursor(0, baris3);
      u8g2.print("DS3231 >");
      snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", rtc_hour, rtc_min, rtc_sec);
      //width = u8g2.getStrWidth(buffer);
      u8g2.setCursor(75, baris3);
      u8g2.print(buffer);
      
      //u8g2.setCursor(0, baris4);
      //u8g2.print("RTC");
      uint8_t safe_month = (rtc_month >= 1 && rtc_month <= 12) ? rtc_month - 1 : 0;
      snprintf(buffer, sizeof(buffer), "%02d-%s-%04d", rtc_day, bulanNames[safe_month], rtc_year + 2000);
      width = u8g2.getStrWidth(buffer);
      u8g2.setCursor(127-width, baris4);
      u8g2.print(buffer);
    }
  u8g2.sendBuffer();
}

// Inisialisasi Utama (Setup)
void setup() {
  pinMode(LED_SYNC_INFO, OUTPUT);
  pinMode(BTN_SYNC_TIME, INPUT_PULLUP);
  digitalWrite(LED_SYNC_INFO, LOW); // Matikan LED awal

  // Inisialisasi I2C dengan custom pin untuk ESP32-C3
  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.setI2CAddress(OLED_ADDR << 1);
  u8g2.begin();
  u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_bytesize_tr);
    u8g2.setCursor(0, 15);
    u8g2.print("Connecting WiFi..");
  u8g2.sendBuffer();
    
  wifiMulti.addAP("Lab-Elektro.USD", "kendali2022");
  wifiMulti.addAP("TA.Elektro", "elektrousd");
  wifiMulti.addAP("MikroTik Elka", "");
  wifiMulti.addAP("MikroTik Digital", "");
  wifiMulti.addAP("Wifi Retta", "kosongan");
  wifiMulti.addAP("TuxZ-Studio", "kosongan");
  wifiMulti.run();
  serviceNTP(true);
}

// =================================================================//
// Loop Utama
// =================================================================//
void loop() {
  unsigned long now = millis();

  // 1. Cek Koneksi Jaringan & NTP setiap 1 detik
  static unsigned long lastNetCheck = 0;
  if (now - lastNetCheck > 1000) {
    serviceNTP();
    lastNetCheck = now;
  }

  // 2. Baca Tombol Sync (Deteksi transisi LOW)
  bool btnState = digitalRead(BTN_SYNC_TIME);
  if (btnState == LOW && lastBtnState == HIGH) {
    syncDS3231WithNTP();
    delay(50); // Sederhana debounce
  }
  lastBtnState = btnState;

  // 3. Interval Utama Eksekusi (100ms) untuk Polling RTC & Update LCD
  if (now - lastDisplayUpdate > 100) {
    lastDisplayUpdate = now;
    
    // Polling Hotplug secara aman
    bool currentlyConnected = checkI2CDevice(DS3231_ADDR);
    if (currentlyConnected && !dsReady) {
      dsReady = true;
    } else if (!currentlyConnected && dsReady) {
      dsReady = false;
    }

    if (dsReady) readDS3231();
    evaluateSync();
    updateDisplay(); // Panggil render UI
  }

  // 4. Logika Indikator LED LED
  if (!dsReady) {
    // Mode Tidak Terdeteksi: Toggle berkedip 1 Hz
    if (now - lastBlinkUpdate >= 500) {
      lastBlinkUpdate = now;
      ledState = !ledState;
      digitalWrite(LED_SYNC_INFO, ledState ? HIGH : LOW);
    }
  } else {
    // Mode Terdeteksi: Mati jika tidak sinkron, Nyala jika sinkron
    digitalWrite(LED_SYNC_INFO, isSynced ? HIGH : LOW);
  }
}