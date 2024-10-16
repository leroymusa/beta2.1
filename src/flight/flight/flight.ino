//========================================================================================================//
//                                                                                                        //
//      This project is part of the main flight computer system designed for a friend's Level 2 (L2)      //
//      rocket certification attempt. The flight is set to launch in Montreal in October 2024.            //
//                                                                                                        //
//      This implementation will be used in the final flight code to ensure reliable and automated        //
//      operations during the flight. ~ Leroy                                                             //
//                                                                                                        //
//========================================================================================================//


// Libraries
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_BNO055.h>
#include <RH_RF95.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include "EKF.h"
#include "apogee.h"
#include "rocket_stages.h"
#include "songs.h"
#include "runcamsplits.h"
#include "quaternion.h"

// LEDs & pyros

  //-----------------//
  // Initialize Pins //
  //-----------------//

const int ledblu = 7, ledgrn = 4, ledred = 0, teensyled = 13;
ApogeeDetector detector;                     // Apogee detector object
double altitude_backing_array[WINDOW_SIZE];  // Array to store altitude data for the rolling window
RocketState currentState = PRE_LAUNCH;
unsigned long stateEntryTime = 0;
bool apogeeReached, mainChuteDeployed, isLowPowerModeEntered = false;
EKF ekf;  // Kalman filter object
bool LORA = false;
double initial_pressure;

// SD CARD(S) CS
const int chipSelect = BUILTIN_SDCARD;
const int sdCardPin = 10;
unsigned long launchTime = 0;
Quaternion q;
imu::Vector<3> accel;
imu::Vector<3> euler;
Adafruit_BNO055 bno = Adafruit_BNO055(55);
Adafruit_BMP280 bmp;

// LoRa settings
#define RFM95_CS 1
#define RFM95_RST 34
#define RFM95_INT 8
#define RF95_FREQ 915.0

RH_RF95 rf95(RFM95_CS, RFM95_INT);  // Declare the rf95 object

/* Already defined but I'm keeping here for visibility
const char* stateNames[] = {
    "PRE_LAUNCH",
    "LAUNCH_DETECTED",
    "FIRST_STAGE_BURNOUT",
    "STAGE_SEPARATION",
    "UPPER_STAGE_IGNITION",
    "APOGEE",
    "MAIN_CHUTE_DEPLOYMENT",
    "LANDING_DETECTED",
    "LOW_POWER_MODE"
};
*/

void setup() {
  Serial.begin(9600);
  Wire.begin();
  blinkMorseName();
  /*
  Serial1.begin(115200); Serial2.begin(115200); Serial3.begin(115200);
  */

  pinMode(ledblu, OUTPUT);
  pinMode(ledgrn, OUTPUT);
  pinMode(ledred, OUTPUT);
  pinMode(teensyled, OUTPUT);
  pinMode(pyrS1droguechute, OUTPUT);
  pinMode(pyrS1mainchute, OUTPUT);
  pinMode(pyrS12sep, OUTPUT);
  pinMode(pyroIgniteS2, OUTPUT);
  pinMode(pyrS2droguechute, OUTPUT);
  pinMode(pyrS2mainchute, OUTPUT);
  pinMode(buzzer, OUTPUT);

  // Initialize SD card 1
  if (!SD.begin(sdCardPin)) {
    Serial.println("SD card initialization failed!");
  } else {
    Serial.println("SD card initialized!");
  }

  // Initialize SD card 2
  if (!SD.begin(chipSelect)) {
    Serial.println("Built-in SD card initialization failed!");
  } else {
    Serial.println("Teensy SD card initialized!");
  }

  // LED indication for startup
  digitalWrite(ledblu, HIGH);
  delay(500);

  // Initialize BMP280 Pressure Sensor
  if (!bmp.begin(0x76/*0x77*/)) { //0x77 is the i2c address for prototyping board
    Serial.println("Could not find a valid BMP280 sensor, check wiring!");
    while (1) {
      digitalWrite(ledred, HIGH);
      delay(500);
      digitalWrite(ledred, LOW);
      delay(500);
    }
  }
  Serial.println("BMP280 initialized!");
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_125); /* Standby time. */

  initial_pressure = bmp.readPressure() / 100.0;

  // Initialize BNO055 Orientation Sensor
  if (!bno.begin()) {
    Serial.println("No BNO055 detected, check wiring!");
    while (1) {
      digitalWrite(ledred, HIGH);
      delay(300);
      digitalWrite(ledred, LOW);
      delay(300);
    }
  }
  Serial.println("BNO055 initialized!");

  digitalWrite(ledblu, LOW);
  digitalWrite(ledgrn, HIGH);
  digitalWrite(ledgrn, LOW);
  digitalWrite(ledred, HIGH);

  init_apogee_detector(&detector, altitude_backing_array, WINDOW_SIZE);
  ekf.begin(bmp.readAltitude(1013.25), 0);

  digitalWrite(ledred, LOW);
}


//------------//
// Begin Loop //
//------------//
//
//
//-------------------------------------------------//
//                                                 //
//    /////////      //            //////////      //
//    //      //     //            //        //    //
//    //      //     //            //        //    //
//    ////////       //            //////////      //
//    //      //     //            //              //
//    //       //    //            //              //
//    /////////      //////////    //              //
//                                                 //
//-------------------------------------------------//

