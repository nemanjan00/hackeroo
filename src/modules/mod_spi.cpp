// ============================================================================
//  spi — generic SPI transfers + 25-series NOR flash probe/dump.
//  CS is driven manually (active-low). Opts: sck= mosi= miso= cs= hz= mode=.
// ============================================================================
#include "console.h"
#include "config.h"
#include "util.h"
#include <SPI.h>
#include <hardware/gpio.h>

static int  s_sck = CFG_SPI_SCK, s_mosi = CFG_SPI_MOSI, s_miso = CFG_SPI_MISO, s_cs = CFG_SPI_CS;
static long s_hz  = CFG_SPI_HZ;
static int  s_mode = SPI_MODE0;
static bool s_up  = false;

static void spi_begin(int argc, char** argv) {
  s_sck  = util::optNum(argc, argv, "sck",  s_sck);
  s_mosi = util::optNum(argc, argv, "mosi", s_mosi);
  s_miso = util::optNum(argc, argv, "miso", s_miso);
  s_cs   = util::optNum(argc, argv, "cs",   s_cs);
  s_hz   = util::optNum(argc, argv, "hz",   s_hz);
  long mode = util::optNum(argc, argv, "mode", -1);   // SPI_MODE0..3 == 0..3
  if (mode >= 0 && mode <= 3) s_mode = (int)mode;
  if (!s_up) {
    SPI.setSCK(s_sck);
    SPI.setTX(s_mosi);
    SPI.setRX(s_miso);
    SPI.begin();
    s_up = true;
  }
  pinMode(s_cs, OUTPUT);
  digitalWrite(s_cs, HIGH);   // deselect
}

static inline void csLow()  { digitalWrite(s_cs, LOW); }
static inline void csHigh() { digitalWrite(s_cs, HIGH); }

static void beginTx() { SPI.beginTransaction(SPISettings(s_hz, MSBFIRST, s_mode)); }
static void endTx()   { SPI.endTransaction(); }

static const char* jedecVendor(uint8_t id) {
  switch (id) {
    case 0xEF: return "Winbond";
    case 0xC2: return "Macronix";
    case 0x20: return "Micron/ST";
    case 0xC8: return "GigaDevice";
    case 0x9D: return "ISSI";
    case 0xBF: return "SST";
    case 0x01: return "Spansion/Cypress";
    case 0x1F: return "Adesto/Atmel";
    case 0x1C: return "Eon";
    default:   return "unknown";
  }
}

// JEDEC RDID (0x9F): manufacturer, memory type, capacity.
static uint32_t spi_flash_id(bool print) {
  uint8_t m, t, c;
  beginTx(); csLow();
  SPI.transfer(0x9F);
  m = SPI.transfer(0x00);
  t = SPI.transfer(0x00);
  c = SPI.transfer(0x00);
  csHigh(); endTx();
  uint32_t size = (c >= 0x10 && c <= 0x1f) ? (1UL << c) : 0;
  if (print) {
    Serial.printf("JEDEC id: %02x %02x %02x  (%s)\r\n", m, t, c, jedecVendor(m));
    if (size) Serial.printf("capacity: %lu bytes (%lu Kbit)\r\n",
                            (unsigned long)size, (unsigned long)(size / 128));
    else      Serial.println("capacity: (could not decode)");
  }
  return size;
}

// Flash READ (0x03) with 24-bit address into buf.
static void spi_flash_read(uint32_t addr, uint8_t* buf, size_t len) {
  beginTx(); csLow();
  SPI.transfer(0x03);
  SPI.transfer((addr >> 16) & 0xff);
  SPI.transfer((addr >> 8) & 0xff);
  SPI.transfer(addr & 0xff);
  for (size_t i = 0; i < len; i++) buf[i] = SPI.transfer(0x00);
  csHigh(); endTx();
}

static void spi_dump(uint32_t addr, uint32_t len) {
  uint8_t buf[256];
  Serial.printf("dumping %lu bytes from 0x%06lx — press a key to abort\r\n",
                (unsigned long)len, (unsigned long)addr);
  for (uint32_t off = 0; off < len; off += sizeof(buf)) {
    if (console::aborted()) { Serial.println("\r\naborted."); return; }
    uint32_t n = min((uint32_t)sizeof(buf), len - off);
    spi_flash_read(addr + off, buf, n);
    util::hexdump(buf, n, addr + off);
  }
  Serial.println("done.");
}

// Raw transfer: send given bytes, print what MISO returned.
static void spi_xfer(int argc, char** argv, int first) {
  beginTx(); csLow();
  Serial.print("miso:");
  for (int i = first; i < argc; i++) {
    long b;
    if (!util::parseNum(argv[i], &b)) continue;
    Serial.printf(" %02x", SPI.transfer((uint8_t)b));
  }
  csHigh(); endTx();
  Serial.println();
}

