/*
 * Car Test – Arduino Mega 2560
 *
 * Hardware:
 *   - 5x IR sensors       → analog pins A0-A4 (use IR_THRESHOLD to convert)
 *   - MFRC522 RFID        → SPI (SCK=52, MISO=50, MOSI=51, SS=2, RST=3)
 *   - HC-10 Bluetooth     → Serial3 (TX3=14, RX3=15) at 9600 baud
 *   - TB6612 Motor Driver → AIN1/AIN2/PWMA (left), BIN1/BIN2/PWMB (right), STBY
 *
 * Serial (USB)  → IR sensor raw data printed continuously
 * HC-10 BLE    → RFID UID pushed when a card is detected
 *                ← direction commands received: F/B/L/R/S
 */

#define CUSTOM_NAME "AlexCarCar" // Max length is 12 characters [1]
// ---------------------------

long baudRates[] = {9600, 19200, 38400, 57600, 115200, 4800, 2400, 1200, 230400};
bool moduleReady = false;
#include <SPI.h>
#include <MFRC522.h>

// ── IR Sensor Pins ────────────────────────────────────────────────────────────
#define IR1 A3
#define IR2 A4
#define IR3 A5   // centre
#define IR4 A6
#define IR5 A7
#define IR_THRESHOLD 512  // adjust as needed (0-1023)

// ── RFID Pins ─────────────────────────────────────────────────────────────────
#define RFID_SS  2
#define RFID_RST  3
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

#define R_MOTOR_SPEED 190  // 0-255
#define L_MOTOR_SPEED 200

// ── Timing ────────────────────────────────────────────────────────────────────
unsigned long lastIRPrint = 0;
const unsigned long IR_INTERVAL = 1000;  // print IR data every 100 ms

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

void stopMotors() {
  digitalWrite(AIN1, LOW);  digitalWrite(AIN2, LOW);  analogWrite(PWMA, 0);
  digitalWrite(BIN1, LOW);  digitalWrite(BIN2, LOW);  analogWrite(PWMB, 0);
}

void moveForward() {
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);  analogWrite(PWMA, L_MOTOR_SPEED);
  digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);  analogWrite(PWMB, R_MOTOR_SPEED);
}

void moveBackward() {
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, LOW);  digitalWrite(AIN2, HIGH); analogWrite(PWMA, L_MOTOR_SPEED);
  digitalWrite(BIN1, LOW);  digitalWrite(BIN2, HIGH); analogWrite(PWMB, R_MOTOR_SPEED);
}

void turnLeft() {
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, LOW);  digitalWrite(AIN2, HIGH); analogWrite(PWMA, L_MOTOR_SPEED);
  digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);  analogWrite(PWMB, R_MOTOR_SPEED);
}

void turnRight() {
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);  analogWrite(PWMA, L_MOTOR_SPEED);
  digitalWrite(BIN1, LOW);  digitalWrite(BIN2, HIGH); analogWrite(PWMB, R_MOTOR_SPEED);
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

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);       // USB – IR debug output
  
  while (!Serial);
  Serial.println("Initializing HM-10...");

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

  // IR pins
  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);
  pinMode(IR3, INPUT);
  pinMode(IR4, INPUT);
  pinMode(IR5, INPUT);

  // Motor driver pins
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT); pinMode(PWMA, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT); pinMode(PWMB, OUTPUT);
  pinMode(STBY, OUTPUT);
  stopMotors();
  digitalWrite(STBY, HIGH);

  // RFID (SPI)
  SPI.begin();
  mfrc522.PCD_Init();

  Serial.println("=== Car Test Ready ===");
  Serial.println("IR[1..5]  1=obstacle/line, 0=clear");
  Serial3.println("CAR_READY");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  
  // ── 1. Print IR data to Serial (USB) every IR_INTERVAL ms ─────────────────
  unsigned long now = millis();
  if (now - lastIRPrint >= IR_INTERVAL) {
    lastIRPrint = now;
    int v1 = analogRead(IR1);
    int v2 = analogRead(IR2);
    int v3 = analogRead(IR3);
    int v4 = analogRead(IR4);
    int v5 = analogRead(IR5);
    int s1 = (v1 >= IR_THRESHOLD) ? 1 : 0;
    int s2 = (v2 >= IR_THRESHOLD) ? 1 : 0;
    int s3 = (v3 >= IR_THRESHOLD) ? 1 : 0;
    int s4 = (v4 >= IR_THRESHOLD) ? 1 : 0;
    int s5 = (v5 >= IR_THRESHOLD) ? 1 : 0;
    // Print to USB serial
    Serial.print("IR: ");
    Serial.print(s1); Serial.print(" ");
    Serial.print(s2); Serial.print(" ");
    Serial.print(s3); Serial.print(" ");
    Serial.print(s4); Serial.print(" ");
    Serial.println(s5);
    // Also print to Bluetooth (Serial3)
    Serial3.print("IR: ");
    Serial3.print(s1); Serial3.print(" ");
    Serial3.print(s2); Serial3.print(" ");
    Serial3.print(s3); Serial3.print(" ");
    Serial3.print(s4); Serial3.print(" ");
    Serial3.println(s5);
  }
  
  // ── 2. Check for RFID card and send UID via Bluetooth ─────────────────────
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String uid = "RFID:";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
      uid += String(mfrc522.uid.uidByte[i], HEX);
      if (i < mfrc522.uid.size - 1) uid += ":";
    }
    uid.toUpperCase();
    Serial.println(uid);
    Serial3.println(uid);          // send to phone / PC via HC-10 on Serial3
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }

  
  // ── 3. Receive Bluetooth commands from HC-10 (Serial3) ─────────────────
  if (Serial3.available()) {
    char cmd = Serial3.read();
      switch (cmd) {
      case 'F': case 'f': moveForward();  Serial.println("CMD: Forward");  break;
      case 'B': case 'b': moveBackward(); Serial.println("CMD: Backward"); break;
      case 'L': case 'l': turnLeft();     Serial.println("CMD: Left");     break;
      case 'r': turnRight();    Serial.println("CMD: Right");    break;
      case 'S': case 's': stopMotors();   Serial.println("CMD: Stop");     break;
      default: break;
    }
  }
  
}
