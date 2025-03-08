#define DEBUG 0

#define NUMPIXELS   10
#define PIN         12

#define TIMEOUT     100

#define PN532_IRQ   (2)
#define PN532_RESET (3)  // Not connected by default on the NFC Shield

#define WHITE   strip.Color(255, 255, 255)
#define RED     strip.Color(255, 0,   0)
#define GREEN   strip.Color(0,   255, 0)
#define BLUE    strip.Color(0,   0,   255)
#define YELLOW  strip.Color(255, 255, 0)
#define CYAN    strip.Color(0,   255, 255)
#define MAGENTA strip.Color(255, 0,   255)
#define ORANGE  strip.Color(255, 165, 0)
#define PURPLE  strip.Color(128, 0,   128)
#define PINK    strip.Color(255, 192, 203)
#define LIME    strip.Color(0,   255, 0)
#define AQUA    strip.Color(0,   255, 255)
#define TEAL    strip.Color(0,   128, 128)
#define VIOLET  strip.Color(238, 130, 238)

#include <Arduino.h>
#include <OSCMessage.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

// NEOPIXEL CONFIGURATION
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// ETHERNET CONFIGURATION
IPAddress ip(192, 168, 1, 98);                        //the Arduino's IP
const unsigned int inPort = 7001;                     //Arduino's Port

IPAddress outIp(192, 168, 1, 100);                    //destination IP
const unsigned int outPort = 7000;                    //destination Port
byte mac[] = {0x90, 0xA2, 0xDA, 0x0A, 0x2B, 0X1E};    //Arduino's MAC

EthernetUDP Udp;

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

