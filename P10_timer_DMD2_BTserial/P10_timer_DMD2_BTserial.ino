/*
 Arduino NANO/UNO - Led P10 Matrix - using Library DMD2 (Freetronics v0.0.3)
 Serial Monitor / Bluetooth Serial Command
 Command : S     (Start/Stop),
           R     (Reset),
           D     (Delay),
           M     (Mode),
           B=xx  (Set Brightness to xx; xx = 0 to 255; B=50 default)
 Pin P10   : OE-9; A-6; B-7; CLK-13; SCLK-8; RDATA-11; GND-GND;
 Pin JDY31 : RX-1(TX); TX-0(RX); VCC-5V; GND-GND;
*/
#include <SPI.h>
#include <DMD2.h>
#include <fonts/SystemFont5x7.h>
#include <fonts/Arial_Black_16.h>

// Konfigurasi PIN
#define BTN_START_STOP A0  // untuk START/STOP
#define BTN_DELAY A1       // untuk Delay count Down/Up
#define BTN_RESET A2       // untuk Reset Timer
#define BTN_MODE A3        // untuk pilihan mode count Down/Up
#define BUZZER_PIN 2       // pin Speaker Buzzer

// Konfigurasi panel P10 (32x16 pixel)
#define DISPLAYS_HORIZONTAL_COUNT 1
#define DISPLAYS_VERTICAL_COUNT   1

// Gunakan SPIDMD untuk performa terbaik pada DMD2 (SPIDMD = Hardware SPI)
SPIDMD dmd(DISPLAYS_HORIZONTAL_COUNT, DISPLAYS_VERTICAL_COUNT);

int currentBrightness = 30;  // 0 -255

// Variabel timer
unsigned long remainingTime  = 0;
unsigned long stoppedTime    = 0;
unsigned long lastMillis     = 0;
int  delaySeconds            = 5;  // 0-9 detik delay sebelum start
int  downMinutes             = 30;  // count down tiga menit
unsigned long startTime;
unsigned long sisaWaktu;


bool buzzerActive         = false;
unsigned long buzzerStartTime = 0;
const unsigned long buzzerDuration = 1000;

// State tombol untuk debounce
bool lastButtonState[]      = { HIGH, HIGH, HIGH, HIGH };
bool buttonState[]          = { HIGH, HIGH, HIGH, HIGH };
unsigned long lastDebounceTime[] = { 0, 0, 0, 0 };
const unsigned long debounceDelay = 50;

// State untuk setiap tombol
int  currDelayStart = 0;   // variable yg tampil current Delay Start
bool stateUpDn = false;     // true=up; false=down;

enum State { WAKTU, INIT, MUNDUR, RUNNING, RUNNINGDOWN, STOPPED };
State currentState = WAKTU;

unsigned long lastScroll = 0;

// Fake RTC based on compile time and millis()
byte rtcHour0, rtcMin0, rtcSec0;
unsigned long rtcMillis0 = 0;

// --- Flag “tombol virtual” dari Serial ---
bool serialStartStop = false;
bool serialDelay     = false;
bool serialReset     = false;
bool serialMode      = false;

// --- Prototipe fungsi ---
void displayButtonStatus();                      // menampilkan status tombol
void displayTime(unsigned long timeMs);          // tampilan timer (Menit besar, Detik atas, centiDetik bawah)
void displayTimeMundur(unsigned long elapsed);   // tampilan jeda sebelum start
void displayWAKTU();                             // tampilan jam-menit-detik (RTC palsu)
void initFakeRTC();                              // inisialisasi RTC palsu
void getFakeRTC(byte &hour, byte &minute, byte &second); // hitung waktu sekarang
void changeDelay();       // ganti delay sepelum start
void changeDownTime();    // ganti Hitungan mundur
void displayMarquee();    // Tampilan intro marquee
void BuzzerON();                             // bunyi buzzer
bool readButton(int pin, int buttonIndex);   // menerima pembacaan tombol fisik
void handleSerialCommand();                  // menerima perintah serial

