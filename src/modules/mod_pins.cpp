// ============================================================================
//  pins — view / set / save / reset the runtime pin map (see pins.h).
//    pins                 show the whole map
//    pins set <name> <n>  assign a value (e.g. pins set i2c.sda 20)
//    pins save            persist to flash (survives reboot)
//    pins reset           restore compile-time defaults (then 'pins save')
//    pins load            re-read the saved map from flash
// ============================================================================
#include "console.h"
#include "pins.h"
#include "util.h"
#include <string.h>
#include <stdio.h>

// Name -> field mapping. width: 1=uint8, 2=uint16, 4=uint32.
struct Ent { const char* name; void* ptr; uint8_t width; };

static const Ent ENTS[] = {
  {"uart.tx", &cfg.uart_tx, 1}, {"uart.rx", &cfg.uart_rx, 1}, {"uart.baud", &cfg.uart_baud, 4},
  {"i2c.sda", &cfg.i2c_sda, 1}, {"i2c.scl", &cfg.i2c_scl, 1}, {"i2c.hz", &cfg.i2c_hz, 4},
  {"spi.sck", &cfg.spi_sck, 1}, {"spi.mosi", &cfg.spi_mosi, 1}, {"spi.miso", &cfg.spi_miso, 1},
  {"spi.cs", &cfg.spi_cs, 1}, {"spi.hz", &cfg.spi_hz, 4}, {"spi.mode", &cfg.spi_mode, 1},
  {"sig.pin", &cfg.sig_pin, 1}, {"adc.pin", &cfg.adc_pin, 1},
  {"pio.pin", &cfg.pio_pin, 1},
  {"jtag.ch0", &cfg.jtag_ch[0], 1}, {"jtag.ch1", &cfg.jtag_ch[1], 1},
  {"jtag.ch2", &cfg.jtag_ch[2], 1}, {"jtag.ch3", &cfg.jtag_ch[3], 1},
  {"jtag.ch4", &cfg.jtag_ch[4], 1}, {"jtag.ch5", &cfg.jtag_ch[5], 1},
  {"jtag.ch6", &cfg.jtag_ch[6], 1}, {"jtag.ch7", &cfg.jtag_ch[7], 1},
  {"jtag.nch", &cfg.jtag_nch, 1},
  {"glitch.trig", &cfg.glitch_trig, 1}, {"glitch.out", &cfg.glitch_out, 1}, {"glitch.pwr", &cfg.glitch_pwr, 1},
  {"boot.enable", &cfg.bootanim_enable, 1}, {"boot.first", &cfg.bootanim_first, 1},
  {"boot.last", &cfg.bootanim_last, 1}, {"boot.ms", &cfg.bootanim_ms, 2},
};
static const int NENT = sizeof(ENTS) / sizeof(ENTS[0]);

static uint32_t entGet(const Ent& e) {
  switch (e.width) {
    case 1:  return *(uint8_t*)e.ptr;
    case 2:  return *(uint16_t*)e.ptr;
    default: return *(uint32_t*)e.ptr;
  }
}
static void entSet(const Ent& e, uint32_t v) {
  switch (e.width) {
    case 1:  *(uint8_t*)e.ptr  = (uint8_t)v;  break;
    case 2:  *(uint16_t*)e.ptr = (uint16_t)v; break;
    default: *(uint32_t*)e.ptr = v;           break;
  }
}

// ---- ASCII board pinout ----------------------------------------------------
// Physical 40-pin layout of the Pico 2 (USB at the top). gpio<0 => power/GND.
struct PhysPin { int8_t gpio; const char* name; };
static const PhysPin LEFT[20] = {
  {0,0},{1,0},{-1,"GND"},{2,0},{3,0},{4,0},{5,0},{-1,"GND"},{6,0},{7,0},
  {8,0},{9,0},{-1,"GND"},{10,0},{11,0},{12,0},{13,0},{-1,"GND"},{14,0},{15,0},
};
static const PhysPin RIGHT[20] = {   // top (pin 40) down to bottom (pin 21)
  {-1,"VBUS"},{-1,"VSYS"},{-1,"GND"},{-1,"3V3EN"},{-1,"3V3"},{-1,"VREF"},
  {28,0},{-1,"AGND"},{27,0},{26,0},{-1,"RUN"},{22,0},{-1,"GND"},{21,0},
  {20,0},{19,0},{18,0},{-1,"GND"},{17,0},{16,0},
};

static void appendTok(char* b, size_t n, const char* t) {
  if (b[0]) strncat(b, ",", n - strlen(b) - 1);
  strncat(b, t, n - strlen(b) - 1);
}

