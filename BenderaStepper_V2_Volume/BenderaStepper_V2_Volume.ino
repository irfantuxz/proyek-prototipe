#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"

const int pinADCvol = A2;              // ADC untuk Volume
volatile int lastVolume = -1;

SoftwareSerial mySoftwareSerial(6, 7); // RX, TX
DFRobotDFPlayerMini myDFPlayer;

const int buttonPlay = 2;              // aktif LOW (pakai INPUT_PULLUP)
int benderaPosisi = 1;                 // 0 bawah, 1 atas (default)

const int dirPin = 8;        // DIR-
const int pulPin = 10;       // PUL-
const int dirEnablePin = 9;  // DIR+
const int pulEnablePin = 11; // PUL+

const int stepsPerRevolution = 1600;
int pulseDelay = 500;                 // µs (setengah periode pulsa)
int pulseDelayBendera = 5059;         // contoh: 102 detik total
const float defaultRotasi = 6.3;      // rotasi default (panjang tali & tiang)

unsigned long motorStopTime = 0;
const unsigned long holdTimeoutDetik = 5; // setelah 5 detik idle, matikan coil
bool motorAktif = false;

String inputString = "";

// ------- Parameter sampling volume non-blocking -------
const unsigned long VOL_READ_PERIOD_MS = 40;   // frekuensi sampling ADC volume
const unsigned long VOL_CMD_MIN_MS     = 120;  // jeda minimal kirim perintah volume ke DFPlayer
unsigned long lastVolReadMs = 0;
unsigned long lastVolCmdMs  = 0;

// ===================== UTIL ===========================
static inline void driverEnable(bool en) {
  digitalWrite(dirEnablePin, en ? HIGH : LOW);
  digitalWrite(pulEnablePin, en ? HIGH : LOW);
  motorAktif = en;
  if (!en) motorStopTime = millis();
}

void readVolumeTick() {
  const unsigned long now = millis();
  // Baca ADC tidak lebih sering dari VOL_READ_PERIOD_MS
  if ((now - lastVolReadMs) < VOL_READ_PERIOD_MS) return;
  lastVolReadMs = now;

  // Baca & map volume
  int raw = 1023 - analogRead(pinADCvol);   // dibalik sesuai wiring Anda
  int vol = map(raw, 0, 1023, 0, 30);
  vol = constrain(vol, 0, 30);

  // Hanya kirim ke DFPlayer jika BERUBAH & tidak terlalu sering
  if (vol != lastVolume && (now - lastVolCmdMs) >= VOL_CMD_MIN_MS) {
    lastVolume = vol;
    lastVolCmdMs = now;
    myDFPlayer.volume(vol);  // <- ini bisa blocking, makanya di-rate-limit
  }
}

// Jeda non-blocking (untuk fase "berhenti") sambil cek volume berkala
void pauseWithADC(unsigned long ms) {
  const unsigned long start = millis();
  while ((millis() - start) < ms) {
    readVolumeTick();
    // kecilkan beban CPU; sampling tetap diatur oleh VOL_READ_PERIOD_MS
    // (tanpa delay di sini juga boleh, tapi ini menurunkan busy-loop)
    delay(1);
  }
}

// Stepper pulse generator yang STABIL (tanpa ADC/DFPlayer di dalam)
void stepperPulseRun(bool forward, long totalSteps, int halfPeriodUs) {
  digitalWrite(dirPin, forward ? HIGH : LOW);
  for (long i = 0; i < totalSteps; i++) {
    // satu langkah = HIGH + LOW, masing2 halfPeriodUs
    digitalWrite(pulPin, HIGH);
    delayMicroseconds(halfPeriodUs);
    digitalWrite(pulPin, LOW);
    delayMicroseconds(halfPeriodUs);
  }
}

// ===================== SETUP / LOOP ====================
void setup() {
  pinMode(buttonPlay, INPUT_PULLUP);   // aktif LOW

  pinMode(dirPin, OUTPUT);
  pinMode(pulPin, OUTPUT);
  pinMode(dirEnablePin, OUTPUT);
  pinMode(pulEnablePin, OUTPUT);

  // default: driver OFF & pin LOW
  digitalWrite(dirEnablePin, LOW);
  digitalWrite(pulEnablePin, LOW);
  digitalWrite(dirPin, LOW);
  digitalWrite(pulPin, LOW);

  Serial.begin(115200);
  Serial.println("Ketik F=1.5, R=2, D=1000, DETIK=45, atau NAIK/TURUN/BENDERA/TEST");

  mySoftwareSerial.begin(9600);
  if (!myDFPlayer.begin(mySoftwareSerial)) {
    Serial.println("Unable to begin DFPlayer. Cek wiring & SD card!");
    while (true) { delay(100); }
  }
  Serial.println(F("DFPlayer Mini online."));

  myDFPlayer.setTimeOut(500);
  myDFPlayer.volume(15);
  lastVolume = 15;
  myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
}

void loop() {
  // Tombol PLAY (aktif LOW)
  if (digitalRead(buttonPlay) == LOW) {
    if (benderaPosisi == 1) {
      // Turun cepat
      Serial.println("🔽 Turun cepat");
      pulseDelay = 250;
      stepMotor(defaultRotasi, false);
      pauseWithADC(1000);
      benderaPosisi = 0;

      // Naik bertahap (step & berhenti) + mainkan lagu
      myDFPlayer.play(1);
      Serial.println("🎵 Indonesia Raya - mode step (jalan-berhenti)");
      benderaStep();
      pauseWithADC(500);
      benderaPosisi = 1;
    }
  }

  // Baca input serial
  if (Serial.available()) {
    inputString = Serial.readStringUntil('\n');
    inputString.trim();
    processInput(inputString);
  }

  // Sampling volume rutin saat idle juga boleh (misal pengguna atur sebelum mulai)
  readVolumeTick();

  // Auto nonaktifkan coil setelah beberapa detik idle
  if (motorAktif && (millis() - motorStopTime >= holdTimeoutDetik * 1000UL)) {
    driverEnable(false);
    Serial.println("🛑 Driver dimatikan setelah idle.");
  }
}