void setup() {
  pinMode(BTN_START_STOP, INPUT_PULLUP);
  pinMode(BTN_MODE,       INPUT_PULLUP);
  pinMode(BTN_DELAY,      INPUT_PULLUP);
  pinMode(BTN_RESET,      INPUT_PULLUP);
  pinMode(BUZZER_PIN,     OUTPUT);

  // Inisialisasi DMD2
  dmd.setBrightness(currentBrightness); // 0-255
  dmd.begin();

  Serial.begin(9600);  // Sesuaikan setting baudrate bluetooh JDY31

  Serial.println("Timer siap!");
  Serial.println("Perintah Serial:");
  Serial.println("  S + Enter = START/STOP");
  Serial.println("  D + Enter = DELAY");
  Serial.println("  R + Enter = RESET");
  Serial.println("  M + Enter = MODE");
  Serial.println("  B=xx = Set Brightness=xx");

  displayMarquee();
  sisaWaktu = downMinutes * 60000;

  // Inisialisasi fake RTC dari __TIME__ saat kompilasi
  initFakeRTC();

  // Mulai dari state WAKTU (tampilan jam)
  currentState = WAKTU;
  dmd.clearScreen();
}


void loop() {
  unsigned long currentMillis = millis();

  // Baca perintah dari Serial (meng-set flag serialStartStop, dll.)
  handleSerialCommand();

  // Baca tombol fisik (debounce)
  bool startStopPressed = readButton(BTN_START_STOP, 0);
  bool delayPressed     = readButton(BTN_DELAY,      1);
  bool ModePressed      = readButton(BTN_MODE,       2);
  bool resetPressed     = readButton(BTN_RESET,      3);

  // Gabungkan dengan event dari Serial (tombol virtual)
  startStopPressed |= serialStartStop;
  delayPressed     |= serialDelay;
  ModePressed      |= serialMode;
  resetPressed     |= serialReset;

  // Setelah dipakai satu siklus loop, reset flag serial agar “sekali tekan”
  serialStartStop = false;
  serialDelay     = false;
  serialMode      = false;
  serialReset     = false;

  // Update state tombol untuk tampilan status
  currDelayStart = delaySeconds;

  switch (currentState) {
    case WAKTU:
      displayWAKTU();
      break;

    case INIT:
      displayButtonStatus();

      if (ModePressed) {
        dmd.clearScreen();
        stateUpDn = !stateUpDn;
        displayButtonStatus();

      } else if (delayPressed) {
        changeDelay();

      } else if (resetPressed) {
        changeDownTime();

      } else if (startStopPressed) {
        remainingTime = delaySeconds * 1000;
        currentState  = MUNDUR;
        lastMillis    = currentMillis;
        dmd.clearScreen();
      }
      break;

    case MUNDUR:
      if (currentMillis - lastMillis >= 10) {
        if (remainingTime > 10) {
          remainingTime -= 10;
        } else {
          remainingTime = 0;
        }
        lastMillis = currentMillis;
      }

      displayTimeMundur(remainingTime);

      if (remainingTime <= 0) {
        dmd.clearScreen();
        dmd.selectFont(SystemFont5x7);
        dmd.drawString(2, 4, "MULAI");
        Serial.println("G");   // feedback tanda timer mulai ke serial
        startTime = millis();  // Reset start time untuk RUNNING
        BuzzerON();

        if (stateUpDn) {
          currentState = RUNNING;
        } else {
          currentState = RUNNINGDOWN;
        }
        dmd.clearScreen();
      }
      break;

    case RUNNING: {
      unsigned long elapsed = currentMillis - startTime;
      displayTime(elapsed);

      if (startStopPressed) {
        currentState = STOPPED;
        stoppedTime  = elapsed;

        dmd.clearScreen();
        dmd.selectFont(SystemFont5x7);
        dmd.drawString(0, 4, " STOP");
        Serial.println("g");  // feedback tanda timer stop ke serial
        BuzzerON();

      } else if (resetPressed) {
        currentState = INIT;
        dmd.clearScreen();
      }
    }
    break;

    case RUNNINGDOWN: {
      unsigned long elapsed = currentMillis - startTime;
      sisaWaktu = downMinutes * 60000 - elapsed;

      displayTime(sisaWaktu);
      if (sisaWaktu <= 0) startStopPressed = true;

      if (startStopPressed) {
        currentState = STOPPED;
        stoppedTime  = sisaWaktu;

        dmd.clearScreen();
        dmd.selectFont(SystemFont5x7);
        dmd.drawString(0, 4, " STOP");
        Serial.println("g");  // feedback tanda timer stop ke serial
        BuzzerON();

      } else if (resetPressed) {
        currentState = INIT;
        dmd.clearScreen();
      }
    }
    break;

    case STOPPED:
      displayTime(stoppedTime);

      if (resetPressed) {
        currentState = INIT;
        dmd.clearScreen();
      }
      break;
  }
}

