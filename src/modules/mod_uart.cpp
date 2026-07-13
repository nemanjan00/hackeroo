// ============================================================================
//  uart — auto-baud detection, passive sniffer, transparent bridge.
//  Uses UART0 (Serial1). Wire the TARGET's TX to our RX (default GP1).
//
//  Auto-baud works by timing the *narrowest* pulse seen on the line while the
//  target is transmitting: one bit-time = 1/baud. The target MUST be sending
//  data during the measurement (reset it, or trigger some output).
// ============================================================================
#include "console.h"
#include "config.h"
#include "util.h"
#include <math.h>

static int  s_tx = CFG_UART_TX, s_rx = CFG_UART_RX;
static long s_baud = CFG_UART_BAUD;

static const long kStdBauds[] = {
  300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 28800,
  38400, 57600, 76800, 115200, 230400, 460800, 921600, 1000000, 2000000
};

static long snapBaud(long measured) {
  long best = kStdBauds[0];
  float bestErr = 1e30f;
  for (long b : kStdBauds) {
    float err = fabsf((float)(b - measured) / (float)b);
    if (err < bestErr) { bestErr = err; best = b; }
  }
  return best;
}

// Sample the line for `window_ms`, tracking the shortest pulse (= 1 bit time).
static long uart_autobaud(uint32_t window_ms) {
  pinMode(s_rx, INPUT_PULLUP);
  Serial.printf("auto-baud on GP%d for %lu ms — make the target transmit...\r\n",
                s_rx, (unsigned long)window_ms);
  unsigned long minPulse = 0xFFFFFFFF;
  uint32_t start = millis();
  uint32_t edges = 0;
  while ((millis() - start) < window_ms) {
    // pulseIn times a full high (or low) level in microseconds; timeout 20 ms.
    unsigned long hi = pulseIn(s_rx, HIGH, 20000);
    unsigned long lo = pulseIn(s_rx, LOW, 20000);
    if (hi > 0 && hi < minPulse) { minPulse = hi; edges++; }
    if (lo > 0 && lo < minPulse) { minPulse = lo; edges++; }
    if (console::aborted()) break;
  }
  if (minPulse == 0xFFFFFFFF || edges == 0) {
    Serial.println("no activity seen on the line.");
    return 0;
  }
  long measured = (long)(1000000.0 / (double)minPulse);
  long snapped  = snapBaud(measured);
  Serial.printf("min bit-time %lu us -> ~%ld baud -> nearest standard: %ld\r\n",
                minPulse, measured, snapped);
  s_baud = snapped;
  return snapped;
}

static void uart_open(long baud) {
  Serial1.end();
  Serial1.setTX(s_tx);
  Serial1.setRX(s_rx);
  Serial1.begin(baud);
  s_baud = baud;
}

static void uart_sniff(long baud) {
  uart_open(baud);
  Serial.printf("sniffing GP%d @ %ld baud — press a key to stop\r\n", s_rx, baud);
  Serial.println("(bytes shown as hex; printable ASCII on the right)");
  uint8_t line[16]; int n = 0;
  while (true) {
    if (Serial.available()) { Serial.read(); break; }
    while (Serial1.available()) {
      line[n++] = (uint8_t)Serial1.read();
      if (n == 16) { util::hexdump(line, 16); n = 0; }
    }
  }
  if (n) util::hexdump(line, n);
  Serial.println("\r\nstopped.");
}

// Transparent bridge between the USB console and the target UART.
// Everything you type goes to the target; target output prints here.
// Press Ctrl-] (0x1d) to exit.
static void uart_bridge(long baud) {
  uart_open(baud);
  Serial.printf("bridge @ %ld baud (TX=GP%d RX=GP%d). Ctrl-] to exit.\r\n",
                baud, s_tx, s_rx);
  while (true) {
    if (Serial.available()) {
      int c = Serial.read();
      if (c == 0x1d) break;             // Ctrl-]
      Serial1.write((uint8_t)c);
    }
    while (Serial1.available()) Serial.write((uint8_t)Serial1.read());
  }
  Serial.println("\r\nbridge closed.");
}

static void uart_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd) { Serial.println("uart: need a subcommand (help)"); return; }
  s_tx = util::optNum(argc, argv, "tx", s_tx);
  s_rx = util::optNum(argc, argv, "rx", s_rx);

  if (strcmp(cmd, "auto") == 0) {
    uart_autobaud(util::optNum(argc, argv, "ms", 3000));
  } else if (strcmp(cmd, "sniff") == 0) {
    long baud = util::numOr(util::arg(argc, argv, 1), 0);
    if (baud <= 0) { baud = uart_autobaud(3000); if (baud <= 0) return; }
    uart_sniff(baud);
  } else if (strcmp(cmd, "bridge") == 0) {
    long baud = util::numOr(util::arg(argc, argv, 1), s_baud);
    uart_bridge(baud);
  } else if (strcmp(cmd, "send") == 0) {
    long baud = util::numOr(util::arg(argc, argv, 1), s_baud);
    uart_open(baud);
    for (int i = 2; i < argc; i++) { long b; if (util::parseNum(argv[i], &b)) Serial1.write((uint8_t)b); }
    Serial.println("sent.");
  } else {
    Serial.printf("uart: unknown '%s'\r\n", cmd);
  }
}

static void uart_help() {
  Serial.println("uart — UART0 tools   (opts: tx=N rx=N ms=N)");
  Serial.println("  uart auto              detect baud from live traffic");
  Serial.println("  uart sniff [baud]      passive monitor (autobaud if omitted)");
  Serial.println("  uart bridge [baud]     transparent bridge (Ctrl-] to exit)");
  Serial.println("  uart send <baud> <byte...>");
  Serial.printf ("  defaults: TX=GP%d RX=GP%d  (target TX -> our RX)\r\n", CFG_UART_TX, CFG_UART_RX);
}

extern const Module uartModule = { "uart", "autobaud, sniff, bridge UART", uart_run, uart_help };
