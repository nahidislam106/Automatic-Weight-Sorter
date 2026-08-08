/*
 * ================================================
 *  Automatic Weight Sorter - ADJUSTED
 *
 *  Changes:
 *    1. Display weight -7g (offset correction)
 *    2. 2 second delay before servo sorts
 *    3. 2 second delay before servo returns center
 *
 *  Pins:
 *    HX711 DT    → D5
 *    HX711 SCK   → D4
 *    Tare Button → D6 (other leg → GND)
 *    Servo       → D3
 *    LCD SDA     → A4
 *    LCD SCL     → A5
 * ================================================
 */

#include <HX711_ADC.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711_ADC LoadCell(5, 4);

Servo myServo;
#define SERVO_PIN    3
#define SERVO_CENTER 109
#define SERVO_RIGHT  150
#define SERVO_LEFT   60

const int taree = 6;

#define THRESHOLD      300.0
#define MIN_WEIGHT     10.0
#define STABLE_COUNT   6
#define STABLE_TOL     8.0
#define WEIGHT_OFFSET  7.0    // display te 7g kom dekhabe

#define DELAY_BEFORE_SORT   8000  // ms — weight dekhানোর পর sort এর আগে
#define DELAY_BEFORE_RETURN 8000  // ms — sort এর পর center এ ফেরার আগে

int    stableCount  = 0;
float  lastWeight   = 0;
float  stableWeight = 0;
bool   sorted       = false;
bool   returning    = false;
String lastState    = "";
unsigned long sortTimer   = 0;
unsigned long returnTimer = 0;

void setup() {
  Serial.begin(9600);
  pinMode(taree, INPUT_PULLUP);

  myServo.attach(SERVO_PIN);
  myServo.write(SERVO_CENTER);
  delay(300);

  LoadCell.begin();
  LoadCell.start(1000);
  LoadCell.setCalFactor(375);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("  Weight Sorter ");
  lcd.setCursor(0, 1);
  lcd.print("   HSTU  2026   ");
  delay(2500);
  lcd.clear();

  Serial.println("================================");
  Serial.println("  Weight Sorter Ready");
  Serial.println("  CalFactor : 375");
  Serial.println("  Threshold : 300g");
  Serial.println("  Offset    : -7g");
  Serial.println("================================");
}

void loop() {
  LoadCell.update();
  float rawWeight = LoadCell.getData();
  if (rawWeight < 0) rawWeight = 0;

  // Display weight with -7g offset correction
  float displayWeight = rawWeight - WEIGHT_OFFSET;
  if (displayWeight < 0) displayWeight = 0;

  // ---- Tare button ----
  if (digitalRead(taree) == LOW) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("  Taring...     ");
    LoadCell.start(1000);
    sorted      = false;
    returning   = false;
    stableCount = 0;
    lastState   = "";
    myServo.write(SERVO_CENTER);
    Serial.println(">> Tared & Reset!");
    delay(800);
    lcd.clear();
    return;
  }

  // ---- Stability check (use rawWeight for logic) ----
  if (rawWeight >= MIN_WEIGHT) {
    if (abs(rawWeight - lastWeight) <= STABLE_TOL) {
      stableCount++;
    } else {
      stableCount = 0;
    }
    if (stableCount > STABLE_COUNT) {
      stableWeight = rawWeight;
    }
    lastWeight = rawWeight;
  } else {
    // Weight removed
    if (sorted && !returning) {
      // Start return delay
      returning   = true;
      returnTimer = millis();
      Serial.println(">> Weight removed — waiting before return...");
    }
    if (!returning) {
      stableCount  = 0;
      stableWeight = 0;
      sorted       = false;
    }
  }

  // ---- Return to center after delay ----
  if (returning && millis() - returnTimer >= DELAY_BEFORE_RETURN) {
    myServo.write(SERVO_CENTER);
    returning   = false;
    sorted      = false;
    stableCount = 0;
    stableWeight = 0;
    lastState   = "EMPTY";
    Serial.println(">> CENTER 109 — Ready for next");
  }

  // ---- Serial debug ----
  Serial.print("Raw: "); Serial.print(rawWeight, 1);
  Serial.print("g | Display: "); Serial.print(displayWeight, 1);
  Serial.print("g | Stable: ");
  Serial.print(min(stableCount, STABLE_COUNT));
  Serial.print("/"); Serial.println(STABLE_COUNT);

  // ---- LCD Line 1: Status ----
  lcd.setCursor(0, 0);
  if (rawWeight < MIN_WEIGHT && !returning) {
    lcd.print("Place object... ");
  } else if (returning) {
    lcd.print("Returning...    ");
  } else if (!sorted && stableCount <= STABLE_COUNT) {
    lcd.print("Checking:[");
    int bars = map(stableCount, 0, STABLE_COUNT, 0, 5);
    for (int i = 0; i < 5; i++) lcd.print(i < bars ? "#" : " ");
    lcd.print("]  ");
  } else if (!sorted && stableCount > STABLE_COUNT) {
    lcd.print("Sorting...      ");
  } else if (lastState == "RIGHT") {
    lcd.print(">300g ->  RIGHT ");
  } else if (lastState == "LEFT") {
    lcd.print("<300g ->  LEFT  ");
  }

  // ---- LCD Line 2: Weight ----
  lcd.setCursor(0, 1);
  if (rawWeight < MIN_WEIGHT && !returning) {
    lcd.print("Weight:  ---    ");
  } else if (displayWeight < 1000.0) {
    lcd.print("Weight: ");
    lcd.print(displayWeight, 1);
    lcd.print("g    ");
  } else {
    lcd.print("Weight: ");
    lcd.print(displayWeight / 1000.0, 2);
    lcd.print("kg   ");
  }

  // ---- Servo Decision ----
  if (!sorted && stableCount > STABLE_COUNT) {
    // First time stable — start sort delay
    if (sortTimer == 0) {
      sortTimer = millis();
      Serial.println(">> Stable! Waiting 2s before sort...");
    }

    // Wait DELAY_BEFORE_SORT before moving servo
    if (millis() - sortTimer >= DELAY_BEFORE_SORT) {
      sorted    = true;
      sortTimer = 0;

      if (stableWeight >= THRESHOLD) {
        myServo.write(SERVO_RIGHT);
        lcd.setCursor(0, 0);
        lcd.print(">300g ->  RIGHT ");
        Serial.print(">> HEAVY: "); Serial.print(stableWeight - WEIGHT_OFFSET, 1);
        Serial.println("g → RIGHT 150");
        lastState = "RIGHT";
      } else {
        myServo.write(SERVO_LEFT);
        lcd.setCursor(0, 0);
        lcd.print("<300g ->  LEFT  ");
        Serial.print(">> LIGHT: "); Serial.print(stableWeight - WEIGHT_OFFSET, 1);
        Serial.println("g → LEFT 60");
        lastState = "LEFT";
      }
    }
  }

  // Reset sortTimer if weight removed before sorting
  if (rawWeight < MIN_WEIGHT && !sorted) {
    sortTimer = 0;
  }

  delay(200);
}
