#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// Konstruktor sesuai permintaan
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// Pin tombol
const int BUTTON_PIN = 0;    // GPIO0 (BOOT button ESP32)

// Ukuran layar
const int SCREEN_W = 128;
const int SCREEN_H = 64;

// Properti burung
float birdX = 20;
float birdY = SCREEN_H / 2.0;
float birdVY = 0.0;
const float GRAVITY       = 0.25;   // percepatan jatuh
const float FLAP_STRENGTH = -3.5;   // impuls ke atas saat flap
const float MAX_FALL_SPEED = 4.5;   // batasi kecepatan jatuh
const uint8_t BIRD_R = 3;           // radius burung (lingkaran kecil)

// Properti pipa
float pipeX = SCREEN_W;
int   gapY  = 20;                   // posisi atas gap
const int PIPE_W     = 10;
const int GAP_H      = 40;          // tinggi celah burung lewat
const int PIPE_SPEED = 2;

// Skor
int score = 0;
int bestScore = 0;
bool passedPipe = false;            // supaya skor hanya nambah sekali per pipa

// Status game
bool gameOver = false;

// Tombol (untuk deteksi falling edge)
int lastButtonState = HIGH;

// Timing
unsigned long lastUpdate = 0;
const uint16_t FRAME_INTERVAL = 20; // ~50 FPS (20 ms per frame)

void resetGame()
{
  birdX = 20;
  birdY = SCREEN_H / 2.0;
  birdVY = 0.0;

  pipeX = SCREEN_W;
  // random posisi gap
  int minGapTop = 6;
  int maxGapTop = SCREEN_H - GAP_H - 6;
  gapY = random(minGapTop, maxGapTop);

  score = 0;
  passedPipe = false;
  gameOver = false;
}

void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);  // tombol aktif LOW

  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  // Seed random dari hardware RNG ESP32
  randomSeed(esp_random());

  resetGame();

  // Layar intro singkat
  u8g2.clearBuffer();
  u8g2.drawStr(10, 20, "Flappy Bird ESP32");
  u8g2.drawStr(8, 40, "Tekan BOOT (IO0)");
  u8g2.drawStr(8, 52, "untuk mulai");
  u8g2.sendBuffer();
  delay(1500);

  lastUpdate = millis();
}

void updatePhysics()
{
  // Update kecepatan & posisi burung
  birdVY += GRAVITY;
  if (birdVY > MAX_FALL_SPEED) birdVY = MAX_FALL_SPEED;
  birdY  += birdVY;

  // Gerakkan pipa ke kiri
  pipeX -= PIPE_SPEED;
  if (pipeX + PIPE_W < 0) {
    // Pipa keluar layar, spawn ulang di kanan
    pipeX = SCREEN_W;
    int minGapTop = 6;
    int maxGapTop = SCREEN_H - GAP_H - 6;
    gapY = random(minGapTop, maxGapTop);
    passedPipe = false;
  }

  // Cek skor (burung berhasil lewat pipa)
  if (!passedPipe && (pipeX + PIPE_W) < (birdX - BIRD_R)) {
    score++;
    passedPipe = true;
    if (score > bestScore) bestScore = score;
  }

  // Cek collision dengan batas atas/bawah
  if (birdY - BIRD_R < 0 || birdY + BIRD_R >= SCREEN_H) {
    gameOver = true;
  }

  // Cek collision dengan pipa
  bool withinPipeX = (birdX + BIRD_R >= pipeX) && (birdX - BIRD_R <= pipeX + PIPE_W);
  if (withinPipeX) {
    bool insideGapY = (birdY - BIRD_R >= gapY) && (birdY + BIRD_R <= gapY + GAP_H);
    if (!insideGapY) {
      gameOver = true;
    }
  }
}

void handleInput()
{
  int currentState = digitalRead(BUTTON_PIN);

  // Deteksi tepi: HIGH -> LOW (tombol baru ditekan)
  bool pressed = (lastButtonState == HIGH && currentState == LOW);
  lastButtonState = currentState;

  if (pressed) {
    if (gameOver) {
      // Restart game
      resetGame();
    } else {
      // Flap: beri impuls ke atas
      birdVY = FLAP_STRENGTH;
    }
  }
}

void drawGame()
{
  u8g2.clearBuffer();

  // Gambar pipa atas
  u8g2.drawBox((int)pipeX, 0, PIPE_W, gapY);
  // Gambar pipa bawah
  u8g2.drawBox((int)pipeX, gapY + GAP_H, PIPE_W, SCREEN_H - (gapY + GAP_H));

  // Gambar burung (lingkaran kecil)
  u8g2.drawDisc((int)birdX, (int)birdY, BIRD_R);

  // Tampilkan skor
  char buf[20];
  u8g2.setFont(u8g2_font_6x10_tf);

  snprintf(buf, sizeof(buf), "Score:%d", score);
  u8g2.drawStr(0, 10, buf);

  snprintf(buf, sizeof(buf), "Best:%d", bestScore);
  u8g2.drawStr(80, 10, buf);

  if (gameOver) {
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(30, 30, "GAME OVER");
    u8g2.setFont(u8g2_font_5x8_tf);
    u8g2.drawStr(8, 45, "Tekan BOOT (IO0)");
    u8g2.drawStr(20, 55, "untuk restart");
  }

  u8g2.sendBuffer();
}

void loop()
{
  unsigned long now = millis();
  if (now - lastUpdate < FRAME_INTERVAL) {
    // batasi frame rate
    return;
  }
  lastUpdate = now;

  handleInput();

  if (!gameOver) {
    updatePhysics();
  }

  drawGame();
}
