#include "ui_drawing.hpp"
#ifdef COLOUR_160x40

void ui_draw_homescreen_detailed(TemperatureType_t tipTemp) {
  if (isTipDisconnected()) {
    OLED::drawArea(0, 0, 56, 32, disconnectedTip);
    OLED::setCursor(56, 0);
    uint32_t Vlt = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0);
    OLED::printNumber(Vlt / 10, 2, FontStyle::LARGE);
    OLED::print(LargeSymbolDot, FontStyle::LARGE);
    OLED::printNumber(Vlt % 10, 1, FontStyle::LARGE);
    OLED::setCursor(91, 8);
    OLED::print(SmallSymbolVolts, FontStyle::SMALL);
  } else {
    if (!(getSettingValue(SettingsOptions::CoolingTempBlink) && (tipTemp > 55) && (xTaskGetTickCount() % 1000 < 300))) {
      ui_draw_tip_temperature(true, FontStyle::LARGE);
    }
    OLED::setCursor(73, 0);
    OLED::printNumber(getSettingValue(SettingsOptions::SolderingTemp), 3, FontStyle::SMALL);
    OLED::printSymbolDeg(FontStyle::SMALL);

    OLED::setCursor(67, 8);
    printVoltage();
    OLED::print(SmallSymbolVolts, FontStyle::SMALL);
  }
}
#endif