void loop() {
  euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  accel = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);

  // for ekf updates!
  double current_altitude = bmp.readAltitude(initial_pressure);
  double current_accelY = accel.y();
  ekf.update(current_altitude, current_accelY);

  // for apogee detector and logging updates!
  double filtered_altitude = ekf.getFilteredAltitude();
  double filtered_accelY = ekf.Ay_filtered();

  unsigned long currentTime = millis(); // crucial for state machine and Runcam controls!
  static bool camerasTurnedOn = false;
  
  //============================================================//
  //=========         FSM (FINITE STATE MACHINE)       =========//
  //============================================================//
  Serial.print("Current state: ");
  Serial.println(stateNames[currentState]);

  switch (currentState) {
    case PRE_LAUNCH:
      sdwrite();
      Serial.println("Checking for launch...");
      Serial.print("Current acceleration (Y): ");
      Serial.println(filtered_accelY);

      // Start recording in PRE LAUNCH stage
      if (!camerasTurnedOn && currentTime - stateEntryTime >= 30000 /*Can be changed based on how long we are gonna be on the rail before launch*/) {
        camerasTurnedOn = true;
        Serial.println("Cameras turned on.");
      }

      if (camerasTurnedOn) {
        methodOn();
      }

      if (detectLaunch()) {
        changeState(LAUNCH_DETECTED);
        Serial.println("State changed to LAUNCH_DETECTED");
      }
      break;

    case LAUNCH_DETECTED:
      sdwrite();
      // REDUNDANCY! Just incase
      if (!camerasTurnedOn) {
        methodOn();
        camerasTurnedOn = true;
        Serial.println("Cameras turned on in LAUNCH_DETECTED.");
      }
      tiltLock();
      Serial.println("Launch detected. Checking for burnout...");
      if (detectBurnout()) {
        deployS1drogue();
        delay(10000);                      // Ensure it waits 10 seconds
        if (filtered_altitude >= 457.2) {  // 1500 feet in meters
          deployS1main();
        } else {
          Serial.println("Altitude too low for first stage main chute deployment.");
        }
        changeState(FIRST_STAGE_BURNOUT);
        Serial.println("State changed to FIRST_STAGE_BURNOUT");
      }
      break;

    case FIRST_STAGE_BURNOUT:
      sdwrite();
      tiltLock();
      Serial.println("First stage burnout detected. Waiting 10 seconds...");
      if (currentTime - stateEntryTime > 10000) {
        separatestages();
        changeState(STAGE_SEPARATION);
        Serial.println("State changed to STAGE_SEPARATION");
      }
      break;

    case STAGE_SEPARATION:
      sdwrite();
      tiltLock();
      Serial.println("Stage separation detected. Waiting 10 seconds...");
      if (currentTime - stateEntryTime > 10000) {
        igniteupperstagemotors();
        changeState(UPPER_STAGE_IGNITION);
        Serial.println("State changed to UPPER_STAGE_IGNITION");
      }
      break;

    case UPPER_STAGE_IGNITION:
      sdwrite();
      tiltLock();
      Serial.println("Upper stage ignition detected. Checking for apogee..."); 
      update_apogee_detector(&detector, filtered_altitude);
            if (is_apogee_reached(&detector) && !apogeeReached) {
                apogeeReached = true;
                deployS2drogue();
                changeState(APOGEE);
                Serial.println("State changed to APOGEE");
            }
      break;

    case APOGEE:
      sdwrite();
      Serial.println("Apogee detected. Checking for main chute deployment...");
      if (!mainChuteDeployed) {
        Serial.print("Current altitude: ");
        Serial.println(filtered_altitude);
        if (filtered_altitude <= 500) {      // 500 meters for the second stage deployment window
          if (filtered_altitude >= 457.2) {  // 1500 feet in meters
            deployS2main();
            mainChuteDeployed = true;
            changeState(MAIN_CHUTE_DEPLOYMENT);
            Serial.println("State changed to MAIN_CHUTE_DEPLOYMENT");
          } else {
            Serial.println("Altitude too low for second stage main chute deployment.");
          }
        }
      }
      break;

    case MAIN_CHUTE_DEPLOYMENT:
      sdwrite();
      Serial.println("Main chute deployed. Checking for landing...");
      if (detectLanding(bmp)) {
        changeState(LANDING_DETECTED);
        Serial.println("State changed to LANDING_DETECTED");
      }
      break;

    case LANDING_DETECTED:
      sdwrite();
      Serial.println("Landing detected. Entering low power mode...");
      lowpowermode(sdwrite, transmitData);
      changeState(LOW_POWER_MODE);
      Serial.println("State changed to LOW_POWER_MODE");
      break;

    case LOW_POWER_MODE:
      Serial.println("Low power mode active.");
      break;
  }
  delay(100);  // Prevent excess polling
  sdwrite();
}

void changeState(RocketState newState) {
  currentState = newState;
  stateEntryTime = millis();
  Serial.print("State changed to ");
  Serial.println(stateNames[newState]);
}

void blinkMorse(const char *morse) {
  for (int i = 0; morse[i] != '\0'; i++) {
    if (morse[i] == '.') {
      digitalWrite(teensyled, HIGH);
      delay(250);
    } else if (morse[i] == '-') {
      digitalWrite(teensyled, HIGH);
      delay(750);
    }
    digitalWrite(teensyled, LOW);
    delay(250);
  }
  delay(1000);
}

void blinkMorseName() {
  blinkMorse(".-..");  // L
  blinkMorse(".");     // E
  blinkMorse(".-.");   // R
  blinkMorse("---");   // O
  blinkMorse("-.--");  // Y

  delay(2000);

  blinkMorse("--");    // M
  blinkMorse("..-");   // U
  blinkMorse("...");   // S
  blinkMorse(".-");    // A
}