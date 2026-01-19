#include "FS.h"
#include "SD.h"
#include "SPI.h"

// --- PIN CONFIGURATION ---
// CHANGE THESE if your wiring is different!
// Based on your photo, if you are using the Right Side pins (HSPI):
// const int SD_CS   = 15;
// const int SD_SCK  = 14;
// const int SD_MOSI = 13;
// const int SD_MISO = 12;

// If the above doesn't work, try the Standard (VSPI) pins:
const int SD_CS   = 5;
const int SD_SCK  = 18;
const int SD_MOSI = 23;
const int SD_MISO = 19;

SPIClass customSPI;

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if(!root){
    Serial.println("Failed to open directory");
    return;
  }
  if(!root.isDirectory()){
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()){
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if(levels){
        listDir(fs, file.name(), levels -1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
     file = root.openNextFile();
  }
}

void setup() {
  Serial.begin(115200);
  while(!Serial) { delay(10); } // Wait for serial console

  Serial.println("\n--- ESP32 MicroSD Card Test ---");
  Serial.print("Initializing SD card...");

  // Initialize the SPI bus with your specific pins
  customSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  // Attempt to mount the SD card
  if (!SD.begin(SD_CS, customSPI)) {
    Serial.println("\nFAILED: Card Mount Failed");
    Serial.println("Troubleshooting:");
    Serial.println("1. Check if the card is inserted fully.");
    Serial.println("2. Check if the Pin numbers in the code match your wiring.");
    Serial.println("3. Ensure the card is formatted (FAT32 is best).");
    return;
  }

  Serial.println("SUCCESS: Card initialized.\n");

  // Get Card Type
  uint8_t cardType = SD.cardType();
  Serial.print("Card Type: ");
  if(cardType == CARD_MMC) Serial.println("MMC");
  else if(cardType == CARD_SD) Serial.println("SDSC");
  else if(cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("UNKNOWN");

  // Get Card Size
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("Card Size: %lluMB\n", cardSize);
  
  // // Create a test file
  // Serial.print("Creating test.txt...");
  // File file = SD.open("/test.txt", FILE_WRITE);
  // if(file){
  //   file.println("Hello from ESP32!");
  //   file.close();
  //   Serial.println("Done.");
  // } else {
  //   Serial.println("Failed to open file for writing");
  // }

  // // Read the file back
  // Serial.print("Reading test.txt: ");
  // file = SD.open("/test.txt");
  // if(file){
  //   while(file.available()){
  //     Serial.write(file.read());
  //   }
  //   file.close();
  //   Serial.println("\nRead complete.");
  // }
  listDir(SD, "/", 0);

}

void loop() {
  // Nothing to do here
}