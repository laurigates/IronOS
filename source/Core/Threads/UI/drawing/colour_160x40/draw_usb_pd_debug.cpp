#include "ui_drawing.hpp"
#ifdef COLOUR_160x40

void ui_draw_usb_pd_debug_state(const uint16_t vbus_sense_state, const uint8_t stateNumber) {
  OLED::setCursor(0, 0);
  OLED::print(SmallSymbolPDDebug, FontStyle::SMALL);
  OLED::setCursor(0, 8);
  OLED::print(SmallSymbolState, FontStyle::SMALL);
  OLED::print(SmallSymbolSpace, FontStyle::SMALL);
  OLED::printNumber(stateNumber, 2, FontStyle::SMALL, true);
  OLED::print(SmallSymbolSpace, FontStyle::SMALL);

  if (vbus_sense_state == 2) {
    OLED::print(SmallSymbolNoVBus, FontStyle::SMALL);
  } else if (vbus_sense_state == 1) {
    OLED::print(SmallSymbolVBus, FontStyle::SMALL);
  }
}

void ui_draw_usb_pd_debug_pdo(const uint8_t entry_num, const uint16_t min_voltage, const uint16_t max_voltage, const uint16_t current_a_x100, const uint16_t wattage) {
  OLED::setCursor(0, 0);
  OLED::print(SmallSymbolPDDebug, FontStyle::SMALL);
  OLED::setCursor(0, 8);
  OLED::printNumber(entry_num, 2, FontStyle::SMALL, true);
  OLED::print(SmallSymbolSpace, FontStyle::SMALL);
  if (min_voltage > 0) {
    OLED::printNumber(min_voltage, 2, FontStyle::SMALL, true);
    OLED::print(SmallSymbolMinus, FontStyle::SMALL);
  }
  OLED::printNumber(max_voltage, 2, FontStyle::SMALL, true);
  OLED::print(SmallSymbolVolts, FontStyle::SMALL);
  OLED::print(SmallSymbolSpace, FontStyle::SMALL);
  if (wattage) {
    OLED::printNumber(wattage, 3, FontStyle::SMALL, true);
    OLED::print(SmallSymbolWatts, FontStyle::SMALL);
  } else {
    OLED::printNumber(current_a_x100 / 100, 2, FontStyle::SMALL, true);
    OLED::print(SmallSymbolDot, FontStyle::SMALL);
    OLED::printNumber(current_a_x100 % 100, 2, FontStyle::SMALL, false);
    OLED::print(SmallSymbolAmps, FontStyle::SMALL);
  }
}
#endif
