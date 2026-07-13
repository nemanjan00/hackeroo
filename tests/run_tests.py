#!/usr/bin/env python3
"""
hackeroo on-target integration tests.

Drives the real firmware over USB-serial and asserts on the console output —
this is the test suite that matters for a hardware tool. It covers every
module's commands plus regression tests for known-fixed bugs.

Usage:
    python3 tests/run_tests.py [--port /dev/ttyACM0] [--no-flash]

By default it reflashes the freshly-built UF2 first (reboot-to-BOOTSEL via the
firmware's own `sys bootsel`, then copy the UF2 to the mounted drive), so you
always test what you just built. Pass --no-flash to test whatever is running.

Requires: pyserial, a Pico 2 running (or ready to run) hackeroo.
Exit code 0 = all passed.

NOTE: commands are terminated with a single '\\n'. A trailing '\\r\\n' would
leave a stray byte that instantly aborts any key-abortable command (scan/
sniff/sweep/dump), so keep it single.
"""
import argparse, glob, os, sys, time, subprocess

try:
    import serial
except ImportError:
    print("FATAL: pyserial not installed (pip install pyserial)"); sys.exit(2)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UF2  = os.path.join(ROOT, ".pio", "build", "pico2", "firmware.uf2")

# ---- tiny test framework ---------------------------------------------------
class T:
    passed = 0
    failed = 0
    fails = []

def check(name, ok, detail=""):
    if ok:
        T.passed += 1
        print(f"  \033[32mPASS\033[0m {name}")
    else:
        T.failed += 1
        T.fails.append(name)
        print(f"  \033[31mFAIL\033[0m {name}   {detail}")

# ---- serial helpers --------------------------------------------------------
def drain(s, wait):
    time.sleep(wait)
    out = b""
    while s.in_waiting:
        out += s.read(s.in_waiting)
        time.sleep(0.03)
    return out.decode(errors="replace")

def send(s, line, wait=0.5):
    s.reset_input_buffer()
    s.write((line + "\n").encode()); s.flush()
    return drain(s, wait)

def expect(s, name, cmd, *needles, wait=0.6, absent=()):
    """Send cmd; PASS if all `needles` present and no `absent` strings appear."""
    out = send(s, cmd, wait)
    miss = [n for n in needles if n not in out]
    bad  = [n for n in absent if n in out]
    ok = not miss and not bad
    detail = ""
    if miss: detail += f"missing {miss} "
    if bad:  detail += f"unexpected {bad} "
    if not ok: detail += f"| got: {out.strip()[:160]!r}"
    check(name, ok, detail)
    return out

def expect_abortable(s, name, cmd, header, wait=0.5):
    """Start a key-abortable command, confirm the header, then abort it."""
    s.reset_input_buffer()
    s.write((cmd + "\n").encode()); s.flush()
    out = drain(s, wait)
    s.write(b"\n"); s.flush()          # any key aborts
    out += drain(s, 0.4)
    ok = header in out and "stopped" in out
    check(name, ok, "" if ok else f"got: {out.strip()[:160]!r}")

# ---- flashing --------------------------------------------------------------
def find_mount():
    for base in ("/run/media", "/media", "/mnt"):
        for p in glob.glob(f"{base}/*/RP2350") + glob.glob(f"{base}/*/*/RP2350"):
            if os.path.isdir(p):
                return p
    return None

def flash(port):
    if not os.path.exists(UF2):
        print(f"FATAL: {UF2} not found — run `pio run -e pico2` first"); sys.exit(2)
    print(f"flashing {UF2} ...")
    if find_mount() is None and os.path.exists(port):
        try:
            s = serial.Serial(port, 115200, timeout=0.3); s.dtr = True
            time.sleep(0.3); s.write(b"sys bootsel\n"); s.flush(); time.sleep(0.3); s.close()
        except Exception as e:
            print(f"  (couldn't send sys bootsel: {e})")
    mnt = None
    for _ in range(40):
        mnt = find_mount()
        if mnt: break
        time.sleep(0.5)
    if not mnt:
        print("FATAL: BOOTSEL drive did not appear (hold BOOTSEL and replug?)"); sys.exit(2)
    subprocess.run(["cp", UF2, mnt + "/"], check=True)
    subprocess.run(["sync"], check=True)
    print(f"  copied to {mnt}, waiting for firmware...")
    for _ in range(40):
        if os.path.exists(port): break
        time.sleep(0.5)
    time.sleep(1.0)

