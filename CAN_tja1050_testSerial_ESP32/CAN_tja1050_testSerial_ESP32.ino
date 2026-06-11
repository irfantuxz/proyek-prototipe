/*
  ESP32 Dev Module + TJA1050
  Simple CAN chat antara 2 ESP32 via Serial Monitor.
  
  Topologi:
    Laptop 1 <-> ESP32 #1 <-> TJA1050 #1 <-> CAN bus <-> TJA1050 #2 <-> ESP32 #2 <-> Laptop 2

  - Ketik di Serial Monitor Laptop 1 -> muncul di Serial Monitor Laptop 2
  - Ketik di Serial Monitor Laptop 2 -> muncul di Serial Monitor Laptop 1
  - LED built-in (GPIO2) akan kedip tiap kali ada TX atau RX frame CAN.

  Pin mapping:
    ESP32 GPIO5 -> CAN_TX -> TXD TJA1050
    ESP32 GPIO4 -> CAN_RX -> RXD TJA1050
    ESP32 GPIO2 -> Built-in LED (indikator aktivitas CAN)

  Perhatian:
  - Pastikan level logika RXD TJA1050 ke ESP32 tidak 5V langsung (gunakan level shifter
    atau modul yang memang sudah 3.3V compatible).
*/

#include <driver/twai.h>   // TWAI = CAN controller internal ESP32

// -------------------- Konfigurasi Pin --------------------
static const gpio_num_t CAN_TX_PIN = GPIO_NUM_5;  // TX ke TJA1050 TXD
static const gpio_num_t CAN_RX_PIN = GPIO_NUM_4;  // RX dari TJA1050 RXD
const int LED_CAN = 2;                            // Built-in LED ESP32 (IO2)

// -------------------- Konfigurasi CAN --------------------
// Bitrate: pilih salah satu (gunakan 500 kbps sebagai default)
twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
// alternatif:
// twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
// twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();

// General config: TX pin, RX pin, mode normal
twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
  CAN_TX_PIN,
  CAN_RX_PIN,
  TWAI_MODE_NORMAL
);

// Filter: terima semua ID
twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

// -------------------- Variabel LED Aktivitas --------------------
unsigned long lastActivityMillis = 0;
const unsigned long LED_ON_TIME = 50;  // LED menyala 50 ms tiap aktivitas

// -------------------- Fungsi bantu: pulse LED saat ada data --------------------
void canActivityPulse() {
  digitalWrite(LED_CAN, HIGH);
  lastActivityMillis = millis();
}

// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // tunggu Serial siap (untuk beberapa board)
  }

  pinMode(LED_CAN, OUTPUT);
  digitalWrite(LED_CAN, LOW);

  Serial.println();
  Serial.println("==== ESP32 CAN Chat via TJA1050 ====");
  Serial.println("GPIO5 -> TJA1050 TXD");
  Serial.println("GPIO4 -> TJA1050 RXD");
  Serial.println("LED   -> GPIO2 (aktivitas CAN)");
  Serial.println("Baud Serial: 115200, CAN bitrate: 500 kbps");
  Serial.println("Ketik di sini, teks akan dikirim via CAN ke node lain.");
  Serial.println("====================================");

  // Install driver CAN (TWAI)
  esp_err_t res;

  res = twai_driver_install(&g_config, &t_config, &f_config);
  if (res != ESP_OK) {
    Serial.print("Error twai_driver_install: ");
    Serial.println(esp_err_to_name(res));
    while (true) {
      digitalWrite(LED_CAN, !digitalRead(LED_CAN));
      delay(200);
    }
  }

  res = twai_start();
  if (res != ESP_OK) {
    Serial.print("Error twai_start: ");
    Serial.println(esp_err_to_name(res));
    while (true) {
      digitalWrite(LED_CAN, !digitalRead(LED_CAN));
      delay(200);
    }
  }

  Serial.println("CAN controller started OK.");
}

// -------------------- Loop --------------------
void loop() {
  // 1. Kirim data dari Serial ke CAN (per karakter)
  if (Serial.available() > 0) {
    char c = Serial.read();

    // Siapkan frame CAN
    twai_message_t tx_msg;
    tx_msg.identifier = 0x100;        // ID CAN standar (11-bit)
    tx_msg.extd = 0;                  // 0 = Standard ID
    tx_msg.rtr = 0;                   // Data frame, bukan remote frame
    tx_msg.ss = 0;                    // Single shot disabled
    tx_msg.self = 0;                  // Self reception disabled
    tx_msg.dlc_non_comp = 0;          // DLC normal
    tx_msg.data_length_code = 1;      // hanya 1 byte data (karakter)
    tx_msg.data[0] = (uint8_t)c;

    esp_err_t tx_res = twai_transmit(&tx_msg, pdMS_TO_TICKS(100)); // timeout 100 ms
    if (tx_res == ESP_OK) {
      canActivityPulse();  // LED nyala singkat saat TX berhasil
    } else {
      Serial.print("[TX ERR] ");
      Serial.println(esp_err_to_name(tx_res));
    }
  }

  // 2. Terima data dari CAN dan kirim ke Serial
  twai_message_t rx_msg;
  esp_err_t rx_res = twai_receive(&rx_msg, 0);  // non-blocking (timeout 0 ms)

  if (rx_res == ESP_OK) {
    // Ada frame diterima
    canActivityPulse();  // LED nyala singkat saat RX

    // Hanya proses jika data length > 0
    if (rx_msg.data_length_code > 0) {
      // Di demo ini kita kirim 1 karakter per frame
      char c = (char)rx_msg.data[0];
      Serial.write(c);  // tampilkan apa adanya di Serial Monitor
    }
  } else if (rx_res != ESP_ERR_TIMEOUT) {
    // Error selain timeout
    Serial.print("[RX ERR] ");
    Serial.println(esp_err_to_name(rx_res));
  }

  // 3. Matikan LED setelah durasi tertentu
  if (digitalRead(LED_CAN) == HIGH) {
    if (millis() - lastActivityMillis >= LED_ON_TIME) {
      digitalWrite(LED_CAN, LOW);
    }
  }

  // Loop cepat saja, tidak perlu delay besar
}