bool success          =   false;
bool cardPresesnt     =   false;
bool mode             =   0;                                // Mode of operation
uint8_t numTags       =   0;                            // Number of tags
String removeCommand  =   "";                           // Remove command
String commands[]     = { "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""};     // Commands for tags
String tags[]         = { "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""};         // Tag IDs
String tagID          =   "";                           // Current Tag ID
String prevTagID      =   "";                           // Previous Tag ID

uint32_t colors[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, ORANGE, PURPLE, PINK, LIME, AQUA, TEAL, VIOLET};

/**
 * @brief Sets all NeoPixels to the specified color and updates the display.
 * 
 * This function iterates through all the NeoPixels in the strip and sets each one to the given color.
 * After setting the colors, it calls the show() method to update the display with the new colors.
 * 
 * @param color The color to set for all the NeoPixels. This is a 32-bit unsigned integer where the color
 *              is typically represented in the format 0xRRGGBB.
 */
void neoPixel(uint32_t color) {
  for(int i=0; i<NUMPIXELS; i++) {
    strip.setPixelColor(i, color);
  } strip.show();
}

/**
 * @brief Sends an OSC message to a specified address with a given type and column.
 *
 * This function constructs a full OSC address by appending the column number and 
 * "/connect" to the provided address. It then creates an OSC message with this 
 * full address and sends it via UDP to the specified IP and port.
 *
 * @param address The base OSC address to send the message to.
 * @param type The type of the OSC message (not used in the current implementation).
 * @param column The column number to append to the address.
 */
void oscSend(const char* address, const char* type, uint8_t column) {
  char fullAddress[50];
  snprintf(fullAddress, sizeof(fullAddress), "%s%d/connect", address, column);
  OSCMessage msg(fullAddress);
  Udp.beginPacket(outIp, outPort);
  msg.send(Udp);
  Udp.endPacket();
  msg.empty();
}


/**
 * @brief Writes a string to EEPROM starting at the specified address offset.
 *
 * This function writes the length of the string followed by the string's characters
 * to the EEPROM at the given address offset.
 *
 * @param addrOffset The starting address in EEPROM where the string will be written.
 * @param strToWrite The string to be written to EEPROM.
 */
void writeStringToEEPROM(int addrOffset, const String &strToWrite) {
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++) {
      EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
}

/**
 * @brief Reads a string from EEPROM starting at the specified address offset.
 *
 * This function reads the length of the string stored at the given EEPROM address offset,
 * then reads the subsequent characters to construct the string.
 *
 * @param addrOffset The starting address offset in EEPROM where the string is stored.
 * @return A String object containing the data read from EEPROM.
 */

String readStringFromEEPROM(int addrOffset) {
    int newStrLen = EEPROM.read(addrOffset);
    char data[newStrLen + 1];
    for (int i = 0; i < newStrLen; i++) {
        data[i] = EEPROM.read(addrOffset + 1 + i);
    }
    data[newStrLen] = '\0';
    return String(data);
}

/**
 * @brief Processes the given tag ID and executes the corresponding command if the tag is recognized.
 * 
 * This function compares the provided tag ID with a list of known tags. 
 * If a match is found, it Checks the mode of operation and sends the corresponding command to the Serial or Serial2 output.
 * If no match is found and debugging is enabled, it prints "UNKNOWN TAG" to the serial output.
 * 
 * @param tagID_ The tag ID to be processed.
 */
void processTagID(String tagID_){
  for (int i = 0; i < numTags; i++) {
    if (tagID_ == tags[i]) {
      oscSend("/composition/columns/", "i", i+1);
      Serial.println(); Serial.println(commands[i]); 
      neoPixel(colors[i]);
      return;
    }
  }
  if (DEBUG) {Serial.println("UNKNOWN TAG");}
}

/**
 * @brief Reads an NFC tag using the PN532 NFC reader.
 * 
 * This function waits for an NTAG203 card and reads its UID. It handles three main scenarios:
 * 1. No change in card presence.
 * 2. A new card is detected.
 * 3. The card is removed.
 * 
 * When a new card is detected, the UID is stored in the `tagID` variable and processed.
 * If the card is removed, it checks if the removed card's UID matches any known tags and performs the necessary actions.
 */
void readNFC(){
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  tagID = "";
  // Wait for an NTAG203 card.  When one is found 'uid' will be populated with
  // the UID, and uidLength will indicate the size of the UUID (normally 7)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, TIMEOUT);

  // NO CHANGE IN CARD
  if (success && cardPresesnt){ return; }
  // WAITING FOR NEW CARD
  if (!success && !cardPresesnt) { return; }
  // IF NEW TAG COUND
  if (success & (!cardPresesnt)) {
    cardPresesnt = true;
    // Store UID to tagID
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) { tagID += "0"; }
      tagID += String(uid[i], HEX);
    }
    tagID.toUpperCase();
    prevTagID = tagID;
    if (DEBUG) {
      Serial.println("Found an ISO14443A card");
      Serial.print("  UID Length: ");Serial.print(uidLength, DEC);Serial.println(" bytes");
      Serial.println("TAG ID: "+ tagID); Serial.print("  UID Value: "); nfc.PrintHex(uid, uidLength);
      Serial.println("");
    }
    processTagID(tagID);
    return;
  }
  // IF CARD REMOVED
  if (!success && cardPresesnt) {
    cardPresesnt = false;
    if(DEBUG) {Serial.println("CARD REMOVED");}
    for (int i = 0; i < numTags; i++) {
      if (prevTagID == tags[i]) {
        Serial.println(); Serial.println(removeCommand); 
        oscSend("/composition/columns/", "i", numTags+1);
        neoPixel(strip.Color(0, 0, 0));
        return;
      }
    }
  }
}

/**
 * @brief Initializes the NFC module and checks for the PN53x board.
 * 
 * This function begins communication with the NFC module and retrieves the firmware version.
 * If the PN53x board is not found, it halts the program. If the board is found, it prints
 * the chip and firmware version information to the Serial monitor and indicates that it is
 * waiting for an ISO14443A card.
 */
void nfcInit(){
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  
  if (!versiondata) {
    if (DEBUG) { Serial.println("Didn't find PN53x board");}
    while (1); // halt
  }
  
  if (DEBUG) {
    // Got ok data, print it out!
    Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX);
    Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC);
    Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);

    Serial.println("Waiting for an ISO14443A Card ...");
  } 
}

/**
 * @brief Processes the input data string and performs various actions based on its prefix.
 * 
 * This function handles different commands based on the prefix of the input data string:
 * - "N<num>": Sets the number of tags and stores it in EEPROM.
 * - "T<index>": Sets the last placed tag ID for the specified index and stores it in EEPROM.
 * - "C<index><command>": Sets a command for the specified index and stores it in EEPROM.
 * - "R<command>": Sets the tag remove command and stores it in EEPROM.
 * - "M<mode>": Sets the mode of operation (Master or Standalone) and stores it in EEPROM.
 * - "HELP": Prints help information about the available commands.
 * 
 * The function uses EEPROM to store and retrieve data, and communicates via Serial and Serial Bluetooth.
 * 
 * @param data The input data string containing the command and its parameters.
 */
