#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Inisialisasi LCD: alamat I2C 0x27, ukuran 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Konfigurasi keypad
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// Ganti dengan pin milik Anda
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void setup() {
  Serial.begin(115200);

  // Inisialisasi LCD
  lcd.init();       // untuk library LiquidCrystal_I2C
  lcd.backlight();  // nyalakan backlight
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Keypad Siap...");

  Serial.println("Keypad dan LCD siap...");
}

void loop() {
  char key = keypad.getKey();

  if (key) {
    Serial.print("Tombol ditekan: ");
    Serial.println(key);

    // Tampilkan ke LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Tombol ditekan:");
    lcd.setCursor(6, 1);  // tampil di tengah baris bawah
    lcd.print(key);
  }
}
