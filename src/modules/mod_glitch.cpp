// ============================================================================
//  glitch — voltage/crowbar fault injection (findus / PicoGlitcher /
//           raiden-pico style). A PIO state machine gives jitter-free timing:
//
//              arm ─▶ wait(trigger) ─▶ delay(N cyc) ─▶ pulse(W cyc)
//
//  Timing is in system-clock cycles (1 cycle = 1/F_CPU; ~6.67 ns @150 MHz).
//  The glitch output drives a crowbar MOSFET gate that briefly shorts the
//  target's core/VCC rail (or gates an external power supply / EM injector).
//
//  Parameters (cf. raiden-pico's PAUSE/WIDTH):
//      delay  — cycles from trigger edge to the pulse
//      width  — pulse duration in cycles
//  Triggers: manual (fire from console), rising/falling edge on the trig pin.
//
//  ██ SAFETY ██  Fault injection can brick or destroy hardware. Only target
//  devices you own and are authorised to test. Keep 'arm' off until wired.
//
//  Boilerplate scope: single pulse per trigger, re-armable, plus a delay
//  sweep. COUNT>1 bursts / GAP / UART-triggering / ADC depth-gating are left
//  as clearly-marked extension points (see raiden-pico for a full impl).
// ============================================================================
#include "console.h"
#include "util.h"
#include "pins.h"
#include <hardware/pio.h>
#include <hardware/clocks.h>

enum Edge { EDGE_MANUAL, EDGE_RISING, EDGE_FALLING };

static PIO   s_pio   = pio1;
static int   s_sm    = -1;
static int   s_off   = -1;
static struct pio_program s_prog;
static uint16_t s_insns[11];

static uint32_t s_delay = 100;     // cycles
static uint32_t s_width = 10;      // cycles
static Edge     s_edge  = EDGE_MANUAL;
static bool     s_armed = false;

static float cyc_to_ns(uint32_t c) { return (float)c * 1e9f / (float)clock_get_hz(clk_sys); }
static uint32_t ns_to_cyc(float ns) { return (uint32_t)(ns * (float)clock_get_hz(clk_sys) / 1e9f + 0.5f); }

// Build the 11-instruction program for the current edge/pins and load it.
static void glitch_load() {
  if (s_sm >= 0) {
    pio_sm_set_enabled(s_pio, s_sm, false);
    if (s_off >= 0) pio_remove_program(s_pio, &s_prog, s_off);
    pio_sm_unclaim(s_pio, s_sm);
    s_sm = -1; s_off = -1;
  }
  // Wait polarities per edge (see program layout below).
  bool waitA, waitB;
  switch (s_edge) {
    case EDGE_RISING:  waitA = 0; waitB = 1; break;   // low then high
    case EDGE_FALLING: waitA = 1; waitB = 0; break;   // high then low
    default:           waitA = 1; waitB = 1; break;   // manual: CPU drives high
  }
  //  0 pull            OSR = delay count
  //  1 mov x, osr
  //  2 wait A gpio trig
  //  3 wait B gpio trig   (edge = A->B transition)
  //  4 jmp x-- 4          delay loop  (delay+1 cycles)
  //  5 pull            OSR = width count
  //  6 mov x, osr
  //  7 set pins, 1        glitch ON
  //  8 jmp x-- 8          width loop
  //  9 set pins, 0        glitch OFF
  // 10 push               notify CPU, then wrap -> re-arm
  s_insns[0]  = pio_encode_pull(false, true);
  s_insns[1]  = pio_encode_mov(pio_x, pio_osr);
  s_insns[2]  = pio_encode_wait_gpio(waitA, cfg.glitch_trig);
  s_insns[3]  = pio_encode_wait_gpio(waitB, cfg.glitch_trig);
  s_insns[4]  = pio_encode_jmp_x_dec(4);
  s_insns[5]  = pio_encode_pull(false, true);
  s_insns[6]  = pio_encode_mov(pio_x, pio_osr);
  s_insns[7]  = pio_encode_set(pio_pins, 1);
  s_insns[8]  = pio_encode_jmp_x_dec(8);
  s_insns[9]  = pio_encode_set(pio_pins, 0);
  s_insns[10] = pio_encode_push(false, true);
  s_prog.instructions = s_insns;
  s_prog.length = 11;
  s_prog.origin = -1;

  s_sm  = pio_claim_unused_sm(s_pio, true);
  s_off = pio_add_program(s_pio, &s_prog);

  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_wrap(&c, s_off + 0, s_off + 10);
  sm_config_set_set_pins(&c, cfg.glitch_out, 1);
  sm_config_set_clkdiv(&c, 1.0f);              // full system clock

  // Glitch output pin driven by PIO.
  pio_gpio_init(s_pio, cfg.glitch_out);
  pio_sm_set_consecutive_pindirs(s_pio, s_sm, cfg.glitch_out, 1, true);
  // Trigger pin: input for external edges, output(low) for manual firing.
  if (s_edge == EDGE_MANUAL) { pinMode(cfg.glitch_trig, OUTPUT); digitalWrite(cfg.glitch_trig, LOW); }
  else                        pinMode(cfg.glitch_trig, INPUT);

  pio_sm_init(s_pio, s_sm, s_off, &c);
  pio_sm_set_enabled(s_pio, s_sm, true);
}

