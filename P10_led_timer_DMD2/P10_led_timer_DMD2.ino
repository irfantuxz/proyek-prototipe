#include <SPI.h>
#include <DMD2.h>
#include <fonts/SystemFont5x7.h>
#include <fonts/Arial_Black_16.h>

// Konfigurasi panel P10 (32x16 pixel)
#define DISPLAYS_HORIZONTAL_COUNT 1
#define DISPLAYS_VERTICAL_COUNT 1

// Gunakan SPIDMD untuk performa terbaik pada DMD2 (SPIDMD = Hardware SPI)
SPIDMD dmd(DISPLAYS_HORIZONTAL_COUNT, DISPLAYS_VERTICAL_COUNT);

// Variabel timer
unsigned long remainingTime = 0;
unsigned long stoppedTime = 0;
unsigned long lastMillis = 0;
int delaySeconds = 5;  // 0-9
int downMinutes = 30;   // count down tiga menit
unsigned long startTime;
unsigned long sisaWaktu;

// Tombol
#define BTN_START_STOP A0  // untuk START/STOP
#define BTN_DELAY A1       // untuk Delay count Down/Up
#define BTN_RESET A2       // untuk Reset Timer
#define BTN_MODE A3        // untuk pilihan count Down/Up
#define BUZZER_PIN 2       // pin Speaker Buzzer

bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
const unsigned long buzzerDuration = 1000;

// State tombol untuk debounce
bool lastButtonState[] = { HIGH, HIGH, HIGH, HIGH };
bool buttonState[] = { HIGH, HIGH, HIGH, HIGH };
unsigned long lastDebounceTime[] = { 0, 0, 0, 0 };
const unsigned long debounceDelay = 50;

// State untuk setiap tombol
int currBeforeStart = 0;  // variable count down delay
bool stateUpDn = false;    // true : up, false : down

enum State { INIT,
             MUNDUR,
             RUNNING,
             RUNNINGDOWN,
             STOPPED,
             SET_DELAY };
State currentState = INIT;

// marquee
unsigned long lastScroll = 0;
const unsigned long scrollInterval = 35; // ms antar langkah

// prototipe fungsi
void displayTime(unsigned long timeMs);
void displayDelay();
void changeDelay();
void displayButtonStatus();  // Fungsi baru untuk menampilkan status tombol
void displayMarquee();
bool readButton(int pin, int buttonIndex);
void BuzzerON();
void displayTimeMundur(unsigned long elapsed);
void displaySisaWaktu(unsigned long elapsed);
void changeDownTime();


void setup() {
  pinMode(BTN_START_STOP, INPUT_PULLUP);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_DELAY, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  // Inisialisasi DMD2 (Timer interrupt ditangani otomatis oleh library ini)
  dmd.setBrightness(50); // Opsional: atur kecerahan (0-255)
  dmd.begin();

  Serial.begin(9600);  // Sesuaikan dengan setting baud rate serial bluettoh JDY-31

  // Tampilan awal
  Serial.println("Timer siap!");

  dmd.selectFont(Arial_Black_16);
  displayMarquee();
  sisaWaktu = downMinutes * 60000;
}


