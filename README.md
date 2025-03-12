# LeafCare Pot FirmWare

### Current state: Skeleton

At this moment, the firmware implements lifecycle skeleton:
1. ESP checks wakeup reason (timer/button)
2. If the wakeup is caused by the button - LED turns on
3. If the button is held long enough - ESP switches to pairing mode, or in default mode otherwise
4. ESP goes into the deep sleep until a certain time or the button is pressed