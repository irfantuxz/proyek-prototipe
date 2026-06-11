/* 
  Dual-Channel ADC Voltmeter ADS1115
*/

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <LiquidCrystal_I2C.h>

// ========================
// Konfigurasi I2C devices
// ========================
#define LCD_ADDR 0x27                     // default address 0x27
#define ADS_ADDR 0x48                     // default address 0x48
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);   // LCD 20x4 I2C
Adafruit_ADS1115 ads;                     // ADS1115
bool lcdReady = false;
bool adsReady = false;

// ========================
// Konfigurasi divider & ADS
// ========================
// Resistor pembagi tengangan (330k + 11k) / 11k = 31.0 (ideal)
const float DIVIDER_FACTOR[2] = {31.65552f , 31.16345f};

const float FS_GAIN_ONE      = 4.096f;  // ±4.096V
const float FS_GAIN_TWO      = 2.048f;  // ±2.048V
const float FS_GAIN_FOUR     = 1.024f;  // ±1.024V
const float FS_GAIN_EIGHT    = 0.512f;  // ±0.512V
const float FS_GAIN_SIXTEEN  = 0.256f;  // ±0.256V

// EMA smoothing
const float ALPHA_EMA    = 0.90f;            // 0<alpha<=1  0.9 respon cukup cepat
bool  emaInitialized[2]  = {false, false};   // EMA sudah masuk
float VadcEMA[2]  = {0.0f, 0.0f};            // Vadc yg sudah EMA
float VinEMA[2]   = {0.0f, 0.0f};            // Vin yg sudah EMA
float lastVadc[2] = {0.0f, 0.0f};            // Vadc dengan gain yang dipakai terakhir
float lastVin[2]  = {0.0f, 0.0f};            // Vin sekali baca (sebelum EMA)

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof(array[0]))

// custom glyphs 16
byte customChar[1][8] = {
  {0x17,0x14,0x14,0x17,0x15,0x15,0x17,0x00}   // karakter "16" dalam 1 kotak
};

// ========================
// Fungsi utilitas
// ========================
float rawToVadc(int16_t raw, float fs) {
  // raw = 0..32767 untuk single-ended positif
  if (raw < 0) raw = 0;
  return (float)raw * (fs / 32768.0f);
}

float kalibrasi(float inputSensor, int channel) {
  switch (channel) {
    case 0 : return 1.00213f * inputSensor; break;
    case 1 : return 1.00219f * inputSensor; break;
  }
  return 1.0f * inputSensor;
}

// ========================
// Baca Vin dengan auto-gain "sticky"
// - Memakai GAIN_ONE, TWO, FOUR, EIGHT, SIXTEEN
// - GAIN tetap selama raw masih di range aman
// - Naik / turun gain hanya bila raw < RAW_LOW atau raw > RAW_HIGH
// ========================
float readVinAutoGain(uint8_t channel) {
  // State gain saat ini (persisten antar pemanggilan)
  static adsGain_t currentGain = GAIN_ONE;
  static float     currentFS   = FS_GAIN_ONE;

  // Batas raw ADC (0..32767) untuk naik/turun gain
  const int16_t RAW_HIGH = 21845;   // ~66% FS
  const int16_t RAW_LOW  = 10923;   // ~33% FS

  // Pastikan ADS pakai gain saat ini
  ads.setGain(currentGain);

  // Baca sekali dengan gain sekarang
  int16_t raw = ads.readADC_SingleEnded(channel);

  while (true) {
    bool        needChange = false;
    adsGain_t   nextGain   = currentGain;
    float       nextFS     = currentFS;

    lcd.setCursor(15, channel);
    // ---- Cek apakah perlu geser gain ----
    if (raw > RAW_HIGH) {
      // Terlalu dekat full-scale -> turunkan sensitivitas (range diperlebar)
      if (currentGain == GAIN_SIXTEEN) {
        nextGain = GAIN_EIGHT;
        nextFS   = FS_GAIN_EIGHT;
        needChange = true;
        lcd.print("8");
      } else if (currentGain == GAIN_EIGHT) {
        nextGain = GAIN_FOUR;
        nextFS   = FS_GAIN_FOUR;
        needChange = true;
        lcd.print("4");
      } else if (currentGain == GAIN_FOUR) {
        nextGain = GAIN_TWO;
        nextFS   = FS_GAIN_TWO;
        needChange = true;
        lcd.print("2");
      } else if (currentGain == GAIN_TWO) {
        nextGain = GAIN_ONE;
        nextFS   = FS_GAIN_ONE;
        needChange = true;
        lcd.print("1");
      } else {
        // currentGain == GAIN_ONE -> sudah range paling lebar, tidak bisa naik lagi
      }
    }
    else if (raw < RAW_LOW) {
      // Sinyal terlalu kecil -> naikkan sensitivitas (range dipersempit)
      if (currentGain == GAIN_ONE) {
        nextGain = GAIN_TWO;
        nextFS   = FS_GAIN_TWO;
        needChange = true;
        lcd.print("2");
      } else if (currentGain == GAIN_TWO) {
        nextGain = GAIN_FOUR;
        nextFS   = FS_GAIN_FOUR;
        needChange = true;
        lcd.print("4");
      } else if (currentGain == GAIN_FOUR) {
        nextGain = GAIN_EIGHT;
        nextFS   = FS_GAIN_EIGHT;
        needChange = true;
        lcd.print("8");
      } else if (currentGain == GAIN_EIGHT) {
        nextGain = GAIN_SIXTEEN;
        nextFS   = FS_GAIN_SIXTEEN;
        needChange = true;
        lcd.write(0);
      } else {
        // currentGain == GAIN_SIXTEEN -> sudah paling sensitif, tidak bisa ditambah
      }
    }

    // Kalau sudah di tengah range aman -> tidak perlu ubah gain
    if (!needChange) {
      break;
    }

    // ---- Update gain & FS ----
    currentGain = nextGain;
    currentFS   = nextFS;
    ads.setGain(currentGain);

    // Dummy read untuk buang hasil lama + tunggu konversi baru
    ads.readADC_SingleEnded(channel);
    while (!ads.conversionComplete()) {
      // tunggu hingga konversi selesai
    }

    // Baca ulang dengan gain baru
    raw = ads.readADC_SingleEnded(channel);

    // Loop lagi: kalau masih di luar range, bisa turun/naik satu step lagi
  }

  // Konversi raw final -> Vadc & Vin
  float Vsensor = rawToVadc(raw, currentFS);
  float Vadc = kalibrasi(Vsensor, channel);
  float Vin  = Vadc * DIVIDER_FACTOR[channel];

  // Simpan untuk display
  lastVadc[channel] = Vadc;
  lastVin[channel]  = Vin;

  return Vin;
}