void loop() {
  unsigned long currentMillis = millis();

  // Baca tombol dengan fungsi debounce yang diperbaiki
  bool startStopPressed = readButton(BTN_START_STOP, 0);
  bool delayPressed = readButton(BTN_DELAY, 1);
  bool ModePressed = readButton(BTN_MODE, 2);
  bool resetPressed = readButton(BTN_RESET, 3);

  // Update state tombol
  currBeforeStart = delaySeconds;

  switch (currentState) {
    case INIT:
      displayButtonStatus();

      if (ModePressed) {
        changeDownTime();

      } else if (delayPressed) {
        changeDelay();
      } else if (resetPressed) {
        // Reset action
        dmd.clearScreen();
        stateUpDn = !stateUpDn;
        displayButtonStatus();
      } else if (startStopPressed) {
        remainingTime = delaySeconds * 1000;
        currentState = MUNDUR;
        lastMillis = currentMillis;
        dmd.clearScreen();
      }
  //Serial.println("Display INIT");
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

      // displayTime(remainingTime);
      displayTimeMundur(remainingTime);

      if (remainingTime <= 0) {
        dmd.clearScreen();
        dmd.selectFont(SystemFont5x7);
        // DMD2 drawString: (x, y, text) - tanpa length dan mode
        dmd.drawString(2, 4, "MULAI");
        Serial.println("G");  // feedback tanda timer mulai ke serial
        BuzzerON();
        startTime = millis();  // Reset start time untuk RUNNING
        if (stateUpDn) {
          currentState = RUNNING;
        } else {
          currentState = RUNNINGDOWN;
        }
        dmd.clearScreen();
      }
  //Serial.println("Display MUNDUR");
      break;

    case RUNNING:
      {
        unsigned long elapsed = currentMillis - startTime;
        displayTime(elapsed);

        if (startStopPressed) {
          currentState = STOPPED;
          stoppedTime = elapsed;

          dmd.clearScreen();
          dmd.selectFont(SystemFont5x7);
          dmd.drawString(0, 4, " STOP");
          BuzzerON();

        } else if (resetPressed) {
          currentState = INIT;
          dmd.clearScreen();
        }
      }
  //Serial.println("Display RUNNING");
      break;
    case RUNNINGDOWN:
      {
        unsigned long elapsed = currentMillis - startTime;
        sisaWaktu = downMinutes * 60000 - elapsed;

        displaySisaWaktu(sisaWaktu);

        if (startStopPressed) {
          currentState = STOPPED;
          stoppedTime = sisaWaktu;

          dmd.clearScreen();
          dmd.selectFont(SystemFont5x7);
          dmd.drawString(0, 4, " STOP");
          BuzzerON();

        } else if (resetPressed) {
          currentState = INIT;
          dmd.clearScreen();
        }
      }
  //Serial.println("Display RUNDOWN");
      break;


    case STOPPED:
      displayTime(stoppedTime);

      // displayTime(remainingTime);

      if (resetPressed) {
        currentState = INIT;
        dmd.clearScreen();
      }
  //Serial.println("Display STOP");
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
  dmd.clearScreen();
  displayDelay();
}

void changeDownTime() {
  // dmd.clearScreen();
  downMinutes = (downMinutes + 1) % 30;
  displayButtonStatus();
}

void changeDownTimeB() {
  // dmd.clearScreen();
  downMinutes = (downMinutes - 1) % 30;
  displayButtonStatus();
}

// Fungsi baru untuk menampilkan status tombol
void displayButtonStatus() {
  dmd.selectFont(SystemFont5x7);

  // Buat string status tombol
  char buffer[10];
  char a0Char = stateUpDn ? 'u' : 'd';

  sprintf(buffer, "%c %d %d", a0Char, downMinutes, currBeforeStart);

  dmd.drawString(2, 0, "READY");

  // DMD2 menghitung panjang string secara otomatis
  dmd.drawString(2, 8, buffer);
}

void displayTime(unsigned long elapsed) {
  // Hitung komponen waktu
  unsigned long minutes = (elapsed / 60000) % 60;
  unsigned long seconds = (elapsed / 1000) % 60;
  unsigned long centisec = (elapsed / 10) % 100;  // 10ms = 1 centisecond

  // Format tampilan MM:SS:CS
  char timeStr[3];
  dmd.selectFont(Arial_Black_16);  // Memilih font besar yang didukung
  sprintf(timeStr, "%02lu", minutes);
  // Tampilkan di panel P10
  dmd.drawString(1, 1, timeStr);  // drawString(x,y, str); tanpa length

  dmd.selectFont(SystemFont5x7);
  sprintf(timeStr, "%02lu", seconds);
  dmd.drawString(19, 1, timeStr);

  sprintf(timeStr, "%02lu", centisec);
  dmd.drawString(19, 9, timeStr);
}

void displaySisaWaktu(unsigned long elapsed) {
  // Hitung komponen waktu
  unsigned long minutes = (elapsed / 60000) % 60;
  unsigned long seconds = (elapsed / 1000) % 60;
  unsigned long centisec = (elapsed / 10) % 100;  // 10ms = 1 centisecond

  // Format tampilan MM:SS:CS
  char timeStr[3];
  dmd.selectFont(SystemFont5x7);  // Memilih font besar yang didukung
  sprintf(timeStr, "%02lu", minutes);
  // Tampilkan di panel P10
  dmd.drawString(2, 4, timeStr);


  dmd.selectFont(SystemFont5x7);
  sprintf(timeStr, "%02lu", seconds);
  dmd.drawString(19, 1, timeStr);

  sprintf(timeStr, "%02lu", centisec);
  dmd.drawString(19, 9, timeStr);
}

void displayTimeMundur(unsigned long elapsed) {
  // Hitung komponen waktu
  unsigned long minutes = (elapsed / 60000) % 60;
  unsigned long seconds = (elapsed / 1000) % 60;
  unsigned long centisec = (elapsed / 10) % 100;  // 10ms = 1 centisecond

  // Format tampilan MM:SS:CS
  char timeStr[5];

  dmd.selectFont(SystemFont5x7);
  // Spasi ditambahkan untuk memastikan penghapusan karakter sebelumnya jika diperlukan
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
        return true;
      }
    }
  }

  lastButtonState[buttonIndex] = reading;
  return false;
}

void displayMarquee() {
  dmd.clearScreen();
  dmd.selectFont(Arial_Black_16);

  const char *text = "Timer";
  int textLength = strlen(text);

  // Perkiraan lebar teks (font besar, kira-kira 11–12 pixel per karakter)
  int charWidth = 11;
  int textWidth = textLength * charWidth;

  // Pada DMD2, biasanya ada properti width. Jika tidak, ganti dengan 32.
  int displayWidth = 32; //dmd.width; // atau: int displayWidth = 32;

  int x = displayWidth;                // mulai dari luar kanan
  unsigned long lastScroll = millis();

  // Scroll sampai teks benar-benar lewat sedikit di kiri
  while (x > -textWidth - 4) {         // -4 biar tidak terlalu lama blank
    unsigned long now = millis();

    if (now - lastScroll >= scrollInterval) {
      lastScroll = now;

      //dmd.clearScreen();
      dmd.drawString(x, 0, text);
      x--;
    }

    // Optional: bisa break kalau tombol RESET ditekan
    if (digitalRead(BTN_RESET) == LOW) {
      break;
    }
  }

  dmd.clearScreen();
}