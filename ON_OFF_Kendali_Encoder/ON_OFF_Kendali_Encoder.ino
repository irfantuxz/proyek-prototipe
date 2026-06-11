// Project: Modular ON-OFF & PWM Motor Controller (9-bit)
#include <LiquidCrystal_I2C.h>        // ver.1.1.2 Frank de brabander
#include <ResponsiveAnalogRead.h>     // ver.1.2.1 Damien Clarke

// DEFINISI PIN & KONSTANTA
const uint8_t PIN_ENCODER_A      = 2;  // rotary encoder A (Hardware Interupt)
const uint8_t PIN_BTN_MODE       = 5;  // push button MODE
const uint8_t PIN_BTN_START_STOP = 6;  // push button Start/Stop
const uint8_t PIN_MOTOR          = 9;  // PIN D9 untuk HARDWARE PWM TIMER1 (SILENT PWM 31.25kHz)
const uint8_t PIN_POT_SP         = A0; // knop potensio SetPoint
const uint8_t PIN_POT_DB         = A1; // knop potensio DeadBand
// Terapkan ke semua konstanta PIN

// VARIABEL GLOBAL & OBJEK
const uint16_t STEPS_PER_REV = 270;     // Jumlah Step encoder per 1x putaran
LiquidCrystal_I2C lcd(0x27, 20, 4);
byte plusminus[8] = {0x4,0x4,0x1f,0x4,0x4,0x0,0x1f,0x0};

// Objek Filter ADC (Parameter 'true' mengaktifkan mode sleep agar lebih stabil)
ResponsiveAnalogRead potSP(PIN_POT_SP, true);
ResponsiveAnalogRead potDB(PIN_POT_DB, true);

// Variabel Interrupt
volatile uint8_t currentMode = 1;
volatile unsigned long lastButtonTime = 0;
volatile uint16_t pulseCount = 0;
unsigned long lastPulseCount = 0;

// Variabel Sistem Timer
unsigned long previousMillis = 0;
unsigned long lastDisplayTime = 0;

// Variabel Kontrol
uint8_t waktuJeda = 0;
bool tadiPWMnaik = false; // Status PWM naik atau turun
bool motorAktif = false;  // Status motor (ON) / (OFF)
int16_t pwmMotor = 0;    // pwm output ke motor
uint16_t currentRPM = 0;
uint16_t setPoint = 0;
uint16_t deadBand = 0;

// INTERRUPT SERVICE ROUTINES (ISR)
void encoderISR() {
  pulseCount++;
}

bool lcdConnected = false;
bool detectLCD() {
  Wire.beginTransmission(0x27);
  uint8_t err = Wire.endTransmission();
  if (err == 0) {
    if (!lcdConnected) {
      lcd.init();
      lcd.backlight();
      lcd.createChar(0, plusminus);
      lcd.clear();
      lcdConnected = true;
    }
    return true;
  }
  lcdConnected = false;
  return false;
}

void setup() {
  Serial.begin(115200);
  Serial.println("RPM: ON-OFF");

  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  pinMode(PIN_BTN_START_STOP, INPUT_PULLUP);
  pinMode(PIN_MOTOR, OUTPUT);
  potSP.enableEdgeSnap();
  potDB.enableEdgeSnap();

  // ============================================================================
  // PENGATURAN REGISTER TIMER1 (PIN 9 & 10)

  // TCCR1A (Timer/Counter Control Register 1 A)
  // (1 << COM1A1): Mengaktifkan "Clear OC1A on Compare Match". Pin 9 akan High saat 
  //                counter mulai dan Low saat mencapai nilai OCR1A (Non-Inverting PWM).
  // (1 << WGM11) : Bagian dari bit Mode Generation (digunakan bersama WGM12/13 di TCCR1B).
  TCCR1A = (1 << COM1A1) | (1 << WGM11);

  // TCCR1B (Timer/Counter Control Register 1 B)
  // (1 << WGM13) | (1 << WGM12): Mengatur Timer1 ke Mode 14 (Fast PWM dengan ICR1 sebagai TOP).
  // (1 << CS10) : Mengatur Clock Select ke Prescaler 1 (Artinya Timer berjalan di 16 MHz penuh).
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS10);
  
  // ICR1 (Input Capture Register 1)
  // Berfungsi sebagai nilai puncak (TOP). Timer akan menghitung dari 0 sampai 511 lalu reset.
  // Rumus Frekuensi: F_pwm = F_cpu / (Prescaler * (1 + TOP))
  // F_pwm = 16,000,000 / (1 * (1 + 511)) = 31,250 Hz (31.25 kHz - Silent PWM agar tidak berdenging).
  ICR1 = 511; 
  
  // OCR1A (Output Compare Register 1 A)
  // Menentukan Duty Cycle (lebar pulsa). Inilah yang kita ubah-ubah untuk mengatur kecepatan.
  OCR1A = 0;
  // ============================================================================

  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoderISR, CHANGE);
  Wire.begin();
  detectLCD();
  lcd.setCursor(0,1);
  lcd.print("ON-OFF Speed Control");
  delay(1000);
  lcd.clear();
}

