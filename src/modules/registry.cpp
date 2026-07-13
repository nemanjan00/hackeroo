// ============================================================================
//  registry.cpp — the master list of modules exposed by the console.
//  Add your module's descriptor here (and declare it in include/modules.h).
// ============================================================================
#include "console.h"
#include "modules.h"

const Module* const kModules[] = {
  &sysModule,
  &gpioModule,
  &uartModule,
  &i2cModule,
  &spiModule,
  &siggenModule,
  &pioModule,
  &jtagModule,
  &glitchModule,
};

const size_t kModuleCount = sizeof(kModules) / sizeof(kModules[0]);
