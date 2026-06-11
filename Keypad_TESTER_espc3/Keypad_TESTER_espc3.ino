#include <Arduino.h>
#include <U8g2lib.h>
#include <Keypad.h>
#include <Wire.h>

// ==========================================
// KONFIGURASI PERANGKAT KERAS
// ==========================================
#define BUTTON_MODE_PIN 2

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

const byte MAX_ROWS = 4; 
const byte MAX_COLS = 4; 

char keys4x4[MAX_ROWS][MAX_COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

char keys3x4[MAX_ROWS][3] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

char keys1x4[1][MAX_COLS] = {
  {'1','2','3','4'}
};

// ==========================================
// MANAJEMEN MODE SISTEM
// ==========================================
enum SystemMode {
  MODE_MEMBRANE_4X4 = 0,
  MODE_TACTILE_3X4 = 1,
  MODE_TACTILE_4X4 = 2,
  MODE_PHONE_3X4 = 3,
  MODE_MEMBRANE_1X4 = 4
};

SystemMode currentMode = MODE_MEMBRANE_4X4;
Keypad* activeKeypad = nullptr;
char lastPressedKey = '-';

// Struktur Data Konfigurasi Mode
struct ModeConfig {
  String title;
  const char* pinDisplay;
};

ModeConfig modeInfo[5] = {
  {"1. Membran 4x4", "R1 R2 R3 R4 C1 C2 C3 C4"},
  {"2. Tactile 3x4", "C2 R1 C1 R4 C3 R3 R2 --"},
  {"3. Tactile 4x4", "C1 C2 C3 C4 R1 R2 R3 R4"},
  {"4. Phone 3x4",   "C1 C2 C3 R1 R2 R3 R4 --"},
  {"5. Membran 1x4", "R1 C2 C1 C4 C3 -- -- --"}
};

// Variabel Debounce Tombol
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ==========================================
// DEKLARASI FUNGSI
// ==========================================
void initSystem();
void configureKeypad(SystemMode mode);
void checkModeButton();
void checkKeypad();
void updateDisplay(char key, SystemMode mode);

// ==========================================
// UTAMA
// ==========================================
void setup() {
  Serial.begin(115200);
  initSystem();
}

void loop() {
  checkModeButton();
  checkKeypad();
}

// ==========================================
// IMPLEMENTASI FUNGSI
// ==========================================

void initSystem() {
  Wire.begin(0, 1); 
  u8g2.begin();
  pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
  configureKeypad(currentMode);
}

void configureKeypad(SystemMode mode) {
  if (activeKeypad != nullptr) {
    delete activeKeypad; 
  }

  byte* rowPins;
  byte* colPins;
  byte rCount = 4;
  byte cCount = 4;
  char* keyMap;

  // Pemetaan Array Statis (Pin Fisik Berurutan: 5,6,7,8,9,10,20,21)
  static byte rM1[] = {5, 6, 7, 8};
  static byte cM1[] = {9, 10, 20, 21};
  
  static byte rM2[] = {6, 20, 10, 8};
  static byte cM2[] = {7, 5, 9};

  static byte rM3[] = {9, 10, 20, 21};
  static byte cM3[] = {5, 6, 7, 8};

  static byte rM4[] = {8, 9, 10, 20};
  static byte cM4[] = {5, 6, 7};

  static byte rM5[] = {5};
  static byte cM5[] = {7, 6, 9, 8};

  switch (mode) {
    case MODE_MEMBRANE_4X4:
      rowPins = rM1; colPins = cM1;
      rCount = 4; cCount = 4;
      keyMap = makeKeymap(keys4x4);
      break;
    case MODE_TACTILE_3X4:
      rowPins = rM2; colPins = cM2;
      rCount = 4; cCount = 3;
      keyMap = makeKeymap(keys3x4);
      break;
    case MODE_TACTILE_4X4:
      rowPins = rM3; colPins = cM3;
      rCount = 4; cCount = 4;
      keyMap = makeKeymap(keys4x4);
      break;
    case MODE_PHONE_3X4:
      rowPins = rM4; colPins = cM4;
      rCount = 4; cCount = 3;
      keyMap = makeKeymap(keys3x4);
      break;
    case MODE_MEMBRANE_1X4:
      rowPins = rM5; colPins = cM5;
      rCount = 1; cCount = 4;
      keyMap = makeKeymap(keys1x4);
      break;
  }

  activeKeypad = new Keypad(keyMap, rowPins, colPins, rCount, cCount);
  lastPressedKey = '-';
  updateDisplay(lastPressedKey, mode);
  Serial.println("Mode aktif diganti.");
}

void checkModeButton() {
  int reading = digitalRead(BUTTON_MODE_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW) {
      currentMode = static_cast<SystemMode>((currentMode + 1) % 5);
      configureKeypad(currentMode);
      
      while(digitalRead(BUTTON_MODE_PIN) == LOW) {
        delay(10); 
      }
    }
  }
  lastButtonState = reading;
}

void checkKeypad() {
  if (activeKeypad == nullptr) return;
  char customKey = activeKeypad->getKey();
  
  if (customKey) {
    lastPressedKey = customKey;
    updateDisplay(lastPressedKey, currentMode);
  }
}

void updateDisplay(char key, SystemMode mode) {
  u8g2.clearBuffer();
  
  // 1. Judul Mode
  u8g2.setFont(u8g2_font_profont12_tf);
  u8g2.setCursor(0, 10);
  u8g2.print(modeInfo[mode].title);
  
  // 2. Tampilan Pinout (Menggunakan font kecil 4x6 untuk muat 128px)
  u8g2.setFont(u8g2_font_4x6_tr);
  // Tambahkan spasi agar rata dengan posisi pin secara visual
  u8g2.setCursor(0, 22);
  u8g2.print("[P 5 6 7 8 9 10 20 21]"); // Panduan header
  u8g2.setCursor(0, 30);
  u8g2.print(modeInfo[mode].pinDisplay); 
  
  u8g2.drawHLine(0, 34, 128);
  
  // 3. Karakter Terakhir
  u8g2.setFont(u8g2_font_logisoso24_tf);
  char buf[2] = {key, '\0'}; 
  u8g2.drawStr(54, 62, buf);
  
  u8g2.sendBuffer();
}