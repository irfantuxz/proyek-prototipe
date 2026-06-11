// ATtiny 2313 - dual channel ina219 0x40 0x41 + LCD i2c 1602
// Using ATtinyCore NoBootLoader - 12MHz / 11.0592MHz
// Auto-Range PGA, BRNG 32V FSR, Cal LSB 0.1mA, deadband filter.
// program by @irfan_tuxz

#include <util/delay.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>

//====================== Konstanta & Konfigurasi ========================//
#define LED_PIN     PB4
#define SDA_PIN     PD4
#define SCL_PIN     PD5

#define LCD_ADDR    0x27
#define INA219_ADD1 0x40
#define INA219_ADD2 0x41

// Bit kontrol modul I2C PCF8574T
#define En          0x04
#define Rw          0x02
#define Rs          0x01
#define BACKLIGHT   0x08

// Instruksi kontroler LCD HD44780
#define LCD_CLEARDISPLAY   0x01
#define LCD_RETURNHOME     0x02
#define LCD_ENTRYMODESET   0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET    0x20
#define LCD_SETDDRAMADDR   0x80
#define LCD_ENTRYLEFT      0x02
#define LCD_DISPLAYON      0x04
#define LCD_2LINE          0x08
#define LCD_5x8DOTS        0x00

// Deadband filter: raw |nilai| < DEADBAND dipaksa ke 0. DEADBAND 2 = threshold < 0.2mA. 
#define DEADBAND 2

//====================== Fungsi I2C Bit-Banged ========================//
// Jeda NOP untuk stabilitas bus I2C Standard Mode ~100kHz @ 12MHz
static inline void i2c_delay() {
  asm volatile("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n");
}

static inline void sda_hi() { DDRD &= ~_BV(SDA_PIN); PORTD |= _BV(SDA_PIN); }
static inline void sda_lo() { DDRD |= _BV(SDA_PIN);  PORTD &= ~_BV(SDA_PIN); }
static inline void scl_hi() { DDRD &= ~_BV(SCL_PIN); PORTD |= _BV(SCL_PIN); }
static inline void scl_lo() { DDRD |= _BV(SCL_PIN);  PORTD &= ~_BV(SCL_PIN); }

static void i2c_start() {
  sda_hi(); scl_hi(); i2c_delay();
  sda_lo(); i2c_delay();
  scl_lo(); i2c_delay();
}

static void i2c_stop() {
  sda_lo(); i2c_delay();
  scl_hi(); i2c_delay();
  sda_hi(); i2c_delay();
}

bool i2c_write(uint8_t data) {
  for (uint8_t i = 0; i < 8; i++) {
    (data & 0x80) ? sda_hi() : sda_lo();
    i2c_delay(); scl_hi(); i2c_delay(); scl_lo(); i2c_delay();
    data <<= 1;
  }
  sda_hi(); i2c_delay(); scl_hi(); i2c_delay();
  bool ack = !(PIND & _BV(SDA_PIN));
  scl_lo(); i2c_delay();
  return ack;
}

uint8_t i2c_read(bool ack) {
  uint8_t data = 0;
  sda_hi();
  for (uint8_t i = 0; i < 8; i++) {
    data <<= 1;
    scl_hi(); i2c_delay();
    if (PIND & _BV(SDA_PIN)) data |= 1;
    scl_lo(); i2c_delay();
  }
  ack ? sda_lo() : sda_hi();
  i2c_delay(); scl_hi(); i2c_delay(); scl_lo(); i2c_delay();
  sda_hi();
  return data;
}

// Kirim 9 pulsa SCL jika SDA tertahan low oleh slave yang hang.
// Lalu kirim STOP condition untuk reset state machine slave.
// Dipanggil hanya di setup() — jika hang di tengah loop, WDT restart akan memanggilnya.
static void i2c_bus_clear() {
  sda_hi(); scl_hi(); i2c_delay();
  if (PIND & _BV(SDA_PIN)) return;
  for (uint8_t i = 0; i < 9; i++) {
    scl_lo(); i2c_delay(); scl_hi(); i2c_delay();
    if (PIND & _BV(SDA_PIN)) break;
  }
  scl_lo(); i2c_delay(); sda_lo(); i2c_delay();
  scl_hi(); i2c_delay(); sda_hi(); i2c_delay();
}

//======================= Fungsi LCD I2C =======================//
void lcd_expanderWrite(uint8_t data) {
  i2c_start(); i2c_write(LCD_ADDR << 1); i2c_write(data | BACKLIGHT); i2c_stop();
}

void lcd_pulseEnable(uint8_t data) {
  lcd_expanderWrite(data | En); _delay_us(1);
  lcd_expanderWrite(data & ~En); _delay_us(50);
}

