#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Pins ---
const int micPin    = A0;
const int btn1Pin   = 2;
const int btn2Pin   = 3;
const int resetBtn  = 4;
const int yellowLed = 5;
const int redLed    = 6;
const int buzzerPin = 7;

// --- Thresholds ---
const int MIC_THRESHOLD               = 600;
const unsigned long SAMPLE_WINDOW     = 50;
const unsigned long DRONE_DETECT_TIME = 2000;
const unsigned long TIME_WINDOW       = 2000;
const unsigned long DEBOUNCE_DELAY    = 50;

// --- Drone detection ---
int peakToPeak           = 0;
unsigned long soundStartTime  = 0;
bool soundActive         = false;
bool droneDetected       = false;
int  aboveThresholdCount = 0;
int  totalChecks         = 0;

// --- Intrusion detection ---
bool intrusionDetected = false;
int  btn1Count = 0, btn2Count = 0;
bool btn1Triggered = false, btn2Triggered = false;
unsigned long windowStart = 0;
bool windowActive  = false;
String intrusionType = "";

// --- Button states ---
bool resetLastState = HIGH, btn1LastState = HIGH, btn2LastState = HIGH;
bool resetPressed   = false, btn1Pressed  = false, btn2Pressed  = false;
unsigned long resetDebounceTime = 0, btn1DebounceTime = 0, btn2DebounceTime = 0;

// --- System state ---
bool waitingReset = false;
String currentLCDLine1 = "";  // Track what's on LCD
String currentLCDLine2 = "";

// -----------------------------------------------
int readPeakToPeak() {
  int sMax = 0;
  int sMin = 1023;
  unsigned long start = millis();
  while (millis() - start < SAMPLE_WINDOW) {
    int sample = analogRead(micPin);
    if (sample > sMax) sMax = sample;
    if (sample < sMin) sMin = sample;
  }
  return sMax - sMin;
}

// -----------------------------------------------
// Only updates LCD if content has changed
void printLCD(String line1, String line2) {
  if (line1 == currentLCDLine1 && line2 == currentLCDLine2) return;

  // Pad to 16 chars to overwrite old text (no lcd.clear needed)
  while (line1.length() < 16) line1 += " ";
  while (line2.length() < 16) line2 += " ";

  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);

  currentLCDLine1 = line1;
  currentLCDLine2 = line2;
}

// -----------------------------------------------
void setAlerts() {
  if (droneDetected && intrusionDetected) {
    digitalWrite(yellowLed, LOW);
    digitalWrite(redLed,    HIGH);
    digitalWrite(buzzerPin, HIGH);
  } else if (droneDetected || intrusionDetected) {
    digitalWrite(yellowLed, HIGH);
    digitalWrite(redLed,    LOW);
    digitalWrite(buzzerPin, LOW);
  } else {
    digitalWrite(yellowLed, LOW);
    digitalWrite(redLed,    LOW);
    digitalWrite(buzzerPin, LOW);
  }
}

void showMonitoring() {
  printLCD("System Ready", "Analysing...");
  Serial.println("System Ready - Analysing...");
}

void updateLCD() {
  if (droneDetected && intrusionDetected) {
    printLCD("!! RED ALERT !!", "Drone+" + intrusionType);
    Serial.println("!! RED ALERT: Drone + " + intrusionType + " !!");
  } else if (droneDetected) {
    printLCD("!! ALERT !!", "Drone Detected");
    Serial.println("!! ALERT: Drone Detected !!");
  } else if (intrusionDetected) {
    printLCD("!! ALERT !!", intrusionType);
    Serial.println("!! ALERT: " + intrusionType + " !!");
  }
  Serial.println("Press reset button to continue.");
}

// -----------------------------------------------
void classifyIntrusion() {
  bool vehicleDetected = (btn1Triggered && btn2Triggered);
  int  totalPresses    = btn1Count + btn2Count;

  if (vehicleDetected) {
    intrusionType = "Vehicle";
  } else if (totalPresses >= 2) {
    intrusionType = "Multi Person";
  } else if (totalPresses == 1) {
    intrusionType = "Single Person";
  } else {
    btn1Count = 0; btn2Count = 0;
    btn1Triggered = false; btn2Triggered = false;
    windowActive  = false;
    return;
  }

  intrusionDetected = true;
  waitingReset      = true;
  btn1Count = 0; btn2Count = 0;
  btn1Triggered = false; btn2Triggered = false;
  windowActive  = false;

  setAlerts();
  updateLCD();
}

