#include "ui_drawing.hpp"

#ifdef COLOUR_160x40

void ui_draw_homescreen_simplified(TemperatureType_t tipTemp) {
  bool tempOnDisplay          = false;
  bool tipDisconnectedDisplay = false;

  OLED::drawArea(0, 0, 56, 32, buttonA);
  OLED::drawArea(58, 0, 56, 32, buttonB);
  OLED::setCursor(116, 0);
  ui_draw_power_source_icon();

  if (tipTemp > 55) {
    tempOnDisplay = true;
  } else if (tipTemp < 45) {
    tempOnDisplay = false;
  }
  if (isTipDisconnected()) {
    tempOnDisplay          = false;
    tipDisconnectedDisplay = true;
  }
  if (tempOnDisplay || tipDisconnectedDisplay) {
    OLED::fillArea(0, 0, 56, 32, 0);
    OLED::setCursor(0, 0);
    if (!tipDisconnectedDisplay) {
      if (!(getSettingValue(SettingsOptions::CoolingTempBlink) && (xTaskGetTickCount() % 1000 < 300))) {
        ui_draw_tip_temperature(false, FontStyle::LARGE);
      }
    } else {
      OLED::drawArea(0, 0, 56, 32, disconnectedTip);
    }
  }
}

#endif
