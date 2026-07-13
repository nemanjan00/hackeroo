"""
hackeroo — host-side client library for the hackeroo RP2350 firmware.

A thin, dependency-light wrapper around the USB-serial console that parses the
device's text output into Python values, so you can script hardware-hacking
tasks (flash dumps, bus scans, glitch campaigns) instead of typing commands.

    from hackeroo import Hackeroo

    hk = Hackeroo("/dev/ttyACM0")
    print(hk.info())                 # {'chip_id': '...', 'sys_clk': 150000000, ...}
    print(hk.i2c_scan())             # [0x3c, 0x50]
    data = hk.spi_read(0, 256)       # bytes
    hk.glitch_config(delay=2000, width=120, trigger="rising")
    hk.glitch_arm()
    for d in range(1000, 5000, 10):
        hk.glitch_set(delay=d)
        hk.glitch_fire()

Requires: pyserial.
"""
from __future__ import annotations
import re, time

try:
    import serial
except ImportError as e:                       # pragma: no cover
    raise ImportError("hackeroo client needs pyserial: pip install pyserial") from e

PROMPT = "hackeroo>"


class Hackeroo:
    def __init__(self, port: str = "/dev/ttyACM0", baud: int = 115200,
                 timeout: float = 2.0, connect_wait: float = 1.5):
        self.ser = serial.Serial(port, baud, timeout=0.2)
        self.ser.dtr = True
        # Firmware prints a banner on connect; swallow it.
        self._read_until_prompt(connect_wait)

    # -- low level -----------------------------------------------------------
    def _read_until_prompt(self, timeout: float) -> str:
        deadline = time.time() + timeout
        buf = ""
        while time.time() < deadline:
            n = self.ser.in_waiting
            if n:
                buf += self.ser.read(n).decode(errors="replace")
                if buf.rstrip().endswith(PROMPT):
                    break
            else:
                time.sleep(0.01)
        return buf

    def command(self, cmd: str, timeout: float = 2.0) -> str:
        """Send a console command; return its output (echo + prompt stripped)."""
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()
        out = self._read_until_prompt(timeout)
        # strip the echoed command (first line) and the trailing prompt
        lines = out.splitlines()
        if lines and cmd.strip() in lines[0]:
            lines = lines[1:]
        text = "\n".join(lines)
        return text.rsplit(PROMPT, 1)[0].strip("\r\n ")

    def stream(self, cmd: str, duration: float) -> str:
        """Run a key-abortable command (sniff/watch) for `duration`s then stop."""
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode()); self.ser.flush()
        time.sleep(duration)
        buf = self.ser.read(self.ser.in_waiting or 1).decode(errors="replace")
        self.ser.write(b"\n")                  # any key aborts
        buf += self._read_until_prompt(1.0)
        return buf

    def close(self):
        self.ser.close()

    def __enter__(self): return self
    def __exit__(self, *a): self.close()

    # -- parsing helpers -----------------------------------------------------
    @staticmethod
    def parse_hexdump(text: str) -> bytes:
        """Turn '00000000: 48 65 6c ... |Hell|' lines back into bytes."""
        out = bytearray()
        for line in text.splitlines():
            m = re.match(r"^[0-9a-fA-F]{8}:\s+(.*?)\s*\|", line)
            if not m:
                continue
            for tok in m.group(1).split():
                if re.fullmatch(r"[0-9a-fA-F]{2}", tok):
                    out.append(int(tok, 16))
        return bytes(out)

    # -- sys -----------------------------------------------------------------
    def info(self) -> dict:
        t = self.command("sys info")
        d = {}
        for key, pat in (("chip_id", r"chip id\s*:\s*(\w+)"),
                         ("sys_clk", r"sys clk\s*:\s*(\d+)"),
                         ("usb_clk", r"usb clk\s*:\s*(\d+)"),
                         ("fw",      r"fw\s*:\s*(.+)")):
            m = re.search(pat, t)
            if m:
                v = m.group(1).strip()
                d[key] = int(v) if v.isdigit() else v
        return d

    def bootsel(self):
        """Reboot into the UF2 bootloader (device disconnects)."""
        try:
            self.ser.write(b"sys bootsel\n"); self.ser.flush(); time.sleep(0.3)
        finally:
            self.close()

    # -- gpio ----------------------------------------------------------------
    def gpio_read(self, pin: int) -> int:
        m = re.search(r"=\s*(\d)", self.command(f"gpio read {pin}"))
        return int(m.group(1)) if m else -1

    def gpio_write(self, pin: int, value: int):
        self.command(f"gpio write {pin} {1 if value else 0}")

    # -- i2c -----------------------------------------------------------------
    def i2c_scan(self, sda: int | None = None, scl: int | None = None) -> list[int]:
        opt = (f" sda={sda}" if sda is not None else "") + (f" scl={scl}" if scl is not None else "")
        t = self.command("i2c scan" + opt, timeout=3.0)
        addrs = []
        for line in t.splitlines():
            if re.match(r"^[0-9a-f]{2}:", line):          # a grid row
                body = line.split(":", 1)[1]
                addrs += [int(x, 16) for x in re.findall(r"\b[0-9a-f]{2}\b", body)]
        return sorted(addrs)

    def i2c_read(self, addr: int, reg: int | None = None, length: int = 1) -> bytes:
        cmd = f"i2c read {addr:#x}" + (f" {reg:#x}" if reg is not None else " -") + f" {length}"
        # when reg is None, our firmware wants: i2c read <addr> [reg] [len]; skip reg
        cmd = f"i2c read {addr:#x}" + (f" {reg:#x} {length}" if reg is not None else f" 0 {length}")
        return self.parse_hexdump(self.command(cmd))

    def i2c_write(self, addr: int, data: bytes):
        self.command(f"i2c write {addr:#x} " + " ".join(f"{b:#x}" for b in data))

    def i2c_sniff(self, duration: float = 3.0) -> str:
        return self.stream("i2c sniff", duration)

    # -- spi -----------------------------------------------------------------
    def spi_id(self) -> dict:
        t = self.command("spi id")
        d = {}
        m = re.search(r"JEDEC id:\s*([0-9a-f]{2})\s*([0-9a-f]{2})\s*([0-9a-f]{2})\s*\((.+?)\)", t)
        if m:
            d["manufacturer"] = int(m.group(1), 16)
            d["type"] = int(m.group(2), 16)
            d["capacity_code"] = int(m.group(3), 16)
            d["vendor"] = m.group(4)
        m = re.search(r"capacity:\s*(\d+)\s*bytes", t)
        if m:
            d["bytes"] = int(m.group(1))
        return d

    def spi_read(self, addr: int, length: int) -> bytes:
        """Read `length` bytes from flash starting at `addr` (any length)."""
        out = bytearray()
        off = 0
        while off < length:
            chunk = min(256, length - off)
            out += self.parse_hexdump(self.command(f"spi read {addr + off} {chunk}"))
            off += chunk
        return bytes(out)

    def spi_sniff(self, duration: float = 3.0) -> str:
        return self.stream("spi sniff", duration)

    # -- uart ----------------------------------------------------------------
    def uart_autobaud(self, ms: int = 3000) -> int | None:
        t = self.command(f"uart auto ms={ms}", timeout=ms / 1000 + 2)
        m = re.search(r"nearest standard:\s*(\d+)", t)
        return int(m.group(1)) if m else None

    # -- siggen --------------------------------------------------------------
    def siggen_pwm(self, hz: int, duty: int = 50):
        self.command(f"siggen pwm {hz} duty={duty}")

    def siggen_off(self):
        self.command("siggen off")

    def adc(self, ch: int = 0) -> float:
        m = re.search(r"\(([\d.]+)\s*V\)", self.command(f"siggen adc ch={ch}"))
        return float(m.group(1)) if m else float("nan")

    # -- glitch --------------------------------------------------------------
    def glitch_config(self, delay: int | None = None, width: int | None = None,
                      trigger: str | None = None, trig=None, out=None, pwr=None):
        if trigger:
            self.command(f"glitch trigger {trigger}")
        parts = []
        for k, v in (("delay", delay), ("width", width), ("trig", trig), ("out", out), ("pwr", pwr)):
            if v is not None:
                parts.append(f"{k}={v}")
        if parts:
            self.command("glitch set " + " ".join(parts))

    def glitch_set(self, **kw):
        self.glitch_config(**kw)

    def glitch_arm(self):  self.command("glitch arm")
    def glitch_off(self):  self.command("glitch off")

    def glitch_fire(self) -> bool:
        return "pulse done" in self.command("glitch fire", timeout=4.0)

    def glitch_sweep(self, start: int, end: int, step: int = 1, tries: int = 1) -> str:
        return self.command(f"glitch sweep {start} {end} {step} tries={tries}",
                            timeout=max(4.0, (end - start) / max(step, 1) * tries * 0.05))


if __name__ == "__main__":
    import sys
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    with Hackeroo(port) as hk:
        print("info      :", hk.info())
        print("i2c scan  :", [hex(a) for a in hk.i2c_scan()])
        print("spi id    :", hk.spi_id())
        print("adc0      :", hk.adc(0), "V")
