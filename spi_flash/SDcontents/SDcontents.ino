#include <SPI.h>
#include <SD.h>

const int chipSelect = 10;
const char* filename = "flightlog23.txt"; //Change to actual file name

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

  File dataFile = SD.open(filename);

  if (dataFile) {
    while (dataFile.available()) {
      Serial.write(dataFile.read());
    }
    dataFile.close();
  } else {
    Serial.println("Error opening file");
  }
}

void loop() {}