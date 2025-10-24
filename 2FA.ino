#include <Arduino.h>
#include <U8g2lib.h>
#include <EEPROM.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

// --- Hardware and Display Setup ---
#define SDA_PIN 5
#define SCL_PIN 6
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL_PIN, /* data=*/ SDA_PIN);

// --- Security System ---
enum SystemState { LOCKED, UNLOCKED, ACCESS_DENIED };
SystemState currentState = LOCKED;

char challengePin[5];       // To store the 4-digit random PIN
char inputBuffer[5];        // To store the user's serial input
int bufferIndex = 0;

unsigned long lockoutTime = 0;
const unsigned long lockoutDuration = 3000; // 3-second lockout
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
  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();

  // Use noise from a floating analog pin for true random numbers
  randomSeed(analogRead(A0));
  
  generateNewChallenge(); // Generate the first PIN
  
  Serial.println("--- Challenge-Response Access System ---");
  Serial.println("A PIN has been generated on the OLED screen.");
  Serial.println("Enter the PIN to gain access.");
}

void loop() {
  unsigned long now = millis();

  // State machine for handling access control
  if (currentState == LOCKED || currentState == ACCESS_DENIED) {
    handleSerialInput();
  } else if (currentState == UNLOCKED) {
    handleDataStorageCommands();
  }

  // Update the OLED display periodically
  if (now - lastFrame >= frameMs) {
    lastFrame = now;
    drawOLED();
  }
}

/**
 * @brief Generates a new 4-digit random PIN and stores it as a string.
 */
void generateNewChallenge() {
  long newPin = random(1000, 10000); // Generate a number between 1000 and 9999
  itoa(newPin, challengePin, 10);   // Convert the number to a string
}

/**
 * @brief Handles incoming serial data for PIN entry.
 */
void handleSerialInput() {
  // If in lockout, do nothing until the time has passed
  if (currentState == ACCESS_DENIED && millis() < lockoutTime) {
    return;
  }
  // If lockout is over, generate a new challenge and reset to LOCKED
  if (currentState == ACCESS_DENIED) {
    resetLoginState();
  }

  if (Serial.available() > 0) {
    char receivedChar = Serial.read();

    if (receivedChar == '\n' || receivedChar == '\r') {
      inputBuffer[bufferIndex] = '\0'; // Null-terminate the user's input

      if (strcmp(inputBuffer, challengePin) == 0) {
        // Correct PIN
        currentState = UNLOCKED;
        Serial.println("\nPIN Correct. Access Granted.");
        Serial.println("Data storage commands: read <addr>, write <addr> <val>, logout");
      } else {
        // Incorrect PIN
        currentState = ACCESS_DENIED;
        lockoutTime = millis() + lockoutDuration;
        Serial.println("\nPIN Incorrect. System locked for 3 seconds.");
      }
      bufferIndex = 0; // Reset buffer for next attempt
      memset(inputBuffer, 0, sizeof(inputBuffer));
      
    } else if (isDigit(receivedChar) && bufferIndex < 4) {
      inputBuffer[bufferIndex++] = receivedChar;
      Serial.print("*"); // Mask input in the serial monitor
    }
  }
}

/**
 * @brief Handles EEPROM commands when the system is unlocked.
 */
void handleDataStorageCommands() {
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command.startsWith("read")) {
            int address = command.substring(5).toInt();
            if (address >= 0 && address < EEPROM.length()) {
                byte value = EEPROM.read(address);
                Serial.print("Value at addr "); Serial.print(address); Serial.print(": "); Serial.println(value);
            } else {
                Serial.println("Error: Invalid address.");
            }
        } else if (command.startsWith("write")) {
            int firstSpace = command.indexOf(' ');
            int secondSpace = command.indexOf(' ', firstSpace + 1);
            int address = command.substring(firstSpace + 1, secondSpace).toInt();
            int value = command.substring(secondSpace + 1).toInt();

            if (address >= 0 && address < EEPROM.length()) {
                EEPROM.write(address, (byte)value);
                Serial.print("Wrote "); Serial.print(value); Serial.print(" to addr "); Serial.println(address);
            } else {
                Serial.println("Error: Invalid address.");
            }
        } else if (command == "logout") {
            Serial.println("Logging out. A new PIN has been generated.");
            resetLoginState();
        } else {
            Serial.println("Unknown command.");
        }
    }
}

/**
 * @brief Resets the system to the initial locked state and generates a new challenge.
 */
void resetLoginState() {
    currentState = LOCKED;
    bufferIndex = 0;
    memset(inputBuffer, 0, sizeof(inputBuffer));
    generateNewChallenge(); // Generate a new PIN for the next login attempt
}

/**
 * @brief Main drawing function that updates the OLED based on the system state.
 */
void drawOLED() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB10_tr); // Use a larger font
    
    switch (currentState) {
      case LOCKED:
        u8g2.setFont(u8g2_font_fub20_tr); // Even larger font for the PIN
        u8g2.setCursor(25, 55);
        u8g2.print(challengePin);
        break;
      
      case UNLOCKED:
        u8g2.drawStr(10, 35, "Access Granted");
        u8g2.setFont(u8g2_font_6x10_tf);
        break;

      case ACCESS_DENIED:
        u8g2.drawStr(15, 35, "PIN Incorrect");
        u8g2.setFont(u8g2_font_6x10_tf);
        break;
    }
  } while (u8g2.nextPage());
}
