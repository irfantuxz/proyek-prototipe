#include <PID_v1_bc.h>
#include "BluetoothSerial.h"
#include <EEPROM.h>
const int EEPROM_SIZE = 32;

String device_name = "ESP32-PID-Sudut-MotorDC";

// Check if Bluetooth is available
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// Check Serial Port Profile
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Port Profile for Bluetooth is not available or not enabled. It is only available for the ESP32 chip.
#endif

BluetoothSerial SerialBT;

// === Pin Configuration ===
const int pinSP   = 35;  // Analog input potensio SP
const int pinPV   = 34;  // Analog input potensio PV
const int pinMotorIN1 = 33;  // Output PWM motor IN2
const int pinMotorIN2 = 32;  // Output PWM motor IN1

// === PID Variables ===
#define DefaultKP 10.0
#define DefaultKI 4.0
#define DefaultKD 0.7
#define DefaultPWM 150

double Setpoint = 0;
double Input = 0;
double Output = 0;
double Kp = DefaultKP, Ki = DefaultKI, Kd = DefaultKD;
double lowKp=1.5, lowKi=0.01, lowKd=0.01;

// === PWM Configuration ===
int duty, pwmMin = DefaultPWM;
const int pwmMax = 255;

// === PID Setup ===
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

// === Mode ===
bool autoMode = true;
double manualSP = 110;

// === Helper Function ===

void loadEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, Kp);
  EEPROM.get(8, Ki);
  EEPROM.get(16, Kd);
  EEPROM.get(24, pwmMin);
  // Validasi nilai agar tidak korup
  if (isnan(Kp) || isnan(Ki) || isnan(Kd) || pwmMin < 0 || pwmMin > 255) {
    Kp = DefaultKP;
    Ki = DefaultKI;
    Kd = DefaultKD;
    pwmMin = DefaultPWM;
  }
  myPID.SetTunings(Kp, Ki, Kd);
}

void saveEEPROM() {
  EEPROM.put(0, Kp);
  EEPROM.put(8, Ki);
  EEPROM.put(16, Kd);
  EEPROM.put(24, pwmMin);
  EEPROM.commit();
}

void driveMotor(double pwm) {
  duty = map(abs((int)pwm), 0, 255, pwmMin, pwmMax);
  duty = constrain(duty, pwmMin, pwmMax);

  if (pwm > 0) {
    ledcWrite(pinMotorIN1, duty);
    ledcWrite(pinMotorIN2, 0);
  } else if (pwm < 0) {
    ledcWrite(pinMotorIN1, 0);
    ledcWrite(pinMotorIN2, duty);
  } else {
    ledcWrite(pinMotorIN1, 0);
    ledcWrite(pinMotorIN2, 0);
  }
}

void readADCinput(){
  // Variabel EMA
  const float alphaSP = 0.1;
  const float alphaPV = 0.5;

  if (autoMode) {
    float curSetpoint = map(analogRead(pinSP), 0, 4095, 0, 255);
    if (Setpoint == 0) Setpoint = curSetpoint;
    Setpoint = (alphaSP * curSetpoint) + ((1 - alphaSP) * Setpoint);
  } else {
    Setpoint = manualSP;
  }
  float curInput = map(analogRead(pinPV), 0, 4095, 0, 255);
  if (Input == 0) Input = curInput;
  Input = (alphaPV * curInput) + ((1 - alphaPV) * Input);
}

// === Initialization ===
void setup() {
  Serial.begin(115200);
  delay(500);
  SerialBT.begin(device_name);
  Serial.printf("Bluetooth \"%s\" ready to pair.\n", device_name.c_str());
  delay(500);

  pinMode(pinMotorIN1, OUTPUT);
  pinMode(pinMotorIN2, OUTPUT);
  ledcAttach(pinMotorIN1, 20000, 10);
  ledcAttach(pinMotorIN2, 20000, 10);

  loadEEPROM(); // Load nilai PID & PWMmin dari EEPROM

  readADCinput();
  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(-255, 255);

  ShowHelp();
}


