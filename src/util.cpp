#include "util.h"
#include <string.h>
#include <stdlib.h>

namespace util {

bool parseNum(const char* s, long* out) {
  if (!s || !*s) return false;
  int base = 10;
  const char* p = s;
  bool neg = false;
  if (*p == '-') { neg = true; p++; }
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
  else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) { base = 2; p += 2; }
  else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) { base = 8; p += 2; }
  if (!*p) return false;
  char* end = nullptr;
  long v = strtol(p, &end, base);
  if (end == p || (end && *end != '\0')) return false;
  *out = neg ? -v : v;
  return true;
}

long numOr(const char* s, long def) {
  long v;
  return parseNum(s, &v) ? v : def;
}

void hexdump(const uint8_t* data, size_t len, uint32_t base) {
  for (size_t i = 0; i < len; i += 16) {
    Serial.printf("%08lx: ", (unsigned long)(base + i));
    for (size_t j = 0; j < 16; j++) {
      if (i + j < len) Serial.printf("%02x ", data[i + j]);
      else             Serial.print("   ");
      if (j == 7) Serial.print(' ');
    }
    Serial.print(" |");
    for (size_t j = 0; j < 16 && i + j < len; j++) {
      uint8_t c = data[i + j];
      Serial.write((c >= 0x20 && c < 0x7f) ? c : '.');
    }
    Serial.println('|');
  }
}

const char* arg(int argc, char** argv, int index) {
  return (index >= 0 && index < argc) ? argv[index] : nullptr;
}

const char* opt(int argc, char** argv, const char* key, const char* def) {
  size_t klen = strlen(key);
  for (int i = 0; i < argc; i++) {
    if (strncmp(argv[i], key, klen) == 0 && argv[i][klen] == '=')
      return argv[i] + klen + 1;
  }
  return def;
}

long optNum(int argc, char** argv, const char* key, long def) {
  const char* v = opt(argc, argv, key, nullptr);
  return v ? numOr(v, def) : def;
}

bool flag(int argc, char** argv, const char* name) {
  for (int i = 0; i < argc; i++)
    if (strcmp(argv[i], name) == 0) return true;
  return false;
}

void printHz(uint32_t hz) {
  if (hz >= 1000000)      Serial.printf("%lu.%03lu MHz", (unsigned long)(hz / 1000000), (unsigned long)((hz / 1000) % 1000));
  else if (hz >= 1000)    Serial.printf("%lu.%03lu kHz", (unsigned long)(hz / 1000), (unsigned long)(hz % 1000));
  else                    Serial.printf("%lu Hz", (unsigned long)hz);
}

}  // namespace util
