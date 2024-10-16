#include <SPI.h>
#include <SD.h>

const int chipSelect = 10;
const char* filename = "flightlog001.txt"; // Change to actual file name

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ;
  }

  Serial.print("Initializing SD card...");
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    while (1);
  }
  Serial.println("Card initialized.");
  File dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile) {
    dataFile.truncate(0);

    dataFile.close();
    Serial.println("File contents erased successfully.");
  } else {
    Serial.println("Error opening file");
  }
}

void loop() {
}