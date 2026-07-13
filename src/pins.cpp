#include "pins.h"
#include "config.h"
#include <EEPROM.h>

PinConfig cfg;

// Bump the magic whenever the struct layout changes so stale flash is ignored.
static const uint32_t MAGIC = 0x484B5232;   // 'HKR2'
static const int      EE_SIZE = 512;

void pins::loadDefaults() {
  cfg.magic = MAGIC;
  cfg.uart_tx = CFG_UART_TX; cfg.uart_rx = CFG_UART_RX; cfg.uart_baud = CFG_UART_BAUD;
  cfg.i2c_sda = CFG_I2C_SDA; cfg.i2c_scl = CFG_I2C_SCL; cfg.i2c_hz = CFG_I2C_HZ;
  cfg.spi_sck = CFG_SPI_SCK; cfg.spi_mosi = CFG_SPI_MOSI; cfg.spi_miso = CFG_SPI_MISO;
  cfg.spi_cs = CFG_SPI_CS;   cfg.spi_hz = CFG_SPI_HZ;     cfg.spi_mode = 0;
  cfg.sig_pin = CFG_SIG_PIN; cfg.adc_pin = CFG_ADC_PIN;
  cfg.pio_pin = CFG_PIO_PIN;
  const uint8_t ch[8] = { CFG_JTAG_CH0, CFG_JTAG_CH1, CFG_JTAG_CH2, CFG_JTAG_CH3,
                          CFG_JTAG_CH4, CFG_JTAG_CH5, CFG_JTAG_CH6, CFG_JTAG_CH7 };
  for (int i = 0; i < 8; i++) cfg.jtag_ch[i] = ch[i];
  cfg.jtag_nch = 8;
  cfg.glitch_trig = CFG_GLITCH_TRIGGER; cfg.glitch_out = CFG_GLITCH_OUT; cfg.glitch_pwr = CFG_GLITCH_PWR;
  cfg.bootanim_enable = CFG_BOOTANIM_ENABLE; cfg.bootanim_first = CFG_BOOTANIM_FIRST;
  cfg.bootanim_last = CFG_BOOTANIM_LAST;     cfg.bootanim_ms = CFG_BOOTANIM_MS;
}

void pins::begin() {
  EEPROM.begin(EE_SIZE);
  PinConfig tmp;
  EEPROM.get(0, tmp);
  if (tmp.magic == MAGIC) cfg = tmp;   // restore saved wiring
  else                    loadDefaults();
}

bool pins::save() {
  cfg.magic = MAGIC;
  EEPROM.put(0, cfg);
  return EEPROM.commit();
}
