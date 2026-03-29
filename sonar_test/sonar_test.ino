// Minimal HC-SR04 test — no game code, no timers, no interrupts.
// Open Serial Monitor at 9600 baud.
// Wiring: TRIG -> pin 4, ECHO -> pin 7, VCC -> 5V, GND -> GND

#define TRIG_PIN 4
#define ECHO_PIN 7

void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  Serial.println("HC-SR04 test started");
}

void loop() {
  // Fire trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Read echo — 30 000 µs timeout (~5 m range)
  uint32_t duration = pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) {
    Serial.println("no echo (timeout)");
  } else {
    float cm = duration * 0.034f / 2.0f;
    Serial.print(duration);
    Serial.print(" us  ->  ");
    Serial.print(cm, 1);
    Serial.println(" cm");
  }

  delay(500);
}
