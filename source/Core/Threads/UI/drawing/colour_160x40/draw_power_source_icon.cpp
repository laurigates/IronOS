#include "ui_drawing.hpp"
#ifdef COLOUR_160x40
void ui_draw_power_source_icon(void) {
#if defined(POW_PD) || defined(POW_QC) || defined(POW_PD_EXT)
  if (!getIsPoweredByDCIN()) {
    uint16_t V = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0);
    if (V % 10 >= 5) {
      V = (V / 10) + 1;
    } else {
      V = V / 10;
    }
    int16_t xPos = OLED::getCursorX();
    OLED::printNumber(V / 10, 1, FontStyle::LARGE);
    OLED::setCursor(xPos, 16);
    OLED::printNumber(V % 10, 1, FontStyle::LARGE);
    return;
  }
#endif
#ifdef POW_DC
  if (getSettingValue(SettingsOptions::MinDCVoltageCells)) {
    uint8_t  cellCount = getSettingValue(SettingsOptions::MinDCVoltageCells) + 2;
    uint32_t cellV     = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0) / cellCount;
    if (cellV < getSettingValue(SettingsOptions::MinVoltageCells)) {
      cellV = getSettingValue(SettingsOptions::MinVoltageCells);
    }
    cellV -= getSettingValue(SettingsOptions::MinVoltageCells);
    if (cellV > 9) {
      cellV = 9;
    }
    OLED::drawBattery(cellV + 1);
  } else {
    OLED::drawSymbol(15);
  }
#endif
}
#endif