// ===================== COMMANDS =======================
void processInput(String cmd) {
  cmd.toUpperCase();
  float value = 0.0;

  if (cmd.startsWith("F=")) {
    value = cmd.substring(2).toFloat();
    if (value > 0) stepMotor(value, true);
  } else if (cmd.startsWith("R=")) {
    value = cmd.substring(2).toFloat();
    if (value > 0) stepMotor(value, false);
  } else if (cmd.startsWith("D=")) {
    int totalDelay = cmd.substring(2).toInt();
    if (totalDelay >= 10 && totalDelay <= 50000) {
      pulseDelay = totalDelay / 2;
      Serial.print("Delay per pulsa diatur menjadi ");
      Serial.print(totalDelay);
      Serial.print(" µs (pulseDelay = ");
      Serial.print(pulseDelay);
      Serial.println(" µs)");
    } else {
      Serial.println("❌ Nilai delay tidak valid (10–50000 µs)");
    }
  } else if (cmd.startsWith("DETIK=")) {
    int detik = cmd.substring(6).toInt();
    if (detik > 0) {
      long totalPulse = (long)(defaultRotasi * stepsPerRevolution);
      long halfPeriod = (long)((detik * 1000000.0) / (totalPulse * 2));
      pulseDelay = (int)halfPeriod;
      pulseDelayBendera = pulseDelay;
      Serial.print("PulseDelay dihitung otomatis: ");
      Serial.print(pulseDelay);
      Serial.print(" µs agar ");
      Serial.print(defaultRotasi, 2);
      Serial.print(" rotasi selesai dalam ");
      Serial.print(detik);
      Serial.println(" detik.");
    } else {
      Serial.println("❌ DETIK harus > 0");
    }
  } else if (cmd.startsWith("NAIK")) {
    Serial.println("🔼 Naik cepat");
    pulseDelay = 300;
    stepMotor(defaultRotasi, true);
    benderaPosisi = 1;
  } else if (cmd.startsWith("TURUN")) {
    Serial.println("🔽 Turun cepat");
    pulseDelay = 300;
    stepMotor(defaultRotasi, false);
    benderaPosisi = 0;
  } else if (cmd.startsWith("BENDERA")) {
    myDFPlayer.play(1);
    Serial.println("🎵 Indonesia Raya (musik jalan-step)");
    benderaStep();
    pauseWithADC(500);
    benderaPosisi = 1;
  } else if (cmd.startsWith("TEST")) {
    Serial.println("🎵 Bendera naik kontinu tanpa musik");
    pulseDelay = pulseDelayBendera;
    stepMotorChunked(defaultRotasi, true, 400);
    benderaPosisi = 1;
  } else {
    Serial.println("❓ Perintah tidak dikenali. Gunakan F=, R=, D=, DETIK=, NAIK/TURUN/BENDERA/TEST");
  }
}

// ===================== GERAK MOTOR ====================
void stepMotor(float revolutions, bool forward) {
  long totalSteps = (long)(revolutions * stepsPerRevolution);
  Serial.print("Gerak ");
  Serial.print(forward ? "maju " : "mundur ");
  Serial.print(revolutions, 2);
  Serial.print(" putaran (");
  Serial.print(totalSteps);
  Serial.println(" langkah)");

  driverEnable(true);
  stepperPulseRun(forward, totalSteps, pulseDelay);
  Serial.println("✅ Selesai.\n");

  motorStopTime = millis();
}

// Mode kontinu, tapi volume di-update antar “chunk” kecil agar timing pulsa tetap stabil
void stepMotorChunked(float revolutions, bool forward, long chunkSteps) {
  long totalSteps = (long)(revolutions * stepsPerRevolution);
  if (chunkSteps < 1) chunkSteps = 200;

  driverEnable(true);
  digitalWrite(dirPin, forward ? HIGH : LOW);

  long remain = totalSteps;
  while (remain > 0) {
    long thisChunk = (remain > chunkSteps) ? chunkSteps : remain;
    stepperPulseRun(forward, thisChunk, pulseDelay);  // stabil
    remain -= thisChunk;

    // JANGAN delay; cukup beri kesempatan update volume sejenak (tanpa merusak timing)
    // Di sini tidak ada delayMicroseconds, jadi panggil sekali—rate-limit di readVolumeTick()
    readVolumeTick();
  }

  Serial.println("✅ Selesai.\n");
  motorStopTime = millis();
}

// Mode “jalan–berhenti” untuk sinkron lagu Indonesia Raya
void benderaStep() {
  // bagi antara gerak & berhenti (misal 3/4 : 1/4 dari tempo dasar)
  pulseDelay = pulseDelayBendera * 3 / 4;
  int DelayBendera = pulseDelayBendera * 1 / 4;

  long stepsPerBurst = (long)(0.3 * stepsPerRevolution); // seperti kode Anda
  const int nBurst = 21;

  driverEnable(true);
  for (int i = 0; i < nBurst; i++) {
    stepperPulseRun(true, stepsPerBurst, pulseDelay); // gerak stabil
    pauseWithADC(DelayBendera);                       // saat berhenti => update volume
  }
  Serial.println("✅ Selesai.\n");
  motorStopTime = millis();
}
