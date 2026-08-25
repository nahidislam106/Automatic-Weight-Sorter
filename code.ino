/*
 * ================================================
 *  Automatic Weight Sorter - WITH RELEASE SERVO

 *  Changes from previous version:
 *    1. Added Release Servo on D2
 *    2. Release servo holds object (HOLD pos) by default
 *    3. After sorting servo moves LEFT/RIGHT,
 *       waits 1.5s then release servo flips to
 *       RELEASE pos to push/drop object
 *    4. After 1s, release servo returns to HOLD pos
 *
 *  Pins:
 *    HX711 DT       → D5
 *    HX711 SCK      → D4
 *    Tare Button    → D6 (other leg → GND)
 *    Sorting Servo  → D3
 *    Release Servo  → D2   ← NEW
 *    LCD SDA        → A4
 *    LCD SCL        → A5
 * ================================================
 */

#include <HX711_ADC.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711_ADC LoadCell(5, 4);

// ── Sorting Servo (D3) ──────────────────────────
Servo sortServo;
#define SORT_PIN      3
#define SERVO_CENTER  109
#define SERVO_RIGHT   150
#define SERVO_LEFT    60

// ── Release Servo (D2) ──────────────────────────
Servo releaseServo;
#define RELEASE_PIN   2
#define RELEASE_HOLD  90   // object আটকে রাখার position
#define RELEASE_DROP  0    // উল্টে/push করার position
                           // (mechanic অনুযায়ী 180 করতে পারো)

// ── Tuning ──────────────────────────────────────
const int taree = 6;

#define THRESHOLD           150.0
#define MIN_WEIGHT          5.0
#define STABLE_COUNT        6
#define STABLE_TOL          8.0
#define WEIGHT_OFFSET       7.0

#define DELAY_BEFORE_SORT   3000   // ms — stable হওয়ার পর sort এর আগে
#define DELAY_BEFORE_RETURN 3000   // ms — object সরলে center ফেরার আগে
#define DELAY_RELEASE_AFTER_SORT 1500  // ms — sort servo মুভের পর release এর আগে
#define RELEASE_HOLD_TIME   1000   // ms — release position এ থাকার সময়

// ── State variables ──────────────────────────────
int    stableCount   = 0;
float  lastWeight    = 0;
float  stableWeight  = 0;
bool   sorted        = false;
bool   returning     = false;
bool   released      = false;       // release servo action হয়েছে কিনা
bool   releaseArmed  = false;       // sort হয়েছে, release এর জন্য অপেক্ষা
String lastState     = "";

unsigned long sortTimer    = 0;
unsigned long returnTimer  = 0;
unsigned long releaseTimer = 0;     // release delay timer
unsigned long releaseHoldTimer = 0; // release hold timer (ফেরার জন্য)

// ── Helper: LCD clear line ────────────────────────
void lcdLine(int row, const char* msg) {
  lcd.setCursor(0, row);
  lcd.print(msg);
}

void setup() {
  Serial.begin(9600);
  pinMode(taree, INPUT_PULLUP);

  // Sorting servo init
  sortServo.attach(SORT_PIN);
  sortServo.write(SERVO_CENTER);

  // Release servo init — default HOLD (block object)
  releaseServo.attach(RELEASE_PIN);
  releaseServo.write(RELEASE_HOLD);
  delay(500);

  LoadCell.begin();
  LoadCell.start(1000);
  LoadCell.setCalFactor(375);

  lcd.init();
  lcd.backlight();
  lcdLine(0, "  Weight Sorter ");
  lcdLine(1, "   HSTU  2026   ");
  delay(2500);
  lcd.clear();

  Serial.println("================================");
  Serial.println("  Weight Sorter + Release Servo");
  Serial.println("  CalFactor   : 375");
  Serial.println("  Threshold   : 300g");
  Serial.println("  Offset      : -7g");
  Serial.println("  Release Pin : D2");
  Serial.println("  HOLD pos    : 90deg");
  Serial.println("  DROP pos    :  0deg");
  Serial.println("================================");
}

