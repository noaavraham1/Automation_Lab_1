#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <IRremote.hpp>

// --------------------
// PIN CONNECTIONS
// --------------------
const byte redLed = 6;
const byte greenLed = 9;
const byte servoPin = 3;
const byte soundPin = A0;
const byte irPin = 8;
const byte resetButton = 7;

const byte trigPin = 12;
const byte echoPin = 4;

// --------------------
// COMPONENTS
// --------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo gateServo;

// --------------------
// SETTINGS
// --------------------
const byte NOISE_STATE = LOW;
const byte GUARD_BUTTON = 0x45;

// Continuous servo settings
const int STOP_SIGNAL = 1500;
const int CLOSE_SIGNAL = 1700;
const int OPEN_SIGNAL = 1300;
const int TURN_TIME = 400;

// Wait 10 seconds after reset
const unsigned long RESET_WAIT = 10000UL;

// DISTANCE: 10 CM OR LESS
const float EXIT_DISTANCE = 10.0;

// Distance checks every 100 ms
const unsigned long DISTANCE_INTERVAL = 100UL;

// Print distance every 500 ms
const unsigned long DISTANCE_PRINT = 2000UL;

// 3 clear readings before removing warning
const byte CLEAR_READINGS = 3;

// --------------------
// SYSTEM STATES
// --------------------
bool pollingClosed = false;
bool noiseArmed = false;
bool guardApproved = false;

bool waitingAfterReset = false;
unsigned long resetTime = 0;

byte lastButtonState = HIGH;

bool exitWarning = false;
byte clearCounter = 0;

unsigned long lastDistanceRead = 0;
unsigned long lastDistancePrint = 0;

// --------------------
// LCD FUNCTIONS
// --------------------
void showPollingOpen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("POLLING OPEN"));
}

void showWaiting() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("WAITING"));
}

void showPollingClosed() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("POLLING CLOSED"));
}

void showExitWarning() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("POLLING CLOSED"));

  lcd.setCursor(0, 1);
  lcd.print(F("PLEASE EXIT!"));
}

// --------------------
// MOTOR FUNCTIONS
// --------------------
void closeGate() {
  Serial.println(F("MOTOR CLOSING..."));

  gateServo.writeMicroseconds(CLOSE_SIGNAL);
  delay(TURN_TIME);

  gateServo.writeMicroseconds(STOP_SIGNAL);

  Serial.println(F("MOTOR STOPPED"));
}

void openGate() {
  Serial.println(F("MOTOR OPENING..."));

  gateServo.writeMicroseconds(OPEN_SIGNAL);
  delay(TURN_TIME);

  gateServo.writeMicroseconds(STOP_SIGNAL);

  Serial.println(F("MOTOR STOPPED"));
}

// --------------------
// DISTANCE MEASUREMENT
// --------------------
float readDistance() {

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration =
    pulseIn(echoPin, HIGH, 25000UL);

  // No echo received
  if (duration == 0) {
    return -1;
  }

  float distance = duration * 0.0343 / 2.0;

  // Invalid measurement
  if (distance < 2.0 || distance > 400.0) {
    return -1;
  }

  return distance;
}

// --------------------
// DISTANCE MONITORING
// --------------------
void checkDistanceSensor() {

  unsigned long now = millis();

  if (now - lastDistanceRead < DISTANCE_INTERVAL) {
    return;
  }

  lastDistanceRead = now;

  float distance = readDistance();

  // Invalid reading:
  // Do not assume the area is clear
  if (distance < 0) {
    clearCounter = 0;
    return;
  }

  // ==================================
  // PERSON WITHIN 10 CM
  // ==================================
  if (distance <= EXIT_DISTANCE) {

    clearCounter = 0;

    // Show warning only once
    if (!exitWarning) {

      exitWarning = true;

      showExitWarning();

      Serial.println(F("--------------------"));
      Serial.println(F("PERSON DETECTED"));
      Serial.println(F("LCD: PLEASE EXIT!"));
    }
  }

  // ==================================
  // PERSON OUTSIDE 10 CM
  // ==================================
  else {

    if (exitWarning) {

      clearCounter++;

      // Require 3 valid clear readings
      if (clearCounter >= CLEAR_READINGS) {

        exitWarning = false;
        clearCounter = 0;

        showPollingClosed();

        Serial.println(F("--------------------"));
        Serial.println(F("AREA CLEAR"));
        Serial.println(F("LCD: POLLING CLOSED"));
      }
    }
  }
}

