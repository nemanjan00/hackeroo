// ============================================================================
//  pio — PIO state-machine demos (RP2350 has 3 PIO blocks x 4 SMs).
//    * sq   : precise square wave built from a runtime-assembled PIO program
//    * off  : tear the program down
//
//  This is the reference for how the project drives PIO without a build-time
//  pioasm step: instructions are encoded at runtime with pio_encode_*(), so
//  the whole program lives in this .cpp. The glitch module builds on the same
//  pattern for deterministic, jitter-free pulse timing.
// ============================================================================
#include "console.h"
#include "config.h"
#include "util.h"
#include <hardware/pio.h>
#include <hardware/clocks.h>

static PIO  s_pio = pio0;
static int  s_sm  = -1;
static int  s_off = -1;
static struct pio_program s_prog;
static uint16_t s_insns[2];

static void pio_teardown() {
  if (s_sm >= 0) {
    pio_sm_set_enabled(s_pio, s_sm, false);
    if (s_off >= 0) pio_remove_program(s_pio, &s_prog, s_off);
    pio_sm_unclaim(s_pio, s_sm);
    s_sm = -1; s_off = -1;
  }
}

// Two-instruction loop: set pin high (delay), set pin low (delay), wrap.
static void pio_square(int pin, long hz) {
  pio_teardown();
  uint32_t sys = clock_get_hz(clk_sys);

  // Distribute the period over instruction delays + the clock divider.
  // cycles/period = 2 * (1 + delay);  freq = sys / (clkdiv * cycles).
  int delay = 31;                                    // max per-instruction delay
  int cycles = 2 * (1 + delay);                      // = 64
  double clkdiv = (double)sys / ((double)hz * cycles);
  if (clkdiv < 1.0) {                                // signal too fast for max delay
    delay = (int)((double)sys / (2.0 * hz) - 1.0);
    if (delay < 0) delay = 0;
    if (delay > 31) delay = 31;
    cycles = 2 * (1 + delay);
    clkdiv = 1.0;
  }
  if (clkdiv > 65536.0) clkdiv = 65536.0;

  s_insns[0] = pio_encode_set(pio_pins, 1) | pio_encode_delay(delay);
  s_insns[1] = pio_encode_set(pio_pins, 0) | pio_encode_delay(delay);
  s_prog.instructions = s_insns;
  s_prog.length = 2;
  s_prog.origin = -1;

  s_sm  = pio_claim_unused_sm(s_pio, true);
  s_off = pio_add_program(s_pio, &s_prog);

  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_wrap(&c, s_off, s_off + 1);
  sm_config_set_set_pins(&c, pin, 1);
  sm_config_set_clkdiv(&c, (float)clkdiv);

  pio_gpio_init(s_pio, pin);
  pio_sm_set_consecutive_pindirs(s_pio, s_sm, pin, 1, true);
  pio_sm_init(s_pio, s_sm, s_off, &c);
  pio_sm_set_enabled(s_pio, s_sm, true);

  double actual = (double)sys / (clkdiv * cycles);
  Serial.printf("PIO square on GP%d: target ", pin); util::printHz(hz);
  Serial.printf("  actual ~%.1f Hz (clkdiv=%.2f delay=%d)\r\n", actual, clkdiv, delay);
}

static void pio_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd) { Serial.println("pio: need a subcommand (help)"); return; }

  if (strcmp(cmd, "sq") == 0) {
    int  pin = util::optNum(argc, argv, "pin", CFG_PIO_PIN);
    long hz  = util::numOr(util::arg(argc, argv, 1), 1000);
    pio_square(pin, hz);
  } else if (strcmp(cmd, "off") == 0) {
    pio_teardown();
    Serial.println("PIO stopped.");
  } else {
    Serial.printf("pio: unknown '%s'\r\n", cmd);
  }
}

static void pio_help() {
  Serial.println("pio — PIO state-machine demos");
  Serial.println("  pio sq <hz> [pin=N]   precise square wave via PIO");
  Serial.println("  pio off               stop and free the state machine");
  Serial.printf ("  default pin: GP%d\r\n", CFG_PIO_PIN);
}

extern const Module pioModule = { "pio", "PIO square-wave / SM demo", pio_run, pio_help };