void lcd_write4bits(uint8_t value) { lcd_expanderWrite(value); lcd_pulseEnable(value); }

void lcd_send(uint8_t value, uint8_t mode) {
  lcd_write4bits((value & 0xF0) | mode);
  lcd_write4bits(((value << 4) & 0xF0) | mode);
}

void lcd_command(uint8_t cmd) { lcd_send(cmd, 0); }

// Rutin inisialisasi wajib PCF8574T. Menjamin sinkronisasi mode 4-bit HD44780.
void lcd_init() {
  lcd_expanderWrite(0); _delay_ms(50);
  lcd_write4bits(0x30); _delay_ms(5);
  lcd_write4bits(0x30); _delay_us(150);
  lcd_write4bits(0x30); _delay_us(150);
  lcd_write4bits(0x20); _delay_us(150);
  lcd_command(LCD_FUNCTIONSET | LCD_2LINE | LCD_5x8DOTS);
  lcd_command(LCD_DISPLAYCONTROL | LCD_DISPLAYON);
  lcd_command(LCD_CLEARDISPLAY); _delay_ms(2);
  lcd_command(LCD_ENTRYMODESET | LCD_ENTRYLEFT);
}

// Posisi kursor: row 0=atas (DDRAM 0x00), row 1=bawah (DDRAM 0x40)
void lcd_setcursor(uint8_t row, uint8_t col) {
  if (row > 1) row = 1;
  lcd_command(LCD_SETDDRAMADDR | (col + (row ? 0x40 : 0x00)));
}

// Print dari RAM (untuk buffer char lokal)
void lcd_print(const char *str) { while (*str) lcd_send(*str++, Rs); }

// Print dari flash (PROGMEM) — hemat SRAM, pakai dengan PSTR("...")
void lcd_print_P(const char *str) { char c; while ((c = pgm_read_byte(str++))) lcd_send(c, Rs); }

// Print dari variabel angka / long int
void lcd_print(long angka) { char buffer[12]; ltoa(angka, buffer, 10); lcd_print(buffer); }

// ==================== LCD Read-back Verification ====================//
// Baca satu nibble dari DDRAM HD44780 via PCF8574T.
// PCF8574T quasi-bidirectional: tulis 1 → weak pull-up → LCD drive pin saat EN=1.
// D7-D4 terhubung ke nibble atas byte PCF8574T.
static uint8_t lcd_read_nibble() {
  uint8_t ctrl = Rs | Rw | BACKLIGHT;     // RS=1(data), RW=1(read), BL=on
  lcd_expanderWrite(0xF0 | ctrl);          // D4-D7 high (input), EN=0
  lcd_expanderWrite(0xF0 | ctrl | En);     // EN=1 — LCD drive D4-D7
  _delay_us(1);
  i2c_start();
  i2c_write((LCD_ADDR << 1) | 1);         // baca PCF8574T
  uint8_t val = i2c_read(false);
  i2c_stop();
  lcd_expanderWrite(0xF0 | ctrl);          // EN=0
  _delay_us(50);
  return (val >> 4) & 0x0F;               // ambil nibble atas = D7-D4
}

// Baca karakter di posisi tertentu dari DDRAM HD44780.
static uint8_t lcd_read_char_at(uint8_t row, uint8_t col) {
  // Set address counter ke posisi target, lalu baca 2 nibble
  lcd_command(LCD_SETDDRAMADDR | (col + (row ? 0x40 : 0x00)));
  uint8_t hi = lcd_read_nibble();
  uint8_t lo = lcd_read_nibble();
  return (hi << 4) | lo;
}

// Verifikasi unit karakter yang seharusnya selalu fixed:
//   (0, 7) dan (0,15) = 'V'  — akhir print_voltage sensor 1 & 2
//   (1, 7) dan (1,15) = 'A'  — akhir print_current sensor 1 & 2
// Dipanggil hanya saat kedua sensor aktif (posisi lain bisa berisi "Sensorx").
// Return false → LCD korup, perlu re-init.
static bool lcd_verify_units() {
  if (lcd_read_char_at(1,  7) != 'A') return false;
  if (lcd_read_char_at(1, 15) != 'A') return false;
  return true;
}
// ====================================================================//

//======================= Fungsi INA219 =======================//
void ina219_write(uint8_t reg, uint16_t val, uint8_t addr) {
  i2c_start();
  i2c_write(addr << 1); i2c_write(reg);
  i2c_write(val >> 8);  i2c_write(val & 0xFF);
  i2c_stop();
}

uint16_t ina219_read(uint8_t reg, uint8_t addr) {
  i2c_start(); i2c_write(addr << 1); i2c_write(reg); i2c_stop();
  i2c_start(); i2c_write((addr << 1) | 1);
  uint8_t hb = i2c_read(true);
  uint8_t lb = i2c_read(false);
  i2c_stop();
  return (hb << 8) | lb;
}

