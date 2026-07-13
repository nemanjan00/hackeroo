// ============================================================================
//  gpio — poke pins directly: read, write, mode, toggle, scan, watch.
//  Handy for probing resets, chip-enables, strap pins, LEDs, buttons.
// ============================================================================
#include "console.h"
#include "util.h"

static constexpr int kFirstGpio = 0;
static constexpr int kLastGpio  = 28;   // RP2350A on the Pico 2

static void gpio_scan() {
  Serial.println("reading GP0..GP28 as input-pullup:");
  for (int p = kFirstGpio; p <= kLastGpio; p++) {
    pinMode(p, INPUT_PULLUP);
    Serial.printf("  GP%-2d = %d\r\n", p, digitalRead(p));
  }
}

static void gpio_watch(int pin) {
  pinMode(pin, INPUT_PULLUP);
  int last = digitalRead(pin);
  Serial.printf("watching GP%d (start=%d) — press a key to stop\r\n", pin, last);
  while (!console::aborted()) {
    int v = digitalRead(pin);
    if (v != last) {
      Serial.printf("  GP%d -> %d  @%lu ms\r\n", pin, v, (unsigned long)millis());
      last = v;
    }
  }
  Serial.println("stopped.");
}

// Knight-rider LED chaser: light each pin in turn, bouncing back and forth.
// Great for a row of LEDs (with drivers) on consecutive GPIOs.
static void gpio_chase(int first, int last, int ms) {
  int lo = min(first, last), hi = max(first, last);
  for (int p = lo; p <= hi; p++) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  Serial.printf("chasing GP%d..GP%d @ %dms — press a key to stop\r\n", lo, hi, ms);
  int p = lo, dir = 1;
  while (!console::aborted()) {
    digitalWrite(p, HIGH);
    delay(ms);
    digitalWrite(p, LOW);
    if (hi == lo) continue;
    p += dir;
    if (p >= hi) { p = hi; dir = -1; }
    else if (p <= lo) { p = lo; dir = 1; }
  }
  for (int q = lo; q <= hi; q++) digitalWrite(q, LOW);
  Serial.println("stopped.");
}

static void gpio_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd) { Serial.println("gpio: need a subcommand (help)"); return; }

  if (strcmp(cmd, "scan") == 0) { gpio_scan(); return; }
  if (strcmp(cmd, "chase") == 0) {
    int first = util::numOr(util::arg(argc, argv, 1), 0);
    int last  = util::numOr(util::arg(argc, argv, 2), 28);
    int ms    = util::numOr(util::arg(argc, argv, 3), 100);
    if (first < kFirstGpio || first > kLastGpio || last < kFirstGpio || last > kLastGpio) {
      Serial.println("gpio chase <first> <last> [ms]  (pins 0..28)"); return;
    }
    gpio_chase(first, last, ms);
    return;
  }

  long pin;
  if (!util::parseNum(util::arg(argc, argv, 1), &pin) || pin < kFirstGpio || pin > kLastGpio) {
    Serial.println("gpio: bad or missing pin"); return;
  }

  if (strcmp(cmd, "read") == 0) {
    pinMode(pin, INPUT);
    Serial.printf("GP%ld = %d\r\n", pin, digitalRead(pin));
  } else if (strcmp(cmd, "write") == 0) {
    long v = util::numOr(util::arg(argc, argv, 2), -1);
    if (v != 0 && v != 1) { Serial.println("gpio write <pin> <0|1>"); return; }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, v);
    Serial.printf("GP%ld <- %ld\r\n", pin, v);
  } else if (strcmp(cmd, "toggle") == 0) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, !digitalRead(pin));
    Serial.printf("GP%ld = %d\r\n", pin, digitalRead(pin));
  } else if (strcmp(cmd, "mode") == 0) {
    const char* m = util::arg(argc, argv, 2);
    if      (m && strcmp(m, "in") == 0)       pinMode(pin, INPUT);
    else if (m && strcmp(m, "out") == 0)      pinMode(pin, OUTPUT);
    else if (m && strcmp(m, "pullup") == 0)   pinMode(pin, INPUT_PULLUP);
    else if (m && strcmp(m, "pulldown") == 0) pinMode(pin, INPUT_PULLDOWN);
    else { Serial.println("mode: in|out|pullup|pulldown"); return; }
    Serial.printf("GP%ld mode set\r\n", pin);
  } else if (strcmp(cmd, "watch") == 0) {
    gpio_watch(pin);
  } else {
    Serial.printf("gpio: unknown '%s'\r\n", cmd);
  }
}

static void gpio_help() {
  Serial.println("gpio — direct pin control (GP0..GP28)");
  Serial.println("  gpio read <pin>");
  Serial.println("  gpio write <pin> <0|1>");
  Serial.println("  gpio toggle <pin>");
  Serial.println("  gpio mode <pin> in|out|pullup|pulldown");
  Serial.println("  gpio scan            snapshot all pins (input-pullup)");
  Serial.println("  gpio watch <pin>     print edges until a key is pressed");
  Serial.println("  gpio chase <first> <last> [ms]   knight-rider LED animation");
}

extern const Module gpioModule = { "gpio", "read/write/scan/watch GPIO pins", gpio_run, gpio_help };
