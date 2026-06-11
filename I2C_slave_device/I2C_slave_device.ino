#include <Wire.h>
#include <avr/wdt.h>

/* ================= CONFIG ================= */

#define DEFAULT_I2C_ADDR 0x20
#define LED_PIN LED_BUILTIN
#define LED_PULSE_MS 25

/* ================= I2C DATA ================= */

uint8_t registers[256];

volatile uint8_t regPointer = 0x00;
volatile bool flagWrite = false;
volatile bool flagRead  = false;
volatile bool flagAck   = false;

volatile uint8_t lastReg = 0;
volatile uint8_t lastVal = 0;

/* ================= ADDRESS CONTROL ================= */

uint8_t i2cAddress = DEFAULT_I2C_ADDR;
bool pendingAddrChange = false;
uint8_t newAddress;

/* ================= LED NON BLOCKING ================= */

bool ledActive = false;
unsigned long ledTimer = 0;

void pulseLED() {
  digitalWrite(LED_PIN, HIGH);
  ledTimer = millis();
  ledActive = true;
}

/* ================= I2C ISR ================= */

void onI2CReceive(int count) {
  flagAck = true;

  if (count <= 0) return;   // scanner-safe

  regPointer = Wire.read();
  lastReg = regPointer;
  count--;

  if (count > 0) {
    lastVal = Wire.read();
    registers[regPointer] = lastVal;
    flagWrite = true;
  }
}

void onI2CRequest() {
  flagAck = true;
  flagRead = true;
  Wire.write(registers[regPointer]);
}

/* ================= SERIAL PARSER ================= */

void handleSerial(String cmd) {
  cmd.trim();

  // A=0xYY
  if (cmd.startsWith("A=")) {
    uint8_t addr = strtol(cmd.substring(2).c_str(), NULL, 0);
    if (addr >= 0x08 && addr <= 0x77) {
      newAddress = addr;
      pendingAddrChange = true;
      Serial.print(F("[REQ] Change I2C addr -> 0x"));
      Serial.println(addr, HEX);
    } else {
      Serial.println(F("[ERR] Invalid address"));
    }
  }

  // R=0xZZ=0xAA
  else if (cmd.startsWith("R=")) {
    int p1 = cmd.indexOf('=');
    int p2 = cmd.indexOf('=', p1 + 1);
    if (p2 > 0) {
      uint8_t r = strtol(cmd.substring(p1 + 1, p2).c_str(), NULL, 0);
      uint8_t v = strtol(cmd.substring(p2 + 1).c_str(), NULL, 0);
      registers[r] = v;
      Serial.print(F("[OK] REG 0x"));
      Serial.print(r, HEX);
      Serial.print(F(" = 0x"));
      Serial.println(v, HEX);
      pulseLED();
    }
  }
}

/* ================= SETUP ================= */

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  while (!Serial);

  Wire.begin(i2cAddress);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  wdt_enable(WDTO_4S);   // watchdog 1 detik

  Serial.println(F("=== I2C SLAVE READY (scanner-safe) ==="));
  Serial.print(F("ADDR : 0x"));
  Serial.println(i2cAddress, HEX);
}

/* ================= LOOP ================= */

void loop() {
  wdt_reset();

  /* ---- Serial command ---- */
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleSerial(cmd);
  }

  /* ---- Address change SAFE ---- */
  if (pendingAddrChange) {
    pendingAddrChange = false;

    Wire.end();
    i2cAddress = newAddress;
    Wire.begin(i2cAddress);
    Wire.onReceive(onI2CReceive);
    Wire.onRequest(onI2CRequest);

    Serial.print(F("[OK] I2C addr changed to 0x"));
    Serial.println(i2cAddress, HEX);
    pulseLED();
  }

  /* ---- I2C events (from ISR flags) ---- */
  if (flagWrite) {
    flagWrite = false;
    Serial.print(F("[I2C WRITE] Reg 0x"));
    Serial.print(lastReg, HEX);
    Serial.print(F(" = 0x"));
    Serial.println(lastVal, HEX);
    pulseLED();
  }

  if (flagRead) {
    flagRead = false;
    Serial.print(F("[I2C READ] Reg 0x"));
    Serial.println(lastReg, HEX);
    pulseLED();
  }

  if (flagAck) {
    flagAck = false;
    pulseLED();
  }

  /* ---- LED timing ---- */
  if (ledActive && millis() - ledTimer > LED_PULSE_MS) {
    digitalWrite(LED_PIN, LOW);
    ledActive = false;
  }
}