void BuzzerON() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(1000);
  digitalWrite(BUZZER_PIN, LOW);
}

void changeDelay() {
  delaySeconds = (delaySeconds + 1) % 10;
}

void changeDownTime() {
  downMinutes = (downMinutes + 1) % 30;
}
void changeDownTimeTurun() {
  downMinutes = (downMinutes - 1) % 30;
}

void displayButtonStatus() {
  dmd.selectFont(SystemFont5x7);

  char buffer[10];
  char udChar = stateUpDn ? 'u' : 'd';

  sprintf(buffer, "%c %d %d", udChar, downMinutes, currDelayStart);

  dmd.drawString(2, 0, "READY");
  dmd.drawString(2, 8, buffer);
}

void initFakeRTC() {
  // __TIME__ format "HH:MM:SS"
  const char *t = __TIME__;

  rtcHour0 = (t[0] - '0') * 10 + (t[1] - '0'); // HH
  rtcMin0  = (t[3] - '0') * 10 + (t[4] - '0'); // MM
  rtcSec0  = (t[6] - '0') * 10 + (t[7] - '0'); // SS

  rtcMillis0 = millis();
}

// Hitung jam-menit-detik sekarang berdasarkan millis()
void getFakeRTC(byte &hour, byte &minute, byte &second) {
  unsigned long elapsedMs = millis() - rtcMillis0;
  unsigned long totalSec  = rtcSec0 + elapsedMs / 1000UL;
  unsigned long totalMin  = rtcMin0 + totalSec / 60UL;
  unsigned long totalHour = rtcHour0 + totalMin / 60UL;

  second = totalSec  % 60;
  minute = totalMin  % 60;
  hour   = totalHour % 24;
}

void displayWAKTU() {
  byte h, m, s;
  getFakeRTC(h, m, s);

  char timeStr[3];

  // MENIT -> tetap di posisi minutes (angka besar)
  dmd.selectFont(Arial_Black_16);
  sprintf(timeStr, "%02u", m);
  dmd.drawString(1, 1, timeStr);

  // JAM -> di posisi seconds (kanan atas, font kecil)
  dmd.selectFont(SystemFont5x7);
  sprintf(timeStr, "%02u", h);
  dmd.drawString(19, 1, timeStr);

  // DETIK -> di posisi centisec (kanan bawah, font kecil)
  sprintf(timeStr, "%02u", s);
  dmd.drawString(19, 9, timeStr);
}

void displayTime(unsigned long elapsed) {
  unsigned long minutes  = (elapsed / 60000) % 60;
  unsigned long seconds  = (elapsed / 1000) % 60;
  unsigned long centisec = (elapsed / 10) % 100;

  char timeStr[3];
  dmd.selectFont(Arial_Black_16);
  sprintf(timeStr, "%02lu", minutes);
  dmd.drawString(1, 1, timeStr);

  dmd.selectFont(SystemFont5x7);
  sprintf(timeStr, "%02lu", seconds);
  dmd.drawString(19, 1, timeStr);

  sprintf(timeStr, "%02lu", centisec);
  dmd.drawString(19, 9, timeStr);
}

// REDUNDANT
// void displaySisaWaktu(unsigned long elapsed) {
//   unsigned long minutes  = (elapsed / 60000) % 60;
//   unsigned long seconds  = (elapsed / 1000) % 60;
//   unsigned long centisec = (elapsed / 10) % 100;

//   char timeStr[3];
//   dmd.selectFont(SystemFont5x7);
//   sprintf(timeStr, "%02lu", minutes);
//   dmd.drawString(2, 4, timeStr);

//   sprintf(timeStr, "%02lu", seconds);
//   dmd.drawString(19, 1, timeStr);

//   sprintf(timeStr, "%02lu", centisec);
//   dmd.drawString(19, 9, timeStr);
// }

void displayTimeMundur(unsigned long elapsed) {
  unsigned long minutes  = (elapsed / 60000) % 60;
  unsigned long seconds  = (elapsed / 1000) % 60;
  unsigned long centisec = (elapsed / 10) % 100;

  char timeStr[5];

  dmd.selectFont(SystemFont5x7);
  sprintf(timeStr, "%02lu ", seconds);
  dmd.drawString(2, 4, timeStr);

  sprintf(timeStr, "%02lu", centisec);
  dmd.drawString(18, 4, timeStr);
}

