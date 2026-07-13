// ============================================================================
//  jtag — brute-force JTAG/SWD pinout discovery (JTAGenum / JTAGulator /
//         BlueTag style). Wire the unknown test points to the candidate
//         channels (config.h: CFG_JTAG_CH0..7) and let it find the roles.
//
//  Two independent scanners:
//    * jtag scan   — find TCK/TMS/TDI/TDO by shifting the IDCODE pattern and
//                    the BYPASS pattern through every pin permutation.
//    * swd scan    — find SWCLK/SWDIO by running the JTAG-to-SWD sequence and
//                    reading the DP IDCODE (returns a valid 32-bit ID).
//
//  For AUTHORIZED testing of your own hardware only.
// ============================================================================
#include "console.h"
#include "util.h"
#include "pins.h"

// ---- low-level bit-bang helpers -------------------------------------------
static inline void tckPulse(int tck) {
  digitalWrite(tck, HIGH); delayMicroseconds(1);
  digitalWrite(tck, LOW);  delayMicroseconds(1);
}
// Clock one bit out on TDI / in on TDO (TAP shifts on falling edge, samples on rising).
static inline int tapClock(int tck, int tms, int tdi, int tdo, int tmsv, int tdiv) {
  digitalWrite(tms, tmsv);
  digitalWrite(tdi, tdiv);
  digitalWrite(tck, HIGH); delayMicroseconds(1);
  int b = digitalRead(tdo);
  digitalWrite(tck, LOW);  delayMicroseconds(1);
  return b;
}

// Drive TMS high for n clocks -> Test-Logic-Reset, then to Run-Test/Idle.
static void tapReset(int tck, int tms, int tdi, int tdo) {
  for (int i = 0; i < 6; i++) tapClock(tck, tms, tdi, tdo, 1, 0); // -> reset
  tapClock(tck, tms, tdi, tdo, 0, 0);                             // -> RTI
}

// From RTI, navigate to Shift-DR (TDR selected, in reset that's IDCODE).
static void toShiftDR(int tck, int tms, int tdi, int tdo) {
  tapClock(tck, tms, tdi, tdo, 1, 0); // RTI->Select-DR
  tapClock(tck, tms, tdi, tdo, 0, 0); // ->Capture-DR
  tapClock(tck, tms, tdi, tdo, 0, 0); // ->Shift-DR
}

// Try one (tck,tms,tdi,tdo) assignment; return true if a plausible IDCODE
// (LSB==1, not all-ones) is read out of Shift-DR after a TAP reset.
static bool tryIdcode(int tck, int tms, int tdi, int tdo, uint32_t* idOut) {
  pinMode(tck, OUTPUT); pinMode(tms, OUTPUT); pinMode(tdi, OUTPUT);
  pinMode(tdo, INPUT_PULLUP);
  digitalWrite(tck, LOW);
  tapReset(tck, tms, tdi, tdo);
  toShiftDR(tck, tms, tdi, tdo);
  uint32_t id = 0;
  for (int i = 0; i < 32; i++) {
    int b = tapClock(tck, tms, tdi, tdo, 0, 0);   // stay in Shift-DR
    id |= ((uint32_t)(b & 1)) << i;
  }
  *idOut = id;
  return (id & 1) == 1 && id != 0xFFFFFFFF && id != 0x00000000;
}

static void jtag_scan() {
  Serial.printf("JTAG scan across %d channels (", cfg.jtag_nch);
  for (int i = 0; i < cfg.jtag_nch; i++) Serial.printf("GP%d ", cfg.jtag_ch[i]);
  Serial.println(")");
  Serial.println("looking for a valid IDCODE — press a key to abort");
  int hits = 0;
  // Permute TCK, TMS, TDI, TDO over distinct channels.
  for (int a = 0; a < cfg.jtag_nch; a++)
  for (int b = 0; b < cfg.jtag_nch; b++) {
    if (b == a) continue;
    for (int c = 0; c < cfg.jtag_nch; c++) {
      if (c == a || c == b) continue;
      if (console::aborted()) { Serial.println("aborted."); return; }
      for (int d = 0; d < cfg.jtag_nch; d++) {
        if (d == a || d == b || d == c) continue;
        uint32_t id;
        if (tryIdcode(cfg.jtag_ch[a], cfg.jtag_ch[b], cfg.jtag_ch[c], cfg.jtag_ch[d], &id)) {
          Serial.printf("  HIT  TCK=GP%d TMS=GP%d TDI=GP%d TDO=GP%d  IDCODE=0x%08lx\r\n",
                        cfg.jtag_ch[a], cfg.jtag_ch[b], cfg.jtag_ch[c], cfg.jtag_ch[d], (unsigned long)id);
          hits++;
        }
      }
    }
  }
  Serial.printf("scan done, %d candidate pinout(s).\r\n", hits);
}

