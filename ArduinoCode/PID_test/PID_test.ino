// ── IR Sensor Pins ────────────────────────────────────────────────────────────

#define IR1 A3
#define IR2 A0
#define IR3 A1   // centre
#define IR4 A6
#define IR5 A7
#define IR_THRESHOLD 100  // adjust as needed (0-1023)

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

// Node / command-driven configuration
// The car will be idle on power-up. It will wait for a Bluetooth command before
// moving. Commands received over Serial3 (HC-10):
//  'F' - follow the line using PID until the next node is detected
//  'R' - turn right in place until line is found
//  'L' - turn left in place until line is found
//  'B' - U-turn (return)
//  'S' - stop
bool bluetooth_connected = false;
bool following_line = false;    // true when PID is actively following the line
bool waiting_at_node = false;   // true when stopped at a node and waiting for command
const unsigned long NODE_DEBOUNCE = 500; // ms between node triggers
unsigned long lastNodeAt = 0;

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
    //Serial.print(raw[i]);
    //Serial.print("   ");
    bool onLine;
    if (IR_ACTIVE_WHEN_LOW) onLine = (raw[i] < IR_THRESHOLD);
    else                     onLine = (raw[i] > IR_THRESHOLD);
    vals[i] = onLine ? 1 : 0;
    if (vals[i]) active++;
  }
  //Serial.println();
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

// Helper: execute a node maneuver based on a command character
void handleNode(char cmd) {
  stopMotorsPID();
  delay(120);

  // announce action over Bluetooth
  Serial.print("NODE ACTION: "); Serial.println(cmd);
  Serial3.println(String("NODE_ACTION:") + cmd);

  unsigned long start = millis();
  const unsigned long TURN_TIMEOUT = 600; // safety timeout (ms)

  if (cmd == 'F') {
    // move forward a short distance to pass the node
    setMotors(MOTOR_SPEED, MOTOR_SPEED);
    delay(350);

  } else if (cmd == 'R') {
    // rotate right in place until centre sensor sees the line (or timeout)
    setLeftMotor(MOTOR_SPEED);
    setRightMotor(-MOTOR_SPEED);
    while (millis() - start < TURN_TIMEOUT) {
      int s[5];
      readSensorsBinary(s);
      if (s[2]) break; // centre sensor found the line
      delay(10);
    }

  } else if (cmd == 'L') {
    // rotate left in place until centre sensor sees the line (or timeout)
    setLeftMotor(-MOTOR_SPEED);
    setRightMotor(MOTOR_SPEED);
    while (millis() - start < TURN_TIMEOUT) {
      int s[5];
      readSensorsBinary(s);
      if (s[2]) break;
      delay(10);
    }

  } else if (cmd == 'B') {
    // simple U-turn: rotate right for a fixed duration (tune as needed)
    setLeftMotor(MOTOR_SPEED);
    setRightMotor(-MOTOR_SPEED);
    delay(600);
  }

  stopMotorsPID();
  delay(120);

  // announce completion
  Serial.print("NODE DONE: "); Serial.println(cmd);
  Serial3.println(String("NODE_DONE:") + cmd);
}

// ---------------------- Setup & Loop -------------------------------------

void setup() {
  // also start Serial3 for HC-10 (Bluetooth) so we can send status updates
  Serial3.begin(9600);
  delay(50);
  Serial3.println("CAR_READY");

  // Motor pins
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT); pinMode(PWMA, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT); pinMode(PWMB, OUTPUT);
  pinMode(STBY, OUTPUT);
  digitalWrite(STBY, HIGH);
  stopMotorsPID();

  lastTime = millis();
}

