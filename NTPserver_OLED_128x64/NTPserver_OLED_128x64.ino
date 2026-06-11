#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <Adafruit_SSD1306.h>
#include <MD_YX5300.h>
#include <SoftwareSerial.h>

WiFiMulti wifiMulti;
struct tm timeinfo;
time_t now;
char waktuNow[20];
bool waktuOK = false;
bool tombolNext = false;

byte tadiMin = 0;
byte cekMin = 0;

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET    -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define NTP_SERVER     "0.id.pool.ntp.org"
#define UTC_OFFSET     7 * 3600
#define UTC_OFFSET_DST 0

bool playerPause = true;  // true if player is currently paused
const uint8_t PIN_PLAY  = 4;    // play/pause toggle digital pin, active low (PULLUP)
const uint8_t PIN_NEXT  = 23;   // next toggle digital pin, active low (PULLUP)
const uint8_t PLAY_FOLDER = 1;   // tracks are all placed in this folder
// Connections for serial interface to the YX5300 module
const uint8_t MP3_RX = 18;    // connect to RX of MP3 Player module
const uint8_t MP3_TX = 19;    // connect to TX of MP3 Player module
SoftwareSerial  MP3Stream(MP3_RX, MP3_TX);  // MP3 player serial stream for comms
// Define global variables
MD_YX5300 mp3(MP3Stream);

unsigned long previousMillis = 0;
unsigned long interval = 10000;
short oldPlayState = 0;
short oldNextState = 0;
bool statusPlay = false;

void spinner() {
  static int8_t counter = 0;
  //const char* glyphs[] = {"\"","|","/","-"};
  const char glyphs[] = {'\\','|','/','-'};

  display.setCursor(120, 50);
  display.fillRect(120, 50, 10, 14, BLACK);
  display.print(glyphs[counter++]);
  if (counter == sizeof(glyphs)) {
    counter = 0;
  }
  display.display();
}

void printLocalTime() {
  //struct tm timeinfo;
  //if (!waktuOK) {
  if (!getLocalTime(&timeinfo)) {
    display.setCursor(0, 0);
    display.println("Network Error");
    configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
    display.display();
    waktuOK = false;
    return;
  }
  //}
  waktuOK = true;
  display.setCursor(0, 0);
  display.println(&timeinfo, "%H:%M:%S");
  display.println(&timeinfo, "%d/%m/%Y");
  display.display();
  strftime(waktuNow, sizeof(waktuNow), "%H:%M:%S %d-%m-%Y", &timeinfo);
}

void printCenter(const String buf, int x, int y)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(buf, x, y, &x1, &y1, &w, &h); //calc width of new string
  display.setCursor((x - w / 2) + (128 / 2), y);
  display.print(buf);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_PLAY, INPUT);
  pinMode(PIN_NEXT, INPUT);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;){
    }; // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.setTextSize(1);           
  display.setTextColor(SSD1306_WHITE);      
  display.setCursor(0,0); 
  display.println("Connecting to WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  wifiMulti.addAP("Wifi Shared", "12345678");
  wifiMulti.addAP("Wifi Retta", "kosongan");
  wifiMulti.addAP("Wifi Pemda", "kosongan");
  wifiMulti.addAP("Lab-Elektro.USD", "kendali2022");
  wifiMulti.run();

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    spinner();
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  display.println("Connected, IPadd:");
  display.println(WiFi.localIP());
  display.display();

  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);

  MP3Stream.begin(MD_YX5300::SERIAL_BPS);
  mp3.begin();
  mp3.setSynchronous(true);
  //mp3.playFolderRepeat(PLAY_FOLDER);
  Serial.print("Max vol : ");
  Serial.println(mp3.volumeMax());
  mp3.volume(mp3.volumeMax());
  mp3.check();
}

void loop() {
  mp3.check();
  display.clearDisplay();
  display.setTextSize(2);

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(0, 45);
    display.println("Online  ");
    //display.println(" WIB");
  }else{
    display.setCursor(0, 45);
    display.println("OFFline ");
    if ((WiFi.status() != WL_CONNECTED) && (millis() - previousMillis >= interval)) {
        delay(200);
        WiFi.disconnect();
        delay(50);
        wifiMulti.run();
        spinner();
        previousMillis = millis();
    }
  }
  
  cekMin = timeinfo.tm_min;
  if (cekMin != tadiMin){
    if(cekMin == 30){
      mp3.playNext();
      statusPlay = true;
    }
  tadiMin = cekMin;
  }

  delay(100);
  printLocalTime();

  int pinPlayState = digitalRead(PIN_PLAY);
  int pinNextState = digitalRead(PIN_NEXT);
  if(pinPlayState != oldPlayState){
    if(pinPlayState == 0) statusPlay = !statusPlay;
    oldPlayState = pinPlayState;
  }
  if(pinNextState != oldNextState){
    if(pinNextState == 0) mp3.playNext();
    if(pinNextState == 0) tombolNext = true;
    oldNextState = pinNextState;
  }

  if(statusPlay){
    mp3.playPause();
  }else{
    mp3.playStart();
  }

  if (tombolNext){
    if (waktuOK){
      Serial.print("waktuNow: ");
      Serial.println(waktuNow);
      Serial.print("jam: ");
      Serial.println(timeinfo.tm_hour);
      Serial.print("menit: ");
      Serial.println(timeinfo.tm_min);
    }
    tombolNext = false;
  }
}
