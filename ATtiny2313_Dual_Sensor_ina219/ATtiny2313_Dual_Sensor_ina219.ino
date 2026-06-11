// ATtiny 2313 - dual channel ina219 0x40 0x41 + LCD i2c 1602
// using ATtinyCore NoBootLoader - 12MHz / 11.0592MHz
// program by @irfan_tuxz

#include <util/delay.h>
#include <avr/wdt.h>

//====================== Konstanta & Konfigurasi ========================//
#define LED_PIN PB4
#define SDA_PIN PD4
#define SCL_PIN PD5

#define LCD_ADDR 0x27
#define INA219_ADD1 0x40
#define INA219_ADD2 0x41

// Bit kontrol modul antarmuka LCD I2C
#define En 0x04
#define Rw 0x02
#define Rs 0x01
#define BACKLIGHT 0x08

// Heksadesimal perintah instruksi kontroler HD44780
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET 0x20
#define LCD_SETDDRAMADDR 0x80
#define LCD_ENTRYLEFT 0x02
#define LCD_DISPLAYON 0x04
#define LCD_2LINE 0x08
#define LCD_5x8DOTS 0x00

//====================== Fungsi I2C Bit-Banged ========================//
// Makro jeda siklus instruksi untuk stabilitas bus I2C Standard Mode (100kHz)
static inline void i2c_delay() {
  asm volatile("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n");
}

static inline void sda_hi() {
  DDRD &= ~_BV(SDA_PIN);  PORTD |= _BV(SDA_PIN);
}
static inline void sda_lo() {
  DDRD |= _BV(SDA_PIN);   PORTD &= ~_BV(SDA_PIN);
}
static inline void scl_hi() {
  DDRD &= ~_BV(SCL_PIN);  PORTD |= _BV(SCL_PIN);
}
static inline void scl_lo() {
  DDRD |= _BV(SCL_PIN);   PORTD &= ~_BV(SCL_PIN);
}

static void i2c_start() {
  sda_hi();  scl_hi();  i2c_delay();
  sda_lo();  i2c_delay();
  scl_lo();  i2c_delay();
}

static void i2c_stop() {
  sda_lo();  i2c_delay();
  scl_hi();  i2c_delay();
  sda_hi();  i2c_delay();
}

bool i2c_write(uint8_t data) {
  for (uint8_t i = 0; i < 8; i++) {
    (data & 0x80) ? sda_hi() : sda_lo();
    i2c_delay();
    scl_hi();    i2c_delay();
    scl_lo();    i2c_delay();
    data <<= 1;
  }
  sda_hi();  i2c_delay();
  scl_hi();  i2c_delay();
  bool ack = !(PIND & _BV(SDA_PIN));
  scl_lo();  i2c_delay();
  return ack;
}

uint8_t i2c_read(bool ack) {
  uint8_t data = 0;
  sda_hi();
  for (uint8_t i = 0; i < 8; i++) {
    data <<= 1;
    scl_hi();    i2c_delay();
    if (PIND & _BV(SDA_PIN)) data |= 1;
    scl_lo();    i2c_delay();
  }
  ack ? sda_lo() : sda_hi();
  i2c_delay();
  scl_hi();  i2c_delay();
  scl_lo();  i2c_delay();
  sda_hi();
  return data;
}

static void i2c_bus_clear() {
  sda_hi();  // Set SDA high (pull-up)
  scl_hi();  // Set SCL high (pull-up)
  i2c_delay();

  // Jika SDA terdeteksi high, bus normal. Hentikan proses.
  if (PIND & _BV(SDA_PIN)) {
    return;
  }

  // Jika SDA low, kirim pulsa SCL hingga SDA dilepas.
  for (uint8_t i = 0; i < 9; i++) {
    scl_lo();    i2c_delay();
    scl_hi();    i2c_delay();

    // Cek jika slave sudah melepas jalur SDA (SDA kembali high).
    if (PIND & _BV(SDA_PIN)) {
      break;
    }
  }

  // Kirim kondisi I2C STOP untuk mereset state machine slave.
  scl_lo();  i2c_delay();
  sda_lo();  i2c_delay();
  scl_hi();  i2c_delay();
  sda_hi();  i2c_delay();
}

//======================= Fungsi LCD I2C =======================//
void lcd_expanderWrite(uint8_t data) {
  i2c_start();
  i2c_write(LCD_ADDR << 1);
  i2c_write(data | BACKLIGHT);
  i2c_stop();
}

