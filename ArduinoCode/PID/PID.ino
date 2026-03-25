#include <Arduino.h>

// ── IR Sensor Pins ────────────────────────────────────────────────────────────

#define IR1 A3
#define IR2 A0
#define IR3 A1   // centre
#define IR4 A6
#define IR5 A7

String Path = "LLRRUUFF";
const int IR_THRESHOLD[5] = {};

// ── TB6612 Motor Driver Pins ──────────────────────────────────────────────────
// Left motor  (Motor A)
#define AIN1  7
#define AIN2  6
#define PWMA   10   // must be PWM-capable

// Right motor (Motor B)
#define BIN1  8
#define BIN2  9
#define PWMB  11   // must be PWM-capable

#define STBY  34   // standby pin

#define MOTOR_SPEED 100  // 0-255

// ---------------------- PID controller parameters --------------------------
float Kp = 40.0;   // proportional gain (tune this first)
float Ki = 0.00;   // integral gain
float Kd = 8.0;    // derivative gain

const unsigned long SAMPLE_TIME = 20; // ms

// runtime variables
float integral = 0.0;
int lastError = 0;
unsigned long lastTime = 0;

// sensor configuration: set true if the sensor reads LOWER value on the line
const bool IR_ACTIVE_WHEN_LOW = true; // change if your sensors behave opposite

// weights for sensor positions: left-most = -2 ... right-most = +2
const int weights[5] = {2, 1, 0, -1, -2};

void IR_calibration(){

  IR_WHITE[5] = {0};

  Serial.println("Place the car on white block and press any key to calibrate IR sensors...");
  while (!Serial.available()) {
    delay(100);
  }
  Serial.read();  // clear the input buffer

  for (int i = 0; i < 5; i++) {
    Serial.print("Calibrating IR sensor ");
    Serial.print(i + 1);
    Serial.print("... ");
    delay(1000);  // wait for 1 second
    IR_WHITE[i] = analogRead(IR1 + i);
    Serial.print("Value: ");
    Serial.println(IR_WHITE[i]);
  }

  Serial.println("Place the car on black block and press any key to calibrate IR sensors...");
  while (!Serial.available()) {
    delay(100);
  }
  Serial.read();  // clear the input buffer

  for (int i = 0; i < 5; i++) {
    Serial.print("Calibrating IR sensor ");
    Serial.print(i + 1);
    Serial.print("... ");
    delay(1000);  // wait for 1 second
    IR_THRESHOLD[i] = (analogRead(IR1 + i) + IR_WHITE[i]) / 2;  // set threshold halfway between white and black readings
    Serial.print("Value: ");
    Serial.println(IR_THRESHOLD[i]);
  }
}

// ---------------------- Motor utility helpers -----------------------------

void setLeftMotor(int speed) {
  // speed: -255 .. 255 (negative => reverse)
  if (speed >= 0) {
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);
    analogWrite(PWMA, constrain(speed, 0, 255));
  } else {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
    analogWrite(PWMA, constrain(-speed, 0, 255));
  }
}

void setRightMotor(int speed) {
  // speed: -255 .. 255 (negative => reverse)
  if (speed >= 0) {
    digitalWrite(BIN1, HIGH);
    digitalWrite(BIN2, LOW);
    analogWrite(PWMB, constrain(speed, 0, 255));
  } else {
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, HIGH);
    analogWrite(PWMB, constrain(-speed, 0, 255));
  }
}

void setMotors(int leftSpeed, int rightSpeed) {
  digitalWrite(STBY, HIGH);
  setLeftMotor(leftSpeed);
  setRightMotor(rightSpeed);
}

// Emergency stop
void stopMotorsPID() {
  analogWrite(PWMA, 0);
  analogWrite(PWMB, 0);
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);
}

// ---------------------- Sensor reading & error computation -----------------

int readSensorsBinary(int vals[5]) {
  // returns number of active sensors
  int active = 0;
  int raw[5];
  raw[0] = analogRead(IR1);
  raw[1] = analogRead(IR2);
  raw[2] = analogRead(IR3);
  raw[3] = analogRead(IR4);
  raw[4] = analogRead(IR5);

  for (int i = 0; i < 5; i++) {
    Serial.print(raw[i]);
    Serial.print("   ");
    bool onLine;
    if (IR_ACTIVE_WHEN_LOW) onLine = (raw[i] < IR_THRESHOLD[i]);
    else                     onLine = (raw[i] > IR_THRESHOLD[i]);
    vals[i] = onLine ? 1 : 0;
    if (vals[i]) active++;
  }
  Serial.println();
  return active;
}

