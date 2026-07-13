// ============================================================================
//  pins.h — the single source of truth for the runtime pin map.
//
//  Every module reads its pins from the global `cfg` (not from hard-coded
//  values), so you can re-assign any pin at runtime with the `pins` module and
//  persist it to flash. config.h still holds the compile-time DEFAULTS that
//  `cfg` falls back to on first boot / after `pins reset`.
// ============================================================================
#pragma once
#include <Arduino.h>

struct PinConfig {
  uint32_t magic;                 // validity marker for the flash copy

  uint8_t  uart_tx, uart_rx;   uint32_t uart_baud;
  uint8_t  i2c_sda, i2c_scl;   uint32_t i2c_hz;
  uint8_t  spi_sck, spi_mosi, spi_miso, spi_cs;  uint32_t spi_hz;  uint8_t spi_mode;
  uint8_t  sig_pin, adc_pin;
  uint8_t  pio_pin;
  uint8_t  jtag_ch[8];         uint8_t  jtag_nch;
  uint8_t  glitch_trig, glitch_out, glitch_pwr;
  uint8_t  bootanim_enable, bootanim_first, bootanim_last;  uint16_t bootanim_ms;
};

extern PinConfig cfg;

namespace pins {
void begin();          // load from flash, or defaults if none/invalid
bool save();           // persist current cfg to flash
void loadDefaults();   // reset cfg to config.h values (in RAM only)
}
