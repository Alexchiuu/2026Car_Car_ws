#define CUSTOM_NAME "AlexCarCar" // Max length is 12 characters [1]

long baudRates[] = {9600, 19200, 38400, 57600, 115200, 4800, 2400, 1200, 230400};
bool moduleReady = false;


#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

// ── IR Sensor Pins ────────────────────────────────────────────────────────────

#define IR1 A0
#define IR2 A10
#define IR3 A1   // centre
#define IR4 A6
#define IR5 A7

// Array for easy sensor access in loops
const int IR_PINS[5] = {IR1, IR2, IR3, IR4, IR5};

String Path = "";
int IR_THRESHOLD[5] = {100,100,100,100,100};  // to be calibrated
const int NODE_DETECT_THRESHOLD = 1;


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

#define LEFT_BASE_SPEED 200  // 0-255
#define RIGHT_BASE_SPEED 200  // 0-255
#define LEFT_TURN_SPEED 90  // 0-255
#define RIGHT_TURN_SPEED 90  // 0-255


// ---------------------- PID controller parameters --------------------------
float Kp_val = 20;  
float Kd_val = 90; 

const unsigned long SAMPLE_TIME = 20; // ms

// runtime variables
float integral = 0.0;
int lastError = 0;
unsigned long lastTime = 0;

// sensor configuration: set true if the sensor reads LOWER value on the line
const bool IR_ACTIVE_WHEN_LOW = false; // change if your sensors behave opposite

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

// calibration values (stored as global variables)
int IR_WHITE[5] = {0};

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

  lastTime = millis();
}

void loop() {
  // Listen for commands from Bluetooth serial at any time
  if (Serial3.available()) {
    String command = Serial3.readStringUntil('\n');
    command.trim();
    
    Serial.println("Received command: " + command);
    
    // ==================== HANDLE CALIBRATION COMMANDS ====================
    if (command == "CALIB_WHITE") {
      handleWhiteCalibration();
    }
    else if (command == "CALIB_BLACK") {
      handleBlackCalibration();
    }
    // ==================== HANDLE PATH EXECUTION ====================
    else if (command == "SEND_PATH") {
      handlePathExecution();
    }
    // Unknown command
    else {
      Serial.println("Unknown command: " + command);
      Serial3.println("ERROR: Unknown command");
    }
  }
  
  delay(100);
}

// ==================== CALIBRATION HANDLERS ====================

void handleWhiteCalibration() {
  Serial.println("Starting WHITE calibration...");
  
  for (int i = 0; i < 5; i++) {
    Serial.print("Calibrating WHITE sensor ");
    Serial.print(i + 1);
    Serial.print("... ");
    delay(500);
    int white_val = analogRead(IR_PINS[i]);
    Serial.print("Value: ");
    Serial.println(white_val);
    Serial3.print("WHITE_");
    Serial3.print(i + 1);
    Serial3.print(":");
    Serial3.println(white_val);
  }
  Serial3.println("✓ WHITE calibration complete!");
}

void handleBlackCalibration() {
  Serial.println("Starting BLACK calibration...");
  
  for (int i = 0; i < 5; i++) {
    Serial.print("Calibrating BLACK sensor ");
    Serial.print(i + 1);
    Serial.print("... ");
    delay(500);
    int black_val = analogRead(IR_PINS[i]);
    Serial.print("Black value: ");
    Serial.println(black_val);
    Serial3.print("BLACK_");
    Serial3.print(i + 1);
    Serial3.print(":");
    Serial3.println(black_val);
  }
  Serial3.println("✓ BLACK calibration complete!");
}

// ==================== PATH EXECUTION HANDLER ====================

