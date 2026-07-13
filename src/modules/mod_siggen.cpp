// ============================================================================
//  siggen — signal generator + analog helpers.
//    * pwm    : square wave at a given frequency/duty (direct on a PWM pin)
//    * wave   : "analog" waveform via PWM+RC low-pass acting as a crude DAC
//               (sine/tri/saw/square) — put an RC filter on the output pin
//    * dc     : hold a DC level (0..1) through the PWM-DAC
//    * adc    : sample the ADC input (single-shot or continuous)
//
//  The RP2350 has no true DAC, so "analog" output = high-frequency PWM fed
//  through an external RC low-pass (e.g. 1k + 10nF). Good enough for signal
//  injection / bias / simple waveform generation while hacking.
// ============================================================================
#include "console.h"
#include "config.h"
#include "util.h"
#include <hardware/pwm.h>
#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <math.h>
#include "pins.h"

// A plain square wave straight out of the PWM slice.
// period = div * (wrap+1) system clocks; solve for a valid (div, wrap) pair.
// div: 1..255, wrap+1: 2..65536. All maths in double to avoid 32-bit overflow.
static void sig_pwm(long hz, float duty) {
  if (duty < 0) duty = 0;
  if (duty > 1) duty = 1;
  uint32_t sys = clock_get_hz(clk_sys);
  double total = (double)sys / (double)hz;        // = div * (wrap+1)
  double div = total / 65536.0;                   // smallest div that keeps wrap in range
  if (div < 1.0)   div = 1.0;
  if (div > 255.0) div = 255.0;
  uint32_t period = (uint32_t)(total / div + 0.5);
  if (period < 2)      period = 2;
  if (period > 65536)  period = 65536;
  uint32_t wrap = period - 1;

  gpio_set_function(cfg.sig_pin, GPIO_FUNC_PWM);
  uint slice = pwm_gpio_to_slice_num(cfg.sig_pin);
  uint chan  = pwm_gpio_to_channel(cfg.sig_pin);
  pwm_set_clkdiv(slice, (float)div);
  pwm_set_wrap(slice, wrap);
  pwm_set_chan_level(slice, chan, (uint32_t)(period * duty));
  pwm_set_enabled(slice, true);

  double actual = (double)sys / (div * (double)period);
  Serial.printf("PWM on GP%d: target ", cfg.sig_pin); util::printHz(hz);
  Serial.printf(" duty %.0f%%  actual ~%.1f Hz (div=%.2f wrap=%lu)\r\n",
                duty * 100, actual, div, (unsigned long)wrap);
}

// PWM-as-DAC: run PWM fast (11-bit) and vary duty to set the average voltage.
static void dac_setup() {
  gpio_set_function(cfg.sig_pin, GPIO_FUNC_PWM);
  uint slice = pwm_gpio_to_slice_num(cfg.sig_pin);
  pwm_set_clkdiv(slice, 1.0f);
  pwm_set_wrap(slice, 2047);            // 11-bit, ~73 kHz carrier @150MHz
  pwm_set_enabled(slice, true);
}
static inline void dac_write(float level) {
  if (level < 0) level = 0; if (level > 1) level = 1;
  pwm_set_chan_level(pwm_gpio_to_slice_num(cfg.sig_pin), pwm_gpio_to_channel(cfg.sig_pin),
                     (uint32_t)(level * 2047));
}

static void sig_dc(float level) {
  dac_setup();
  dac_write(level);
  Serial.printf("DC ~%.3f V on GP%d (of 3.3V) via PWM-DAC\r\n", level * 3.3f, cfg.sig_pin);
}

static void sig_wave(const char* shape, long hz) {
  dac_setup();
  Serial.printf("%s wave ~", shape); util::printHz(hz);
  Serial.printf(" on GP%d (RC-filter the pin!) — press a key to stop\r\n", cfg.sig_pin);
  const int N = 64;                     // samples per period
  float lut[N];
  for (int i = 0; i < N; i++) {
    float ph = (float)i / N;
    if      (!strcmp(shape, "sine")) lut[i] = 0.5f + 0.5f * sinf(2 * PI * ph);
    else if (!strcmp(shape, "tri"))  lut[i] = ph < 0.5f ? 2 * ph : 2 * (1 - ph);
    else if (!strcmp(shape, "saw"))  lut[i] = ph;
    else                              lut[i] = ph < 0.5f ? 0.0f : 1.0f;  // square
  }
  uint32_t step_us = 1000000UL / (hz * N);
  if (step_us == 0) step_us = 1;
  int i = 0;
  while (!console::aborted()) {
    dac_write(lut[i]);
    i = (i + 1) % N;
    delayMicroseconds(step_us);
  }
  Serial.println("stopped.");
}

static void sig_adc(int argc, char** argv) {
  int ch = util::optNum(argc, argv, "ch", 0);        // ADC0..2 = GP26..28
  bool cont = util::flag(argc, argv, "-c");
  adc_init();
  adc_gpio_init(26 + ch);
  adc_select_input(ch);
  do {
    uint32_t raw = adc_read();
    Serial.printf("ADC%d = %lu  (%.3f V)\r\n", ch, (unsigned long)raw, raw * 3.3f / 4096.0f);
    if (cont) delay(200);
  } while (cont && !console::aborted());
}

static void sig_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd) { Serial.println("siggen: need a subcommand (help)"); return; }
  cfg.sig_pin = util::optNum(argc, argv, "pin", cfg.sig_pin);

  if (strcmp(cmd, "pwm") == 0) {
    long hz = util::numOr(util::arg(argc, argv, 1), 1000);
    float duty = util::numOr(util::opt(argc, argv, "duty", "50"), 50) / 100.0f;
    sig_pwm(hz, duty);
  } else if (strcmp(cmd, "wave") == 0) {
    const char* shape = util::arg(argc, argv, 1); if (!shape) shape = "sine";
    long hz = util::numOr(util::arg(argc, argv, 2), 100);
    sig_wave(shape, hz);
  } else if (strcmp(cmd, "dc") == 0) {
    sig_dc(util::numOr(util::arg(argc, argv, 1), 0) / 100.0f);
  } else if (strcmp(cmd, "adc") == 0) {
    sig_adc(argc, argv);
  } else if (strcmp(cmd, "off") == 0) {
    pwm_set_enabled(pwm_gpio_to_slice_num(cfg.sig_pin), false);
    Serial.printf("PWM off on GP%d\r\n", cfg.sig_pin);
  } else {
    Serial.printf("siggen: unknown '%s'\r\n", cmd);
  }
}

static void sig_help() {
  Serial.println("siggen — waveform/DAC/ADC   (opt: pin=N)");
  Serial.println("  siggen pwm <hz> [duty=PCT]   hardware square wave");
  Serial.println("  siggen wave sine|tri|saw|square <hz>   PWM-DAC waveform (RC filter!)");
  Serial.println("  siggen dc <percent>          hold DC level (0..100)");
  Serial.println("  siggen adc [ch=0] [-c]        read ADC (0..2 = GP26..28)");
  Serial.println("  siggen off                   stop output");
  Serial.printf ("  pins (pins module): out=GP%d  adc-in=GP%d\r\n", cfg.sig_pin, cfg.adc_pin);
}

extern const Module siggenModule = { "siggen", "PWM/DAC waveform gen + ADC read", sig_run, sig_help };
