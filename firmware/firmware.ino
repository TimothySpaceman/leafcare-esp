#include <Wire.h>
#include <SHT2x.h>

// UTILITY DEFINES
#define MILLIS_TO_MICROS_FACTOR 1000
#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)

// DEFAULT VALUES
#define DEFAULT_SLEEP_INTERVAL 5000 // Time interval (ms) for ESP wake-up

// PAIRING MODE
#define PAIRING_MODE_HOLD_TIME 4000 // Time (ms) for the User Button needs to be held to go into Pairing Mode

// USER INTERFACE
#define USER_BUTTON_PIN 25 // User Interface Button Pin
#define USER_LED_PIN 15    // User Interface LED Pin

// LIGHT
#define LIGHT_PIN1 32 // Light Sensor 1 Pins
#define LIGHT_PIN2 35
#define LIGHT_PIN3 34
#define LIGHT_MIN 0 // Light Sensor Value Range
#define LIGHT_MAX 4096

// MOISTURE
#define MOISTURE_SENSOR_PIN 33 // Moisture Sensor Pin
#define MOISTURE_MIN 3015      // Moisture Sensor Value Range
#define MOISTURE_MAX 1650

// WATER
#define WATER_L1_PIN 13 // Water Level Pins
#define WATER_L2_PIN 12
#define WATER_L3_PIN 14
#define WATER_L4_PIN 27
#define WATER_L1_VALUE 25 // Water Level Values
#define WATER_L2_VALUE 50
#define WATER_L3_VALUE 75
#define WATER_L4_VALUE 100
#define WATER_SENSOR_PIN 39    // Water Sensor Pin
#define WATER_THRESHOLD 1      // Water Sensor Value Threshold
#define WATER_READING_TIME 100 // Time (ms, >= 2) for each level to be powered and measured

// ROTATION
#define ROTATION_MOTOR_PIN 2      // Rotation Motor Control Pin
#define ROTATION_MOTOR_VALUE 250  // Value to be given on the Control Pin (<= 255)
#define ROTATION_PERIOD 3         // Timer WakeUps Count for Pot to rotate after
#define ROTATION_MS_LIMIT 10000   // Maximum rotation time (ms)
#define ROTATION_LIGHT_DIFF_MIN 5 // Average difference in light metrics range for Pot to stop rotating
#define ROTATION_LIGHT_DIFF_MAX 275

// GLOBALS
RTC_DATA_ATTR int timerWakeUpCount = 0;
int buttonWakeUp = 0;
SHT2x sht;

// TYPES
struct HumAndTemp
{
  float humidity;
  float temperature;
};

struct Light
{
  int max; // Maximum value
  int l1;  // Individual values in percents
  int l2;
  int l3;
  int l1Raw; // Raws are values without mapping to percents
  int l2Raw;
  int l3Raw;
};

// UTILITIES
int isButtonHeld(int pin, int time)
{
  int start = millis();

  while (true)
  {
    int state = digitalRead(pin);
    int moment = millis();

    if (state == LOW && moment - start < time)
    {
      return 0;
    }

    if (moment - start >= time)
    {
      return 1;
    }
  }
}

// MEASURING
HumAndTemp readHumAndTemp()
{
  struct HumAndTemp values;
  sht.read();
  values.humidity = constrain(sht.getHumidity(), 0, 100);
  values.temperature = sht.getTemperature();
  return values;
}

Light readLight()
{
  struct Light values;
  values.l1Raw = analogRead(LIGHT_PIN1);
  values.l2Raw = analogRead(LIGHT_PIN2);
  values.l3Raw = analogRead(LIGHT_PIN3);
  values.l1 = constrain(map(values.l1Raw, LIGHT_MIN, LIGHT_MAX, 0, 100), 0, 100);
  values.l2 = constrain(map(values.l2Raw, LIGHT_MIN, LIGHT_MAX, 0, 100), 0, 100);
  values.l3 = constrain(map(values.l3Raw, LIGHT_MIN, LIGHT_MAX, 0, 100), 0, 100);
  values.max = max(values.l1, max(values.l2, values.l3));
  return values;
}

int readMoisture()
{
  int value = analogRead(MOISTURE_SENSOR_PIN);
  return constrain(map(value, MOISTURE_MIN, MOISTURE_MAX, 0, 100), 0, 100);
}