// Build a comma-joined list of every function currently mapped to GPIO g.
static void funcLabel(int g, char* b, size_t n) {
  b[0] = 0;
  if (g < 0) return;
  if (g == cfg.uart_tx)  appendTok(b, n, "UART.TX");
  if (g == cfg.uart_rx)  appendTok(b, n, "UART.RX");
  if (g == cfg.i2c_sda)  appendTok(b, n, "I2C.SDA");
  if (g == cfg.i2c_scl)  appendTok(b, n, "I2C.SCL");
  if (g == cfg.spi_sck)  appendTok(b, n, "SPI.SCK");
  if (g == cfg.spi_mosi) appendTok(b, n, "SPI.MOSI");
  if (g == cfg.spi_miso) appendTok(b, n, "SPI.MISO");
  if (g == cfg.spi_cs)   appendTok(b, n, "SPI.CS");
  if (g == cfg.sig_pin)  appendTok(b, n, "SIG");
  if (g == cfg.adc_pin)  appendTok(b, n, "ADC");
  if (g == cfg.pio_pin)  appendTok(b, n, "PIO");
  for (int i = 0; i < cfg.jtag_nch; i++)
    if (g == cfg.jtag_ch[i]) { appendTok(b, n, "JTAG"); break; }
  if (g == cfg.glitch_trig) appendTok(b, n, "GL.TRIG");
  if (g == cfg.glitch_out)  appendTok(b, n, "GL.OUT");
  if (g == cfg.glitch_pwr)  appendTok(b, n, "GL.PWR");
}

static void nameOf(const PhysPin& p, char* out) {
  if (p.gpio >= 0) sprintf(out, "GP%d", p.gpio);
  else             strcpy(out, p.name);
}

static void pins_map() {
  Serial.println();
  Serial.println("                    +----- USB -----+");
  char ln[8], rn[8], lf[40], rf[40];
  for (int r = 0; r < 20; r++) {
    nameOf(LEFT[r], ln);  nameOf(RIGHT[r], rn);
    funcLabel(LEFT[r].gpio,  lf, sizeof(lf));
    funcLabel(RIGHT[r].gpio, rf, sizeof(rf));
    Serial.printf("%18s %-5s|%2d     %2d|%-6s %s\r\n",
                  lf, ln, r + 1, 40 - r, rn, rf);
  }
  Serial.println("                    +---------------+");
  Serial.println("(edit with 'pins set <name> <gpio>', then 'pins save')");
}

static void pins_show() {
  Serial.println("pin map (name = value):");
  for (int i = 0; i < NENT; i++)
    Serial.printf("  %-12s %lu\r\n", ENTS[i].name, (unsigned long)entGet(ENTS[i]));
  Serial.println("edit: 'pins set <name> <n>'   persist: 'pins save'");
}

static void pins_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd || strcmp(cmd, "show") == 0) { pins_show(); return; }
  if (strcmp(cmd, "map") == 0) { pins_map(); return; }

  if (strcmp(cmd, "set") == 0) {
    const char* name = util::arg(argc, argv, 1);
    long val;
    if (!name || !util::parseNum(util::arg(argc, argv, 2), &val)) {
      Serial.println("usage: pins set <name> <value>"); return;
    }
    for (int i = 0; i < NENT; i++) {
      if (strcmp(ENTS[i].name, name) == 0) {
        entSet(ENTS[i], (uint32_t)val);
        Serial.printf("%s = %lu  (unsaved — 'pins save' to persist)\r\n",
                      name, (unsigned long)entGet(ENTS[i]));
        return;
      }
    }
    Serial.printf("unknown pin name '%s' — 'pins' lists them\r\n", name);
  } else if (strcmp(cmd, "save") == 0) {
    Serial.println(pins::save() ? "saved to flash." : "save FAILED.");
  } else if (strcmp(cmd, "reset") == 0) {
    pins::loadDefaults();
    Serial.println("reset to compile-time defaults ('pins save' to persist).");
  } else if (strcmp(cmd, "load") == 0) {
    pins::begin();
    Serial.println("reloaded from flash.");
  } else {
    Serial.printf("pins: unknown '%s'\r\n", cmd);
  }
}

static void pins_help() {
  Serial.println("pins — runtime pin map (persisted to flash)");
  Serial.println("  pins show            list the whole map (name = value)");
  Serial.println("  pins map             ASCII board diagram with pin functions");
  Serial.println("  pins set <name> <n>  e.g. pins set i2c.sda 20");
  Serial.println("  pins save | load | reset");
  Serial.println("  names: uart.* i2c.* spi.* sig.pin adc.pin pio.pin jtag.chN jtag.nch");
  Serial.println("         glitch.* boot.*");
}

extern const Module pinsModule = { "pins", "view/set/save the runtime pin map", pins_run, pins_help };