// Baca register arus (0x04).
// Cal Register = 0x1000 → LSB = 100µA = 0.1mA per count, data raw langsung tanpa shift.
int16_t ina219_get_uA(uint8_t addr) {
  return (int16_t)ina219_read(0x04, addr);
}

// Baca register tegangan bus (0x02).
// Bit [2:1] adalah flag OVF dan CNVR, dibuang dengan shift kanan 3 bit.
// LSB bus voltage = 4mV. BRNG=1 → range 0–32V.
uint16_t ina219_get_mV(uint8_t addr) {
  return (ina219_read(0x02, addr) >> 3) * 4;
}

// ========================== Auto-Range PGA ========================== //
//
// R_shunt = 100mΩ. Cal=0x1000 → Current_LSB = 100µA = 0.1mA per count.
// Tabel PGA disimpan di PROGMEM — tidak memakai SRAM.
//
// Config register decode [15:0]:
//   Bit 13      : BRNG = 1       → 32V FSR
//   Bits [12:11]: PGA[1:0]       → 00=/1  01=/2  10=/4  11=/8
//   Bits [10:7] : BADC[3:0]      → 1110 = 12-bit + 64 sample averaging
//   Bits [6:3]  : SADC[3:0]      → 1110 = 12-bit + 64 sample averaging
//   Bits [2:0]  : MODE[2:0]      → 111  = Shunt+Bus continuous
//
//   PGA /1 : 0010 0111 0111 0111 = 0x2777 → shunt ±40mV  → arus ±400mA
//   PGA /2 : 0010 1111 0111 0111 = 0x2F77 → shunt ±80mV  → arus ±800mA
//   PGA /4 : 0011 0111 0111 0111 = 0x3777 → shunt ±160mV → arus ±1600mA
//   PGA /8 : 0011 1111 0111 0111 = 0x3F77 → shunt ±320mV → arus ±3200mA
//
// Threshold dalam satuan count (1 count = 0.1mA):
//   Naik  PGA : arus > 90% batas atas PGA aktif
//   Turun PGA : arus < 80% batas atas PGA di bawahnya (hysteresis 10%)

static const uint16_t PGA_CFG[4]  PROGMEM = {0x2777, 0x2F77, 0x3777, 0x3F77};
static const uint16_t PGA_UP[4]   PROGMEM = {3600,   7200,   14400,  32767};
static const uint16_t PGA_DOWN[4] PROGMEM = {0,      3200,   6400,   12800};
//                                    idx:    0=/1    1=/2    2=/4    3=/8

uint8_t pga_idx1 = 2;  // Default PGA /4 saat init
uint8_t pga_idx2 = 2;

// Evaluasi apakah PGA perlu diubah berdasarkan arus terukur.
// Jika ya, tulis config register langsung. Pembacaan valid pada iterasi berikutnya.
// Kalibrasi (0x05) tidak perlu ditulis ulang — LSB tetap 0.1mA di semua PGA.
void ina219_set_pga(uint8_t addr, uint8_t *idx, int16_t raw) {
  uint16_t v = (raw < 0) ? (uint16_t)(-raw) : (uint16_t)raw;
  uint8_t ni = *idx;

  if (*idx < 3 && v > pgm_read_word(&PGA_UP[*idx]))        ni = *idx + 1;
  else if (*idx > 0 && v < pgm_read_word(&PGA_DOWN[*idx])) ni = *idx - 1;

  if (ni != *idx) {
    *idx = ni;
    ina219_write(0x00, pgm_read_word(&PGA_CFG[ni]), addr);
  }
}
// ============== Fungsi Init dan Register Kalibrasi INA219 ===================== //

// (satuan: count = 0.1mA)
const int8_t offset1 =  0;  // Koreksi bias idle sensor 1
const int8_t offset2 =  0;  // Koreksi bias idle sensor 2

void ina219_init1(uint8_t addr) {
  pga_idx1 = 2;
  ina219_write(0x05, 0x1000, addr);  // Default 0x1000 Cal=4096 → Current_LSB = 100µA = 0.1mA
  ina219_write(0x00, pgm_read_word(&PGA_CFG[pga_idx1]), addr);
}

void ina219_init2(uint8_t addr) {
  pga_idx2 = 2;
  ina219_write(0x05, 0x1000, addr);  // Default 0x1000 Cal=4096 → Current_LSB = 100µA = 0.1mA
  ina219_write(0x00, pgm_read_word(&PGA_CFG[pga_idx2]), addr);
}

