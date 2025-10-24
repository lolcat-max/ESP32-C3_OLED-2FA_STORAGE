#include <Arduino.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include <Bitcoin.h>
#if defined(ESP32)
//#include <WiFi.h> // Required to manage the WiFi radio for entropy
#endif

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
char challengePin[5], inputBuffer[5];
int bufferIndex = 0;
unsigned long lockoutTime = 0;
const unsigned long lockoutDuration = 3000;
unsigned long lastFrame = 0;
const unsigned long frameMs = 50;

// --- Function Declarations ---
void generateNewChallenge();
void handleSerialInput();
void handleDataStorageCommands();
void drawOLED();
void resetLoginState();
void printHD(String mnemonic, String password = ""); // Default argument is defined here

void setup() {
  Serial.begin(115200);

  // Wait for Serial Connection
  unsigned long startTime = millis();
  while (!Serial && (millis() - startTime < 4000)) { /* wait */ }

  Serial.println("--- Dynamic Hardware Wallet Utility ---");
  Serial.println("A PIN has been generated on the OLED screen.");
  Serial.println("Enter the PIN to gain access.");

  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();

  #if defined(ESP32)
    EEPROM.begin(32);
    //WiFi.mode(WIFI_OFF);
  #endif

  randomSeed(analogRead(A0));
  generateNewChallenge(); 
}

void loop() {
  unsigned long now = millis();
  if (currentState == LOCKED || currentState == ACCESS_DENIED) { handleSerialInput(); } 
  else if (currentState == UNLOCKED) { handleDataStorageCommands(); }
  if (now - lastFrame >= frameMs) { lastFrame = now; drawOLED(); }
}

// =================================================================
// --- Bitcoin HD Wallet and Core System Functions ---
// =================================================================

// CORRECTED: The default argument is removed from the function definition
void printHD(String mnemonic, String password){
  HDPrivateKey hd(mnemonic, password);
  if(!hd){ Serial.println("Invalid mnemonic or password"); return; }
  Serial.println("--- HD Wallet Details ---");
  Serial.println("Mnemonic: " + mnemonic);
  Serial.println("Password: \"" + password + "\"");
  HDPrivateKey account = hd.derive("m/84'/0'/0'/");
  Serial.println("BIP84 Master Private Key (zprv): " + String(account));
  Serial.println("First Receiving Address (m/84'/0'/0'/0/0): " + String(account.derive("m/0/0").address()));
  Serial.println("---------------------------\n");
}

void handleDataStorageCommands() {
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command.startsWith("read")) {
            int address = command.substring(5).toInt();
            if (address >= 0 && address < EEPROM.length()) {
                Serial.print("Value at EEPROM addr " + String(address) + ": ");
                Serial.println(EEPROM.read(address));
            } else { Serial.println("Error: Invalid address."); }
        
        } else if (command.startsWith("btc")) {
            Serial.println("Loading wallet from EEPROM...");
            uint8_t storedEntropy[16];
            for (int i = 0; i < 16; i++) { storedEntropy[i] = EEPROM.read(i); }
            String mnemonic = mnemonicFromEntropy(storedEntropy, sizeof(storedEntropy));
            HDPrivateKey hd(mnemonic, "");
            if (!hd) { Serial.println("Error: No valid wallet in EEPROM. Use 'genbtc' first."); return; }
            HDPrivateKey account = hd.derive("m/84'/0'/0'/");
            String arg = (command.length() > 4) ? command.substring(4) : "";
            arg.trim();
            if (arg == "key") { Serial.println("BIP84 Master Private Key (zprv):\n" + String(account)); } 
            else if (arg == "addr") { Serial.println("First Receiving Address:\n" + String(account.derive("m/0/0").address())); } 
            else { Serial.println("Usage: btc <key | addr>"); }

        } else if (command == "genbtc") {
            Serial.println("\n--- Generating & Saving New 12-Word BIP39 Mnemonic ---");
            uint8_t entropy[16];

            #if defined(ESP32)
              //bool wifiWasOff = (WiFi.getMode() == WIFI_OFF);
              //if (wifiWasOff) { Serial.println("Temporarily enabling WiFi for entropy..."); WiFi.mode(WIFI_STA); delay(100); }
            #endif

            for(int i=0; i < sizeof(entropy); i++){
              #if defined(ESP32)
                entropy[i] = random(256); //quick fix
                //entropy[i] = esp_random() % 256; 
              #else
                entropy[i] = random(256);
              #endif
            }

            #if defined(ESP32)
              //if (wifiWasOff) { WiFi.mode(WIFI_OFF); Serial.println("WiFi radio disabled."); }
            #endif

            String newMnemonic = mnemonicFromEntropy(entropy, sizeof(entropy));
            printHD(newMnemonic, "");
            for (int i = 0; i < sizeof(entropy); i++) { EEPROM.write(i, entropy[i]); }
            #if defined(ESP32)
              EEPROM.commit();
            #endif
            Serial.println("Success: Wallet entropy saved to EEPROM addresses 0-15.\n");

        } else if (command == "logout") {
            Serial.println("Logging out. A new PIN has been generated.");
            resetLoginState();
        } else {
            Serial.println("Unknown command. Try: read, btc <key|addr>, genbtc, logout");
        }
    }
}

void handleSerialInput() {
  if (currentState == ACCESS_DENIED && millis() < lockoutTime) { return; }
  if (currentState == ACCESS_DENIED) { resetLoginState(); }

  if (Serial.available() > 0) {
    char receivedChar = Serial.read();
    if (receivedChar == '\n' || receivedChar == '\r') {
      inputBuffer[bufferIndex] = '\0';
      if (strcmp(inputBuffer, challengePin) == 0) {
        currentState = UNLOCKED;
        Serial.println("\nPIN Correct. Access Granted.");
        Serial.println("Commands: read, btc <key|addr>, genbtc, logout");
      } else {
        currentState = ACCESS_DENIED;
        lockoutTime = millis() + lockoutDuration;
        Serial.println("\nPIN Incorrect. System locked.");
      }
      bufferIndex = 0;
      memset(inputBuffer, 0, sizeof(inputBuffer));
    } else if (isDigit(receivedChar) && bufferIndex < 4) {
      inputBuffer[bufferIndex++] = receivedChar;
      Serial.print("*");
    }
  }
}

void generateNewChallenge() {
  long newPin = random(1000, 10000);
  itoa(newPin, challengePin, 10);
}

void resetLoginState() {
    currentState = LOCKED;
    bufferIndex = 0;
    memset(inputBuffer, 0, sizeof(inputBuffer));
    generateNewChallenge();
}

void drawOLED() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB10_tr);
    switch (currentState) {
      case LOCKED:
        u8g2.drawStr(0, 15, "Enter PIN on OLED:");
        u8g2.setFont(u8g2_font_fub20_tr);
        u8g2.setCursor(25, 55);
        u8g2.print(challengePin);
        break;
      case UNLOCKED:
        u8g2.drawStr(10, 35, "Access Granted");
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(5, 60, "Awaiting Command...");
        break;
      case ACCESS_DENIED:
        break;
    }
  } while (u8g2.nextPage());
}
