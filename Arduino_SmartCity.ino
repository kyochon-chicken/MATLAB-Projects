#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <pm2008_i2c.h>

#define DHTPIN 2
#define DHTTYPE DHT11

#define HUMIDIFIER_PIN 9
#define LIGHT_SENSOR_PIN A2

#define RED_LED 5
#define GREEN_LED 6
#define BLUE_LED 10
#define POWER_BUTTON_PIN 7

// false: common cathode (common pin -> GND), true: common anode (common pin -> 5V)
const bool RGB_COMMON_ANODE = false;

// Hysteresis: DAY -> NIGHT at 200 or below, NIGHT -> DAY at 300 or above.
const int LIGHT_TO_NIGHT_THRESHOLD = 200;
const int LIGHT_TO_DAY_THRESHOLD = 300;

const unsigned long SAMPLE_INTERVAL = 1000;
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long HUMIDIFIER_SLOT_TIME = 1000;
const byte AVERAGE_SAMPLE_COUNT = 3;

// Light is used only to distinguish day and night, so it is excluded from D.
// The previous H/P/T weights are normalized so that their sum remains 1.
// D = 0.56H + 0.28P + 0.16T
const float HUMIDITY_WEIGHT = 0.56;
const float PM25_WEIGHT = 0.28;
const float TEMPERATURE_WEIGHT = 0.16;

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
PM2008_I2C pm2008;

bool systemOff = false;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long debounceStartTime = 0;

unsigned long lastSampleTime = 0;
unsigned long humidifierCycleStart = 0;

float temperatureSamples[AVERAGE_SAMPLE_COUNT];
float humiditySamples[AVERAGE_SAMPLE_COUNT];
float pm25Samples[AVERAGE_SAMPLE_COUNT];
byte sampleIndex = 0;
byte validSampleCount = 0;

float averageTemperature = 0.0;
float averageHumidity = 0.0;
float averagePm25 = 0.0;
float finalScore = 0.0;
int currentLightValue = 0;
bool isDaytime = false;

int humidifierStage = 0;

void setup() {
  pinMode(HUMIDIFIER_PIN, OUTPUT);
  pinMode(LIGHT_SENSOR_PIN, INPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

  stopHumidifier();
  allLedOff();

  Serial.begin(9600);
  Wire.begin();
  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM ON");
  lcd.setCursor(0, 1);
  lcd.print("Collecting 0/3");

  pm2008.begin();
  pm2008.command();

  resetMeasurements();
  lastSampleTime = millis();
  Serial.println("System ON - collecting 3 samples");
}

void loop() {
  checkPowerButton();

  if (systemOff) {
    stopHumidifier();
    return;
  }

  unsigned long now = millis();
  if (now - lastSampleTime >= SAMPLE_INTERVAL) {
    lastSampleTime += SAMPLE_INTERVAL;

    // Prevent a burst of delayed readings after an unusually long pause.
    if (now - lastSampleTime >= SAMPLE_INTERVAL) {
      lastSampleTime = now;
    }

    takeSensorSample();
  }

  updateHumidifier();
}

void checkPowerButton() {
  bool reading = digitalRead(POWER_BUTTON_PIN);

  if (reading != lastButtonReading) {
    debounceStartTime = millis();
  }

  if (millis() - debounceStartTime >= DEBOUNCE_DELAY &&
      reading != stableButtonState) {
    stableButtonState = reading;

    if (stableButtonState == LOW) {
      systemOff = !systemOff;
      stopHumidifier();
      allLedOff();
      lcd.clear();

      if (systemOff) {
        lcd.setCursor(0, 0);
        lcd.print("SYSTEM OFF");
        lcd.setCursor(0, 1);
        lcd.print("Press to start");
        Serial.println("System OFF by button");
      } else {
        resetMeasurements();
        lastSampleTime = millis();
        lcd.setCursor(0, 0);
        lcd.print("SYSTEM ON");
        lcd.setCursor(0, 1);
        lcd.print("Collecting 0/3");
        Serial.println("System ON - measurement history reset");
      }
    }
  }

  lastButtonReading = reading;
}

void takeSensorSample() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int pmReadResult = pm2008.read();
  currentLightValue = analogRead(LIGHT_SENSOR_PIN);
  updateDayNightState(currentLightValue);

  if (isnan(temperature) || isnan(humidity)) {
    showSensorError("DHT ERROR");
    Serial.println("DHT read error - sample discarded");
    return;
  }

  if (pmReadResult != 0) {
    showSensorError("PM ERROR");
    Serial.println("PM2008SE read error - sample discarded");
    return;
  }

  int pm25 = pm2008.pm2p5_grimm;

  temperatureSamples[sampleIndex] = temperature;
  humiditySamples[sampleIndex] = humidity;
  pm25Samples[sampleIndex] = pm25;

  sampleIndex = (sampleIndex + 1) % AVERAGE_SAMPLE_COUNT;
  if (validSampleCount < AVERAGE_SAMPLE_COUNT) {
    validSampleCount++;
  }

  Serial.print("RAW T:");
  Serial.print(temperature, 1);
  Serial.print("C H:");
  Serial.print(humidity, 1);
  Serial.print("% PM2.5:");
  Serial.print(pm25);
  Serial.print(" Light:");
  Serial.print(currentLightValue);
  Serial.print(" (");
  Serial.print(isDaytime ? "DAY" : "NIGHT");
  Serial.println(")");

  if (validSampleCount < AVERAGE_SAMPLE_COUNT) {
    showCollectingScreen();
    return;
  }

  calculateAverages();
  calculateScoreAndStage();
  printCalculatedValues();
  showMeasurementScreen();
}