void processData(String data) {
  if (data.startsWith("N")) {
    numTags = data.substring(1, data.length()).toInt();
    if (numTags > 20) numTags = 10;                   // Ensure numTags does not exceed array bounds
    Serial.println(numTags);
    EEPROM.write(0, numTags);
    delay(10);
    int num = EEPROM.read(0);
    Serial.println("NUM TAGS SET TO: " + String(num));
    return;
  } else if (data.startsWith("T")) {
    int index = data.substring(1, data.length()).toInt() - 1;
    if (index >= 0 && index < 20) {
      tags[index] = prevTagID;
      writeStringToEEPROM(10 + index * 10, prevTagID);
      delay(10);
    }
    for (int i = 0; i < numTags; i++) {
      String tagID_ = readStringFromEEPROM(10 + i * 10);
      Serial.println("Index: " + String(i+1) + " Tag ID: " + tagID_);
    } 
    return;
  } else if (data.startsWith("C")) {
    int index = data.substring(1, data.length()).toInt() - 1;
    if (index >= 0 && index < 20) {
      commands[index] = data.substring(3, data.length());
      writeStringToEEPROM(200 + index * 10, commands[index]);
      delay(10);
    }
    for (int i = 0; i < numTags; i++) {
      String command = readStringFromEEPROM(200 + i * 10);
      Serial.println("Index: " + String(i+1) + " Command: " + command);
    }
    return;
  } else if (data.startsWith("R")) {
    removeCommand = data.substring(1, data.length());
    writeStringToEEPROM(400, removeCommand);
    String command = readStringFromEEPROM(400);
    delay(10);
    Serial.println("Remove Command: " + command);
    return;
  } else if (data.indexOf("HELP")>=0){

    Serial.println("RFID Cube Podium PN532 - Firmware v1.0"); Serial.println();
    Serial.println("N<num> - Set number of tags. 'Eg: N10' ");
    Serial.println("T<index> - Set Last placed tag ID for index. Eg: T01");
    Serial.println("C<index><command> - Set command for index. Eg: C01HELLO - Set HELLO command for index 1");
    Serial.println("R<command> - Set Tag Remove command. Eg: RREMOVED - Set REMOVED command for tag remove");
    return;
  }
}

/**
 * @brief Reads data from the serial input if available and processes it.
 *
 * This function checks if there is any data available on the serial input.
 * If data is available, it reads the incoming data as a string until a newline character is encountered.
 * The read data is then passed to the processData function for further processing.
 * If debugging is enabled, the incoming data is also printed to the serial output.
 */
void readSerial(){
  if (Serial.available()) {
    String incoming = Serial.readStringUntil('\n');
    processData(incoming);
    if (DEBUG) {Serial.println(incoming);}
  }
}

/**
 * @brief Initializes the EEPROM and reads stored data.
 *
 * This function initializes the EEPROM with a size of 512 bytes. 
 * It then reads the number of stored tags from the EEPROM at address 0. 
 * It also reads the mode of operation from address 5.
 * The remove command is read from address 300. For each tag, it reads the tag ID starting from address
 * 10 and increments by 10 for each subsequent tag. Similarly, it reads the commands
 * associated with each tag starting from address 100 and increments by 10 for each
 * subsequent command.
 */
void eepromInit(){
  EEPROM.begin();                                  // eeprom init
  numTags = EEPROM.read(0);                           // read number of tags
  if (numTags > 20) numTags = 10;                     // Ensure numTags does not exceed array bounds
  removeCommand = readStringFromEEPROM(400);          // read remove command
  for (int i = 0; i < numTags; i++) {
    tags[i] = readStringFromEEPROM(10 + i * 10);      // read tagIDs
    commands[i] = readStringFromEEPROM(200 + i * 10); // read commands
  }
}

void initStrip() {
  strip.begin();
  strip.setBrightness(200); // Set brightness to 50%
  strip.show(); // Initialize all pixels to 'off'
}

void setup() {
  Serial.begin(115200);
  initStrip();
  eepromInit();
  nfcInit();
  Ethernet.begin(mac, ip);
  Udp.begin(inPort);
}

void loop() {
  readNFC();
  readSerial();
}

