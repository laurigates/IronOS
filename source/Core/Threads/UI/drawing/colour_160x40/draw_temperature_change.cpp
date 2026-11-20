#include "ui_drawing.hpp"

#ifdef COLOUR_160x40
void ui_draw_temperature_change(void) {
  // ColourOLED rotates the whole frame in software, so read getRawRotation() to place
  // the +/- symbols on the side of the corresponding physical button.
  const bool rot = !OLED::getRawRotation();

  OLED::setCursor(8, 8);
  if (rot) {
    OLED::print(getSettingValue(SettingsOptions::ReverseButtonTempChangeEnabled) ? LargeSymbolPlus : LargeSymbolMinus, FontStyle::LARGE);
  } else {
    OLED::print(getSettingValue(SettingsOptions::ReverseButtonTempChangeEnabled) ? LargeSymbolMinus : LargeSymbolPlus, FontStyle::LARGE);
  }

  OLED::print(LargeSymbolSpace, FontStyle::LARGE);
  OLED::printNumber(getSettingValue(SettingsOptions::SolderingTemp), 3, FontStyle::LARGE);
  OLED::printSymbolDeg(FontStyle::EXTRAS);
  OLED::print(LargeSymbolSpace, FontStyle::LARGE);
  if (rot) {
    OLED::print(getSettingValue(SettingsOptions::ReverseButtonTempChangeEnabled) ? LargeSymbolMinus : LargeSymbolPlus, FontStyle::LARGE);
  } else {
    OLED::print(getSettingValue(SettingsOptions::ReverseButtonTempChangeEnabled) ? LargeSymbolPlus : LargeSymbolMinus, FontStyle::LARGE);
  }
}
#endif