void loop() {
  static uint8_t lastExecutedMode = 0;
  if (currentMode != lastExecutedMode) {
    lcd.clear();
    pwmMotor = 0; // Reset PWM saat pindah mode
    OCR1A = 0;    // Set HW_PWM 0
    lastExecutedMode = currentMode;
  }

  bacaTombol();
  bacaSensorAnalog();
  kalkulasiDanKontrol();
}

// MODUL INPUT & PEMBACAAN
void bacaTombol() {
  unsigned long currentMillis = millis();
  
  // Deteksi tekanan tombol dengan interval debounce 1 detik
  if (currentMillis - lastButtonTime >= 1000) {
    // Tombol START_STOP
    if (digitalRead(PIN_BTN_START_STOP) == LOW) {
      lastButtonTime = currentMillis;
      motorAktif = !motorAktif; // Toggle status
      if (motorAktif){
        tadiPWMnaik = true;
        pwmMotor = 100;  // saat start langsung ke 100 pwm - karena respon motor baru mulai di 100 @ 31.25 kHz
        Serial.print("Start Mode:");
        Serial.println(currentMode);
      } else {
        Serial.print("Stop");
      }
    }
    // Tombol MODE
    if (digitalRead(PIN_BTN_MODE) == LOW) {
      lastButtonTime = currentMillis;
      currentMode++;
      if (currentMode > 3) currentMode = 1;
      motorAktif = false;
      Serial.print("Change Mode:");
      Serial.println(currentMode);
    }
  }

}

void bacaSensorAnalog() {
  potSP.update();
  potDB.update();

  if (currentMode == 1) {
    setPoint = map(potSP.getValue(), 0, 1023, 0, 511);
  } else {
    setPoint = map(potSP.getValue(), 0, 1023, 0, 500);  
    setPoint = ((setPoint + 5) / 10) * 10;

    if (currentMode == 3) {
      deadBand = map(potDB.getValue(), 0, 1023, 0, 150);
      deadBand = ((deadBand + 5) / 10) * 10;
    }
  }
}

// MODUL KALKULASI & KONTROL
void kalkulasiDanKontrol() {
  unsigned long currentMillis = millis();
  
  // Optimal refresh rate deteksi 200ms, dibawah itu kurang akurat
  if (currentMillis - previousMillis >= 200) {
    noInterrupts();
    unsigned long currentPulses = pulseCount; 
    interrupts();

    uint16_t deltaSteps = currentPulses - lastPulseCount;
    
    // Casting perhitungan float ke (int)
    //currentRPM = (int)(((float)deltaSteps / STEPS_PER_REV) * (60000.0 / calcInterval));
    //currentRPM = (int)(((float)deltaSteps / 270) * (60000.0 / 200));
    currentRPM = (deltaSteps * 10) / 9;

    lastPulseCount = currentPulses;
    previousMillis = currentMillis;

    // Hanya jalankan kendali motor jika status sistem ON
    if (motorAktif) {
      eksekusiLogikaMotor();
    } else {
      // Pastikan pin benar-benar LOW (OFF)
      if (currentMode > 1) pwmMotor = 0; else
      pwmMotor = map(setPoint, 0, 511, 100, 511);
      OCR1A = 0;
      digitalWrite(LED_BUILTIN, false);
    }
    
    // Batasi update layar 3x per detik
    if (currentMillis - lastDisplayTime >= 250) {
      updateDisplay();
      lastDisplayTime = currentMillis;
    }
  }
}