void handlePathExecution() {
  Serial.println("Waiting for path data...");
  
  String Path = "";
  bool pathReceived = false;
  unsigned long pathTimeout = millis() + 5000; // 5 second timeout
  
  while (!pathReceived && millis() < pathTimeout) {
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

  if (!pathReceived) {
    Serial.println("Path reception timeout!");
    Serial3.println("ERROR: Path reception timeout");
    return;
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
}


// ── Updated Navigation Logic ────────────────────────────────────────────────

// ── Updated Navigation Logic ────────────────────────────────────────────────

void goForwardThenFollowToNode() {
  // Initial burst to move off the current node
  setMotors(LEFT_BASE_SPEED, RIGHT_BASE_SPEED);
  delay(250); 

  lastError = 0; 
  int nodeDetectCount = 0;  
  // Counter for serial printing
  int loopCounter = 0;

  while (true) {

    //Serial3.print("hello");
    int sense[5];
    int active = readSensorsBinary(sense);
    //Serial3.println(active);


    // --- 1. ENHANCED DIGITAL ERROR MAPPING ---
    int error = 0;
    
    if (active > 0) {
      if (sense[0]){
        error = 4;
      }
      else if (sense[1]) error = 2;
      else if (sense[2]) error = 0;
      else if (sense[3]) error = -2;
      else if (sense[4]) error = -4;

      if (sense[0] && sense[1])      error = 3;
      else if (sense[1] && sense[2]) error = 1;
      else if (sense[2] && sense[3]) error = -1;
      else if (sense[3] && sense[4]) error = -3;
    } 
    else {
      if(lastError == 4) error = 5;
      else if(lastError == -4) error = -5;
      else error = lastError;
    }    

    // --- 2. PD CALCULATION ---
    float P = error * Kp_val;
    float D = (error - lastError) * Kd_val;
    float PD_Value = P + D;

    // --- PRINTING EVERY 10 LOOPS ---

    lastError = error; 

    // --- 3. APPLY TO MOTORS ---
    int leftSpeed  = LEFT_BASE_SPEED - (int)PD_Value;
    int rightSpeed = RIGHT_BASE_SPEED + (int)PD_Value;

    setMotors(leftSpeed, rightSpeed);

    // --- 4. NODE DETECTION ---
    if (active >= 3) {
      nodeDetectCount++;
      if (nodeDetectCount >= NODE_DETECT_THRESHOLD){
        setMotors(0, 0);
        break; 
      }
    } else {
      nodeDetectCount = 0;
    }
    
    // Check for RFID
    String rfidData = scanRFID();
    if (rfidData.length() > 0) {
      Serial3.println(rfidData);
    }

    delay(10); 
  }

  stopMotorsPID();
}

// Pivot left until center sensor finds the line in the middle
void leftTurn() {

  setMotors(0, RIGHT_BASE_SPEED * 0.5);

  delay(300);

  int state = 0;

  int sense[5];
  readSensorsBinary(sense);

  while(!sense[2] && state == 2) { // while center sensor is not active
    readSensorsBinary(sense);
    if(state == 0 && sense[0]) { // left sensor detects line first
      state = 1;
      setMotors(0, RIGHT_BASE_SPEED * 0.3); // slow down for better accuracy
    } else if(state == 1 && sense[1]) { // right sensor detects line first
      state = 2;
      setMotors(0, RIGHT_BASE_SPEED * 0.1); // slow down for better accuracy
    }
    delay(10); // small delay for sensor reading stability
  }
  stopMotorsPID();
}

// Pivot right until center sensor finds the line in the middle
void rightTurn() {

  setMotors(LEFT_BASE_SPEED * 0.5, 0);

  delay(300);

  int state = 0;

  int sense[5];
  readSensorsBinary(sense);

  while(!sense[2] && state == 2) { // while center sensor is not active
    readSensorsBinary(sense);
    if(state == 0 && sense[0]) { // left sensor detects line first
      state = 1;
      setMotors(LEFT_BASE_SPEED * 0.3, 0); // slow down for better accuracy
    } else if(state == 1 && sense[1]) { // right sensor detects line first
      state = 2;
      setMotors(LEFT_BASE_SPEED * 0.1, 0); // slow down for better accuracy
    }
    delay(10); // small delay for sensor reading stability
  }
  
  stopMotorsPID();
}

// Perform a U-turn by rotating until center sensor finds the line
void uTurn() {

  setMotors(LEFT_BASE_SPEED * 0.5, -RIGHT_BASE_SPEED * 0.5);

  delay(300);

  int state = 0;

  int sense[5];
  readSensorsBinary(sense);

  while(!sense[2] && state == 2) { // while center sensor is not active
    readSensorsBinary(sense);
    if(state == 0 && sense[0]) { // left sensor detects line first
      state = 1;
      setMotors(LEFT_BASE_SPEED * 0.3, -RIGHT_BASE_SPEED * 0.3); // slow down for better accuracy
    } else if(state == 1 && sense[1]) { // right sensor detects line first
      state = 2;
      setMotors(LEFT_BASE_SPEED * 0.1, -RIGHT_BASE_SPEED * 0.1); // slow down for better accuracy
    }
    delay(10); // small delay for sensor reading stability
  }
  
  stopMotorsPID();
}

void vTurn() {

  setMotors(-LEFT_BASE_SPEED * 0.5, RIGHT_BASE_SPEED * 0.5);

  delay(300);

  int state = 0;

  int sense[5];
  readSensorsBinary(sense);

  while(!sense[2] && state == 2) { // while center sensor is not active
    readSensorsBinary(sense);
    if(state == 0 && sense[0]) { // left sensor detects line first
      state = 1;
      setMotors(-LEFT_BASE_SPEED * 0.3, RIGHT_BASE_SPEED * 0.3); // slow down for better accuracy
    } else if(state == 1 && sense[1]) { // right sensor detects line first
      state = 2;
      setMotors(-LEFT_BASE_SPEED * 0.1, RIGHT_BASE_SPEED * 0.1); // slow down for better accuracy
    }
    delay(10); // small delay for sensor reading stability
  }
  
  stopMotorsPID();
}