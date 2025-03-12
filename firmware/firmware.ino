// UTILS
#define MILLIS_TO_MICROS_FACTOR 1000
#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)

// DEFAULT VALUES
#define DEFAULT_SLEEP_INTERVAL 5000 // Time interval (ms) for ESP wake-up

// PAIRING MODE
#define PAIRING_MODE_HOLD_TIME 4000 // Time (ms) for the User Button needs to be held to go into Pairing Mode

// USER INTERFACE
#define USER_BUTTON_PIN 25 // User Interface Button Pin
#define USER_LED_PIN 15    // User Interface LED Pin

// GLOBALS
RTC_DATA_ATTR int wakeUpCount = 0;

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

void setup()
{
  // Determining WakeUp Cause
  int buttonWakeUp = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;

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
    analogWrite(USER_LED_PIN, 0);
  }

  // Pairing Mode Check
  if (isButtonHeld(USER_BUTTON_PIN, PAIRING_MODE_HOLD_TIME) == 1)
  {
    Serial.println("Pairing Mode");
    delay(1000);
  }
  else
  {
    Serial.println("Default Mode");
    delay(1000);
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
