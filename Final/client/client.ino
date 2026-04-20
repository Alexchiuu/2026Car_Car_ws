#define CUSTOM_NAME "AlexCarCar" // Max length is 12 characters [1]

long baudRates[] = {9600, 19200, 38400, 57600, 115200, 4800, 2400, 1200, 230400};
bool moduleReady = false;


#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

// ── IR Sensor Pins ────────────────────────────────────────────────────────────

#define IR1 A3
#define IR2 A4
#define IR3 A5   // centre
#define IR4 A6
#define IR5 A7

String Path = "";
int IR_THRESHOLD[5] = {100,100,100,100,100};  // to be calibrated

// ── RFID Pins ─────────────────────────────────────────────────────────────────
#define RFID_SS  53
#define RFID_RST  49
MFRC522 mfrc522(RFID_SS, RFID_RST);

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

#define RMOTOR_SPEED 200  // 0-255
#define LMOTOR_SPEED 200  // 0-255

// ---------------------- PID controller parameters --------------------------
float Kp = 50.0;   // proportional gain (tune this first)
float Ki = 0.00;   // integral gain
float Kd = 0.0;    // derivative gain

const unsigned long SAMPLE_TIME = 20; // ms

// runtime variables
float integral = 0.0;
int lastError = 0;
unsigned long lastTime = 0;

// sensor configuration: set true if the sensor reads LOWER value on the line
const bool IR_ACTIVE_WHEN_LOW = true; // change if your sensors behave opposite

// weights for sensor positions: left-most = -2 ... right-most = +2
const int weights[5] = {2, 1, 0, -1, -2};

// ── Helpers ───────────────────────────────────────────────────────────────────

void sendATCommand(const char* command) {
  Serial3.print(command);
  waitForResponse("", 1000); 
}

/**
 * Helper to check response for specific substrings
 */
bool waitForResponse(const char* expected, unsigned long timeout) {
  unsigned long start = millis();
  Serial3.setTimeout(timeout);
  String response = Serial3.readString();
  if (response.length() > 0) {
    Serial.print("HM10 Response: ");
    Serial.println(response);
  }
  return (response.indexOf(expected) != -1);
}

// ── HC-10 AT helpers (set name from Arduino via Serial3) ───────────────────
void readSerial3Response(unsigned long timeout_ms) {
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    while (Serial3.available()) {
      char c = Serial3.read();
      Serial.write(c); // forward HC-10 reply to USB serial for debugging
    }
  }
}

// calibration values (to be set in setup)