void eksekusiLogikaMotor() {
  if (currentMode == 1) {
    pwmMotor = map(setPoint, 0, 511, 100, 511);
    tadiPWMnaik = true;
  } 
  else if (currentMode == 2) {
    if (currentRPM < setPoint) {
      pwmMotor+=2; 
      tadiPWMnaik = true;
    } else {
      if (waktuJeda == 0) {
        waktuJeda = 2;
      } else if (waktuJeda == 1) {
        tadiPWMnaik = false;
        pwmMotor -= 40;
      }
    }
  } 
  else if (currentMode == 3) {
    int bBawah = setPoint - (deadBand / 2);
    int bAtas  = setPoint + (deadBand / 2);

    if (currentRPM <= bBawah){
      if (!tadiPWMnaik) {
        if (waktuJeda == 0) {
          waktuJeda = 2;
        } else if (waktuJeda == 1) {
          tadiPWMnaik = true;
          pwmMotor++;
        }
      } else {
        if (waktuJeda > 0) pwmMotor++; else pwmMotor+=2;
      }
      // Serial.print("currentRPM < bBawah ");
      // Serial.println(currentRPM);
    }
    else if (currentRPM >= bAtas){
      if (tadiPWMnaik) {
        if (waktuJeda == 0) {
          waktuJeda = 2;
        } else if (waktuJeda == 1) {
          tadiPWMnaik = false;
          pwmMotor--;
        }
      } else {
          if (waktuJeda > 0) pwmMotor--; else pwmMotor-=5;
      }
      // Serial.print("currentRPM > bAtas ");
      // Serial.println(currentRPM);
    }
    else {
      if (tadiPWMnaik) pwmMotor+=2; 
      else pwmMotor-=7;
    }
  }
  // Jika PWM naik nyalakan LED. Jika tidak, matikan.
  digitalWrite(LED_BUILTIN, tadiPWMnaik);
  // PROTEKSI KRUSIAL: Mencegah nilai PWM overflow (di bawah 0 atau di atas 511)
  pwmMotor = constrain(pwmMotor, 0, 511);
  OCR1A = pwmMotor;  // Pengganti analogWrite(9, pwmMotor);
}

// MODUL DISPLAY
void updateDisplay() {
  // Cek hot-plug LCD
  detectLCD();

  if (motorAktif) Serial.println(currentRPM);
  // Jeda agar diam di SP 3 siklus
  if(waktuJeda > 0) waktuJeda--;

  lcd.setCursor(0, 0);
  if (currentMode == 1)      lcd.print("1. RPM Analytics    ");
  else if (currentMode == 2) lcd.print("2. Basic Control    ");
  else if (currentMode == 3) lcd.print("3. Histerisis Ctrl  ");

  // Baris 2: RPM Tampilan Integer & Status Start/Stop
  lcd.setCursor(0, 1);
  lcd.print("PV->RPM : "); if (currentRPM<100) lcd.print(" "); if (currentRPM<10) lcd.print(" ");
  lcd.print(currentRPM); 
  lcd.print("  "); // Padding untuk menghapus sisa angka

  // Menulis status ON/OFF di kolom 15 (index 14)
  lcd.setCursor(14, 1);
  if (motorAktif) {
    lcd.print(" ON");
  } else {
    lcd.print("OFF");
  }
  // Menulis status PWM
  lcd.print(pwmMotor); if (pwmMotor<100) lcd.print(" "); if (pwmMotor<10) lcd.print(" ");

  // Baris 3 & 4: Parameter
  if (currentMode >= 2) {
    lcd.setCursor(0, 2);
    lcd.print("SetPoint: "); if (setPoint<100) lcd.print(" "); if (setPoint<10) lcd.print(" ");
    lcd.print(setPoint); lcd.print(" RPM   ");
  }
  if (currentMode == 3) {
    lcd.setCursor(0, 3); 
    lcd.print("DeadBand:"); lcd.print((char)0); if (deadBand<100) lcd.print(" "); if (deadBand<10) lcd.print(" ");
    lcd.print(deadBand); lcd.print(" RPM   ");
  }

}