void lcd_pulseEnable(uint8_t data) {
  lcd_expanderWrite(data | En);
  _delay_us(1);
  lcd_expanderWrite(data & ~En);
  _delay_us(50);
}

void lcd_write4bits(uint8_t value) {
  lcd_expanderWrite(value);
  lcd_pulseEnable(value);
}

void lcd_send(uint8_t value, uint8_t mode) {
  uint8_t high = (value & 0xF0) | mode;
  uint8_t low = ((value << 4) & 0xF0) | mode;
  lcd_write4bits(high);
  lcd_write4bits(low);
}

void lcd_command(uint8_t cmd) {
  lcd_send(cmd, 0);
}

// Rutin inisialisasi wajib PCF8574T. Menjamin sinkronisasi mode 4-bit.
void lcd_init() {
  lcd_expanderWrite(0);  _delay_ms(50);
  lcd_write4bits(0x30);  _delay_ms(5);
  lcd_write4bits(0x30);  _delay_us(150);
  lcd_write4bits(0x30);  _delay_us(150);
  lcd_write4bits(0x20);  _delay_us(150);

  lcd_command(LCD_FUNCTIONSET | LCD_2LINE | LCD_5x8DOTS);
  lcd_command(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
  lcd_command(LCD_CLEARDISPLAY);
  _delay_ms(2);
  lcd_command(LCD_ENTRYMODESET | LCD_ENTRYLEFT);
}

void lcd_setcursor(uint8_t row, uint8_t col) {
  const uint8_t offsets[] = { 0x00, 0x40 };
  if (row > 1) row = 1;
  lcd_command(LCD_SETDDRAMADDR | (col + offsets[row]));
}

void lcd_print(const char *str) {
  while (*str) lcd_send(*str++, Rs);
}

void lcd_print(long angka) {
  char buffer[12]; 
  ltoa(angka, buffer, 10); 
  lcd_print(buffer); 
}

//======================= Fungsi INA219 =======================//
void ina219_write(uint8_t reg, uint16_t val, uint8_t addr) {
  i2c_start();
  i2c_write(addr << 1);
  i2c_write(reg);
  i2c_write(val >> 8);
  i2c_write(val & 0xFF);
  i2c_stop();
}

uint16_t ina219_read(uint8_t reg, uint8_t addr) {
  uint8_t hb, lb;
  i2c_start();
  i2c_write(addr << 1);
  i2c_write(reg);
  i2c_stop();
  i2c_start();
  i2c_write((addr << 1) | 1);
  hb = i2c_read(true);
  lb = i2c_read(false);
  i2c_stop();
  return (hb << 8) | lb;
}

// Membaca register arus 0x04. Resolusi internal 50 µA.
// Shift kanan membagi 2 secara efisien. Resolusi output menjadi 100 µA (0.1 mA).
int16_t ina219_get_uA(uint8_t addr) {
  return (int16_t)ina219_read(0x04, addr) >> 1;
}

// Membaca register tegangan bus 0x02.
// Shift kanan 3 bit membuang flag OVF dan CNVR. Perkalian 4 sesuai rasio 4mV/LSB.
uint16_t ina219_get_mV(uint8_t addr) {
  return (ina219_read(0x02, addr) >> 3) * 4;
}

const uint8_t offset1 = 2;  // saat kondisi arus 0 pembacaan tidak 0;
const uint8_t offset2 = 2;

void ina219_init1(uint8_t addr) {
  ina219_write(0x05, 0x2000, addr);  // Kalibrasi LSB arus = 50 µA
  ina219_write(0x00, 0x1777, addr);  // Config: 16V, ±160mV, 12-bit + 128x Sample
}

void ina219_init2(uint8_t addr) {
  ina219_write(0x05, 0x2000, addr);  // Kalibrasi LSB arus = 50 µA
  ina219_write(0x00, 0x1777, addr);  // Config: 16V, ±160mV, 12-bit + 128x Sample
}

//======================= Fungsi Tambahan =======================//
bool LCDinit = 0;
bool INA1init = 0;
bool INA2init = 0;

bool scan_address(uint8_t addr7) {
  i2c_start();
  bool ack = i2c_write(addr7 << 1);
  i2c_stop();
  return ack;
}

// Format tegangan 8-karakter statis. Resolusi 0.01 V.
void print_voltage(uint8_t row, uint8_t col, uint16_t mV) {
  char buf[9] = { ' ', ' ', ' ', '.', ' ', ' ', ' ', 'V', '\0' };
  uint16_t cv = mV / 10;

  buf[5] = (cv % 10) + '0';  cv /= 10;
  buf[4] = (cv % 10) + '0';  cv /= 10;
  buf[2] = (cv % 10) + '0';  cv /= 10;
  if (cv > 0) buf[1] = (cv % 10) + '0';

  lcd_setcursor(row, col);
  lcd_print(buf);
}

// Format tampilan 8-karakter statis. Resolusi 0.1 mA.
void print_current(uint8_t row, uint8_t col, int16_t raw_val) {
  char buf[9] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'A', '\0' };
  bool is_neg = (raw_val < 0);
  uint16_t v = is_neg ? -raw_val : raw_val;

  if (v >= 10000) {
    buf[0] = is_neg ? '-' : ' ';
    buf[1] = (v / 10000) + '0';
    buf[2] = '.';
    buf[3] = ((v / 1000) % 10) + '0';
    buf[4] = ((v / 100) % 10) + '0';
    buf[5] = ((v / 10) % 10) + '0';
    buf[6] = (v % 10) + '0';
  } else {
    buf[6] = 'm';
    buf[5] = (v % 10) + '0';
    buf[4] = '.';

    uint16_t int_mA = v / 10;
    if (int_mA >= 100) {
      buf[3] = (int_mA % 10) + '0';
      buf[2] = ((int_mA / 10) % 10) + '0';
      buf[1] = (int_mA / 100) + '0';
      buf[0] = is_neg ? '-' : ' ';
    } else if (int_mA >= 10) {
      buf[3] = (int_mA % 10) + '0';
      buf[2] = (int_mA / 10) + '0';
      buf[1] = is_neg ? '-' : ' ';
    } else {
      buf[3] = int_mA + '0';
      buf[2] = is_neg ? '-' : ' ';
    }
  }
  lcd_setcursor(row, col);
  lcd_print(buf);
}