// Reset the SM to a known clean state: both FIFOs empty, scratch/shift regs
// cleared, PC back at the program start (the pull). Without this, a fire that
// times out (external trigger never arrives) leaves stale delay/width loaded
// and queued, corrupting later fires and eventually deadlocking put_blocking.
static void prime() {
  pio_sm_set_enabled(s_pio, s_sm, false);
  pio_sm_clear_fifos(s_pio, s_sm);
  pio_sm_restart(s_pio, s_sm);
  pio_sm_exec(s_pio, s_sm, pio_encode_jmp(s_off));   // PC -> program start
  if (s_edge == EDGE_MANUAL) digitalWrite(cfg.glitch_trig, LOW);
  pio_sm_set_enabled(s_pio, s_sm, true);
}

// Load the (delay,width) counts and wait for the pulse to complete.
// Returns true if the SM reported done within timeout_ms.
static bool fireOnce(uint32_t timeout_ms) {
  prime();
  uint32_t d = s_delay ? s_delay - 1 : 0;
  uint32_t w = s_width ? s_width - 1 : 0;
  pio_sm_put_blocking(s_pio, s_sm, d);
  pio_sm_put_blocking(s_pio, s_sm, w);
  if (s_edge == EDGE_MANUAL) { digitalWrite(cfg.glitch_trig, HIGH); }  // fire!
  uint32_t t0 = millis();
  bool done = false;
  while ((millis() - t0) < timeout_ms) {
    if (!pio_sm_is_rx_fifo_empty(s_pio, s_sm)) { pio_sm_get(s_pio, s_sm); done = true; break; }
  }
  if (s_edge == EDGE_MANUAL) { digitalWrite(cfg.glitch_trig, LOW); }
  return done;
}

static void glitch_status() {
  const char* em = s_edge == EDGE_MANUAL ? "manual" : (s_edge == EDGE_RISING ? "rising" : "falling");
  Serial.printf("armed   : %s\r\n", s_armed ? "YES" : "no");
  Serial.printf("trigger : %s\r\n", em);
  Serial.printf("delay   : %lu cyc (%.1f ns)\r\n", (unsigned long)s_delay, cyc_to_ns(s_delay));
  Serial.printf("width   : %lu cyc (%.1f ns)\r\n", (unsigned long)s_width, cyc_to_ns(s_width));
  Serial.printf("pins    : trig=GP%d out=GP%d pwr=GP%d\r\n", cfg.glitch_trig, cfg.glitch_out, cfg.glitch_pwr);
}

