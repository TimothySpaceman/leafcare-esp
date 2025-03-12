# LeafCare Pot FirmWare

### Current state: Measuring + Automation + MQTT

At this moment, the firmware implements the following lifecycle:
1. ESP checks wakeup reason (timer/button)
2. If the wakeup is caused by the button - LED turns on
3. If the button is held long enough - ESP switches to pairing mode, or in default mode otherwise
4. ESP tries to establish the WiFi Connection. Then tries to check the API Status
5. If API status is OK - ESP tries to establish the MQTT Connection
6. If MQTT Connection failed - ESP sends an error report to the API
7. **Default mode**
7.1. If the pot should rotate, it rotates until the light metrics shift by 1
7.2. If the pot should water the plant, it waters until the required moisture is reached or there's no water in the tank
7.3. Collecting all measurements
7.4. If MQTT connected - sending measurements
7.5. Printing results in Serial
8. **Pairing mode**
8.1. Serial Dummy
9. ESP goes into the deep sleep until a certain time or the button is pressed