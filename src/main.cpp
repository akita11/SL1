#include <Arduino.h>
#include <M5Unified.h>

// SCL: G8
// SDA: G7
// SOL: G5
// SW : G6
// LED: G39

#define PIN_SCL 8
#define PIN_SDA 7
#define PIN_SOL 5
#define PIN_SW  6
#define PIN_LED 39

void setup() {
	M5.begin();
	pinMode(PIN_SOL, OUTPUT); digitalWrite(PIN_SOL, LOW);
	pinMode(PIN_SW, INPUT_PULLUP);
}

void loop() {
}