static void glitch_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd) { glitch_status(); return; }

  if (strcmp(cmd, "set") == 0) {
    cfg.glitch_trig = util::optNum(argc, argv, "trig", cfg.glitch_trig);
    cfg.glitch_out  = util::optNum(argc, argv, "out",  cfg.glitch_out);
    cfg.glitch_pwr  = util::optNum(argc, argv, "pwr",  cfg.glitch_pwr);
    if (util::opt(argc, argv, "delayns", nullptr)) s_delay = ns_to_cyc(util::optNum(argc, argv, "delayns", 0));
    if (util::opt(argc, argv, "widthns", nullptr)) s_width = ns_to_cyc(util::optNum(argc, argv, "widthns", 0));
    s_delay = util::optNum(argc, argv, "delay", s_delay);
    s_width = util::optNum(argc, argv, "width", s_width);
    glitch_status();
    if (s_armed) glitch_load();     // re-load with new pins/edge
  } else if (strcmp(cmd, "trigger") == 0) {
    const char* m = util::arg(argc, argv, 1);
    if      (m && !strcmp(m, "manual"))  s_edge = EDGE_MANUAL;
    else if (m && !strcmp(m, "rising"))  s_edge = EDGE_RISING;
    else if (m && !strcmp(m, "falling")) s_edge = EDGE_FALLING;
    else { Serial.println("trigger manual|rising|falling"); return; }
    if (s_armed) glitch_load();
    Serial.printf("trigger = %s\r\n", m);
  } else if (strcmp(cmd, "arm") == 0) {
    glitch_load();
    s_armed = true;
    Serial.println("ARMED. glitch out is live. 'glitch off' to disarm.");
  } else if (strcmp(cmd, "off") == 0) {
    if (s_sm >= 0) {
      pio_sm_set_enabled(s_pio, s_sm, false);
      if (s_off >= 0) pio_remove_program(s_pio, &s_prog, s_off);
      pio_sm_unclaim(s_pio, s_sm);
      s_sm = -1; s_off = -1;
    }
    pinMode(cfg.glitch_out, INPUT);           // release the line
    s_armed = false;
    Serial.println("disarmed.");
  } else if (strcmp(cmd, "fire") == 0) {
    if (!s_armed) { Serial.println("not armed — 'glitch arm' first"); return; }
    Serial.printf("firing (delay=%lu width=%lu cyc)...\r\n",
                  (unsigned long)s_delay, (unsigned long)s_width);
    bool ok = fireOnce(s_edge == EDGE_MANUAL ? 100 : 3000);
    Serial.println(ok ? "pulse done." : "timeout (no trigger?).");
  } else if (strcmp(cmd, "sweep") == 0) {
    if (!s_armed) { Serial.println("not armed — 'glitch arm' first"); return; }
    long start = util::numOr(util::arg(argc, argv, 1), 0);
    long end   = util::numOr(util::arg(argc, argv, 2), start + 100);
    long step  = util::numOr(util::arg(argc, argv, 3), 1);
    long tries = util::optNum(argc, argv, "tries", 1);
    if (step <= 0) step = 1;
    Serial.printf("sweeping delay %ld..%ld step %ld, %ld try/step — key to abort\r\n",
                  start, end, step, tries);
    for (long d = start; d <= end; d += step) {
      if (console::aborted()) { Serial.println("aborted."); break; }
      s_delay = d;
      for (long t = 0; t < tries; t++) {
        bool ok = fireOnce(s_edge == EDGE_MANUAL ? 100 : 2000);
        Serial.printf("  delay=%ld try=%ld -> %s\r\n", d, t, ok ? "fired" : "no-trig");
      }
    }
    Serial.println("sweep done.");
  } else if (strcmp(cmd, "power") == 0) {
    const char* m = util::arg(argc, argv, 1);
    pinMode(cfg.glitch_pwr, OUTPUT);
    if      (m && !strcmp(m, "on"))    { digitalWrite(cfg.glitch_pwr, HIGH); Serial.println("power on"); }
    else if (m && !strcmp(m, "off"))   { digitalWrite(cfg.glitch_pwr, LOW);  Serial.println("power off"); }
    else if (m && !strcmp(m, "cycle")) { digitalWrite(cfg.glitch_pwr, LOW); delay(200); digitalWrite(cfg.glitch_pwr, HIGH); Serial.println("power cycled"); }
    else Serial.println("power on|off|cycle");
  } else if (strcmp(cmd, "status") == 0) {
    glitch_status();
  } else {
    Serial.printf("glitch: unknown '%s'\r\n", cmd);
  }
}

static void glitch_help() {
  Serial.println("glitch — PIO fault injection (arm->trigger->delay->pulse)");
  Serial.println("  glitch set delay=<c> width=<c> [delayns=] [widthns=] [trig=] [out=] [pwr=]");
  Serial.println("  glitch trigger manual|rising|falling");
  Serial.println("  glitch arm | off          enable/disable (out goes live)");
  Serial.println("  glitch fire               one pulse (manual) or wait for edge");
  Serial.println("  glitch sweep <s> <e> [step] [tries=N]   sweep delay range");
  Serial.println("  glitch power on|off|cycle  drive target power/EN pin");
  Serial.println("  glitch status");
  Serial.println("  !! only glitch hardware you own & are authorised to test !!");
}

extern const Module glitchModule = { "glitch", "PIO voltage/crowbar fault injection", glitch_run, glitch_help };
