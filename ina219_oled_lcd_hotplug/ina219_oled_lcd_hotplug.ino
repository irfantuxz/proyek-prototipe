// by @irfan_tuxz - May 2025
// LCD i2c, OLED SSD1306 (U8g2), Sensor INA219
// Volt-meter & Ampere-meter dengan dukungan hotplug

#include <Wire.h>
#include <Adafruit_INA219.h>
#include <LiquidCrystal_I2C.h>
#include <U8g2lib.h>

// Alamat I2C
#define LCD_ADDR  0x27
#define OLED_ADDR 0x3D
#define INA_ADDR  0x40

// Objek global
Adafruit_INA219 ina219(INA_ADDR);
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

bool lcdReady = false;
bool oledReady = false;
bool inaReady = false;
bool blink = 0;


// Ambil posisi sekarang, geser ke kanan (membuat spasi ramping)
void addSpace(uint8_t width = 5) {
  uint16_t x = u8g2.getCursorX();
  uint16_t y = u8g2.getCursorY();
  u8g2.setCursor(x + width, y);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  Wire.begin();
  
  lcdReady  = tryInitLCD();
  oledReady = tryInitOLED();
  inaReady  = tryInitINA();
}

void loop() {
  // Deteksi hotplug
  lcdReady  = handleHotplug(LCD_ADDR, lcdReady, tryInitLCD, "LCD");
  oledReady = handleHotplug(OLED_ADDR, oledReady, tryInitOLED, "OLED");
  inaReady  = handleHotplug(INA_ADDR, inaReady, tryInitINA, "INA219");

  float busVoltage = 0, current_mA = 0;
  if (inaReady) {
    busVoltage = ina219.getBusVoltage_V();
    current_mA = ina219.getCurrent_mA();

    Serial.print(F("Voltage : ")); Serial.print(busVoltage); Serial.println(F(" V"));
    Serial.print(F("Current : ")); Serial.print(current_mA); Serial.println(F(" mA"));
    Serial.println();
  }

  // Tampilkan ke LCD jika tersedia
  if (lcdReady) {
    lcd.setCursor(0, 0);
    if (inaReady) {
      lcd.print(F("Volt : "));
      lcd.print(busVoltage, 2);
      lcd.print(F(" V   "));

      lcd.setCursor(0, 1);
      lcd.print(F("Curr : "));
      lcd.print(current_mA, 1);
      lcd.print(F(" mA   "));
    } else {
      lcd.print(F("INA219 terlepas "));
      lcd.setCursor(0, 1);
      lcd.print(F("Reconnecting... "));
    }
  }

  // Tampilkan ke OLED jika tersedia
  if (oledReady) {

    u8g2.firstPage();
    do {
      u8g2.clearBuffer();
      //u8g2.setFont(u8g2_font_neuecraft_tr);
      u8g2.setFont(u8g2_font_profont22_tr);
      if (inaReady) {
        u8g2.setCursor(0, 25);
        u8g2.print(F("V:")); addSpace(); u8g2.print(busVoltage, 2); addSpace(); u8g2.print(F("V"));

        u8g2.setCursor(0, 52);
        u8g2.print(F("I:")); addSpace(); u8g2.print(current_mA, 1); addSpace(); u8g2.print(F("mA"));
      } else {
        u8g2.setCursor(0, 25);
        u8g2.print(F("INA219"));

        u8g2.setCursor(0, 52);
        u8g2.print(F("NotFound"));
      }
      u8g2.sendBuffer();
    } while (u8g2.nextPage());

  }

  delay(500);
  blink = !blink;
  digitalWrite(LED_BUILTIN, blink);
}

// =============================
// Fungsi bantu inisiasi ulang
// =============================

bool tryInitINA() {
  if (ina219.begin()) {
    ina219.setCalibration_16V_400mA();
    Serial.println(F("INA219 Terdeteksi."));

    if (lcdReady) {
      lcd.setCursor(0, 0);
      lcd.print(F("INA219 terdeteksi"));
      lcd.setCursor(0, 1);
      lcd.print(F("Connecting..."));
    }

    if (oledReady) {
      u8g2.clearBuffer();
      u8g2.setCursor(0, 25);
      u8g2.print(F("INA219"));
      u8g2.setCursor(0, 52);
      u8g2.print(F("Connect"));
      u8g2.sendBuffer();
    }

    delay(1200);
    return true;
  }
  return false;
}

bool tryInitLCD() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  Serial.println(F("LCD Terdeteksi."));
  return true;
}

bool tryInitOLED() {
  u8g2.setI2CAddress(OLED_ADDR * 2); 
  u8g2.begin();
  Serial.println(F("OLED Terdeteksi."));
  return true;
}

// Cek device dan panggil init jika perlu
bool handleHotplug(uint8_t addr, bool &readyFlag, bool (*initFunc)(), const char *name) {
  bool connected = isDeviceConnected(addr);
  if (!connected && readyFlag) {
    Serial.print(name); Serial.println(F(" terputus!"));
    readyFlag = false;
  } else if (connected && !readyFlag) {
    Serial.print(name); Serial.println(F(" terhubung kembali."));
    readyFlag = initFunc();
  }
  return readyFlag;
}

bool isDeviceConnected(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}