//=========================== SETUP =============================//
void setup() {
  DDRB |= _BV(LED_PIN);
  PORTB |= _BV(LED_PIN);
  wdt_enable(WDTO_2S);  //Aktifkan WDT dengan timeout 2 detik
  i2c_bus_clear();
  
  if (scan_address(LCD_ADDR)) {
    lcd_init();
    LCDinit = 1;
    lcd_setcursor(0, 0);
    lcd_print("Volt-Curr Sensor");
    lcd_setcursor(1, 0);
    lcd_print(".System Restart.");
    _delay_ms(1000);
  }
}

//============================ LOOP =============================//
//unsigned long waktuTadi, waktuNow;
void loop() {
  wdt_reset();
  // waktuNow = micros();
  // lcd_setcursor(0, 0);
  // lcd_print(waktuNow - waktuTadi);
  // waktuTadi = micros();  

  PORTB |= _BV(LED_PIN);
  bool LCDok = scan_address(LCD_ADDR);
  bool INA1ok = scan_address(INA219_ADD1);
  bool INA2ok = scan_address(INA219_ADD2);

  if (LCDok) {
    if (!LCDinit) {
      lcd_init();
      LCDinit = 1;
    }
  } else {
    LCDinit = 0;
  }

  if (INA1ok) {
    if (!INA1init) {
      ina219_init1(INA219_ADD1);
      INA1init = INA1ok;
    }
    print_voltage(0, 0, ina219_get_mV(INA219_ADD1));
    print_current(1, 0, ina219_get_uA(INA219_ADD1) - offset1);
  } else {
    INA1init = 0;
    lcd_setcursor(0, 0);    lcd_print("Sensor1 ");
    lcd_setcursor(1, 0);    lcd_print("unplug  ");
  }

  if (INA2ok) {
    if (!INA2init) {
      ina219_init2(INA219_ADD2);
      INA2init = INA2ok;
    }
    print_voltage(0, 8, ina219_get_mV(INA219_ADD2));
    print_current(1, 8, ina219_get_uA(INA219_ADD2) - offset2);
  } else {
    INA2init = 0;
    lcd_setcursor(0, 8);    lcd_print("Sensor2 ");
    lcd_setcursor(1, 8);    lcd_print("unplug  ");
  }

  PORTB &= ~_BV(LED_PIN);
  _delay_ms(458);  // 458=0,5s ; 955=1s  _delay_ms(955);
}