// -----------------------------------------------
void setup() {
  Serial.begin(9600);

  pinMode(btn1Pin,   INPUT_PULLUP);
  pinMode(btn2Pin,   INPUT_PULLUP);
  pinMode(resetBtn,  INPUT_PULLUP);
  pinMode(yellowLed, OUTPUT);
  pinMode(redLed,    OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  digitalWrite(yellowLed, LOW);
  digitalWrite(redLed,    LOW);
  digitalWrite(buzzerPin, LOW);

  // Init LCD with retry
  Wire.begin();
  lcd.init();
  delay(100);
  lcd.init();  // Init twice for reliability
  lcd.backlight();
  delay(100);

  printLCD("Calibrating...", "Stay Silent...");
  Serial.println("Calibrating... keep silent for 3 seconds.");

  int maxIdle = 0;
  unsigned long calStart = millis();
  while (millis() - calStart < 3000) {
    int val = readPeakToPeak();
    if (val > maxIdle) maxIdle = val;
    delay(100);
  }

  Serial.print("Idle noise level: ");
  Serial.println(maxIdle);
  Serial.print("Using threshold: ");
  Serial.println(MIC_THRESHOLD);
  Serial.println("Calibration done. System ready.");

  showMonitoring();
}

// -----------------------------------------------
void loop() {
  unsigned long now = millis();

  // ---- Reset Button ----
  bool resetReading = digitalRead(resetBtn);
  if (resetReading != resetLastState) resetDebounceTime = now;
  if ((now - resetDebounceTime) > DEBOUNCE_DELAY) {
    if (resetReading == LOW && !resetPressed) {
      resetPressed = true;
      if (waitingReset) {
        waitingReset        = false;
        droneDetected       = false;
        intrusionDetected   = false;
        soundActive         = false;
        soundStartTime      = 0;
        intrusionType       = "";
        aboveThresholdCount = 0;
        totalChecks         = 0;
        btn1Count = 0; btn2Count = 0;
        btn1Triggered = false; btn2Triggered = false;
        windowActive  = false;
        currentLCDLine1 = "";  // Force LCD refresh
        currentLCDLine2 = "";
        setAlerts();
        showMonitoring();
      }
    }
    if (resetReading == HIGH) resetPressed = false;
  }
  resetLastState = resetReading;

  // ---- Button 1 ----
  bool btn1Reading = digitalRead(btn1Pin);
  if (btn1Reading != btn1LastState) btn1DebounceTime = now;
  if ((now - btn1DebounceTime) > DEBOUNCE_DELAY) {
    if (btn1Reading == LOW && !btn1Pressed && !intrusionDetected) {
      btn1Pressed   = true;
      btn1Count++;
      btn1Triggered = true;
      if (!windowActive) {
        windowActive = true;
        windowStart  = now;
      }
      Serial.print("Button 1 Pressed | Count: ");
      Serial.println(btn1Count);
    }
    if (btn1Reading == HIGH) btn1Pressed = false;
  }
  btn1LastState = btn1Reading;

  // ---- Button 2 ----
  bool btn2Reading = digitalRead(btn2Pin);
  if (btn2Reading != btn2LastState) btn2DebounceTime = now;
  if ((now - btn2DebounceTime) > DEBOUNCE_DELAY) {
    if (btn2Reading == LOW && !btn2Pressed && !intrusionDetected) {
      btn2Pressed   = true;
      btn2Count++;
      btn2Triggered = true;
      if (!windowActive) {
        windowActive = true;
        windowStart  = now;
      }
      Serial.print("Button 2 Pressed | Count: ");
      Serial.println(btn2Count);
    }
    if (btn2Reading == HIGH) btn2Pressed = false;
  }
  btn2LastState = btn2Reading;

  // ---- Intrusion Time Window ----
  if (windowActive && !intrusionDetected && (now - windowStart >= TIME_WINDOW)) {
    classifyIntrusion();
  }

  // ---- Drone Detection ----
  if (!droneDetected) {
    peakToPeak = readPeakToPeak();

    Serial.print("Peak-to-Peak: ");
    Serial.println(peakToPeak);

    if (peakToPeak >= MIC_THRESHOLD) {
      if (!soundActive) {
        soundActive         = true;
        soundStartTime      = millis();
        aboveThresholdCount = 0;
        totalChecks         = 0;
        Serial.println("Sound threshold crossed - starting drone timer...");
        if (!intrusionDetected) {
          printLCD("Sound Detected", "Analysing...");
        }
      }
      aboveThresholdCount++;
      totalChecks++;

    } else {
      if (soundActive) {
        totalChecks++;
        float ratio = (float)aboveThresholdCount / (float)totalChecks;
        Serial.print("Consistency ratio: ");
        Serial.println(ratio);

        if (ratio < 0.75) {
          Serial.println("Sound inconsistent - resetting drone timer.");
          soundActive         = false;
          soundStartTime      = 0;
          aboveThresholdCount = 0;
          totalChecks         = 0;
          if (!intrusionDetected) showMonitoring();
        }
      }
    }

    if (soundActive && (millis() - soundStartTime >= DRONE_DETECT_TIME)) {
      float ratio = (float)aboveThresholdCount / (float)totalChecks;
      Serial.print("Final consistency ratio: ");
      Serial.println(ratio);

      if (ratio >= 0.75) {
        droneDetected = true;
        waitingReset  = true;
        setAlerts();
        updateLCD();
      } else {
        Serial.println("Not consistent enough - resetting.");
        soundActive         = false;
        soundStartTime      = 0;
        aboveThresholdCount = 0;
        totalChecks         = 0;
        if (!intrusionDetected) showMonitoring();
      }
    }
  }
}