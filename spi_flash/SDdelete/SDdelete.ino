#include <SPI.h>
#include <SD.h>

const int chipSelect = BUILTIN_SDCARD;
const char* filename = "flightlog001.txt"; //Change to actual file name

void setup() {
    Serial.begin(9600);
    while (!Serial) { ; }

    Serial.println("Initializing SD card...");

    if (!SD.begin(chipSelect)) {
        Serial.println("Initialization failed!");
        return;
    }
    Serial.println("Initialization done.");
    deleteFile();
}

void loop() {
}

void deleteFile() {
    if (SD.exists(filename)) {
        Serial.print("Deleting file ");
        Serial.print(filename);
        Serial.println("...");
        if (SD.remove(filename)) {
            Serial.print("File ");
            Serial.print(filename);
            Serial.println(" deleted successfully.");
        } else {
            Serial.print("Error deleting ");
            Serial.println(filename);
        }
    } else {
        Serial.print("File ");
        Serial.print(filename);
        Serial.println(" does not exist.");
    }
}