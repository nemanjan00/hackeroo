#include "console.h"
#include "config.h"
#include <string.h>

namespace {

constexpr size_t kLineMax = 256;
constexpr size_t kMaxArgs = 24;

char     lineBuf[kLineMax];
size_t   lineLen = 0;
bool      greeted = false;

void prompt() { Serial.print("hackeroo> "); }

const Module* findModule(const char* name) {
  for (size_t i = 0; i < kModuleCount; i++)
    if (strcmp(kModules[i]->name, name) == 0) return kModules[i];
  return nullptr;
}

void listModules() {
  Serial.println("modules:");
  for (size_t i = 0; i < kModuleCount; i++)
    Serial.printf("  %-8s %s\r\n", kModules[i]->name, kModules[i]->summary);
  Serial.println("type '<module> help' for details, or 'help'.");
}

// Split a line into argv in place. Returns argc.
int tokenize(char* line, char** argv, int maxArgs) {
  int argc = 0;
  char* p = line;
  while (*p && argc < maxArgs) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) *p++ = '\0';
  }
  return argc;
}

void dispatch(char* line) {
  char* argv[kMaxArgs];
  int argc = tokenize(line, argv, kMaxArgs);
  if (argc == 0) return;

  if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
    listModules();
    return;
  }

  const Module* m = findModule(argv[0]);
  if (!m) {
    Serial.printf("unknown command '%s' — try 'help'\r\n", argv[0]);
    return;
  }
  // A bare module name, or "<module> help", prints the module's usage.
  if (argc == 1 || strcmp(argv[1], "help") == 0) {
    if (m->help) m->help();
    else Serial.printf("%s: %s\r\n", m->name, m->summary);
    return;
  }
  // Hand the module everything after its own name (argv[0] = subcommand).
  m->run(argc - 1, argv + 1);
}

}  // namespace

namespace console {

void banner() {
  Serial.println();
  Serial.println("  _   _   __   ___ _  _____ ___ ___  ___  ___");
  Serial.println(" | |_| | /  \\ / __| |/ / __| _ \\ _ \\/ _ \\/ _ \\");
  Serial.println(" | ' \\ |/ /\\ \\ (__| ' <| _||   /   / (_) | (_) |");
  Serial.println(" |_||_|_/    \\_\\___|_|\\_\\___|_|_\\_|_\\\\___/ \\___/");
  Serial.printf("  RP2350 hardware-hacking multitool  v%s\r\n", HACKEROO_VERSION);
  Serial.println();
  listModules();
}

void begin() {
  lineLen = 0;
  greeted = false;
}

void poll() {
  // Greet on the rising edge of a host connection, and re-arm so that
  // re-opening the serial monitor shows the banner again.
  bool connected = (bool)Serial;
  if (connected && !greeted) {
    delay(60);        // let the CDC endpoint settle so no leading output drops
    banner();
    prompt();
    greeted = true;
  } else if (!connected && greeted) {
    greeted = false;
  }

  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r' || c == '\n') {
      Serial.println();
      lineBuf[lineLen] = '\0';
      if (lineLen) dispatch(lineBuf);
      lineLen = 0;
      prompt();
    } else if (c == 0x08 || c == 0x7f) {          // backspace / delete
      if (lineLen) { lineLen--; Serial.print("\b \b"); }
    } else if (c == 0x03) {                        // Ctrl-C: clear line
      lineLen = 0;
      Serial.println("^C");
      prompt();
    } else if (lineLen < kLineMax - 1 && c >= 0x20) {
      lineBuf[lineLen++] = (char)c;
      Serial.write((char)c);                       // local echo
    }
  }
}

bool aborted() {
  bool hit = false;
  while (Serial.available()) { Serial.read(); hit = true; }
  return hit;
}

int readByteTimeout(uint32_t timeout_ms) {
  uint32_t start = millis();
  while ((millis() - start) < timeout_ms) {
    if (Serial.available()) return Serial.read();
  }
  return -1;
}

}  // namespace console
