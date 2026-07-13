// ============================================================================
//  config.h — build-wide configuration & default pin map for hackeroo
// ----------------------------------------------------------------------------
//  Every module takes its pins as runtime arguments, but falls back to the
//  defaults below when you don't pass any. Keep these in one place so you can
//  re-wire the board to your target without hunting through the code.
//
//  RP2350A / Pico 2 usable GPIO: GP0..GP28  (GP26/27/28 = ADC0/1/2).
//  Modules are used one-at-a-time from the console, so pin ranges may overlap
//  between modules — just don't wire two active functions to the same pin.
// ============================================================================
#pragma once

#define HACKEROO_VERSION "0.1.0"

// ---- UART (sniff / bridge / autobaud) --------------------------------------
// We listen to the *target's TX* on UART_RX. UART_TX only matters for bridging.
#define CFG_UART_TX      0    // GP0  -> target RX
#define CFG_UART_RX      1    // GP1  <- target TX
#define CFG_UART_BAUD    115200

// ---- I2C -------------------------------------------------------------------
#define CFG_I2C_SDA      4    // GP4
#define CFG_I2C_SCL      5    // GP5
#define CFG_I2C_HZ       100000

// ---- SPI (bus + 25-series flash dumping) -----------------------------------
#define CFG_SPI_SCK      18   // GP18
#define CFG_SPI_MOSI     19   // GP19  (TX)
#define CFG_SPI_MISO     16   // GP16  (RX)
#define CFG_SPI_CS       17   // GP17  (chip select, active low)
#define CFG_SPI_HZ       4000000

// ---- Analog signal generator -----------------------------------------------
#define CFG_SIG_PIN      15   // GP15  PWM output (feed through an RC low-pass
                              //       for an analog-ish waveform / crude DAC)
#define CFG_ADC_PIN      26   // GP26 / ADC0  measurement input

// ---- PIO demo --------------------------------------------------------------
#define CFG_PIO_PIN      14   // GP14

// ---- JTAG / SWD pin scanner (JTAGenum / BlueTag style) ---------------------
// Candidate channels wired to the unknown test points. Order is irrelevant;
// the scanner brute-forces role assignment across all of them.
#define CFG_JTAG_CH0     6
#define CFG_JTAG_CH1     7
#define CFG_JTAG_CH2     8
#define CFG_JTAG_CH3     9
#define CFG_JTAG_CH4     10
#define CFG_JTAG_CH5     11
#define CFG_JTAG_CH6     12
#define CFG_JTAG_CH7     13

// ---- Glitcher (findus / PicoGlitcher style fault injection) ----------------
// TRIGGER: input, edge that starts the delay countdown.
// GLITCH : output to a crowbar MOSFET gate (short VCC/CORE to GND) or MUX.
#define CFG_GLITCH_TRIGGER 2  // GP2  input
#define CFG_GLITCH_OUT     3  // GP3  output -> MOSFET gate
#define CFG_GLITCH_PWR     4  // GP4  optional target power/EN control (reset)

// ---- Power-on LED animation ------------------------------------------------
// A knight-rider sweep across this GPIO range at boot (point it at your LEDs).
// Set CFG_BOOTANIM_ENABLE to 0 to disable. Pins are released (INPUT) after.
#define CFG_BOOTANIM_ENABLE 1
#define CFG_BOOTANIM_FIRST  0
#define CFG_BOOTANIM_LAST   15
#define CFG_BOOTANIM_MS     35   // per-LED step (faster than the console default)

// System clock (RP2350 default). Used to convert cycles <-> nanoseconds.
#define CFG_SYS_HZ       (F_CPU)
