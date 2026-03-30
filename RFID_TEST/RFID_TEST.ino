#define CUSTOM_NAME "AlexCarCar" // Max length is 12 characters

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

long baudRates[] = {9600, 19200, 38400, 57600, 115200, 4800, 2400, 1200, 230400};
bool moduleReady = false;

// ── RFID Pins ─────────────────────────────────────────────────────────────────
#define RFID_SS  53
#define RFID_RST  49
MFRC522 mfrc522(RFID_SS, RFID_RST);

// ── Bluetooth AT Helpers ──────────────────────────────────────────────────────

void sendATCommand(const char* command) {
  Serial3.print(command);
  waitForResponse("", 1000);
}

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

// ── RFID Scanning Function ────────────────────────────────────────────────────
/**
 * Scans for RFID card and returns UID if found
 * Returns: String with format "RFID:XX:XX:XX:XX" or empty string if no card
 */
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

void setup() {
  Serial.begin(9600);   // USB serial for debugging
  
  Serial.println("Initializing HM-10 Bluetooth module...");
  
  // 1. Automatic Baud Rate Detection
  for (int i = 0; i < 9; i++) {
    Serial.print("Testing baud rate: ");
    Serial.println(baudRates[i]);
    
    Serial3.begin(baudRates[i]);
    Serial3.setTimeout(100);
    delay(100);

    // 2. Force Disconnection
    // Sending "AT" while connected forces the module to disconnect
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
  sendATCommand("AT+RENEW");
  delay(500);

  // 4. Set Custom Name
  Serial.print("Setting name to: ");
  Serial.println(CUSTOM_NAME);
  String nameCmd = "AT+NAME" + String(CUSTOM_NAME);
  sendATCommand(nameCmd.c_str());
  
  // 5. Enable Connection Notifications
  Serial.println("Enabling notifications...");
  sendATCommand("AT+NOTI1");

  // 6. Get the Bluetooth MAC Address
  Serial.println("Querying Bluetooth Address");
  sendATCommand("AT+ADDR?");

  // 7. Restart the module to apply changes
  Serial.println("Restarting module...");
  sendATCommand("AT+RESET");
  delay(1000);
  Serial3.begin(9600); // Reconnect at 9600 baud
  
  Serial.println("Initialization Complete.");

  // RFID initialization (SPI)
  SPI.begin();
  mfrc522.PCD_Init();

  Serial.println("✨ RFID Test Ready!");
  Serial.println("Scan an RFID card to send it to the server...");
}

void loop() {
  // Scan for RFID card
  String rfidData = scanRFID();
  
  if (rfidData.length() > 0) {
    // RFID card detected!
    
    // Print to USB serial for debugging
    Serial.println("✓ RFID Card Detected!");
    Serial.print("  UID: ");
    Serial.println(rfidData);
    
    // Send RFID data to server via Bluetooth (Serial3)
    Serial3.println(rfidData);
    Serial.println("  📤 Sent to server via Bluetooth");
    
    // Wait a bit to avoid reading the same card multiple times
    delay(1000);
  }
  
  delay(100);  // Small delay to avoid overwhelming the loop
}
