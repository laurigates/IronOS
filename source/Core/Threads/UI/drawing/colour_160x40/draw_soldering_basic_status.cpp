#include "ColorUI.hpp"
#include "power.hpp"
#include "ui_drawing.hpp"
#ifdef COLOUR_160x40

void ui_draw_soldering_basic_status(bool boostModeOn) {
  ColorUI::prepareSoldering(boostModeOn);
  if (ColorUI::hasActiveScene()) {
    return;
  }
  OLED::setCursor(0, 0);
  OLED::drawHeatSymbol(X10WattsToPWM(x10WattHistory.average()));
  if (boostModeOn) {
    OLED::drawSymbol(2);
  } else {
    OLED::print(LargeSymbolSpace, FontStyle::LARGE);
  }
  ui_draw_tip_temperature(true, FontStyle::LARGE);
  OLED::print(LargeSymbolSpace, FontStyle::LARGE);
  ui_draw_power_source_icon();
}

#endif
