#include <Wire.h>
#include <AS5600.h>
#include <LiquidCrystal_I2C.h>

// Inisialisasi objek untuk LCD I2C (alamat default 0x27, bisa berbeda)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Inisialisasi objek untuk AS5600
AS5600 sensor;

// Variabel untuk penghalusan nilai sudut
const int numReadings = 25;  // Jumlah pembacaan untuk penghalusan
int readings[numReadings];   // Array untuk menyimpan pembacaan
int readIndex = 0;           // Indeks untuk pembacaan saat ini
long total = 0;              // Jumlah total untuk penghalusan
int average = 0;             // Nilai rata-rata sudut
int lastAngle = 0;           // Nilai sudut sebelumnya

void setup() {
  // Mulai komunikasi serial untuk monitor
  Serial.begin(115200);
  
  // Mulai komunikasi I2C
  Wire.begin();
  
  // Inisialisasi LCD
  lcd.init(); // Memulai LCD
  lcd.backlight(); // Menyalakan backlight LCD
  lcd.print("Sensor AS5600");
  delay(500); // Delay untuk menampilkan pesan sementara
  
  // Inisialisasi sensor AS5600
  if (sensor.begin()) {
    Serial.println("AS5600 sensor initialized.");
  } else {
    Serial.println("Failed to initialize AS5600 sensor.");
    while (1); // Jika sensor gagal diinisialisasi, berhenti
  }
  delay(500); // Delay untuk menampilkan pesan sementara
  
  // Inisialisasi array pembacaan untuk penghalusan
  for (int i = 0; i < numReadings; i++) {
    readings[i] = 0;
  }
  
  // Inisialisasi sudut terakhir
  lastAngle = sensor.readAngle();
}

void loop() {
  // Membaca nilai sudut dari sensor AS5600 (0-4095)
  int angleRaw = sensor.readAngle();
  
  // Menghapus pembacaan tertua dari total
  total -= readings[readIndex];
  
  // Penanganan wraparound untuk perubahan besar
  int delta = angleRaw - lastAngle; // Perbedaan dengan pembacaan sebelumnya
  if (delta > 2048) {
    delta -= 4096;  // Jika perubahan lebih dari 2048, dianggap putaran balik
  } else if (delta < -2048) {
    delta += 4096;  // Jika perubahan lebih dari -2048, dianggap putaran balik
  }

  // Menyimpan pembacaan terbaru
  readings[readIndex] = angleRaw;
  
  // Menambahkan pembacaan terbaru ke total
  total += readings[readIndex];
  
  // Menentukan rata-rata
  readIndex++;
  if (readIndex >= numReadings) {
    readIndex = 0;
  }
  
  // Nilai rata-rata sudut mentah (0-4095)
  average = total / numReadings;
  
  // Menghitung nilai sudut dalam derajat (0-360)
  float angleDegree = (float)average / 4095 * 360;

  // Menampilkan nilai sudut mentah (0-4095) di LCD sebelah kiri
  lcd.setCursor(0, 0);
  lcd.print("Data : ");
  lcd.print(average);
  lcd.print("   ");
  
  // Menampilkan nilai sudut dalam derajat (0-360°) di LCD sebelah kanan
  lcd.setCursor(0, 1);
  lcd.print("Angle: ");
  lcd.print((int)angleDegree);  // Tampilkan sudut dalam derajat
  lcd.print((char)0xDF);  // Simbol derajat
  lcd.print("  ");
  
  // Menampilkan nilai sudut pada serial monitor
  Serial.print("Data: ");
  Serial.print(average);
  Serial.print("\tAngle: ");
  Serial.println(angleDegree);
  
  // Memperbarui sudut terakhir
  lastAngle = angleRaw;
  
  // Tunggu 100 ms
  delay(1);
}