void calculateAverages() {
  averageTemperature = averageOf(temperatureSamples);
  averageHumidity = averageOf(humiditySamples);
  averagePm25 = averageOf(pm25Samples);
}

float averageOf(float values[]) {
  float sum = 0.0;
  for (byte i = 0; i < AVERAGE_SAMPLE_COUNT; i++) {
    sum += values[i];
  }
  return sum / AVERAGE_SAMPLE_COUNT;
}

void calculateScoreAndStage() {
  int humidityScore = getHumidityScore(averageHumidity);
  int pm25Score = getPm25Score(averagePm25);
  int temperatureScore = getTemperatureScore(averageTemperature);

  finalScore = HUMIDITY_WEIGHT * humidityScore +
               PM25_WEIGHT * pm25Score +
               TEMPERATURE_WEIGHT * temperatureScore;

  int newStage;
  if (finalScore < 2.0) {
    newStage = 0;  // OFF, OFF, OFF
  } else if (finalScore < 2.5) {
    newStage = 1;  // OFF, ON, OFF
  } else if (finalScore < 3.0) {
    newStage = 2;  // ON, OFF, ON
  } else {
    newStage = 3;  // ON, ON, ON
  }

  if (newStage != humidifierStage) {
    humidifierStage = newStage;
    humidifierCycleStart = millis();
  }

  // Restore the LEDs after startup or a temporary sensor error as well.
  showStage(humidifierStage);

  Serial.print("Scores H:");
  Serial.print(humidityScore);
  Serial.print(" P:");
  Serial.print(pm25Score);
  Serial.print(" T:");
  Serial.print(temperatureScore);
  Serial.print(" -> D:");
  Serial.print(finalScore, 2);
  Serial.print(" Stage:");
  Serial.println(humidifierStage);
}

int getHumidityScore(float humidity) {
  if (humidity < 78.0) return 4;
  if (humidity < 83.0) return 3;
  if (humidity < 88.0) return 2;
  if (humidity < 93.0) return 1;
  return 0;
}

int getPm25Score(float pm25) {
  if (pm25 <= 15.0) return 0;
  if (pm25 <= 35.0) return 3;
  if (pm25 <= 75.0) return 7;
  return 10;
}

int getTemperatureScore(float temperature) {
  if (temperature < 21.0) return 1;
  if (temperature < 23.0) return 4;
  if (temperature < 26.0) return 7;
  return 10;
}

