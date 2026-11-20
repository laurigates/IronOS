#include "OperatingModeUtilities.h"
#include "OperatingModes.h"
#include "SolderingCommon.h"
#include "TipThermoModel.h"
#ifdef COLOUR_160x40

void ui_draw_tip_temperature(bool symbol, const FontStyle font) {
  TemperatureType_t Temp = getTipTemp();
  OLED::printNumber(Temp, 3, font);
  if (symbol) {
    OLED::printSymbolDeg(font == FontStyle::LARGE ? FontStyle::EXTRAS : font);
  }
}
#endif
