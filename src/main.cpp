// ============================================================================
//  hackeroo — RP2350 (Raspberry Pi Pico 2) hardware-hacking multitool
//
//  A USB-serial command console exposing pluggable hardware-hacking modules.
//  Connect at 115200 8N1 (e.g. `pio device monitor`) and type `help`.
//
//  For AUTHORIZED security research / hardware bring-up on your own devices.
// ============================================================================
#include <Arduino.h>
#include "console.h"
#include "config.h"

// Power-on flourish: a fast knight-rider sweep across the configured GPIO
// range, then a double flash. Pins are released to INPUT afterwards so the
// modules can drive/read them normally.
static void bootAnimation() {
#if CFG_BOOTANIM_ENABLE
  const int lo = CFG_BOOTANIM_FIRST, hi = CFG_BOOTANIM_LAST, ms = CFG_BOOTANIM_MS;
  for (int p = lo; p <= hi; p++) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  for (int p = lo; p <= hi; p++) { digitalWrite(p, HIGH); delay(ms); digitalWrite(p, LOW); }
  for (int p = hi - 1; p > lo;  p--) { digitalWrite(p, HIGH); delay(ms); digitalWrite(p, LOW); }
  for (int f = 0; f < 2; f++) {
    for (int p = lo; p <= hi; p++) digitalWrite(p, HIGH);
    delay(70);
    for (int p = lo; p <= hi; p++) digitalWrite(p, LOW);
    delay(70);
  }
  for (int p = lo; p <= hi; p++) pinMode(p, INPUT);   // release the lines
#endif
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);   // solid = alive & waiting for a host
  bootAnimation();
  console::begin();
}

void loop() {
  console::poll();

  // Heartbeat so you can tell the firmware is alive without a serial monitor.
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}

// The RP2350 has two cores. Leave core1 idle by default; modules that need
// deterministic timing (e.g. glitch) can launch work on it themselves.
void setup1() {}
void loop1() { delay(1000); }
