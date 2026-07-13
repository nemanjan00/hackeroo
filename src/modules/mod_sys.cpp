// ============================================================================
//  sys — board info, clocks, on-chip temperature, reboot to BOOTSEL.
// ============================================================================
#include "console.h"
#include "config.h"
#include "util.h"
#include <hardware/clocks.h>
#include <hardware/adc.h>
#include <pico/unique_id.h>
#include <pico/bootrom.h>

static void sys_info() {
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);
  Serial.print("chip id  : ");
  for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
    Serial.printf("%02x", id.id[i]);
  Serial.println();
  Serial.printf("fw       : hackeroo v%s\r\n", HACKEROO_VERSION);
  Serial.printf("sys clk  : %lu Hz\r\n", (unsigned long)clock_get_hz(clk_sys));
  Serial.printf("usb clk  : %lu Hz\r\n", (unsigned long)clock_get_hz(clk_usb));
  Serial.printf("F_CPU    : %lu Hz\r\n", (unsigned long)F_CPU);
}

// RP2350 on-chip temperature sensor (ADC channel 4). We show the raw ADC
// reading alongside the datasheet conversion: the RP2350's ADC has a notable
// offset and generally needs per-board calibration, so the absolute number is
// approximate — the raw value is the honest datum to trust/trend.
static void sys_temp() {
  adc_init();
  adc_set_temp_sensor_enabled(true);
  delay(2);
  adc_select_input(4);
  uint32_t raw = adc_read();
  adc_set_temp_sensor_enabled(false);
  float v = raw * 3.3f / 4096.0f;
  float t = 27.0f - (v - 0.706f) / 0.001721f;
  Serial.printf("temp sensor: ADC ch4 raw=%lu (%.3f V) -> %.1f C (uncalibrated)\r\n",
                (unsigned long)raw, v, t);
}

static void sys_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd || strcmp(cmd, "info") == 0)      sys_info();
  else if (strcmp(cmd, "temp") == 0)          sys_temp();
  else if (strcmp(cmd, "bootsel") == 0) {
    Serial.println("rebooting to BOOTSEL...");
    Serial.flush();
    reset_usb_boot(0, 0);                     // back to the UF2 bootloader
  } else if (strcmp(cmd, "reboot") == 0) {
    Serial.println("rebooting...");
    Serial.flush();
    rp2040.reboot();
  } else {
    Serial.printf("sys: unknown '%s'\r\n", cmd);
  }
}

static void sys_help() {
  Serial.println("sys — board info & control");
  Serial.println("  sys info      chip id, clocks, firmware version");
  Serial.println("  sys temp      on-chip temperature");
  Serial.println("  sys bootsel   reboot into UF2 bootloader (for reflashing)");
  Serial.println("  sys reboot    warm reboot");
}

extern const Module sysModule = { "sys", "board info, temp, reboot/bootsel", sys_run, sys_help };
