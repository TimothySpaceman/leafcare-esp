# LeafCare Pot FirmWare

### Current state: Measuring + Automation + MQTT + Pairing

At this moment, the firmware implements the following lifecycle:
1. ESP checks wakeup reason (timer/button)
2. If the wakeup is caused by the button - LED turns on
3. If the button is held long enough - ESP switches to pairing mode, or in default mode otherwise
4. ESP tries to read the WiFi settings from the memory
4.1. If WiFi is not configured yet - switching to pairing mode
5. ESP tries to establish the WiFi Connection. Then tries to check the API Status
6. If API status is OK - ESP tries to establish the MQTT Connection
7. If MQTT Connection failed - ESP sends an error report to the API
8. **Default mode**
8.1. If the pot should rotate, it rotates until the light metrics shift by 1
8.2. If the pot should water the plant, it waters until the required moisture is reached or there's no water in the tank
8.3. Collecting all measurements
8.4. If MQTT connected - sending measurements
8.5. Printing results in Serial
9. **Pairing mode**
9.1. LED starts to blink
9.2. Trying to read the WiFi settings from the memory or replacing with defaults
9.3. Initing BLE Server with four properties: Restart, PotID, SSID and Password
9.4. If SSID or Password is received - saving new value to the memory
9.5. If Restart "1" is received - ESP restarts
9.6. If there's no connections after X second of the pairing mode - ESP restarts
0. ESP goes into the deep sleep until a certain time or the button is pressed