// ========================
// Setup
// ========================
void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin();  // SDA=21, SCL=22 default ESP32

  // Init
  lcdReady = tryInitLCD();
  adsReady = tryInitADS();

  //lcd.print("1234567890123456");
  if (lcdReady) {
    lcd.setCursor(0, 0);
    lcd.print("2ch DC Voltmeter");
    lcd.setCursor(0, 1);
    lcd.print("ADS1115 AutoGain");
    // lcd.setCursor(0, 2);
    // lcd.print("NTC InternetTime");
    // lcd.setCursor(0, 3);
    // lcd.print("Data Logging SDC");
    delay(1000);
    lcd.clear();
  }else{
    Serial.println("Program boot, LCD not detected!");
  }
}

// ========================
// Loop utama
// ========================
void loop() {
  // Hotplug Routines
  lcdReady = handleHotplug(LCD_ADDR, lcdReady, tryInitLCD, "LCD");
  adsReady = handleHotplug(ADS_ADDR, adsReady, tryInitADS, "ADS");

  if (adsReady) {

    for (int i = 0; i < 2; i++) {
      
      // 1. Baca Vin dengan auto-gain (sekali baca = 1..2 konversi ADS)
      readVinAutoGain(i);

      // 2. EMA smoothing
      if (!emaInitialized[i]) {
        VinEMA[i]         = lastVin[i];
        emaInitialized[i] = true;
      } else {
        VinEMA[i] = ALPHA_EMA * lastVin[i] + (1.0f - ALPHA_EMA) * VinEMA[i];
      }

      VadcEMA[i] = VinEMA[i] / DIVIDER_FACTOR[i];

      // 3. Debug ke Serial
      Serial.print("Channel : ");
      Serial.print(i);
      Serial.print(" -- Vadc=");
      Serial.print(lastVadc[i], 5);
      Serial.print(" V  Vin=");
      Serial.print(lastVin[i], 5);
      Serial.println(" V");

    }

  } else {
    Serial.println("ADS1115 disconnected");
  }

  if (lcdReady) {

    // 4. Tampilkan ke LCD
    char buf[17];

    for (int i = 0; i < 2; i++) {
      lcd.setCursor(0, i);
      lcd.print("Ch");
      lcd.print(i + 1);
      lcd.print(" :");
      dtostrf(VinEMA[i], 7, 3, buf);   // xxx.xxx
      lcd.setCursor(5, i);
      lcd.print(buf);
      lcd.print(" V ");
    }

  }else{
    Serial.println("LCD disconnected");
  }
  delay(200);  // update ~5x/detik
}

// =============================
// Fungsi bantu inisiasi ulang
// =============================

bool tryInitLCD() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  for (int i = 0; i < ARRAY_LENGTH(customChar); i++ )
    lcd.createChar(i, customChar[i]);
  Serial.println("LCD detected");
  return true;
}

bool tryInitADS() {
  if (!ads.begin()) {
    Serial.println("Fail init ADS1115");
    return false;
  }
  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_16SPS);
  return true;
}

// Cek device dan panggil init jika perlu
bool handleHotplug(uint8_t addr, bool &readyFlag, bool (*initFunc)(), const char *name) {
  bool connected = isDeviceConnected(addr);
  if (!connected && readyFlag) {
    Serial.print(name); Serial.println(" unplugged!");
    readyFlag = false;
  } else if (connected && !readyFlag) {
    Serial.print(name); Serial.println(" plugged~");
    readyFlag = initFunc();
  }
  return readyFlag;
}

bool isDeviceConnected(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}