int readWaterPin(int pin)
{
  analogWrite(WATER_L1_PIN, 0);
  analogWrite(WATER_L2_PIN, 0);
  analogWrite(WATER_L3_PIN, 0);
  analogWrite(WATER_L4_PIN, 0);
  analogWrite(pin, 255);
  delay(WATER_READING_TIME / 2);
  int value = analogRead(WATER_SENSOR_PIN);
  delay(WATER_READING_TIME / 2);
  analogWrite(pin, 0);
  return value;
}

int readWater()
{
  if (readWaterPin(WATER_L4_PIN) >= WATER_THRESHOLD)
  {
    return WATER_L4_VALUE;
  }

  if (readWaterPin(WATER_L3_PIN) >= WATER_THRESHOLD)
  {
    return WATER_L3_VALUE;
  }

  if (readWaterPin(WATER_L2_PIN) >= WATER_THRESHOLD)
  {
    return WATER_L2_VALUE;
  }

  if (readWaterPin(WATER_L1_PIN) >= WATER_THRESHOLD)
  {
    return WATER_L1_VALUE;
  }

  return 0;
}

// AUTOMATION
int rotatedLightDiff(Light before, Light after)
{
  int diff1 = abs(before.l1Raw - after.l2Raw);
  int diff2 = abs(before.l2Raw - after.l3Raw);
  int diff3 = abs(before.l3Raw - after.l1Raw);

  return (diff1 + diff2 + diff3) / 3;
}

void rotate()
{
  if (buttonWakeUp != 0 || timerWakeUpCount % ROTATION_PERIOD != 0 || timerWakeUpCount == 0)
  {
    return;
  }

  int start = millis();
  Light before = readLight();

  analogWrite(ROTATION_MOTOR_PIN, ROTATION_MOTOR_VALUE);
  delay(1000);
  while (true)
  {
    int now = millis();
    if (now - start >= ROTATION_MS_LIMIT)
    {
      break;
    }

    Light after = readLight();

    int diff = rotatedLightDiff(before, after);
    if (ROTATION_LIGHT_DIFF_MIN < diff && diff <= ROTATION_LIGHT_DIFF_MAX)
    {
      break;
    }
  }
  analogWrite(ROTATION_MOTOR_PIN, 0);
}

// LIFECYCLE
void normalMode()
{
  // I2C Init
  Wire.begin();
  sht.begin();

  rotate();

  Serial.println("Measurements:");

  int moisture = readMoisture();
  Serial.print("Moisture: ");
  Serial.println(moisture);

  int water = readWater();
  Serial.print("Water: ");
  Serial.println(water);

  Light light = readLight();
  Serial.print("Light: ");
  Serial.print(light.max);
  Serial.print(" (");
  Serial.print(light.l1);
  Serial.print(", ");
  Serial.print(light.l2);
  Serial.print(", ");
  Serial.print(light.l3);
  Serial.print(") (");
  Serial.print(light.l1Raw);
  Serial.print(", ");
  Serial.print(light.l2Raw);
  Serial.print(", ");
  Serial.print(light.l3Raw);
  Serial.println(")");

  HumAndTemp humAndTemp = readHumAndTemp();
  Serial.print("Humidity: ");
  Serial.println(humAndTemp.humidity);
  Serial.print("Temperature: ");
  Serial.println(humAndTemp.temperature);
}

void pairingMode()
{
  Serial.println("Pairing Mode");
  delay(1000);
}

void setup()
{
  // Determining WakeUp Cause
  buttonWakeUp = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;

  // Serial Init
  Serial.begin(9600);

  // User Interface Init
  pinMode(USER_BUTTON_PIN, INPUT);
  if (buttonWakeUp == 1)
  {
    analogWrite(USER_LED_PIN, 255);
  }
  else
  {
    timerWakeUpCount++;
    analogWrite(USER_LED_PIN, 0);
  }

  // Pairing Mode Check
  if (isButtonHeld(USER_BUTTON_PIN, PAIRING_MODE_HOLD_TIME) == 1)
  {
    pairingMode();
  }
  else
  {
    normalMode();
  }

  Serial.flush();

  // Sleep Setup
  esp_sleep_enable_ext1_wakeup_io(BUTTON_PIN_BITMASK(USER_BUTTON_PIN), ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup(DEFAULT_SLEEP_INTERVAL * MILLIS_TO_MICROS_FACTOR);
  esp_deep_sleep_start();
}

void loop()
{
}