// --------------------
// SETUP
// --------------------
void setup() {

  Serial.begin(9600);

  // LEDs OFF
  pinMode(redLed, OUTPUT);
  pinMode(greenLed, OUTPUT);

  digitalWrite(redLed, LOW);
  digitalWrite(greenLed, LOW);

  // Sound sensor
  pinMode(soundPin, INPUT);

  // Ultrasonic sensor
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);

  // Reset button
  pinMode(resetButton, INPUT_PULLUP);
  lastButtonState = digitalRead(resetButton);

  // Servo starts stopped
  gateServo.writeMicroseconds(STOP_SIGNAL);
  gateServo.attach(servoPin);

  // LCD blank during startup
  lcd.init();
  lcd.clear();
  lcd.noBacklight();

  // IR receiver
  IrReceiver.begin(irPin, DISABLE_LED_FEEDBACK);

  Serial.println(F("===================="));
  Serial.println(F("SYSTEM INITIALIZING"));
  Serial.println(F("LEDS OFF"));
  Serial.println(F("MOTOR STOPPED"));
  Serial.println(F("LCD BLANK"));

  // Wait 5 seconds
  delay(5000);

  // Show polling open
  lcd.backlight();
  showPollingOpen();

  Serial.println(F("===================="));
  Serial.println(F("SYSTEM READY"));
  Serial.println(F("POLLING OPEN"));
  Serial.println(F("SOUND SENSOR ACTIVE"));
}

// --------------------
// MAIN LOOP
// --------------------
void loop() {

  // ====================
  // STEP 1: RESET WAIT
  // ====================

  if (waitingAfterReset) {

    if (millis() - resetTime >= RESET_WAIT) {

     waitingAfterReset = false;
      noiseArmed = false;

      showPollingOpen();

      Serial.println(F("===================="));
      Serial.println(F("10 SECONDS COMPLETED"));
      Serial.println(F("SOUND SENSOR ACTIVE"));
    }
  }

  // ====================
  // STEP 2: SOUND SENSOR
  // ====================

  if (!pollingClosed && !waitingAfterReset) {

    byte soundState = digitalRead(soundPin);

    // First wait for quiet
    if (!noiseArmed) {

      if (soundState != NOISE_STATE) {

        noiseArmed = true;
        Serial.println(F("SOUND SENSOR ARMED"));
      }
    }

    // Noise detected
    else if (soundState == NOISE_STATE) {

      pollingClosed = true;

      exitWarning = false;
      clearCounter = 0;

      Serial.println(F("===================="));
      Serial.println(F("NOISE DETECTED!"));
      Serial.println(F("EMERGENCY MODE"));

      // Red LED ON
      digitalWrite(redLed, HIGH);
      digitalWrite(greenLed, LOW);

      Serial.println(F("RED LED ON"));

      // Polling closed
      showPollingClosed();

      Serial.println(F("POLLING CLOSED"));

      // Move servo once
      closeGate();

      // Activate distance monitoring
      lastDistanceRead = millis();
      lastDistancePrint = millis();

      Serial.println(F("DISTANCE SENSOR ACTIVE"));
      Serial.println(F("WAITING FOR GUARD"));
    }
  }

  // ====================
  // STEP 3: IR REMOTE
  // ====================

  if (IrReceiver.decode()) {

    byte command =
      IrReceiver.decodedIRData.command;

    bool isRepeat =
      IrReceiver.decodedIRData.flags &
      IRDATA_FLAGS_IS_REPEAT;

   // Print only before guard approval
    if (pollingClosed &&
        !guardApproved &&
        command != 0x00) {

      Serial.print(F("IR BUTTON RECEIVED: 0x"));
      Serial.println(command, HEX);

      if (command == GUARD_BUTTON) {

        guardApproved = true;

        digitalWrite(greenLed, HIGH);

        Serial.println(F("BUTTON 1 DETECTED"));
        Serial.println(F("GREEN LED ON"));
        Serial.println(F("GUARD APPROVED"));
        Serial.println(F("WAITING FOR RESET"));
      }
    }

    IrReceiver.resume();
  }

  // ====================
  // STEP 4: RESET BUTTON
  // ====================

  byte buttonState = digitalRead(resetButton);

  if (lastButtonState == HIGH &&
      buttonState == LOW) {

    delay(30);

    if (digitalRead(resetButton) == LOW) {

      Serial.println(F("RESET BUTTON PRESSED"));

      if (pollingClosed && guardApproved) {

        Serial.println(F("===================="));
        Serial.println(F("RESET STARTED"));

        // Open gate once
        openGate();

        // LEDs OFF
        digitalWrite(redLed, LOW);
        digitalWrite(greenLed, LOW);

        Serial.println(F("LEDS OFF"));

       // LCD waiting
        showWaiting();

        // Reset states
        pollingClosed = false;
        guardApproved = false;
        noiseArmed = false;

        // Stop distance monitoring
        exitWarning = false;
        clearCounter = 0;

        Serial.println(F("DISTANCE SENSOR DISABLED"));

        // Wait 10 seconds
        waitingAfterReset = true;
        resetTime = millis();

        Serial.println(F("SYSTEM RESET COMPLETE"));
        Serial.println(F("WAITING 10 SECONDS..."));
      }

      else {
        Serial.println(F("RESET DENIED"));
      }
    }
  }

  lastButtonState = digitalRead(resetButton);

  // ====================
  // STEP 5: DISTANCE SENSOR
  // ====================

  // Active only in emergency mode
  if (pollingClosed) {

    checkDistanceSensor();
  }
}