bool readButton(int pin, int buttonIndex) {
  bool reading = digitalRead(pin);

  if (reading != lastButtonState[buttonIndex]) {
    lastDebounceTime[buttonIndex] = millis();
  }

  if ((millis() - lastDebounceTime[buttonIndex]) > debounceDelay) {
    if (reading != buttonState[buttonIndex]) {
      buttonState[buttonIndex] = reading;
      if (buttonState[buttonIndex] == LOW) {
        lastButtonState[buttonIndex] = reading;
        return true;  // “tekan sekali”
      }
    }
  }

  lastButtonState[buttonIndex] = reading;
  return false;
}

// Marquee sederhana di awal
void displayMarquee() {
  // dmd.clearScreen();
  // dmd.selectFont(Arial_Black_16);

  // const char *text = "Timer";
  // int textWidth  = dmd.stringWidth(text);
  // int displayWidth = dmd.width;

  // int x = displayWidth;
  // unsigned long lastScroll = millis();
  // const unsigned long scrollInterval = 35;

  // while (x > -textWidth - 4) {
  //   unsigned long now = millis();

  //   if (now - lastScroll >= scrollInterval) {
  //     lastScroll = now;
  //     dmd.clearScreen();
  //     dmd.drawString(x, 0, text);
  //     x--;
  //   }

  //   // Baca tombol RESET / serial R
  //   handleSerialCommand();
  //   bool resetPressed   = digitalRead(BTN_RESET) == LOW;
  //   resetPressed       |= serialReset;
  //   serialReset         = false;
  //   if (resetPressed) {
  //     break;
  //   }
  // }
  
  // dmd.clearScreen();
}

// --- Handler perintah Serial ---
// S / s = start/stop
// D / d = delay
// R / r = reset
// M / m = mode
// B=xx : setBrigtness
void handleSerialCommand() {
  // Kalau tidak ada data, langsung keluar
  if (Serial.available() == 0) return;

  // Baca satu baris sampai '\n'
  String line = Serial.readStringUntil('\n');
  line.trim();               // buang spasi, \r, dll di kiri/kanan
  line.toUpperCase();        // supaya huruf kecil juga diterima

  if (line.length() == 0) return; // kalau baris kosong, abaikan

  char cmd = line.charAt(0); // karakter pertama = kode perintah

  switch (cmd) {
    case 'B': {
      // Format yang diharapkan: B=20 atau B 20
      int val = 0;
      int eqPos = line.indexOf('=');  // cari '='

      if (eqPos >= 0) {
        // Ada '=' → ambil setelah '='
        String numStr = line.substring(eqPos + 1);
        numStr.trim();
        val = numStr.toInt();
      } else if (line.length() > 1) {
        // Tidak ada '=' tapi ada angka setelah B, misal "B20"
        String numStr = line.substring(1);
        numStr.trim();
        val = numStr.toInt();
      } else {
        // Tidak ada angka → gunakan nilai sekarang / abaikan
        Serial.println(F("Format B salah. Contoh: B=20"));
        return;
      }

      // Batasi 0–255
      if (val < 0)   val = 0;
      if (val > 255) val = 255;

      currentBrightness = val;
      dmd.setBrightness(currentBrightness);

      Serial.print(F("Brightness set to: "));
      Serial.println(currentBrightness);
      break;
    }

    case 'S':
      serialStartStop = true;
      Serial.println(F("CMD: START/STOP"));
      break;

    case 'D':
      serialDelay = true;
      Serial.println(F("CMD: DELAY"));
      break;

    case 'R':
      serialReset = true;
      Serial.println(F("CMD: RESET"));
      break;

    case 'M':
      serialMode = true;
      Serial.println(F("CMD: MODE"));
      break;

    case 'T':
      changeDownTimeTurun();
      Serial.println(F("CMD: CHANGE DOWN TIME"));
      break;

    case 'W':
      // Toggle antara WAKTU dan INIT
      if (currentState == WAKTU) {
        currentState = INIT;
        Serial.println(F("STATE: INIT"));
      } else {
        currentState = WAKTU;
        Serial.println(F("STATE: WAKTU"));
      }
      dmd.clearScreen();
      break;

    default:
      Serial.print(F("Unknown command: "));
      Serial.println(cmd);
      break;
  }
}
