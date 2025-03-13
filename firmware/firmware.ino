#include <Wire.h>
#include <SHT2x.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "nvs.h"
#include "nvs_flash.h"

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
#define LIGHT_SENSOR_MIN 0 // Light Sensor Value Range
#define LIGHT_SENSOR_MAX 4096

// MOISTURE
#define MOISTURE_SENSOR_PIN 33   // Moisture Sensor Pin
#define MOISTURE_SENSOR_MIN 3015 // Moisture Sensor Value Range
#define MOISTURE_SENSOR_MAX 1650

// WATER TANK
#define WATER_L1_PIN 13 // Water Level Pins
#define WATER_L2_PIN 12
#define WATER_L3_PIN 14
#define WATER_L4_PIN 27
#define WATER_L1_VALUE 25 // Water Level Values
#define WATER_L2_VALUE 50
#define WATER_L3_VALUE 75
#define WATER_L4_VALUE 100
#define WATER_SENSOR_PIN 39   // Water Sensor Pin
#define WATER_THRESHOLD 1     // Water Sensor Value Threshold
#define WATER_READING_TIME 20 // Time (ms, >= 2) for each level to be powered and measured

// ROTATION
#define ROTATION_MOTOR_PIN 2       // Rotation Motor Control Pin
#define ROTATION_MOTOR_VALUE 250   // Value to be given on the Motor Control Pin (<= 255)
#define ROTATION_PERIOD 3          // Timer WakeUps Count for Pot to rotate after
#define ROTATION_MS_LIMIT 10000    // Maximum rotation time (ms)
#define ROTATION_LIGHT_DIFF_MIN 10 // Average difference in light metrics range for Pot to stop rotating
#define ROTATION_LIGHT_DIFF_MAX 300

// WATERING
#define WATERING_PUMP_PIN 4          // Watering Pump Control Pin
#define WATERING_PUMP_VALUE 250      // Value to be given on the Pump Control Pin (<= 255)
#define WATERING_PERIOD 3            // Timer WakeUps Count for Pot to Water the plant
#define WATERING_ITERATION_TIME 2000 // Watering iteration time (ms)
#define WATERING_PAUSE_TIME 1000     // Watering pause time (ms)
#define WATERING_ITERATIONS_LIMIT 5  // Maximum watering iterations
#define WATERING_MOISTURE_MIN 10     // Range to keep moisture in
#define WATERING_MOISTURE_MAX 50

// WIFI
#define WIFI_SSID ""
#define WIFI_SSID_NVS_KEY "wifi_ssid"
#define WIFI_PASSWORD ""
#define WIFI_PASSWORD_NVS_KEY "wifi_password"
#define WIFI_TIMEOUT 10000
#define WIFI_BACKEND "192.168.1.231:3000"
#define WIFI_BACKEND_STATUS_ENDPOINT "/status"

// MQTT
#define MQTT_BROKER "192.168.1.231"
#define MQTT_PORT 1883
#define MQTT_USERNAME "client1"
#define MQTT_PASSWORD "Pass1234"
#define MQTT_TOPIC "lc/plants/plant001"
#define MQTT_POT_ID "10" // Must be a string!
#define MQTT_TIMEOUT 10000
#define MQTT_ERROR_REPORT_ENDPOINT "/pot/report/mqtt"

// BLE
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define POT_ID_UUID "59d99a3d-c694-4826-8fbf-26859fa7c4f0"
#define RESTART_UUID "b7d8c6b8-aa64-49a5-8f37-d437acc2842b"
#define WIFI_SSID_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define WIFI_PASSWORD_UUID "aaf4a436-9747-4622-b57c-ec585c132325"

// PAIRING
#define PAIRING_MODE_TIMEOUT 45

// GLOBALS
RTC_DATA_ATTR int timerWakeUpCount = 0;
int buttonWakeUp = 0;

SHT2x sht;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// TYPES
struct WiFiSettings
{
  char ssid[32];
  char password[64];
};

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