void IR_calibration(){

  int IR_WHITE[5] = {0};
  int IR_THRESHOLD[5] = {0};
  bool white_done = false;
  bool black_done = false;

  Serial.println("Waiting for calibration commands from server...");
  Serial3.println("Ready for calibration. Send 'CALIB_WHITE' or 'CALIB_BLACK'");
  
  while (!white_done || !black_done) {
    if (Serial3.available()) {
      String cmd = Serial3.readStringUntil('\n');
      cmd.trim();
      Serial.println("Received: " + cmd);
      
      if (cmd == "CALIB_WHITE" && !white_done) {
        Serial.println("Starting WHITE calibration...");
        Serial3.println("Place car on WHITE block. Calibrating in 3 seconds...");
        delay(3000);
        
        for (int i = 0; i < 5; i++) {
          Serial.print("Calibrating WHITE sensor ");
          Serial.print(i + 1);
          Serial.print("... ");
          delay(500);
          IR_WHITE[i] = analogRead(IR1 + i);
          Serial.print("Value: ");
          Serial.println(IR_WHITE[i]);
          Serial3.print("WHITE_");
          Serial3.print(i + 1);
          Serial3.print(":");
          Serial3.println(IR_WHITE[i]);
        }
        Serial.println("✓ WHITE calibration complete!");
        Serial3.println("✓ WHITE calibration complete!");
        white_done = true;
        delay(1000);
      }
      
      else if (cmd == "CALIB_BLACK" && !black_done) {
        Serial.println("Starting BLACK calibration...");
        Serial3.println("Place car on BLACK block. Calibrating in 3 seconds...");
        delay(3000);
        
        for (int i = 0; i < 5; i++) {
          Serial.print("Calibrating BLACK sensor ");
          Serial.print(i + 1);
          Serial.print("... ");
          delay(500);
          int black_val = analogRead(IR1 + i);
          IR_THRESHOLD[i] = (black_val + IR_WHITE[i]) / 2;
          Serial.print("Black value: ");
          Serial.print(black_val);
          Serial.print(", Threshold: ");
          Serial.println(IR_THRESHOLD[i]);
          Serial3.print("BLACK_");
          Serial3.print(i + 1);
          Serial3.print(":");
          Serial3.println(IR_THRESHOLD[i]);
        }
        Serial.println("✓ BLACK calibration complete!");
        Serial3.println("✓ BLACK calibration complete!");
        black_done = true;
        delay(1000);
      }
    }
    delay(100);
  }
  
  Serial.println("Calibration finished! Waiting for path...");
  Serial3.println("Calibration finished! Ready to receive path.");
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

// ---------------------- RFID Scanning -----------------

String scanRFID() {
  // Check for RFID card and return UID if found, empty string otherwise
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String uid = "RFID:";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
      uid += String(mfrc522.uid.uidByte[i], HEX);
      if (i < mfrc522.uid.size - 1) uid += ":";
    }
    uid.toUpperCase();
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return uid;
  }
  return "";
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
  Serial.begin(9600);// USB – IR debug output
  
  Serial.println("Initializing HM-10...");
  // IR pins are analog – no need to pinMode()
  // 1. Automatic Baud Rate Detection
  for (int i = 0; i < 9; i++) {
    Serial.print("Testing baud rate: ");
    Serial.println(baudRates[i]);
    
    Serial3.begin(baudRates[i]);
    Serial3.setTimeout(100);
    delay(100);

    // 2. Force Disconnection
    // Sending "AT" while connected forces the module to disconnect [2].
    Serial3.print("AT"); 
    
    if (waitForResponse("OK", 800)) {
      Serial.println("HM-10 detected and ready.");
      moduleReady = true;
      break; 
    } else {
      Serial3.end();
      delay(100);
    }
  }

  if (!moduleReady) {
    Serial.println("Failed to detect HM-10. Check 3.3V VCC and wiring.");
    return;
  }

  // 3. Restore Factory Defaults
  Serial.println("Restoring factory defaults...");
  sendATCommand("AT+RENEW"); // Restores all setup values
  delay(500);

  // 4. Set Custom Name via Macro
  Serial.print("Setting name to: ");
  Serial.println(CUSTOM_NAME);
  String nameCmd = "AT+NAME" + String(CUSTOM_NAME);
  sendATCommand(nameCmd.c_str()); // Max length is 12
  
  // 5. Enable Connection Notifications
  Serial.println("Enabling notifications...");
  sendATCommand("AT+NOTI1"); // Notify when link is established/lost

  // 6. Get the Bluetooth MAC Address
  Serial.println("Querying Bluetooth Address");
  sendATCommand("AT+ADDR?");

  // 7. Restart the module to apply changes
  Serial.println("Restarting module...");
  sendATCommand("AT+RESET"); // Restart the module
  delay(1000);
  Serial3.begin(9600); // Now the module would use baudrate 9600
  
  Serial.println("Initialization Complete.");

  // Motor pins
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT); pinMode(PWMA, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT); pinMode(PWMB, OUTPUT);
  pinMode(STBY, OUTPUT);
  digitalWrite(STBY, HIGH);
  stopMotorsPID();

  // RFID (SPI)
  SPI.begin();
  mfrc522.PCD_Init();

  Serial.println("PID line follower ready");
  Serial.print("Kp="); Serial.print(Kp);
  Serial.print(" Ki="); Serial.print(Ki);
  Serial.print(" Kd="); Serial.println(Kd);

  lastTime = millis();
}

