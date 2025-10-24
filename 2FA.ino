#include <Arduino.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include <Wire.h>

// --- Hardware and Display Setup ---
#define SDA_PIN 5
#define SCL_PIN 6
#define EEPROM_SIZE 5120  // ESP32-C3 uses flash emulation

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL_PIN, /* data=*/ SDA_PIN);

// --- Security System ---
enum SystemState { LOCKED, UNLOCKED, ACCESS_DENIED };
SystemState currentState = LOCKED;

char challengePin[5];       // To store the 4-digit random PIN
char inputBuffer[5];        // To store the user's serial input
int bufferIndex = 0;

unsigned long lockoutTime = 0;
const unsigned long lockoutDuration = 600000; // 10-minute lockout
unsigned long lastFrame = 0;
const unsigned long frameMs = 50; // OLED refresh rate

// --- Function Declarations ---
void generateNewChallenge();
void handleSerialInput();
void handleDataStorageCommands();
void drawOLED();
void resetLoginState();

void setup() {
  Serial.begin(115200);
  Serial.println("--- ESP32-C3 Challenge-Response Access System ---");
  Serial.println("A PIN has been generated on the OLED screen.");
  Serial.println("Enter the PIN to gain access.");
  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();

  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Failed to initialize EEPROM emulation!");
    while (true);
  }

  randomSeed(analogRead(A0));
  generateNewChallenge();

 
}

void loop() {
  unsigned long now = millis();

  if (currentState == LOCKED || currentState == ACCESS_DENIED) {
    handleSerialInput();
  } else if (currentState == UNLOCKED) {
    handleDataStorageCommands();
  }

  if (now - lastFrame >= frameMs) {
    lastFrame = now;
    drawOLED();
  }
}

/**
 * @brief Generates a new 4-digit random PIN.
 */
void generateNewChallenge() {
  long newPin = random(1000, 10000);
  itoa(newPin, challengePin, 10);
}

/**
 * @brief Handles incoming serial data for PIN entry.
 */
void handleSerialInput() {
  if (currentState == ACCESS_DENIED && millis() < lockoutTime) return;
  if (currentState == ACCESS_DENIED) resetLoginState();

  if (Serial.available() > 0) {
    char receivedChar = Serial.read();

    if (receivedChar == '\n' || receivedChar == '\r') {
      inputBuffer[bufferIndex] = '\0';

      if (strcmp(inputBuffer, challengePin) == 0) {
        currentState = UNLOCKED;
        Serial.println("\nPIN Correct. Access Granted.");
        Serial.println("Commands: read <addr>, write <addr> <text>, logout");
      } else {
        currentState = ACCESS_DENIED;
        lockoutTime = millis() + lockoutDuration;
        Serial.println("\nPIN Incorrect. System locked for 10 minutes.");
      }
      bufferIndex = 0;
      memset(inputBuffer, 0, sizeof(inputBuffer));

    } else if (isDigit(receivedChar) && bufferIndex < 4) {
      inputBuffer[bufferIndex++] = receivedChar;
      Serial.print("*");
    }
  }
}

/**
 * @brief EEPROM commands for ESP32-C3 (string read/write).
 */
void handleDataStorageCommands() {
  if (!Serial.available()) return;

  String command = Serial.readStringUntil('\n');
  command.trim();

  if (command.startsWith("read")) {
    int address = command.substring(5).toInt();
    if (address < 0 || address >= EEPROM_SIZE) {
      Serial.println("Error: Invalid address.");
      return;
    }

    Serial.print("Read @ ");
    Serial.print(address);
    Serial.print(": ");

    // Read until null terminator or EEPROM end
    for (int i = address; i < EEPROM_SIZE; i++) {
      byte b = EEPROM.read(i);
      if (b == 0x00) break;
      Serial.print((char)b);
    }
    Serial.println();

  } else if (command.startsWith("write")) {
    int firstSpace = command.indexOf(' ');
    int secondSpace = command.indexOf(' ', firstSpace + 1);

    if (firstSpace == -1 || secondSpace == -1) {
      Serial.println("Error: Format is 'write <addr> <text>'");
      return;
    }

    int address = command.substring(firstSpace + 1, secondSpace).toInt();
    String data = command.substring(secondSpace + 1);

    if (address < 0 || address >= EEPROM_SIZE) {
      Serial.println("Error: Invalid address.");
      return;
    }

    int required = data.length() + 1;
    if (address + required > EEPROM_SIZE) {
      Serial.println("Error: Not enough EEPROM space for string.");
      return;
    }

    for (int i = 0; i < data.length(); i++) {
      EEPROM.write(address + i, data[i]);
    }
    EEPROM.write(address + data.length(), 0x00);
    EEPROM.commit(); // ⚠️ Required on ESP32

    Serial.print("Wrote '");
    Serial.print(data);
    Serial.print("' to address ");
    Serial.println(address);

  } else if (command.equalsIgnoreCase("logout")) {
    Serial.println("Logging out...");
    resetLoginState();
  } else {
    Serial.println("Unknown command. Use: read <addr> | write <addr> <text> | logout");
  }
}

/**
 * @brief Resets the system and generates a new challenge PIN.
 */
void resetLoginState() {
  currentState = LOCKED;
  bufferIndex = 0;
  memset(inputBuffer, 0, sizeof(inputBuffer));
  generateNewChallenge();
}

/**
 * @brief Main drawing function that updates the OLED.
 */
void drawOLED() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB10_tr);
    switch (currentState) {
      case LOCKED:
        u8g2.setFont(u8g2_font_fub20_tr);
        u8g2.setCursor(25, 55);
        u8g2.print(challengePin);
        break;
      case UNLOCKED:
   x
        break;
      case ACCESS_DENIED:
        
        break;
    }
  } while (u8g2.nextPage());
}