void updateHumidifier() {
  // The humidifier must never operate at night.
  if (systemOff || !isDaytime ||
      validSampleCount < AVERAGE_SAMPLE_COUNT) {
    digitalWrite(HUMIDIFIER_PIN, LOW);
    return;
  }

  byte slot = ((millis() - humidifierCycleStart) / HUMIDIFIER_SLOT_TIME) % 3;
  bool humidifierOn = false;

  if (humidifierStage == 1) {
    humidifierOn = (slot == 1);              // OFF, ON, OFF
  } else if (humidifierStage == 2) {
    humidifierOn = (slot == 0 || slot == 2); // ON, OFF, ON
  } else if (humidifierStage == 3) {
    humidifierOn = true;                     // ON, ON, ON
  }

  digitalWrite(HUMIDIFIER_PIN, humidifierOn ? HIGH : LOW);
}

void resetMeasurements() {
  sampleIndex = 0;
  validSampleCount = 0;
  finalScore = 0.0;
  humidifierStage = 0;
  humidifierCycleStart = millis();
}

void updateDayNightState(int lightValue) {
  bool previousDaytime = isDaytime;

  if (isDaytime) {
    if (lightValue <= LIGHT_TO_NIGHT_THRESHOLD) {
      isDaytime = false;
    }
  } else {
    if (lightValue >= LIGHT_TO_DAY_THRESHOLD) {
      isDaytime = true;
    }
  }

  if (isDaytime != previousDaytime) {
    Serial.print("Day/Night changed: ");
    Serial.println(isDaytime ? "DAY" : "NIGHT");

    if (isDaytime) {
      // Start the 3-second humidifier pattern from slot 0 on sunrise.
      humidifierCycleStart = millis();
    } else {
      // Stop immediately when the state changes to night.
      digitalWrite(HUMIDIFIER_PIN, LOW);
    }
  }
}

void stopHumidifier() {
  humidifierStage = 0;
  digitalWrite(HUMIDIFIER_PIN, LOW);
}

void showCollectingScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Collecting data");
  lcd.setCursor(0, 1);
  lcd.print("Samples: ");
  lcd.print(validSampleCount);
  lcd.print("/3");
}

void showMeasurementScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("T");
  lcd.print(averageTemperature, 0);
  lcd.print(" H");
  lcd.print(averageHumidity, 0);
  lcd.print(" P");
  lcd.print(averagePm25, 0);

  lcd.setCursor(0, 1);
  lcd.print("D");
  lcd.print(finalScore, 1);
  lcd.print(" S");
  lcd.print(humidifierStage);
  lcd.print(isDaytime ? " DAY" : " NIGHT");
}

void showSensorError(const char *message) {
  // Discard old samples so the next average contains three consecutive,
  // newly measured seconds rather than stale data from before the error.
  resetMeasurements();
  digitalWrite(HUMIDIFIER_PIN, LOW);
  allLedOff();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(message);
  lcd.setCursor(0, 1);
  lcd.print("Sample skipped");
}

void printCalculatedValues() {
  Serial.print("AVG(3s) T:");
  Serial.print(averageTemperature, 1);
  Serial.print("C H:");
  Serial.print(averageHumidity, 1);
  Serial.print("% PM2.5:");
  Serial.print(averagePm25, 1);
  Serial.print(" Day/Night:");
  Serial.println(isDaytime ? "DAY" : "NIGHT");
}

void setLed(int pin, bool on) {
  digitalWrite(pin, RGB_COMMON_ANODE ? !on : on);
}

void allLedOff() {
  setLed(RED_LED, false);
  setLed(GREEN_LED, false);
  setLed(BLUE_LED, false);
}

void showStage(int stage) {
  allLedOff();

  if (stage == 0) {
    setLed(RED_LED, true);
  } else if (stage == 1) {
    setLed(GREEN_LED, true);
  } else if (stage == 2) {
    setLed(BLUE_LED, true);
  } else {
    setLed(RED_LED, true);
    setLed(BLUE_LED, true);
  }
}
