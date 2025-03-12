# LeafCare Pot FirmWare

### Current state: Measuring

At this moment, the firmware implements lifecycle skeleton with collecting measurements:
1. ESP checks wakeup reason (timer/button)
2. If the wakeup is caused by the button - LED turns on
3. If the button is held long enough - ESP switches to pairing mode, or in default mode otherwise
4. **Default mode**
4.1. Collecting all measurements
4.2. Printing results in Serial
5. **Pairing mode**
5.1. Serial Dummy
6. ESP goes into the deep sleep until a certain time or the button is pressed