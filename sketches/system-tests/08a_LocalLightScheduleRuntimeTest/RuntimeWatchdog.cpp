#include "RuntimeWatchdog.h"

#if defined(ARDUINO_ARCH_SAMD)
#include <sam.h>
#endif

uint8_t RuntimeWatchdog::resetCause() {
#if defined(ARDUINO_ARCH_SAMD)
  return PM->RCAUSE.reg;
#else
  return 0;
#endif
}

bool RuntimeWatchdog::wasWatchdogReset(uint8_t cause) {
#if defined(ARDUINO_ARCH_SAMD)
  return (cause & PM_RCAUSE_WDT) != 0;
#else
  (void)cause;
  return false;
#endif
}

const char* RuntimeWatchdog::resetCauseText(uint8_t cause) {
#if defined(ARDUINO_ARCH_SAMD)
  if ((cause & PM_RCAUSE_WDT) != 0) return "watchdog";
  if ((cause & PM_RCAUSE_EXT) != 0) return "external";
  if ((cause & PM_RCAUSE_SYST) != 0) return "system";
  if ((cause & PM_RCAUSE_BOD33) != 0) return "brownout_3v3";
  if ((cause & PM_RCAUSE_BOD12) != 0) return "brownout_1v2";
  if ((cause & PM_RCAUSE_POR) != 0) return "power_on";
#else
  (void)cause;
#endif
  return "unknown";
}

void RuntimeWatchdog::begin16Seconds() {
#if defined(ARDUINO_ARCH_SAMD)
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_WDT | GCLK_CLKCTRL_GEN_GCLK2 | GCLK_CLKCTRL_CLKEN;
  while (GCLK->STATUS.bit.SYNCBUSY) {}
  WDT->CTRL.reg = 0;
  while (WDT->STATUS.bit.SYNCBUSY) {}
  WDT->CONFIG.reg = WDT_CONFIG_PER_16K;
  WDT->CTRL.reg = WDT_CTRL_ENABLE;
  while (WDT->STATUS.bit.SYNCBUSY) {}
  feed();
#endif
}

void RuntimeWatchdog::feed() {
#if defined(ARDUINO_ARCH_SAMD)
  while (WDT->STATUS.bit.SYNCBUSY) {}
  WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;
#endif
}