// Passive bus sniffer: read SCK/MOSI/MISO/CS as plain inputs and reconstruct
// bytes on each clock edge while CS is asserted (active low). CPU-polled, so
// good for slow buses (up to a few hundred kHz); use a logic analyser / PIO
// for fast SPI. Sample edge follows the SPI mode (rising for mode 0/3).
static void spi_sniff() {
  pinMode(s_sck, INPUT); pinMode(s_mosi, INPUT);
  pinMode(s_miso, INPUT); pinMode(s_cs, INPUT);
  bool sampleRising = (s_mode == 0 || s_mode == 3);
  Serial.printf("sniffing SPI SCK=GP%d MOSI=GP%d MISO=GP%d CS=GP%d mode%d — key to stop\r\n",
                s_sck, s_mosi, s_miso, s_cs, s_mode);
  Serial.println("(CS-low frames; each byte shown as MOSI/MISO)");
  const uint32_t sckM = 1u << s_sck, csM = 1u << s_cs, moM = 1u << s_mosi, miM = 1u << s_miso;
  uint32_t g = gpio_get_all();
  int psck = (g & sckM) ? 1 : 0, pcs = (g & csM) ? 1 : 0;
  uint8_t mo = 0, mi = 0; int nb = 0; bool active = false; uint32_t it = 0;
  while (true) {
    if ((++it & 0x3FF) == 0 && console::aborted()) break;
    g = gpio_get_all();
    int cs = (g & csM) ? 1 : 0, sck = (g & sckM) ? 1 : 0;
    if (pcs && !cs) { active = true; nb = 0; mo = mi = 0; Serial.print("\r\nCS:"); }
    else if (!pcs && cs && active) { active = false; Serial.print(" |end"); }
    if (active) {
      bool edge = sampleRising ? (!psck && sck) : (psck && !sck);
      if (edge) {
        mo = (mo << 1) | ((g & moM) ? 1 : 0);
        mi = (mi << 1) | ((g & miM) ? 1 : 0);
        if (++nb == 8) { Serial.printf(" %02x/%02x", mo, mi); nb = 0; mo = mi = 0; }
      }
    }
    psck = sck; pcs = cs;
  }
  Serial.println("\r\nstopped.");
}

static void spi_run(int argc, char** argv) {
  const char* cmd = util::arg(argc, argv, 0);
  if (!cmd) { Serial.println("spi: need a subcommand (help)"); return; }

  // Sniff must NOT take the bus (no SPI.begin) — parse pins and read as GPIO.
  if (strcmp(cmd, "sniff") == 0) {
    s_sck  = util::optNum(argc, argv, "sck",  s_sck);
    s_mosi = util::optNum(argc, argv, "mosi", s_mosi);
    s_miso = util::optNum(argc, argv, "miso", s_miso);
    s_cs   = util::optNum(argc, argv, "cs",   s_cs);
    long mode = util::optNum(argc, argv, "mode", -1);
    if (mode >= 0 && mode <= 3) s_mode = (int)mode;
    if (s_up) { SPI.end(); s_up = false; }
    spi_sniff();
    return;
  }
  spi_begin(argc, argv);

  if (strcmp(cmd, "id") == 0) {
    spi_flash_id(true);
  } else if (strcmp(cmd, "read") == 0) {
    long addr = util::numOr(util::arg(argc, argv, 1), 0);
    long len  = util::numOr(util::arg(argc, argv, 2), 256);
    uint8_t buf[256];
    if (len < 1)   len = 1;                    // guard: negative -> size_t underflow
    if (len > 256) len = 256;
    spi_flash_read(addr, buf, len);
    util::hexdump(buf, len, addr);
  } else if (strcmp(cmd, "dump") == 0) {
    long addr = util::numOr(util::arg(argc, argv, 1), 0);
    long len  = util::numOr(util::arg(argc, argv, 2), 0);
    if (len <= 0) { uint32_t sz = spi_flash_id(false); len = sz ? sz : 4096; }
    spi_dump(addr, len);
  } else if (strcmp(cmd, "xfer") == 0) {
    spi_xfer(argc, argv, 1);
  } else {
    Serial.printf("spi: unknown '%s'\r\n", cmd);
  }
}

static void spi_help() {
  Serial.println("spi — SPI bus + 25-series flash   (opts: sck= mosi= miso= cs= hz= mode=)");
  Serial.println("  spi id                    read JEDEC id, decode size");
  Serial.println("  spi read <addr> [len]     flash read (0x03), hexdump");
  Serial.println("  spi dump [addr] [len]     dump (len=0 -> whole chip)");
  Serial.println("  spi xfer <byte...>        raw transfer, print MISO");
  Serial.println("  spi sniff                 passive bus monitor (slow buses)");
  Serial.printf ("  defaults: SCK=GP%d MOSI=GP%d MISO=GP%d CS=GP%d\r\n",
                 CFG_SPI_SCK, CFG_SPI_MOSI, CFG_SPI_MISO, CFG_SPI_CS);
}

extern const Module spiModule = { "spi", "SPI bus + NOR-flash probe/dump", spi_run, spi_help };
