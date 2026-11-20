#include "Buttons.hpp"
#include "OperatingModeUtilities.h"
#include "OperatingModes.h"
#ifdef COLOUR_160x40
bool warnUser(const char *warning, const ButtonState buttons) {
  OLED::clearScreen(); // also calls ColorUI::resetScene(), so warnings in soldering mode are visible
  OLED::printWholeScreen(warning);
  if ((xTaskGetTickCount() - lastButtonTime) > TICKS_SECOND * 5) {
    return true;
  }
  return buttons != BUTTON_NONE;
}
#endif
