// ============================================================================
//  console.h — the USB-serial command console and the module interface
// ----------------------------------------------------------------------------
//  Commands are two-level:  <module> <subcommand> [args...]
//  e.g.  i2c scan      spi id      uart auto      jtag scan      glitch run
//
//  Add a module in four steps:
//    1. Create src/modules/mod_foo.cpp
//    2. Define `const Module fooModule = { "foo", "…", foo_run, foo_help };`
//    3. Declare it in include/modules.h
//    4. Add &fooModule to kModules[] in src/modules/registry.cpp
// ============================================================================
#pragma once
#include <Arduino.h>

// A module bundles related sub-commands under one keyword.
struct Module {
  const char* name;                     // invocation keyword, e.g. "i2c"
  const char* summary;                  // one-liner shown by `help`
  void (*run)(int argc, char** argv);   // argv[0] = first token after `name`
  void (*help)();                       // detailed usage, shown by `<name> help`
};

// The module table lives in registry.cpp.
extern const Module* const kModules[];
extern const size_t kModuleCount;

namespace console {

// Call once from setup() (after Serial.begin) and every loop() iteration.
void begin();
void poll();

void banner();

// True if the user pressed a key since the last check — modules should poll
// this inside long-running loops (dumps, scans) so they can be aborted.
// Consumes the pending byte(s).
bool aborted();

// Blocks until a byte arrives or `timeout_ms` elapses; returns -1 on timeout.
int readByteTimeout(uint32_t timeout_ms);

}  // namespace console
