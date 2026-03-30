sensor:
- trig: arduino PWM 4
- echo: arduino PWM 7

LCD:
- green: arduino A4
- blue: arduino A5
- red: 5V
- black: GND

button:
- top: GND
- bottom: arduino PWM 2

shift register:
- pin 1: arduino a0
- pin 2: pin 1
- pin 3: resistor -> led 1 -> GND
- pin 4: resistor -> led 2 -> GND
- pin 7: GND
- pin 8: arduino A1
- pin 9: pin 14
- pin 14: 5V