# ---- the tests -------------------------------------------------------------
def run(port):
    s = serial.Serial(port, 115200, timeout=0.3)
    banner = drain(s, 2.0)   # firmware greets on connect

    print("\n[connect]")
    check("banner shows product",  "hackeroo" in banner, banner[:80])
    check("banner lists modules",  "modules:" in banner and "glitch" in banner)

    print("\n[console]")
    expect(s, "help lists all 9 modules", "help",
           "sys", "gpio", "uart", "i2c", "spi", "siggen", "pio", "jtag", "glitch")
    expect(s, "unknown command handled", "definitelynotacommand", "unknown command")

    print("\n[sys]")
    expect(s, "sys info clocks", "sys info", "chip id", "150000000")
    expect(s, "sys temp reports", "sys temp", "temp sensor")

    print("\n[pins]")
    expect(s, "pins show list", "pins show", "i2c.sda", "glitch.trig")
    expect(s, "pins map board", "pins map", "USB", "GP0", "GP15", "SPI.MISO", wait=0.9)
    expect(s, "pins set updates", "pins set pio.pin 9", "pio.pin = 9")
    expect(s, "pins reset restores", "pins reset", "defaults")

    print("\n[gpio]")
    expect(s, "gpio scan range", "gpio scan", "GP0", "GP28")
    expect(s, "gpio read", "gpio read 15", "GP15 =")
    expect(s, "gpio write", "gpio write 15 1", "GP15 <- 1")
    expect_abortable(s, "gpio watch aborts", "gpio watch 15", "watching GP15")
    expect_abortable(s, "gpio chase animates", "gpio chase 0 7 20", "chasing GP0..GP7")

    print("\n[uart]")
    expect(s, "uart autobaud no-signal", "uart auto ms=600", "auto-baud", "activity")

    print("\n[i2c]")
    expect(s, "i2c scan runs", "i2c scan", "device(s) found")
    expect(s, "i2c help has sniff", "i2c help", "sniff")
    expect_abortable(s, "i2c sniff aborts", "i2c sniff", "sniffing I2C")

    print("\n[spi]")
    expect(s, "spi id reads JEDEC", "spi id", "JEDEC id")
    expect(s, "spi read hexdump", "spi read 0 16", "00000000:")
    # regression: negative length must NOT overflow / crash (bug #1)
    expect(s, "spi read -1 guarded (no crash)", "spi read 0 -1", "00000000:",
           absent=("hackeroo> hackeroo> hackeroo>",))
    expect(s, "spi help has sniff", "spi help", "sniff")
    expect_abortable(s, "spi sniff aborts", "spi sniff", "sniffing SPI")

    print("\n[siggen]")
    # regression: low frequency must be accurate, not clamped to ~587 Hz (bug #3)
    out = expect(s, "siggen pwm 50 accurate", "siggen pwm 50", "actual ~5")
    check("siggen pwm 50 ~= 50 Hz (not 587)", "587" not in out and "actual ~50" in out,
          out.strip()[:120])
    expect(s, "siggen pwm 1MHz no-overflow", "siggen pwm 1000000", "actual ~1000000")
    expect(s, "siggen dc", "siggen dc 50", "1.6")
    expect(s, "siggen adc", "siggen adc", "ADC0")
    expect(s, "siggen off", "siggen off", "off")

    print("\n[pio]")
    expect(s, "pio square wave", "pio sq 1000", "actual", "clkdiv")
    expect(s, "pio off", "pio off", "stopped")

    print("\n[jtag]")
    expect(s, "jtag scan completes", "jtag scan", "scan done", wait=2.0)
    expect(s, "jtag swd completes", "jtag swd", "scan done", wait=1.5)

    print("\n[glitch]")
    expect(s, "glitch trigger manual", "glitch trigger manual", "manual")
    expect(s, "glitch set params", "glitch set delay=150 width=75", "150", "75")
    expect(s, "glitch arm", "glitch arm", "ARMED")
    expect(s, "glitch fire (manual pulse)", "glitch fire", "pulse done")
    expect(s, "glitch status armed", "glitch status", "armed", "YES")
    # regression: sweep re-primes each fire, no stale/hang (bug #2, manual path)
    expect(s, "glitch sweep manual", "glitch sweep 10 30 10", "sweep done", wait=1.0)
    expect(s, "glitch off", "glitch off", "disarmed")

    s.close()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--no-flash", action="store_true")
    args = ap.parse_args()

    if not args.no_flash:
        flash(args.port)
    if not os.path.exists(args.port):
        print(f"FATAL: {args.port} not present"); sys.exit(2)

    print(f"=== hackeroo integration tests on {args.port} ===")
    run(args.port)

    print(f"\n=== {T.passed} passed, {T.failed} failed ===")
    if T.failed:
        print("failed:", ", ".join(T.fails)); sys.exit(1)

if __name__ == "__main__":
    main()
