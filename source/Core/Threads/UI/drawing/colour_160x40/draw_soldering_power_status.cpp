#include "ColorUI.hpp"
#include "power.hpp"
#include "ui_drawing.hpp"
#include <OperatingModes.h>
#ifdef COLOUR_160x40

void ui_draw_soldering_power_status(bool boost_mode_on) {
  ColorUI::prepareSoldering(boost_mode_on);
  if (ColorUI::hasActiveScene()) {
    return;
  }
  OLED::setCursor(-1, 0);
  ui_draw_tip_temperature(true, FontStyle::LARGE);

  if (boost_mode_on) {
    OLED::setCursor(50, 0);
    OLED::print(LargeSymbolPlus, FontStyle::LARGE);
  } else {
#ifndef NO_SLEEP_MODE
    if (getSettingValue(SettingsOptions::Sensitivity) && getSettingValue(SettingsOptions::SleepTime)) {
      OLED::setCursor(47, 0);
      printCountdownUntilSleep(getSleepTimeout());
    }
#endif
    OLED::setCursor(47, 8);
    OLED::print(PowerSourceNames[getPowerSourceNumber()], FontStyle::SMALL, 2);
  }

  OLED::setCursor(67, 0);
  {
    uint32_t x10Watt = x10WattHistory.average();
    if (x10Watt > 999) {
      OLED::print(SmallSymbolSpace, FontStyle::SMALL);
      OLED::printNumber(x10WattHistory.average() / 10, 3, FontStyle::SMALL);
    } else {
      OLED::printNumber(x10WattHistory.average() / 10, 2, FontStyle::SMALL);
      OLED::print(SmallSymbolDot, FontStyle::SMALL);
      OLED::printNumber(x10WattHistory.average() % 10, 1, FontStyle::SMALL);
    }
    OLED::print(SmallSymbolWatts, FontStyle::SMALL);
  }

  OLED::setCursor(67, 8);
  printVoltage();
  OLED::print(SmallSymbolVolts, FontStyle::SMALL);
}
#endif