//======================= Fungsi Display =======================//
bool LCDinit  = 0;
bool INA1init = 0;
bool INA2init = 0;

bool scan_address(uint8_t addr7) {
  i2c_start(); bool ack = i2c_write(addr7 << 1); i2c_stop(); return ack;
}

// Format tegangan: " XX.XX V" — 8 karakter statis, resolusi 0.01V.
void print_voltage(uint8_t row, uint8_t col, uint16_t mV) {
  char buf[9] = { ' ', ' ', ' ', '.', ' ', ' ', ' ', 'V', '\0' };
  uint16_t cv = mV / 10;
  buf[5] = (cv % 10) + '0'; cv /= 10;
  buf[4] = (cv % 10) + '0'; cv /= 10;
  buf[2] = (cv % 10) + '0'; cv /= 10;
  if (cv > 0) buf[1] = (cv % 10) + '0';
  lcd_setcursor(row, col); lcd_print(buf);
}

// Format arus: 8 karakter statis.
// < 1A: tampil "±XXX.XmA" (resolusi 0.1mA, unit miliAmpere)
// ≥ 1A: tampil "±X.XXX A" (resolusi 1mA, unit Ampere)
void print_current(uint8_t row, uint8_t col, int16_t raw_val) {
  char buf[9] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'A', '\0' };
  bool is_neg = (raw_val < 0);
  uint16_t v = is_neg ? (uint16_t)(-raw_val) : (uint16_t)raw_val;

  if (v >= 10000) {
    buf[0] = is_neg ? '-' : ' ';
    buf[1] = (v / 10000) + '0';
    buf[2] = '.';
    buf[3] = ((v / 1000) % 10) + '0';
    buf[4] = ((v / 100)  % 10) + '0';
    buf[5] = ((v / 10)   % 10) + '0';
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
  lcd_setcursor(row, col); lcd_print(buf);
}

//=========================== SETUP =============================//
void setup() {
  DDRB  |= _BV(LED_PIN);
  PORTB |= _BV(LED_PIN);
  wdt_enable(WDTO_2S);
  i2c_bus_clear();

  if (scan_address(LCD_ADDR)) {
    lcd_init(); LCDinit = 1;
    lcd_setcursor(0, 0); lcd_print_P(PSTR("Volt-Curr Sensor"));
    lcd_setcursor(1, 0); lcd_print_P(PSTR(" System Restart "));
    _delay_ms(1000);
  }
}

//============================ LOOP =============================//
void loop() {
  wdt_reset();

  PORTB |= _BV(LED_PIN);
  bool LCDok  = scan_address(LCD_ADDR);
  bool INA1ok = scan_address(INA219_ADD1);
  bool INA2ok = scan_address(INA219_ADD2);

  if (LCDok) {
    LCDinit = lcd_verify_units();
    if (!LCDinit) { lcd_init(); LCDinit = 1; }
  } else {
    LCDinit = 0;
  }
			  
  if (INA1ok) {
    if (!INA1init) { ina219_init1(INA219_ADD1); INA1init = 1; }
    int16_t i1 = ina219_get_uA(INA219_ADD1) - offset1;
    // Deadband: noise ±1 LSB di sekitar 0 dibulatkan ke 0
    if (i1 > -DEADBAND && i1 < 1) i1 = 0;
    print_voltage(0, 0, ina219_get_mV(INA219_ADD1));
    print_current(1, 0, i1);
    ina219_set_pga(INA219_ADD1, &pga_idx1, i1);
  } else {
    INA1init = 0;
    pga_idx1 = 2;  // Reset agar tidak stuck di PGA tinggi saat hotplug
    if (LCDok) {
      lcd_setcursor(0, 0); lcd_print_P(PSTR("Sensor1 "));
      lcd_setcursor(1, 0); lcd_print_P(PSTR("unplug A"));
    }
  }
			  
  if (INA2ok) {
    if (!INA2init) { ina219_init2(INA219_ADD2); INA2init = 1; }
    int16_t i2 = ina219_get_uA(INA219_ADD2) - offset2;
    // Deadband: noise ±1 LSB di sekitar 0 dibulatkan ke 0
    if (i2 > -DEADBAND && i2 < 1) i2 = 0;
    print_voltage(0, 8, ina219_get_mV(INA219_ADD2));
    print_current(1, 8, i2);
    ina219_set_pga(INA219_ADD2, &pga_idx2, i2);
  } else {
    INA2init = 0;
    pga_idx2 = 2;
    if (LCDok) {
      lcd_setcursor(0, 8); lcd_print_P(PSTR("Sensor2 "));
      lcd_setcursor(1, 8); lcd_print_P(PSTR("unplug A"));
    }
  }

  PORTB &= ~_BV(LED_PIN);
  _delay_ms(458);
}
