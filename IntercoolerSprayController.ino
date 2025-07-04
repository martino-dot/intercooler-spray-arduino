// === Intercooler Spray Controller with Arduino Due ===
// Inputs:
//   - Low water level sensor (digital, active LOW)
//   - Manual toggle button (momentary switch)
//   - HOBBS switch (digital, active HIGH when above boost threshold)
// Outputs:
//   - Pump relay (digital HIGH when spraying)
//   - Status LED (ON when spraying, BLINKS when water low)

#include <DueFlashStorage.h>
#include <DueTimer.h> 
DueFlashStorage dueFlashStorage;

// === Pin Assignments ===
const int lowLevelPin   = 5;   // Active LOW (NC sensor)
const int toggleButton  = 6;   // Momentary button, INPUT_PULLUP
const int hobbSwitchPin = 7;   // Active HIGH (NO switch)
const int pumpRelayPin  = 8;   // Output to pump relay
const int statusLEDPin  = 3;   // Status LED (incandescent)
// Pin 4 is open on terminal block

// === Constants ===
const uint32_t debounceDelay = 50;
const uint32_t holdThreshold = 2000;    // 2 sec hold for force mode
const uint32_t doubleClickMaxGap = 500; // max ms between clicks for double click

const uint32_t sprayDuration = 2000;    // ms
const uint32_t sprayInterval = 30000;   // ms

const uint32_t lowWaterBlinkDuration = 10000; // ms
const uint32_t startupDelay = 15000;  // ms delay before control
const uint32_t lowLevelDelay = 3000; // ms

const uint32_t waterStableTime = 1000;  // ms delay for water to "normalize"


// === Modes ===
enum Mode : uint8_t {
  MODE_OFF = 0,
  MODE_BOOST = 1,
  MODE_INTERVAL = 2,
  MODE_FORCE = 3
};

Mode currentMode = MODE_OFF;
Mode lastSavedMode = MODE_OFF;

// === Button State ===
bool lastButtonState       = HIGH;
uint32_t buttonPressStart  = 0;
bool buttonHeld            = false;
bool waitingForSecondClick = false;
uint32_t lastClickTime     = 0;
uint8_t clickCount         = 0;
uint32_t lastDebounceTime  = 0;
int      lastRawState      = HIGH;  
int      debouncedState    = HIGH; 

// === Spray State ===
bool spraying = false;
uint32_t sprayStartTime = 0;
uint32_t lastSprayTime = 0;

// === LED Blink State ===
bool ledState = false;
uint32_t lastBlinkTime = 0;
volatile bool flashActive = false;
volatile uint8_t flashCount = 0;
volatile uint8_t flashTotal = 0;
volatile uint32_t flashInterval = 0;
volatile uint32_t lastFlashTime = 0;

// === Low water warning ===
bool lowWaterWarningActive = false;
uint32_t lowWaterWarningStart = 0;
int      lastRawWater     = HIGH;           // last raw reading from sensor
int      lowWater   = HIGH;           // stable, debounced water level
uint32_t lastWaterChange  = 0;              // timestamp when water raw changed

// === Water Refill States ===
bool lastLowWaterState = true; // Assume last was LOW at startup
uint32_t refillDetectionTime = 0;
bool refillRecentlyDetected = false;

// === Startup timer ===
uint32_t startupTime = 0;
bool startupDone = false;

void setup() {
  Serial.begin(9600);
  
  pinMode(lowLevelPin, INPUT_PULLUP);
  pinMode(toggleButton, INPUT_PULLUP);
  pinMode(hobbSwitchPin, INPUT_PULLUP);
  pinMode(pumpRelayPin, OUTPUT);
  pinMode(statusLEDPin, OUTPUT);

  digitalWrite(pumpRelayPin, LOW);
  digitalWrite(statusLEDPin, LOW);

  // Read saved mode from flash storage
  uint8_t saved = dueFlashStorage.read(0);
  if (saved == MODE_BOOST || saved == MODE_INTERVAL) {
    currentMode = (Mode)saved;
    lastSavedMode = currentMode;
  } else {
    currentMode = MODE_OFF;
    lastSavedMode = MODE_OFF;
  }

  
  
  // Flash once for boost mode, twice for interval mode
  if (currentMode == MODE_BOOST) {
    startFlashPattern(1, 400);
  } else if (currentMode == MODE_INTERVAL) {
    startFlashPattern(2, 400);
  }

  startupTime = millis();
  startupDone = false;
}