void loop() {
  unsigned long now = millis();
  unsigned long timeChange = now - lastTime;
  if (timeChange < SAMPLE_TIME) return; // run at SAMPLE_TIME
  lastTime = now;

  int sense[5];
  int active = readSensorsBinary(sense);
  int error = computeErrorFromSensors(sense);

  // If we're not yet connected, check for any incoming byte to mark Bluetooth as connected
  if (!bluetooth_connected) {
    if (Serial3.available()) {
      // treat first incoming byte as connection/handshake
      bluetooth_connected = true;
      // flush the initial byte(s)
      while (Serial3.available()) Serial3.read();
      Serial.println("BT CONNECTED");
      Serial3.println("BT_CONNECTED");
    }
  }

  // If we are following the line using PID, run PID to keep on the line.
  if (following_line) {
    // Node detection while following: when we detect a node we stop and wait
    if (active >= 4 && (millis() - lastNodeAt) > NODE_DEBOUNCE) {
      lastNodeAt = millis();
      following_line = false;
      waiting_at_node = true;

      // Announce node detection and wait for command
      Serial.println("NODE DETECTED");
      Serial3.println("NODE_DETECTED");

      // stop motors and wait for a command from Bluetooth
      stopMotorsPID();

      // wait indefinitely (or you can add a timeout) for a single-char command
      char cmd = 0;
      while (true) {
        if (Serial3.available()) {
          int r = Serial3.read();
          if (r <= 0) continue;
          cmd = (char)r;
          if (cmd == '\n' || cmd == '\r' || cmd == ' ') { cmd = 0; continue; }

          // If more bytes follow, read them into a string to support word commands
          String rest = "";
          unsigned long t0 = millis();
          while (millis() - t0 < 50 && Serial3.available()) {
            char rc = (char)Serial3.read();
            if (rc == '\n' || rc == '\r') break;
            rest += rc;
          }
          // normalize
          if (rest.length() > 0) {
            rest.toUpperCase();
            String full = String(cmd);
            full += rest;
            full.trim();
            if (full.startsWith("FOLLOW") || full == "F") cmd = 'F';
            else if (full.startsWith("RIGHT") || full.startsWith("R")) cmd = 'R';
            else if (full.startsWith("LEFT") || full.startsWith("L")) cmd = 'L';
            else if (full.startsWith("BACK") || full.startsWith("B") || full.startsWith("RETURN")) cmd = 'B';
            else if (full.startsWith("STOP") || full.startsWith("S")) cmd = 'S';
            // otherwise keep first char mapping
          } else {
            // single-char normalization
            if (cmd >= 'a' && cmd <= 'z') cmd = cmd - 'a' + 'A';
          }

          break;
        }
        delay(10);
      }

      // echo and execute
      Serial3.println(String("RECV_CMD:") + cmd);

      if (cmd == 'F') {
        // resume PID following
        following_line = true;
        waiting_at_node = false;
        // reset PID state
        integral = 0.0;
        lastError = 0;
        // if currently not over the line, give a short forward bump to engage the motors
        if (active == 0) {
          setMotors(MOTOR_SPEED, MOTOR_SPEED);
          delay(200); // keep motors running briefly so they start
          // do NOT immediately stop; let PID take over on next loop
        }
        Serial3.println("RESUME_FOLLOW");
      } else if (cmd == 'R' || cmd == 'L' || cmd == 'B') {
        // perform requested maneuver; handleNode will announce over Serial3
        handleNode(cmd);
        waiting_at_node = false;
        // after turning we remain idle until next command (or you can auto-resume)
      } else if (cmd == 'S') {
        // stop and remain idle
        stopMotorsPID();
        waiting_at_node = false;
      }
    }

    // PID calculations only while following_line
    if (following_line) {
      float dt = (float)timeChange / 1000.0; // seconds
      integral += error * dt;
      float derivative = (error - lastError) / dt;
      float output = Kp * error + Ki * integral + Kd * derivative;

      // map output to motor differential
      int base = MOTOR_SPEED; // from existing definitions
      int leftSpeed  = (int)constrain(base + output, -255, 255);
      int rightSpeed = (int)constrain(base - output, -255, 255);

      // if no sensors active, we may want to pivot to find the line
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
  } else {
    // not following — ensure motors are stopped unless we are executing a manual turn
    // (handleNode manages motors during its operation)
    stopMotorsPID();
  }

  // debug print every 200 ms (mirror to Bluetooth)
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 200) {
    lastPrint = now;
    Serial.print("sensors:");
    for (int i = 0; i < 5; i++) Serial.print(sense[i]);
    Serial.print(" err="); Serial.print(error);
    Serial.print(" LstErr="); Serial.print(lastError);
    Serial.println();

    Serial3.print("sensors:");
    for (int i = 0; i < 5; i++) Serial3.print(sense[i]);
    Serial3.print(" err="); Serial3.print(error);
    Serial3.print(" LstErr="); Serial3.print(lastError);
    Serial3.println();
  }

  // Process any immediate single-char commands even when idle (optional)
  if (Serial3.available()) {
    int r = Serial3.read();
    if (r > 0) {
      char c = (char)r;
      if (c == '\n' || c == '\r' || c == ' ') { /* ignore */ }
      else {
        // read any trailing bytes to support full words (e.g. "FOLLOW\n")
        String rest = "";
        unsigned long t0 = millis();
        while (millis() - t0 < 50 && Serial3.available()) {
          char rc = (char)Serial3.read();
          if (rc == '\n' || rc == '\r') break;
          rest += rc;
        }
        String full = String(c);
        full += rest;
        full.toUpperCase();
        full.trim();

        char cmdc = 0;
        if (full.startsWith("FOLLOW") || full == "F") cmdc = 'F';
        else if (full.startsWith("RIGHT") || full.startsWith("R")) cmdc = 'R';
        else if (full.startsWith("LEFT") || full.startsWith("L")) cmdc = 'L';
        else if (full.startsWith("BACK") || full.startsWith("B") || full.startsWith("RETURN")) cmdc = 'B';
        else if (full.startsWith("STOP") || full.startsWith("S")) cmdc = 'S';

        // Accept commands even when not at node: F starts following, R/L/B execute immediately
        if (cmdc == 'F') {
          following_line = true;
          // reset PID state
          integral = 0.0;
          lastError = 0;
          // quick forward bump if not currently over the line
          int tmp_s[5];
          int tmp_active = readSensorsBinary(tmp_s);
          if (tmp_active == 0) {
            setMotors(MOTOR_SPEED, MOTOR_SPEED);
            delay(200); // allow motors to spin up
            // do NOT call stopMotorsPID() here
          }
          Serial3.println("CMD_START_F");
        } else if (cmdc == 'R' || cmdc == 'L' || cmdc == 'B') {
          handleNode(cmdc);
        } else if (cmdc == 'S') {
          stopMotorsPID();
        }
      }
    }
  }

  // simple serial tuning interface on USB (unchanged)
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'p': Kp += 0.5; break; // increase P
      case 'o': Kp = max(0.0, Kp - 0.5); break; // decrease P
      case 'i': Ki += 0.01; break;
      case 'k': Ki = max(0.0, Ki - 0.01); break;
      case 'd': Kd += 0.5; break;
      case 'c': Kd = max(0.0, Kd - 0.5); break;
      case 's': stopMotorsPID(); Serial.println("STOP"); break;
      case 'v': // print current gains
        Serial.print("Kp="); Serial.print(Kp);
        Serial.print(" Ki="); Serial.print(Ki);
        Serial.print(" Kd="); Serial.println(Kd);
        break;
      default: break;
    }
  }
}