int computeErrorFromSensors(int bv[5]) {
  // compute weighted average error; if no sensor sees the line return lastError*2
  int sumW = 0;
  int sumV = 0;
  for (int i = 0; i < 5; i++) {
    sumW += weights[i] * bv[i];
    sumV += bv[i];
  }
  if (sumV == 0) {
    // lost the line: try to use lastError to steer back
    return (lastError >= 0) ? 3 : -3; // strong steer towards last known direction
  }
  // return fractional error as integer (scaled by 1)
  float err = (float)sumW / (float)sumV;
  return (int)round(err);
}

// ---------------------- Setup & Loop -------------------------------------

void setup() {
  Serial.begin(115200);
  // IR pins are analog – no need to pinMode()

  // Motor pins
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT); pinMode(PWMA, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT); pinMode(PWMB, OUTPUT);
  pinMode(STBY, OUTPUT);
  digitalWrite(STBY, HIGH);
  stopMotorsPID();

  Serial.println("PID line follower ready");
  Serial.print("Kp="); Serial.print(Kp);
  Serial.print(" Ki="); Serial.print(Ki);
  Serial.print(" Kd="); Serial.println(Kd);

  lastTime = millis();
}

void loop() {
  IR_calibration();
  for(int i = 0; i < Path.length(); i++) {
    char cmd = Path.charAt(i);
    if (cmd == 'L') {
      leftTurn();
    } else if (cmd == 'R') {
      rightTurn();
    } else if (cmd == 'U') {
      uTurn();
    } else if (cmd == 'F') {
      goForwardThenFollowToNode();
    }
  }
  delay(10000);
}

void goForwardThenFollowToNode() {
  // drive straight for 500 ms
  setMotors(MOTOR_SPEED, MOTOR_SPEED);
  unsigned long start = millis();
  while (millis() - start < 500) {
    // small delay to avoid starving background tasks
    delay(1);
  }

  // reset integral term for cleaner behaviour while following
  integral = 0.0;

  // use a local timing variable so we don't disturb the main loop's lastTime
  unsigned long localLastTime = millis();

  // follow the line until a node is detected (>= 3 sensors active)
  while (true) {
    unsigned long now = millis();
    unsigned long timeChange = now - localLastTime;
    if (timeChange < SAMPLE_TIME) {
      delay(1);
      continue;
    }
    localLastTime = now;

    int sense[5];
    int active = readSensorsBinary(sense);
    Serial.println(active);

    // if we reached a node (3 or more sensors on the line) stop
    if (active <= 2){
      setMotors(MOTOR_SPEED, MOTOR_SPEED);
      delay(200);
      break;
    }
    int error = computeErrorFromSensors(sense);

    // PID calculations (reuse globals Kp, Ki, Kd and lastError)
    float dt = (float)timeChange / 1000.0;
    integral += error * dt;
    float derivative = (error - lastError) / dt;
    float output = Kp * error + Ki * integral + Kd * derivative;

    // map output to motor speeds
    int base = MOTOR_SPEED;
    int leftSpeed  = (int)constrain(base + output, -255, 255);
    int rightSpeed = (int)constrain(base - output, -255, 255);

    // if the line is lost, pivot to last known direction (same logic as loop)
    if (active == 0) {
      if (lastError > 0) {
        leftSpeed = base;
        rightSpeed = -base/2;
      } else {
        leftSpeed = -base/2;
        rightSpeed = base;
      }
    }

    setMotors(leftSpeed, rightSpeed);
    lastError = error;
  }

  // stop at the node
  stopMotorsPID();
}

// Pivot left until a new line is found (center sensor). Uses safety timeouts.
void leftTurn() {
  // start pivot left (left reverse, right forward)
  setMotors(0, MOTOR_SPEED);

  // wait until we leave the node (active sensors drop below 3)
  delay(600);

  stopMotorsPID();
}

// Pivot right until a new line is found (center sensor). Uses safety timeouts.
void rightTurn() {
  // start pivot left (left reverse, right forward)
  setMotors(MOTOR_SPEED, 0);

  // wait until we leave the node (active sensors drop below 3)
  delay(600);

  stopMotorsPID();
}

// Perform a U-turn by performing two ~90deg pivots so the center sensor is found twice.
void uTurn() {
  setMotors(MOTOR_SPEED, -MOTOR_SPEED);

  // wait until we leave the node (active sensors drop below 3)
  delay(750);

  stopMotorsPID();
}