int isServerAvailable()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return 0;
  }

  String host = WIFI_BACKEND;
  String path = WIFI_BACKEND_STATUS_ENDPOINT;

  HTTPClient http;
  http.begin("http://" + host + path);
  int code = http.GET();
  http.end();
  return code == 200;
}

int readWiFiSettings(WiFiSettings *settings)
{
  nvs_handle_t handle;
  esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
  if (err != ESP_OK)
  {
    Serial.println("Failed to open NVS.");
    return 0;
  }

  size_t ssidSize = sizeof(settings->ssid);
  err = nvs_get_str(handle, WIFI_SSID_NVS_KEY, settings->ssid, &ssidSize);
  if (err != ESP_OK)
  {
    Serial.println("Failed to read SSID from NVS.");
    nvs_close(handle);
    return 0;
  }

  size_t passwordSize = sizeof(settings->password);
  err = nvs_get_str(handle, WIFI_PASSWORD_NVS_KEY, settings->password, &passwordSize);
  nvs_close(handle);
  if (err != ESP_OK)
  {
    Serial.println("Failed to read password from NVS.");
    return 0;
  }

  return 1;
}

esp_err_t saveString(const char *key, const char *value)
{
  nvs_handle_t handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_set_str(handle, key, value);
  if (err == ESP_OK)
  {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
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
  values.l1 = constrain(map(values.l1Raw, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX, 0, 100), 0, 100);
  values.l2 = constrain(map(values.l2Raw, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX, 0, 100), 0, 100);
  values.l3 = constrain(map(values.l3Raw, LIGHT_SENSOR_MIN, LIGHT_SENSOR_MAX, 0, 100), 0, 100);
  values.max = max(values.l1, max(values.l2, values.l3));
  return values;
}

int readMoisture()
{
  int value = analogRead(MOISTURE_SENSOR_PIN);
  return constrain(map(value, MOISTURE_SENSOR_MIN, MOISTURE_SENSOR_MAX, 0, 100), 0, 100);
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

void rotating()
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

void watering()
{
  if (readMoisture() > WATERING_MOISTURE_MIN || buttonWakeUp != 0 || timerWakeUpCount % WATERING_PERIOD != 0 || timerWakeUpCount == 0)
  {
    return;
  }

  for (int i = 0; i < WATERING_ITERATIONS_LIMIT; i++)
  {
    if (readWater() == 0 || readMoisture() >= WATERING_MOISTURE_MAX)
    {
      return;
    }

    analogWrite(WATERING_PUMP_PIN, WATERING_PUMP_VALUE);
    delay(WATERING_ITERATION_TIME);
    analogWrite(WATERING_PUMP_PIN, 0);
    delay(WATERING_PAUSE_TIME);
  }
}

// NORMAL MODE
void normalMode()
{
  // I2C Init
  Wire.begin();
  sht.begin();

  // Automation
  rotating();
  delay(250);
  watering();

  // Collecting Measurements
  int moisture = readMoisture();
  int water = readWater();
  Light light = readLight();
  HumAndTemp humAndTemp = readHumAndTemp();

  // Sending Measurements
  if (mqttClient.connected())
  {
    JsonDocument payload;
    payload["potId"] = MQTT_POT_ID;
    payload["temperature"] = humAndTemp.temperature;
    payload["humidity"] = humAndTemp.humidity;
    payload["moisture"] = moisture;
    payload["light"] = light.max;
    payload["water"] = water;

    char output[256];
    serializeJson(payload, output);

    mqttClient.publish(MQTT_TOPIC, output);

    mqttClient.loop();
  }

  // Printing Measurements
  Serial.println("Measurements:");
  Serial.print("Moisture: ");
  Serial.println(moisture);
  Serial.print("Water: ");
  Serial.println(water);
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
  Serial.print("Humidity: ");
  Serial.println(humAndTemp.humidity);
  Serial.print("Temperature: ");
  Serial.println(humAndTemp.temperature);
}

// PAIRING MODE
class WiFiSsidCallBack : public BLECharacteristicCallbacks
{
public:
  void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param)
  {
    saveString(WIFI_SSID_NVS_KEY, pCharacteristic->getValue().c_str());
  }
};

class WiFiPasswordCallBack : public BLECharacteristicCallbacks
{
public:
  void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param)
  {
    saveString(WIFI_PASSWORD_NVS_KEY, pCharacteristic->getValue().c_str());
  }
};