// === Main Loop ===
void loop() {
  readADCinput();
  float gap = abs(Setpoint - Input);
  //if (gap < 10)
  //{
  //  myPID.SetTunings(lowKp, lowKi, lowKd);
  //  if (gap == 0) Output = 0;
  //}
  //else
    myPID.SetTunings(Kp, Ki, Kd);

  myPID.Compute();
  driveMotor(Output);

  // Output monitoring
  Stream& s = Serial;
  Stream& bt = SerialBT;
  for (Stream* out : {&s, &bt}) {
    (*out).print(autoMode ? "A " : "M ");
    (*out).print("SP: "); (*out).print(Setpoint, 0);
    (*out).print(" | PV: "); (*out).print(Input, 0);
    (*out).print(" | PWM: "); (*out).print(Output, 0);
    (*out).print("@"); (*out).print(duty, 0);
    (*out).print(" | Kp: "); (*out).print(Kp, 2);
    (*out).print(" Ki: "); (*out).print(Ki, 2);
    (*out).print(" Kd: "); (*out).println(Kd, 2);
  }

  // Input command handler
  serialCommand(Serial);
  serialCommand(SerialBT);

  delay(100);
}

// === Serial Command Handler ===
void ShowHelp(){
  Stream& s = Serial;
  Stream& bt = SerialBT;
  for (Stream* out : {&s, &bt}) {
    (*out).println("HELP command: ");
    (*out).println("sp=xxx   | set manual SetPoint to xxx");
    (*out).println("kp=xx.xx | set PID kP to xx.xx");
    (*out).println("ki=xx.xx | set PID kI to xx.xx");
    (*out).println("kd=xx.xx | set PID kD to xx.xx");
    (*out).println("pwm=xxx  | set motor PWM minimum to xxx");
    (*out).println("m | set mode MANUAL (adjust SP from bluetooth/USB)");
    (*out).println("a | set mode AUTO (adjust SP from knop input)");
    (*out).println("? | Show this help 3 seconds");
  }
  delay(3000);
}

void serialCommand(Stream& stream) {
  static String inputString = "";
  while (stream.available()) {
    char c = stream.read();
    if (c == '\n' || c == '\r') {
      inputString.trim();
      if (inputString.length() > 0) {
        handleCommand(inputString, stream);
      }
      inputString = "";
    } else {
      inputString += c;
    }
  }
}

// === Command Parser ===
void handleCommand(String cmd, Stream& stream) {
  cmd.toLowerCase();

  if (cmd == "a") {
    autoMode = true;
    stream.println("Mode: AUTO");
  } else if (cmd == "m") {
    autoMode = false;
    stream.println("Mode: MANUAL");
  } else if (cmd == "?") {
    ShowHelp();
  } else if (cmd.startsWith("sp=")) {
    manualSP = constrain(cmd.substring(3).toFloat(), 0, 255);
    stream.print("Manual Setpoint: ");
    stream.println(manualSP, 1);
  } else if (cmd.startsWith("kp=")) {
    Kp = cmd.substring(3).toFloat();
    myPID.SetTunings(Kp, Ki, Kd);
    stream.print("Kp updated: "); stream.println(Kp, 2);
    saveEEPROM();
  } else if (cmd.startsWith("ki=")) {
    Ki = cmd.substring(3).toFloat();
    myPID.SetTunings(Kp, Ki, Kd);
    stream.print("Ki updated: "); stream.println(Ki, 2);
    saveEEPROM();
  } else if (cmd.startsWith("kd=")) {
    Kd = cmd.substring(3).toFloat();
    myPID.SetTunings(Kp, Ki, Kd);
    stream.print("Kd updated: "); stream.println(Kd, 2);
    saveEEPROM();
  } else if (cmd.startsWith("pwm=")) {
    pwmMin = constrain(cmd.substring(4).toInt(), 0, pwmMax);
    stream.print("pwmMin updated: "); stream.println(pwmMin);
    saveEEPROM();
  } else {
    stream.println("Unknown command");
  }
}