void loop() {

  IR_calibration();

  do {
    
    // ==================== WAIT FOR SEND_PATH SIGNAL ====================
  Serial.println("Waiting for SEND_PATH signal from server...");
  bool sendPathReceived = false;
  
  while (!sendPathReceived) {
    if (Serial3.available()) {
      String received_msg = Serial3.readStringUntil('\n');
      received_msg.trim();
      Serial.println("Received: " + received_msg);

      if (received_msg == "SEND_PATH") {
        Serial.println("SEND_PATH signal received, waiting for path data...");
        sendPathReceived = true;
      }
    }
    delay(100);
  }

  // ==================== WAIT FOR PATH ====================
  String Path = "";
  bool pathReceived = false;
  
  while (!pathReceived) {
    if (Serial3.available()) {
      String received_msg = Serial3.readStringUntil('\n');
      received_msg.trim();
      
      // Check if it looks like a path (contains L, R, U, F)
      if (received_msg.length() > 0 && 
          (received_msg.indexOf('L') >= 0 || received_msg.indexOf('R') >= 0 || 
           received_msg.indexOf('U') >= 0 || received_msg.indexOf('F') >= 0)) {
        Path = received_msg;
        Serial.println("Path received: " + Path);
        pathReceived = true;
        break;
      }
    }
    delay(100);
  }

  // Confirm path reception to server
  Serial3.println("PATH_ACK");
  Serial.println("✓ Path ACK sent to server");
  delay(500);
  
  // ==================== EXECUTE PATH ====================
  Serial.println("Starting path execution...");
  Serial3.println("Execute: " + Path);

  for(int i = 0; i < Path.length(); i++) {
    char cmd = Path.charAt(i);
    if (cmd == 'L') {
      Serial3.println("CMD: Left");
      leftTurn();
    } else if (cmd == 'R') {
      Serial3.println("CMD: Right");
      rightTurn();
    } else if (cmd == 'U') {
      Serial3.println("CMD: U-Turn");
      uTurn();
    } else if (cmd == 'F') {
      Serial3.println("CMD: Forward");
      goForwardThenFollowToNode();
    }
    
    // Check for RFID card after each movement
    String rfidData = scanRFID();
    if (rfidData.length() > 0) {
      Serial.println("RFID detected: " + rfidData);
      Serial3.println(rfidData);  // Send RFID data to server
    }
  }

  Serial3.println("Path execution complete!");
  }while(1);
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
  
  // counter to detect node (must be active <= 2 for 10 consecutive readings)
  int nodeDetectCount = 0;
  const int NODE_DETECT_THRESHOLD = 5;
  bool pidEnabled = true;  // PID control state

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

    // Deactivate PID when active <= 2, reactivate when threshold not met
    if (active <= 2){
      pidEnabled = false;
      nodeDetectCount++;
      if (nodeDetectCount >= NODE_DETECT_THRESHOLD) {
        setMotors(LMOTOR_SPEED, RMOTOR_SPEED);
        delay(200);
        break;
      }
    } else {
      // reset counter and reactivate PID if condition not met
      nodeDetectCount = 0;
      pidEnabled = true;
    }
    
    int error = computeErrorFromSensors(sense);
    int leftSpeed, rightSpeed;
    int base = LMOTOR_SPEED;

    // Only apply PID if enabled
    if (pidEnabled) {
      // PID calculations (reuse globals Kp, Ki, Kd and lastError)
      float dt = (float)timeChange / 1000.0;
      integral += error * dt;
      float derivative = (error - lastError) / dt;
      float output = Kp * error + Ki * integral + Kd * derivative;

      // map output to motor speeds
      leftSpeed  = (int)constrain(base + output, -255, 255);
      rightSpeed = (int)constrain(base - output, -255, 255);

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
    } else {
      // When PID disabled, maintain straight movement
      leftSpeed = base;
      rightSpeed = base;
    }

    setMotors(leftSpeed, rightSpeed);
    lastError = error;
    
    // Check for RFID card while following the line
    String rfidData = scanRFID();
    if (rfidData.length() > 0) {
      Serial.println("RFID detected: " + rfidData);
      Serial3.println(rfidData);  // Send RFID data to server
    }
  }

  // stop at the node
  stopMotorsPID();
}

// Pivot left until center sensor finds the line in the middle
void leftTurn() {

  // start pivot left (left reverse, right forward)
  setMotors(0, RMOTOR_SPEED);
  delay(900);  // wait until we leave the node (active sensors drop below 3)

  stopMotorsPID();
}

// Pivot right until center sensor finds the line in the middle
void rightTurn() {

  // start pivot right (left forward, right reverse)
  setMotors(LMOTOR_SPEED, 0);
  delay(900); // Delay for 900 milliseconds

  stopMotorsPID();
}

// Perform a U-turn by rotating until center sensor finds the line
void uTurn() {

  setMotors(LMOTOR_SPEED, -RMOTOR_SPEED);

  delay(950);
  
  stopMotorsPID();
}