// ---- SWD discovery ---------------------------------------------------------
static inline void swdClk(int clk) {
  digitalWrite(clk, HIGH); delayMicroseconds(1);
  digitalWrite(clk, LOW);  delayMicroseconds(1);
}
static void swdWrite(int clk, int io, uint32_t val, int bits) {
  pinMode(io, OUTPUT);
  for (int i = 0; i < bits; i++) { digitalWrite(io, (val >> i) & 1); swdClk(clk); }
}
static uint32_t swdRead(int clk, int io, int bits) {
  pinMode(io, INPUT_PULLUP);
  uint32_t v = 0;
  for (int i = 0; i < bits; i++) { v |= ((uint32_t)digitalRead(io)) << i; swdClk(clk); }
  return v;
}

// Run the JTAG->SWD switch sequence and read DP IDCODE (read reg 0, DP).
static bool trySwd(int clk, int io, uint32_t* idOut) {
  pinMode(clk, OUTPUT); digitalWrite(clk, LOW);
  swdWrite(clk, io, 0xFFFFFFFF, 32);           // line reset: >=50 clocks SWDIO high
  swdWrite(clk, io, 0xFFFFFFFF, 32);           //   (64 total)
  swdWrite(clk, io, 0xE79E, 16);               // JTAG-to-SWD switch (0xE79E, LSB first)
  swdWrite(clk, io, 0xFFFFFFFF, 32);           // line reset again: >=50 clocks high
  swdWrite(clk, io, 0xFFFFFFFF, 32);
  swdWrite(clk, io, 0x00, 8);                  // >=2 idle clocks
  // Read request for DP IDCODE: start=1 APnDP=0 RnW=1 A[2:3]=00 parity=1 stop=0 park=1
  swdWrite(clk, io, 0xA5, 8);
  swdClk(clk);                                  // turnaround
  uint32_t ack = swdRead(clk, io, 3);
  if (ack != 0x1) return false;                 // 0b001 = OK
  uint32_t id = swdRead(clk, io, 32);
  swdRead(clk, io, 1);                          // parity
  *idOut = id;
  return id != 0 && id != 0xFFFFFFFF;
}

static void swd_scan() {
  Serial.printf("SWD scan across %d channels — press a key to abort\r\n", cfg.jtag_nch);
  int hits = 0;
  for (int a = 0; a < cfg.jtag_nch; a++)
  for (int b = 0; b < cfg.jtag_nch; b++) {
    if (b == a) continue;
    if (console::aborted()) { Serial.println("aborted."); return; }
    uint32_t id;
    if (trySwd(cfg.jtag_ch[a], cfg.jtag_ch[b], &id)) {
      Serial.printf("  HIT  SWCLK=GP%d SWDIO=GP%d  DPIDR=0x%08lx\r\n",
                    cfg.jtag_ch[a], cfg.jtag_ch[b], (unsigned long)id);
      hits++;
    }
  }
  Serial.printf("scan done, %d candidate pinout(s).\r\n", hits);
}

static void jtag_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd || strcmp(cmd, "scan") == 0) jtag_scan();
  else if (strcmp(cmd, "swd") == 0)      swd_scan();
  else Serial.printf("jtag: unknown '%s'\r\n", cmd);
}

static void jtag_help() {
  Serial.println("jtag — brute-force JTAG/SWD pinout discovery");
  Serial.println("  jtag scan   find TCK/TMS/TDI/TDO via IDCODE (permutes all channels)");
  Serial.println("  jtag swd    find SWCLK/SWDIO via JTAG-to-SWD + DP IDCODE");
  Serial.print  ("  channels: ");
  for (int i = 0; i < cfg.jtag_nch; i++) Serial.printf("GP%d ", cfg.jtag_ch[i]);
  Serial.println("\r\n  (edit CFG_JTAG_CH* in config.h to change)");
}

extern const Module jtagModule = { "jtag", "brute-force JTAG/SWD pinout scan", jtag_run, jtag_help };
