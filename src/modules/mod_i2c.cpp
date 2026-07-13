// ============================================================================
//  i2c — bus scanner + register read/write + block dump.
//  Uses I2C0 (Wire). Default pins in config.h; override with sda=/scl=/hz=.
// ============================================================================
#include "console.h"
#include "config.h"
#include "util.h"
#include <Wire.h>
#include <hardware/gpio.h>

static int   s_sda = CFG_I2C_SDA, s_scl = CFG_I2C_SCL;
static long  s_hz  = CFG_I2C_HZ;
static bool  s_up  = false;

static void i2c_begin(int argc, char** argv) {
  s_sda = util::optNum(argc, argv, "sda", s_sda);
  s_scl = util::optNum(argc, argv, "scl", s_scl);
  s_hz  = util::optNum(argc, argv, "hz",  s_hz);
  if (s_up) Wire.end();
  Wire.setSDA(s_sda);
  Wire.setSCL(s_scl);
  Wire.setClock(s_hz);
  Wire.begin();
  s_up = true;
}

static void i2c_scan() {
  Serial.printf("scanning I2C  (SDA=GP%d SCL=GP%d ", s_sda, s_scl);
  util::printHz(s_hz); Serial.println(")");
  Serial.println("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
  int found = 0;
  for (int hi = 0; hi < 8; hi++) {
    Serial.printf("%02x: ", hi << 4);
    for (int lo = 0; lo < 16; lo++) {
      int addr = (hi << 4) | lo;
      if (addr < 0x08 || addr > 0x77) { Serial.print("   "); continue; }
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) { Serial.printf("%02x ", addr); found++; }
      else                             Serial.print("-- ");
    }
    Serial.println();
  }
  Serial.printf("%d device(s) found.\r\n", found);
}

// Read `len` bytes starting at register `reg` from device `addr`.
static void i2c_read(int addr, int reg, int len) {
  uint8_t buf[256];
  if (len > (int)sizeof(buf)) len = sizeof(buf);
  if (reg >= 0) {
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)reg);
    if (Wire.endTransmission(false) != 0) { Serial.println("i2c: NAK on reg write"); return; }
  }
  int got = Wire.requestFrom(addr, len);
  for (int i = 0; i < got && Wire.available(); i++) buf[i] = Wire.read();
  Serial.printf("read %d byte(s) from 0x%02x", got, addr);
  if (reg >= 0) Serial.printf(" @reg 0x%02x", reg);
  Serial.println(":");
  util::hexdump(buf, got, reg >= 0 ? reg : 0);
}

static void i2c_write(int addr, int argc, char** argv, int firstByte) {
  Wire.beginTransmission(addr);
  int n = 0;
  for (int i = firstByte; i < argc; i++) {
    long b;
    if (!util::parseNum(argv[i], &b)) continue;
    Wire.write((uint8_t)b);
    n++;
  }
  int rc = Wire.endTransmission();
  Serial.printf("wrote %d byte(s) to 0x%02x — %s\r\n", n, addr, rc == 0 ? "ACK" : "NAK");
}

// Passive bus sniffer: read SDA/SCL as plain inputs (the bus supplies its own
// pull-ups) and decode START/STOP + bytes + ACK/NACK. CPU-polled, so reliable
// for standard-mode (100 kHz); marginal at 400 kHz. First byte after START is
// the address<<1 | R/W.
static void i2c_sniff() {
  pinMode(s_sda, INPUT); pinMode(s_scl, INPUT);
  Serial.printf("sniffing I2C SDA=GP%d SCL=GP%d — passive; key to stop\r\n", s_sda, s_scl);
  Serial.println("(S=start P=stop; byte+ =ACK, byte- =NACK; 1st byte = addr<<1|rw)");
  const uint32_t sdaM = 1u << s_sda, sclM = 1u << s_scl;
  uint32_t g = gpio_get_all();
  int pscl = (g & sclM) ? 1 : 0, psda = (g & sdaM) ? 1 : 0;
  uint8_t byte = 0; int nb = 0; bool inFrame = false, awaitAck = false; uint32_t it = 0;
  while (true) {
    if ((++it & 0x3FF) == 0 && console::aborted()) break;
    g = gpio_get_all();
    int scl = (g & sclM) ? 1 : 0, sda = (g & sdaM) ? 1 : 0;
    if (pscl && scl) {                          // SCL high: SDA edge = START/STOP
      if (psda && !sda)      { Serial.print("\r\nS "); inFrame = true;  nb = 0; byte = 0; awaitAck = false; }
      else if (!psda && sda) { Serial.print("P");      inFrame = false; }
    }
    if (!pscl && scl && inFrame) {              // SCL rising edge: sample a bit
      if (!awaitAck) { byte = (byte << 1) | sda; if (++nb == 8) awaitAck = true; }
      else           { Serial.printf("%02x%c ", byte, sda ? '-' : '+'); byte = 0; nb = 0; awaitAck = false; }
    }
    pscl = scl; psda = sda;
  }
  Serial.println("\r\nstopped.");
}

static void i2c_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd) { Serial.println("i2c: need a subcommand (help)"); return; }

  // Sniff must NOT drive the bus — parse pins and read as plain GPIO.
  if (strcmp(cmd, "sniff") == 0) {
    s_sda = util::optNum(argc, argv, "sda", s_sda);
    s_scl = util::optNum(argc, argv, "scl", s_scl);
    if (s_up) { Wire.end(); s_up = false; }
    i2c_sniff();
    return;
  }
  i2c_begin(argc, argv);

  if (strcmp(cmd, "scan") == 0) {
    i2c_scan();
  } else if (strcmp(cmd, "read") == 0) {
    long addr = util::numOr(util::arg(argc, argv, 1), -1);
    long reg  = util::numOr(util::arg(argc, argv, 2), -1);
    long len  = util::numOr(util::arg(argc, argv, 3), 1);
    if (addr < 0) { Serial.println("i2c read <addr> [reg] [len]"); return; }
    i2c_read(addr, reg, len);
  } else if (strcmp(cmd, "write") == 0) {
    long addr = util::numOr(util::arg(argc, argv, 1), -1);
    if (addr < 0) { Serial.println("i2c write <addr> <byte...>"); return; }
    i2c_write(addr, argc, argv, 2);
  } else if (strcmp(cmd, "dump") == 0) {
    long addr = util::numOr(util::arg(argc, argv, 1), -1);
    long len  = util::numOr(util::arg(argc, argv, 2), 256);
    if (addr < 0) { Serial.println("i2c dump <addr> [len]"); return; }
    for (long off = 0; off < len && !console::aborted(); off += 16)
      i2c_read(addr, off, min(16L, len - off));
  } else {
    Serial.printf("i2c: unknown '%s'\r\n", cmd);
  }
}

static void i2c_help() {
  Serial.println("i2c — I2C0 bus tools   (opts: sda=N scl=N hz=N)");
  Serial.println("  i2c scan                     probe 0x08..0x77");
  Serial.println("  i2c read <addr> [reg] [len]  read len bytes (opt. from reg)");
  Serial.println("  i2c write <addr> <byte...>   write raw bytes");
  Serial.println("  i2c dump <addr> [len]        hexdump len bytes from reg 0");
  Serial.println("  i2c sniff                    passive bus monitor (<=100 kHz)");
  Serial.printf ("  defaults: SDA=GP%d SCL=GP%d %ld Hz\r\n", CFG_I2C_SDA, CFG_I2C_SCL, (long)CFG_I2C_HZ);
}

extern const Module i2cModule = { "i2c", "scan bus, read/write/dump devices", i2c_run, i2c_help };
