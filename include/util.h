// ============================================================================
//  util.h — small shared helpers (number parsing, hexdump, argument lookup)
// ============================================================================
#pragma once
#include <Arduino.h>

namespace util {

// Parse a base-aware integer: "255", "0xFF", "0b1010", "0o17".
// Returns true on success and writes *out.
bool parseNum(const char* s, long* out);

// Convenience: parse or return `def` if s is null / unparseable.
long numOr(const char* s, long def);

// Classic hexdump:  0000: 48 65 6c 6c ...  |Hell...|
void hexdump(const uint8_t* data, size_t len, uint32_t base = 0);

// Look up a positional argument (0-based) or return nullptr.
const char* arg(int argc, char** argv, int index);

// Look up a "key=value" style option; returns value or `def`.
const char* opt(int argc, char** argv, const char* key, const char* def);
long optNum(int argc, char** argv, const char* key, long def);

// True if a bare flag (e.g. "-v" or "verbose") is present.
bool flag(int argc, char** argv, const char* name);

// Pretty print a duration/frequency.
void printHz(uint32_t hz);

}  // namespace util