void handleButton(uint32_t now, int reading) {

  // 1) track raw changes for debounce timing
  if (reading != lastRawState) {
    lastDebounceTime = now;
  }

  // 2) if stable beyond debounce interval, update debouncedState
  if (now - lastDebounceTime > debounceDelay) {
    if (reading != debouncedState) {
      debouncedState = reading;

      // on stable transition, handle press/release edges:
      if (debouncedState == LOW) {
        // falling edge: button pressed
        buttonPressStart = now;
        buttonHeld       = false;  // reset hold flag
        // Serial.println("button pressed");
      } else {
        // rising edge: button released
        if (!buttonHeld) {
          // handle quick click here (advance mode, etc.)
          //currentMode = (currentMode + 1) % NUM_MODES;
          //lastSavedMode = currentMode;
          //saveModeIfNeeded();
          clickCount++;
          if (!waitingForSecondClick) {
            waitingForSecondClick = true;
            lastClickTime = now;
          }
        } else {
          // was held & then released: revert to saved mode
          currentMode = lastSavedMode;
          stopSpray();
          Serial.println("button reverted");
        }
      }
    }

    // 3) check for hold event when stable LOW
    if (debouncedState == LOW && !buttonHeld && (now - buttonPressStart >= holdThreshold)) {
      buttonHeld  = true;
      currentMode = MODE_FORCE;
    }

    // Handle clicks after delay
    if (waitingForSecondClick && (now - lastClickTime > doubleClickMaxGap)) {
      // Single click
      if (clickCount == 1) {
        if (currentMode == MODE_OFF) {
          currentMode = MODE_BOOST;
          startFlashPattern(1, 250);
          saveModeIfNeeded();
          print("mode boost");
        } else if (currentMode == MODE_BOOST) {
          currentMode = MODE_OFF;
          startFlashPattern(3, 250);
          saveModeIfNeeded();
          print("mode off 1");
        } else if (currentMode == MODE_INTERVAL) {
          currentMode = MODE_OFF;
          startFlashPattern(3, 200);
          saveModeIfNeeded();
          print("mode off 2");
        }
        clickCount = 0;
        waitingForSecondClick = false;
      } else if (clickCount == 2) {
        // Only activate interval if no other mode active
        if (currentMode == MODE_OFF) {
          currentMode = MODE_INTERVAL;
          startFlashPattern(2, 400);
          saveModeIfNeeded();
          print("mode interval");
        }
        clickCount = 0;
        waitingForSecondClick = false;
      }

    }
  }

  // update raw state for next cycle
  lastRawState = reading;
}

inline void print(const char *msg) {
  Serial.println(msg);
}
inline void print(const String &msg) {
  Serial.println(msg);
}

void loop() {
  checkSerialBeforeStartup();
  
  uint32_t now = millis();

  bool rawWater = (digitalRead(lowLevelPin) != HIGH);     // LOW = low water (NC sensor)
  debouncedWaterLevel(now, rawWater);

  // Startup delay
  if (!startupDone) {
    // During startup delay, flash based on water & mode

    if ((now - startupTime >= lowLevelDelay) && lowWater) {
      flashLED(now, 200);
    }

    if (now - startupTime < startupDelay) {
      // digitalWrite(pumpRelayPin, LOW);
      return;
    }
    startupDone = true;
    digitalWrite(statusLEDPin, LOW);
  }

  // Read sensors
  bool underBoost = (digitalRead(hobbSwitchPin) == LOW); // LOW = boost (NO switch)
  bool buttonReading = (digitalRead(toggleButton));
  lastLowWaterState = lowWater;

  // Handle button with debounce and multiple clicks
  handleButton(now, buttonReading);

  // Low water warning: blink for 10s then shut off everything
  if (lowWater && !lowWaterWarningActive && currentMode != MODE_OFF) {
    lowWaterWarningActive = true;
    lowWaterWarningStart = now;
    currentMode = MODE_OFF;
    saveModeIfNeeded();
    stopSpray();
  }

  if (lowWaterWarningActive) {
    // Blink LED at 500ms rate
    if (now - lastBlinkTime >= 500) {
      ledState = !ledState;
      digitalWrite(statusLEDPin, ledState);
      lastBlinkTime = now;
    }
    // After 10s, turn off LED and modes
    if (now - lowWaterWarningStart >= lowWaterBlinkDuration) {
      digitalWrite(statusLEDPin, LOW);
      // currentMode = MODE_OFF; (already called)
      lowWaterWarningActive = false;
    }
    stopSpray();
    return;
  }

  // Force spray mode overrides everything else
  if (currentMode == MODE_FORCE) {
    if (!lowWater) {
      startSpray(now);
    } else {
      flashLED(now, 200);
    }
    return;
  }

  // Spray control based on mode
  switch (currentMode) {
    case MODE_BOOST:
      runBoostMode(now, underBoost);
      break;
    case MODE_INTERVAL:
      runIntervalMode(now);
      break;
    case MODE_OFF:
      stopSpray();
    default:
      break;
  }
  
  handleSerialDiagnostics();
  trackBoostDuration(underBoost, now);
  triggerCooldownSpray(now);
}

