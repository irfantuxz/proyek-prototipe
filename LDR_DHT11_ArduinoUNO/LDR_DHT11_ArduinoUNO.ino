#include <Wire.h>
#include <DHT11.h>  //Dhruba Saha 2.1
#include <U8g2lib.h>

// Definisi Pin
#define DHTPIN A1
#define DHTTYPE DHT11
#define LDRPIN A0

// Inisialisasi Objek
DHT11 dht11(DHTPIN);
// Gunakan mode 1 (Page Buffer) untuk HW I2C
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Bitmap Ikon
static const unsigned char image_iconHumi_bits[] U8X8_PROGMEM = {0x20,0x00,0x20,0x00,0x30,0x00,0x50,0x00,0x48,0x00,0x88,0x00,0x04,0x01,0x04,0x01,0x82,0x02,0x02,0x03,0x01,0x05,0x01,0x04,0x02,0x02,0x02,0x02,0x0c,0x01,0xf0,0x00};
static const unsigned char image_iconLight_bits[] U8X8_PROGMEM = {0x04,0x20,0xc8,0x13,0x20,0x04,0x10,0x08,0x95,0xa8,0x90,0x09,0x90,0x08,0x24,0x24,0x42,0x42,0x80,0x00,0xc0,0x03,0x00,0x00,0xc0,0x03,0x00,0x00,0x80,0x01,0x00,0x00};
static const unsigned char image_iconTemp_bits[] U8X8_PROGMEM = {0x38,0x00,0x44,0x40,0xd4,0xa0,0x54,0x40,0xd4,0x1c,0x54,0x06,0xd4,0x02,0x54,0x02,0x54,0x06,0x92,0x1c,0x39,0x01,0x75,0x01,0x7d,0x01,0x39,0x01,0x82,0x00,0x7c,0x00};

// Variabel Global
int suhu = 0;
int kelembapan = 0;
int lux = 0;

// Buffer string untuk layar
char strLight[16];
char strTemp[16];
char strHumi[16];

void setup() {
    Serial.begin(9600);
    
    // Inisialisasi Sensor
    dht11.setDelay(500);
    u8g2.begin();
}

void loop() {
    bacaSensor();
    kirimSerialMonitor();
    siapkanDataLayar();
    tampilkanLayar();
    
    // Jeda 1 detik. DHT11 lambat merespons data baru.
    delay(1000); 
}

/* * FUNGSI MODULAR: Membaca semua sensor.
 * Pisahkan logika pembacaan agar mudah ditambah filter data (misal: Moving Average).
 */
void bacaSensor() {
    // Baca DHT11. Proses ini memakan waktu sekitar 250ms.
    int result = dht11.readTemperatureHumidity(suhu, kelembapan);

    // Validasi data DHT. Hindari pengiriman nilai "NaN" ke HMI atau sistem kendali.
    if (isnan(suhu) || isnan(kelembapan)) {
        suhu = 0;
        kelembapan = 0;
    }

    // Baca LDR. ADC Arduino 10-bit (0-1023).
    int nilaiAnalog = analogRead(LDRPIN);
    
    // Konversi kasar ke Lux. LDR tidak linier. 
    // Nilai ini sangat bergantung pada resistor pembagi tegangan (voltage divider) yang dipakai.
    // Jika menggunakan resistor 10k, rumus aproksimasi sederhana diperlukan.
    lux = map(nilaiAnalog, 0, 1023, 10000, 0); 

    if (lux < 2200) {
      lux = map(lux, 0, 2200, 0, 20);
    }else if (lux < 7700) {
      lux = map(lux, 2200, 7700, 20, 170);
    }else{
      lux = map(lux, 7700, 10000, 170, 4000);
    }
}

/* * FUNGSI MODULAR: Pengiriman data Serial.
 * Format dikunci sesuai spesifikasi: lux,suhu'C,humidity% : 1000,30,90
 */
void kirimSerialMonitor() {
    Serial.print("lux,suhu'C,humidity% : ");
    Serial.print(lux);
    Serial.print(",");
    Serial.print((int)suhu); 
    Serial.print(",");
    Serial.println((int)kelembapan);
}

/* * FUNGSI MODULAR: Konversi nilai numerik ke string.
 * Arduino Uno kurang optimal menangani fungsi String (menyebabkan memori bocor).
 * Gunakan sprintf dengan buffer array karakter.
 */
void siapkanDataLayar() {
    sprintf(strLight, "%dlux", lux);
    sprintf(strTemp, "%d\xb0" "C", (int)suhu); // \xb0 adalah kode heksadesimal simbol derajat
    sprintf(strHumi, "%d%%", (int)kelembapan);
}

/* * FUNGSI MODULAR: Render objek ke memori layar.
 */
void drawScreen_2(void) {
    u8g2.setFontMode(1);
    u8g2.setBitmapMode(1);
    u8g2.setFont(u8g2_font_timR10_tr);
    
    // Label
    u8g2.drawStr(10, 10, "Light");
    u8g2.drawStr(54, 10, "Temp");
    u8g2.drawStr(100, 10, "Humi");
    
    // Ikon
    u8g2.drawXBMP(17, 18, 16, 16, image_iconLight_bits);
    u8g2.drawXBMP(65, 18, 16, 16, image_iconTemp_bits);
    u8g2.drawXBMP(106, 18, 11, 16, image_iconHumi_bits);
    
    // Nilai Dinamis
    u8g2.drawStr(4, 56, strLight);
    u8g2.drawUTF8(61, 56, strTemp);
    u8g2.drawStr(100, 56, strHumi);
}

/* * FUNGSI MODULAR: Eksekusi rendering per halaman.
 * Menghemat penggunaan RAM (SRAM) pada Arduino Uno.
 */
void tampilkanLayar() {
    u8g2.firstPage();
    do {
        drawScreen_2();
    } while (u8g2.nextPage());
}