class RestartCallBack : public BLECharacteristicCallbacks
{
public:
  void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param)
  {
    if (pCharacteristic->getValue() == "1")
    {
      ESP.restart();
    }
  }
};

void pairingMode()
{
  // Reading the WiFi settings or replacing with defaults
  WiFiSettings wifiSettings;
  if (readWiFiSettings(&wifiSettings) == 0)
  {
    strcpy(wifiSettings.ssid, WIFI_SSID);
    strcpy(wifiSettings.password, WIFI_PASSWORD);
  }

  // BLE Init
  BLEDevice::init((std::string("LEAFCARE-POT-") + MQTT_POT_ID).c_str());
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *restartChar = pService->createCharacteristic(RESTART_UUID, BLECharacteristic::PROPERTY_WRITE);
  restartChar->setValue("0");
  restartChar->setCallbacks(new RestartCallBack());

  BLECharacteristic *pPotIdChar = pService->createCharacteristic(POT_ID_UUID, BLECharacteristic::PROPERTY_READ);
  pPotIdChar->setValue(MQTT_POT_ID);

  BLECharacteristic *pWiFiSsidChar = pService->createCharacteristic(WIFI_SSID_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWiFiSsidChar->setValue(wifiSettings.ssid);
  pWiFiSsidChar->setCallbacks(new WiFiSsidCallBack());

  BLECharacteristic *pWiFiPasswordChar = pService->createCharacteristic(WIFI_PASSWORD_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWiFiPasswordChar->setValue(wifiSettings.password);
  pWiFiPasswordChar->setCallbacks(new WiFiPasswordCallBack());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  // Loop to keep ESP here
  int start = millis();
  int lastState = 0;
  while (millis() - start < PAIRING_MODE_TIMEOUT * 1000 || pServer->getConnectedCount() > 0)
  {
    delay(500);
    analogWrite(USER_LED_PIN, lastState);
    lastState = 255 - lastState;
  }

  ESP.restart();
}

// LIFECYCLE
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

  // Manual Pairing Mode Check
  if (isButtonHeld(USER_BUTTON_PIN, PAIRING_MODE_HOLD_TIME) == 1)
  {
    pairingMode();
  }

  // WiFi Init
  WiFiSettings wifiSettings;
  if (readWiFiSettings(&wifiSettings) == 0)
  {
    pairingMode();
  }

  WiFi.begin(wifiSettings.ssid, wifiSettings.password);
  int wifiInitStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiInitStart < WIFI_TIMEOUT)
  {
    delay(250);
  }
  if (isServerAvailable())
  {

    // MQTT Init
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    int mqttInitStart = millis();
    while (!mqttClient.connected() && millis() - mqttInitStart < MQTT_TIMEOUT)
    {
      String client_id = "esp32-client-";
      client_id += String(WiFi.macAddress());
      if (!mqttClient.connect(client_id.c_str(), MQTT_USERNAME, MQTT_PASSWORD))
      {
        delay(1000);
      }
    }
    if (!mqttClient.connected())
    {
      String host = WIFI_BACKEND;
      String path = MQTT_ERROR_REPORT_ENDPOINT;

      HTTPClient http;
      http.begin("http://" + host + path + "?code=" + mqttClient.state());
      http.GET();
      http.end();
    }
  }
  else
  {
    Serial.println("SERVER CONNECTION FAILED");
  }

  normalMode();

  Serial.flush();

  // Sleep Setup
  esp_sleep_enable_ext1_wakeup_io(BUTTON_PIN_BITMASK(USER_BUTTON_PIN), ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup(DEFAULT_SLEEP_INTERVAL * MILLIS_TO_MICROS_FACTOR);
  esp_deep_sleep_start();
}

void loop()
{
}