// Debounce water-level sensor (1s buffer)
void debouncedWaterLevel(uint32_t now, int raw) {
  if (raw != lastRawWater) {
    lastWaterChange = now;
    lastRawWater    = raw;
  }
  if (now - lastWaterChange >= waterStableTime) {
    lowWater = raw;
  }
}

// --- Spray Modes ---

void runBoostMode(uint32_t now, bool underBoost) {
  if (underBoost && !spraying && (now - lastSprayTime >= sprayInterval)) {
    startSpray(now);
  }
  if (spraying && (now - sprayStartTime >= sprayDuration)) {
    stopSpray();
  }
}

void runIntervalMode(uint32_t now) {
  if (!spraying && (now - lastSprayTime >= sprayInterval)) {
    startSpray(now);
  }
  if (spraying && (now - sprayStartTime >= sprayDuration)) {
    stopSpray();
  }
}

void startSpray(uint32_t now) {
  spraying = true;
  sprayStartTime = now;
  lastSprayTime = now;
  digitalWrite(pumpRelayPin, HIGH);
  digitalWrite(statusLEDPin, HIGH);
}

void stopSpray() {
  spraying = false;
  digitalWrite(pumpRelayPin, LOW);
  if (!flashActive) {
    digitalWrite(statusLEDPin, LOW);
  }
  
}

// --- LED Flash Patterns ---
void flashLED(uint32_t now, uint32_t interval) {
  if (now - lastBlinkTime >= interval) {
    ledState = !ledState;
    digitalWrite(statusLEDPin, ledState);
    lastBlinkTime = now;
  }
}

// Flash LED pattern count times with given on/off interval
// Called repeatedly from loop during startup or to signal mode
void startFlashPattern(uint8_t count, uint32_t interval) {
  Timer3.stop();                // stop it first
  Timer3.detachInterrupt();     // ensure it's clean

  flashTotal   = count * 2;  // on + off cycles
  flashCount   = 0;
  flashActive  = true;
  ledState = false;
  digitalWrite(statusLEDPin, LOW);
  Timer3.setPeriod(interval * 1000UL)
        .attachInterrupt(flashCallback)
        .start();
}

void flashCallback() {
  if (!flashActive) return;
  ledState = !ledState;
  digitalWrite(statusLEDPin, ledState);
  flashCount++;
  if (flashCount >= flashTotal) {
    flashActive = false;
    Timer3.stop();
    digitalWrite(statusLEDPin, LOW);
  }
}


// --- Flash Storage Save ---
void saveModeIfNeeded() {
  if (currentMode == MODE_BOOST || currentMode == MODE_INTERVAL || currentMode == MODE_OFF) {
    if (currentMode != lastSavedMode) {
      dueFlashStorage.write(0, currentMode);
      lastSavedMode = currentMode;
    }
  }
}