void loop() {
  LoadCell.update();
  float rawWeight = LoadCell.getData();
  if (rawWeight < 0) rawWeight = 0;

  float displayWeight = rawWeight - WEIGHT_OFFSET;
  if (displayWeight < 0) displayWeight = 0;

  // ═══════════════════════════════════
  //  TARE BUTTON
  // ═══════════════════════════════════
  if (digitalRead(taree) == LOW) {
    lcd.clear();
    lcdLine(0, "  Taring...     ");
    LoadCell.start(1000);
    sorted       = false;
    returning    = false;
    released     = false;
    releaseArmed = false;
    stableCount  = 0;
    lastState    = "";
    sortTimer    = 0;
    sortServo.write(SERVO_CENTER);
    releaseServo.write(RELEASE_HOLD);
    Serial.println(">> Tared & Reset!");
    delay(800);
    lcd.clear();
    return;
  }

  // ═══════════════════════════════════
  //  RELEASE SERVO — armed after sort
  //  Step 1: wait DELAY_RELEASE_AFTER_SORT, then drop
  //  Step 2: wait RELEASE_HOLD_TIME, then return to HOLD
  // ═══════════════════════════════════
  if (releaseArmed && !released) {
    if (millis() - releaseTimer >= DELAY_RELEASE_AFTER_SORT) {
      // Release! উল্টে/push করো
      releaseServo.write(RELEASE_DROP);
      released         = true;
      releaseHoldTimer = millis();
      Serial.println(">> RELEASE SERVO → DROP (object released)");
      lcdLine(0, "Releasing obj...");
    }
  }

  // Step 2: return release servo to HOLD after hold time
  if (released && millis() - releaseHoldTimer >= RELEASE_HOLD_TIME) {
    releaseServo.write(RELEASE_HOLD);
    releaseArmed = false;
    Serial.println(">> RELEASE SERVO → HOLD (ready for next)");
  }

  // ═══════════════════════════════════
  //  STABILITY CHECK
  // ═══════════════════════════════════
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
    // Object সরে গেছে
    if (sorted && !returning) {
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

  // ═══════════════════════════════════
  //  SORTING SERVO → CENTER (return)
  // ═══════════════════════════════════
  if (returning && millis() - returnTimer >= DELAY_BEFORE_RETURN) {
    sortServo.write(SERVO_CENTER);
    returning    = false;
    sorted       = false;
    released     = false;
    releaseArmed = false;
    stableCount  = 0;
    stableWeight = 0;
    lastState    = "EMPTY";
    Serial.println(">> SORT SERVO CENTER 109 — Ready");
    releaseServo.write(RELEASE_HOLD);  // ensure hold
  }

  // ═══════════════════════════════════
  //  SERIAL DEBUG
  // ═══════════════════════════════════
  Serial.print("Raw: "); Serial.print(rawWeight, 1);
  Serial.print("g | Disp: "); Serial.print(displayWeight, 1);
  Serial.print("g | Stable: ");
  Serial.print(min(stableCount, STABLE_COUNT));
  Serial.print("/"); Serial.print(STABLE_COUNT);
  Serial.print(" | Armed: "); Serial.print(releaseArmed);
  Serial.print(" | Released: "); Serial.println(released);

  // ═══════════════════════════════════
  //  LCD LINE 1 — Status
  // ═══════════════════════════════════
  lcd.setCursor(0, 0);
  if (rawWeight < MIN_WEIGHT && !returning && !releaseArmed && !released) {
    lcd.print("Place object... ");
  } else if (returning) {
    lcd.print("Returning...    ");
  } else if (releaseArmed && !released) {
    lcd.print("Releasing obj...");
  } else if (!sorted && stableCount <= STABLE_COUNT) {
    lcd.print("Checking:[");
    int bars = map(stableCount, 0, STABLE_COUNT, 0, 5);
    for (int i = 0; i < 5; i++) lcd.print(i < bars ? '#' : ' ');
    lcd.print("]  ");
  } else if (!sorted && stableCount > STABLE_COUNT) {
    lcd.print("Sorting...      ");
  } else if (lastState == "RIGHT") {
    lcd.print(">300g ->  RIGHT ");
  } else if (lastState == "LEFT") {
    lcd.print("<300g ->  LEFT  ");
  }

  // ═══════════════════════════════════
  //  LCD LINE 2 — Weight
  // ═══════════════════════════════════
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

  // ═══════════════════════════════════
  //  SORT SERVO DECISION
  // ═══════════════════════════════════
  if (!sorted && stableCount > STABLE_COUNT) {
    if (sortTimer == 0) {
      sortTimer = millis();
      Serial.println(">> Stable! Waiting before sort...");
    }

    if (millis() - sortTimer >= DELAY_BEFORE_SORT) {
      sorted    = true;
      sortTimer = 0;

      if (stableWeight >= THRESHOLD) {
        sortServo.write(SERVO_RIGHT);
        lcdLine(0, ">300g ->  RIGHT ");
        Serial.print(">> HEAVY: "); Serial.print(stableWeight - WEIGHT_OFFSET, 1);
        Serial.println("g → RIGHT 150");
        lastState = "RIGHT";
      } else {
        sortServo.write(SERVO_LEFT);
        lcdLine(0, "<300g ->  LEFT  ");
        Serial.print(">> LIGHT: "); Serial.print(stableWeight - WEIGHT_OFFSET, 1);
        Serial.println("g → LEFT 60");
        lastState = "LEFT";
      }

      // ── Arm the release servo ──────────────────
      releaseArmed = true;
      released     = false;
      releaseTimer = millis();
      Serial.println(">> Release servo ARMED — 1.5s delay...");
    }
  }

  // Sort timer reset if object removed before sorting
  if (rawWeight < MIN_WEIGHT && !sorted) {
    sortTimer = 0;
  }

  delay(200);
}
