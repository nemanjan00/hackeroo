# CLAUDE.md — working notes for this repo

**hackeroo** is a hardware-hacking multitool firmware for the Raspberry Pi
Pico 2 (RP2350A). USB-serial command console + pluggable modules. PlatformIO +
Earle Philhower arduino-pico core.

## Build / flash / test

```bash
pio run -e pico2                       # build -> .pio/build/pico2/firmware.uf2
pio run -e pico2 -t upload             # build + flash (picotool)
```

Board flashing without holding BOOTSEL: if hackeroo is already running, send
`sys bootsel` over serial to reboot into the bootloader, then copy the UF2 to
the mounted drive:

```bash
# reboot to bootsel, then:
cp .pio/build/pico2/firmware.uf2 /run/media/$USER/RP2350/ && sync
```

The board enumerates as `/dev/ttyACM0` (CDC) when running, `2e8a:000f`
mass-storage when in BOOTSEL. Drive it non-interactively with pyserial at
115200 8N1 (open port, read banner, write `cmd\r\n`, read reply). Long ops abort
on any key. First full build downloads the toolchain + arduino-pico (~minutes);
incremental builds are ~2 s.

## Architecture

- Two-level commands: `<module> <subcommand> [args]`. `console.cpp` reads a
  line, tokenizes into argv, finds the module in `kModules[]`, calls
  `module->run(argc-1, argv+1)` (so `argv[0]` = subcommand). Bare module name or
  `<module> help` calls `module->help()`.
- Each module is `src/modules/mod_*.cpp` defining `extern const Module xModule
  = { name, summary, run, help }`, declared in `include/modules.h`, registered
  in `src/modules/registry.cpp`.
- `util::` — `parseNum` (dec/hex/bin/oct), `numOr`, positional `arg`,
  `opt`/`optNum` (`key=value`), `flag`, `hexdump`, `printHz`.
- `console::aborted()` — modules poll this in long loops to allow key-abort.

## Pin configuration
- All pin assignments live in one runtime struct `cfg` (`include/pins.h`,
  defined in `src/pins.cpp`), loaded from flash via `EEPROM` at boot or reset to
  `config.h` defaults. Modules read `cfg.<field>` directly — never hard-code a
  pin. The `pins` module (`mod_pins.cpp`) views/sets/saves it and draws the
  `pins map` ASCII board. Bump `MAGIC` in pins.cpp if you change the struct.

## Gotchas learned here (don't re-discover these)

- **`const` linkage:** namespace-scope `const Module x = {…}` has *internal*
  linkage in C++. Module descriptors MUST be `extern const Module x = {…}` or
  the registry gets "undefined reference". This bit us once.
- **RP2350 board config:** use the maxgerhardt platform fork
  (`platform = https://github.com/maxgerhardt/platform-raspberrypi.git`),
  `board = rpipico2`, `board_build.core = earlephilhower`. F_CPU = 150 MHz.
- **RP2350 temp/ADC reads low** (ch4 raw ~55 instead of ~876); `sys temp`
  reports raw honestly. Don't "fix the formula" — the raw ADC value is the
  anomaly, likely a silicon/calibration quirk.
- **PIO without pioasm:** programs are assembled at runtime with `pio_encode_*`
  (see `mod_pio.cpp`, `mod_glitch.cpp`) — no build-time `.pio` step. `jmp`
  targets are program-relative (0-based); `pio_add_program` adds the load offset
  automatically.
- **USB CDC banner:** `console::poll()` prints the banner on the rising edge of
  `(bool)Serial` (== `tud_cdc_connected()`), with a 60 ms settle so the first
  line isn't dropped.
- The clang LSP shows thousands of `Arduino.h not found` errors — ignore them,
  it lacks PlatformIO's include paths. Ground truth is `pio run`.

## Safety / scope
`glitch` does real fault injection — authorized/owned hardware only. Modules are
boilerplate: working but minimal, with extension points marked in comments.