// === Serial Diagnostic Mode ===
void handleSerialDiagnostics() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'd') {
      Serial.println("=== Spray Controller Diagnostics ===");

      // Report current mode
      Serial.print("Mode: ");
      Serial.println(currentMode);

      // Report time since last spray
      Serial.print("Time since last spray: ");
      Serial.print((millis() - lastSprayTime) / 1000);
      Serial.println(" seconds");

      // Water status
      bool lowWater = lowWater;
      Serial.print("Water Level: ");
      Serial.println(lowWater ? "LOW" : "OK");
    }
  }
}

// === Cool-Down Spray Logic ===
// Track boost activity duration within a sliding time window
const uint32_t cooldownWindow = 60000; // 1 minute
const uint32_t boostThreshold = 20000; // 20 seconds in boost triggers cooldown
bool cooldownReady = false;
uint32_t boostTimeAccumulator = 0;
uint32_t lastBoostUpdate = 0;

void trackBoostDuration(bool underBoost, uint32_t now) {
  static bool lastBoost = false;

  if (currentMode != MODE_BOOST) return; // Skip tracking if interval mode is on

  if (underBoost) {
    if (!lastBoost) lastBoostUpdate = now;
    boostTimeAccumulator += now - lastBoostUpdate;
    lastBoostUpdate = now;
  } else {
    lastBoostUpdate = now;
  }

  if ((now - lastBoostUpdate) > cooldownWindow) {
    boostTimeAccumulator = 0;
  }

  if (boostTimeAccumulator >= boostThreshold) {
    cooldownReady = true;
    boostTimeAccumulator = 0; // Reset so it doesn’t re-trigger constantly
  }
  lastBoost = underBoost;
}

void triggerCooldownSpray(uint32_t now) {
  static bool cooling = false;
  static uint32_t cooldownStart = 0;

  if (currentMode != MODE_BOOST) return; // Skip cooldown spray if interval mode is on

  if (cooldownReady && !cooling && !lastLowWaterState) {
    startSpray(now);
    cooling = true;
    cooldownStart = now;
    cooldownReady = false;
  } else {
    return;
  }

  if (cooling && (now - cooldownStart >= sprayDuration)) {
    stopSpray();
    cooling = false;
  }
}

// === Refill Detection ===
void checkRefillDetection(uint32_t now) {
  bool currentLowWater = lowWater;

  // Detect transition from LOW to OK
  if (lastLowWaterState && !currentLowWater) {
    refillDetectionTime = now;
    refillRecentlyDetected = true;
    blinkLED(5, 100); // 5 quick blinks to acknowledge refill
  }

  // Clear flag after 5 seconds
  if (refillRecentlyDetected && (now - refillDetectionTime > 5000)) {
    refillRecentlyDetected = false;
  }

  lastLowWaterState = currentLowWater;
}

void blinkLED(uint8_t times, uint16_t duration) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(statusLEDPin, HIGH);
    delay(duration);
    digitalWrite(statusLEDPin, LOW);
    delay(duration);
  }
}

// === Diagnostic Relay Test & Mode Switch (before startup delay) ===
void checkSerialBeforeStartup() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'p') {
      Serial.println("Pump relay ON (test)");
      digitalWrite(pumpRelayPin, HIGH);
      delay(1000);
      digitalWrite(pumpRelayPin, LOW);
      Serial.println("Pump relay OFF");
    } else if (cmd == 'w') {
      Serial.print("Water Level: ");
      Serial.println(lowWater ? "LOW" : "OK");
    } else if (cmd == 'l') {
      Serial.println("LED ON (test)");
      digitalWrite(statusLEDPin, HIGH);
      delay(1000);
      digitalWrite(statusLEDPin, LOW);
      Serial.println("LED OFF");
    } else if (cmd == 'm') {
      Serial.println("Enter mode: 0=OFF, 1=BOOST, 2=INTERVAL");
      unsigned long startMs = millis();
      while (!Serial.available() && millis() - startMs < 10000) {
        
      }
      uint8_t newMode = Serial.parseInt();
      if (newMode <= 2) {
        currentMode = (Mode)newMode;
        saveModeIfNeeded();
        Serial.print("Mode set to ");
        Serial.println(currentMode);
      } else {
        Serial.println("Invalid mode");
      }
    } else if (cmd == 'r') {
      Serial.println("Resetting!");
      Serial.flush();
      NVIC_SystemReset();
    }
  }
}
