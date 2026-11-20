/*
 * ColourOLED.cpp
 *
 *  Display driver implementation for 160x40 RGB565 colour panels (COLOUR_160x40).
 *  Replaces OLED.cpp when COLOUR_160x40 is defined, talking directly to GC9Display
 *  without SSD1306/SSD1307 I2C command headers or register tables.
 */

#include "configuration.h"
#ifdef COLOUR_160x40

#include "Buttons.hpp"
#include "ColorTheme.hpp"
#include "ColorUI.hpp"
#include "GC9Display.hpp"
#include "Settings.h"
#include "Translation.h"
#include "cmsis_os.h"
#include <OLED.hpp>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint8_t           *OLED::stripPointers[4];
bool               OLED::inLeftHandedMode = false;
OLED::DisplayState OLED::displayState     = OLED::DisplayState::OFF;
int16_t            OLED::cursor_x         = 0;
int16_t            OLED::cursor_y         = 0;
bool               OLED::initDone         = false;
uint32_t           OLED::displayChecksum  = 0;

alignas(uint32_t) uint8_t OLED::screenBuffer[OLED_WIDTH * (OLED_HEIGHT / 8)];
alignas(uint32_t) uint8_t OLED::secondFrameBuffer[OLED_WIDTH * (OLED_HEIGHT / 8)];

static uint16_t easeInOutTiming(uint16_t t) { return t * t * (300 - 2 * t) / 10000; }
static uint16_t lerp(uint16_t a, uint16_t b, uint16_t t) { return a + t * (b - a) / 100; }

void OLED::initialize() {
  cursor_x = cursor_y = 0;
  inLeftHandedMode    = false;
  setFramebuffer(screenBuffer);
  GC9Display::init();
  setDisplayState(DisplayState::ON);
  initDone = true;
}

void OLED::setFramebuffer(uint8_t *buffer) {
  stripPointers[0] = &buffer[0];
  stripPointers[1] = &buffer[OLED_WIDTH];
  stripPointers[2] = &buffer[2 * OLED_WIDTH];
  stripPointers[3] = &buffer[3 * OLED_WIDTH];
}

void OLED::clearScreen() {
  memset(stripPointers[0], 0, OLED_WIDTH * (OLED_HEIGHT / 8));
  if (stripPointers[0] == screenBuffer) {
    ColorUI::resetScene();
  }
}

bool OLED::checkDisplayBufferChecksum() {
  uint32_t hash = 0;
  if (ColorUI::hasActiveScene()) {
    hash = ColorUI::getSceneHash() ^ 0xA5A5A5A5u;
  } else {
    const int len = sizeof(screenBuffer);
    for (int i = 0; i < len; i++) {
      hash += ((uint32_t)(i + 1) * screenBuffer[i]);
    }
  }
  bool result     = (hash != displayChecksum);
  displayChecksum = hash;
  return result;
}

void OLED::blitFrame() {
  const bool locked = GC9Display::lockFrame();
  if (ColorUI::hasActiveScene()) {
    ColorUI::renderScene();
    GC9Display::unlockFrame(locked);
    return;
  }

  const uint8_t   *pix        = screenBuffer;
  const FrameTheme th         = T90Theme::computeFrame();
  const int        barY0      = GC9_PANEL_W - GC9_HEATBAR_H;
  const int        barLen     = th.heatBar ? (int)((uint32_t)(GC9_PANEL_H - 1) * th.heatPct / 100u) : 0;
  const bool       leftHanded = OLED::getRawRotation();

  GC9Display::beginFrame();
  for (int row = 0; row < GC9_PANEL_H; row++) {
    const int landscapeX = leftHanded ? (GC9_PANEL_H - 1 - row) : row;
    const int x          = landscapeX - GC9_X_OFF;
    for (int col = 0; col < GC9_PANEL_W; col++) {
      const int landscapeY = leftHanded ? col : (GC9_PANEL_W - 1 - col);
      const int y          = landscapeY - GC9_Y_OFF;
      uint16_t  color      = th.bg;
      if (x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT) {
        const uint8_t *strip = pix + (y >> 3) * OLED_WIDTH;
        if ((strip[x] >> (y & 7)) & 1) {
          color = th.fg;
        }
      } else if (th.heatBar && landscapeY >= barY0) {
        color = (landscapeX <= barLen) ? T90Theme::heatBarColorAt((uint8_t)((uint32_t)landscapeX * 255u / (GC9_PANEL_H - 1))) : th.barTrack;
      }
      GC9Display::pushPixel(color);
    }
  }
  GC9Display::endFrame();
  GC9Display::unlockFrame(locked);
}

void OLED::refresh() {
  if (checkDisplayBufferChecksum()) {
    blitFrame();
  }
}

void OLED::setDisplayState(DisplayState state) {
  if (state != displayState) {
    displayState = state;
    GC9Display::setDisplayOn(state == ON);
  }
}

void OLED::setRotation(bool leftHanded) {
#ifdef OLED_FLIP
  leftHanded = !leftHanded;
#endif
  if (inLeftHandedMode == leftHanded) {
    return;
  }
  inLeftHandedMode = leftHanded;
  blitFrame();
  checkDisplayBufferChecksum();
}

void OLED::setBrightness(uint8_t contrast) { (void)contrast; }

void OLED::setInverseDisplay(bool inverted) {
  static bool lastInverted = false;
  if (inverted != lastInverted) {
    lastInverted = inverted;
    GC9Display::setInverted(inverted);
  }
}

void OLED::drawChar(const uint16_t charCode, const FontStyle fontStyle, const uint8_t soft_x_limit) {
  const uint8_t *currentFont;
  static uint8_t fontWidth, fontHeight;
  uint16_t       index;
  switch (fontStyle) {
  case FontStyle::EXTRAS:
    currentFont = ExtraFontChars;
    index       = charCode;
    fontHeight  = 16;
    fontWidth   = 12;
    break;
  case FontStyle::SMALL:
  case FontStyle::LARGE:
  default:
    currentFont = nullptr;
    index       = 0;
    switch (fontStyle) {
    case FontStyle::SMALL:
      fontHeight = 8;
      fontWidth  = 6;
      break;
    case FontStyle::LARGE:
    default:
      fontHeight = 16;
      fontWidth  = 12;
      break;
    }
    if (charCode == '\x01' && cursor_y == 0) {
      setCursor(soft_x_limit, fontHeight);
      return;
    } else if (charCode <= 0x01) {
      return;
    }

    currentFont = fontStyle == FontStyle::SMALL ? FontSectionInfo.font06_start_ptr : FontSectionInfo.font12_start_ptr;
    index       = charCode - 2;
    break;
  }
  const uint8_t *charPointer = currentFont + ((fontWidth * (fontHeight / 8)) * index);
  drawArea(cursor_x, cursor_y, fontWidth, fontHeight, charPointer);
  cursor_x += fontWidth;
}

void OLED::drawScrollIndicator(uint8_t y, uint8_t height) {
  const uint32_t whole     = ((1 << height) - 1) << y;
  const uint8_t  strips[4] = {static_cast<uint8_t>(whole & 0xff), static_cast<uint8_t>((whole & 0xff00) >> 8 * 1), static_cast<uint8_t>((whole & 0xff0000) >> 8 * 2),
                              static_cast<uint8_t>((whole & 0xff000000) >> 8 * 3)};
  fillArea(OLED_WIDTH - 1, 0, 1, 8, strips[0]);
  fillArea(OLED_WIDTH - 1, 8, 1, 8, strips[1]);
  fillArea(OLED_WIDTH - 1, 16, 1, 8, strips[2]);
  fillArea(OLED_WIDTH - 1, 24, 1, 8, strips[3]);
}

void OLED::maskScrollIndicatorOnOLED() {}

void OLED::transitionSecondaryFramebuffer(const bool forwardNavigation, const TickType_t viewEnterTime) {
  (void)viewEnterTime;
  bool     buttonsReleased = getButtonState() == BUTTON_NONE;
  uint8_t *stripBackPointers[4];
  stripBackPointers[0] = &secondFrameBuffer[0];
  stripBackPointers[1] = &secondFrameBuffer[OLED_WIDTH];
  stripBackPointers[2] = &secondFrameBuffer[OLED_WIDTH * 2];
  stripBackPointers[3] = &secondFrameBuffer[OLED_WIDTH * 3];

  TickType_t totalDuration = TICKS_100MS * 5;
  TickType_t duration      = 0;
  TickType_t start         = xTaskGetTickCount();
  uint8_t    offset        = 0;
  uint32_t   loopCounter   = 0;
  TickType_t startDraw     = xTaskGetTickCount();
  while (duration <= totalDuration) {
    loopCounter++;
    duration          = xTaskGetTickCount() - start;
    uint16_t progress = ((duration * 100) / totalDuration);
    progress          = easeInOutTiming(progress);
    progress          = lerp(0, OLED_WIDTH, progress);
    if (progress > OLED_WIDTH) {
      progress = OLED_WIDTH;
    }

    uint8_t oldStart    = forwardNavigation ? 0 : progress;
    uint8_t oldPrevious = forwardNavigation ? progress - offset : offset;
    uint8_t newStart    = forwardNavigation ? OLED_WIDTH - progress : 0;
    uint8_t newEnd      = forwardNavigation ? 0 : OLED_WIDTH - progress;

    offset = progress;

    memmove(&stripPointers[0][oldStart], &stripPointers[0][oldPrevious], OLED_WIDTH - progress);
    memmove(&stripPointers[1][oldStart], &stripPointers[1][oldPrevious], OLED_WIDTH - progress);
    memmove(&stripPointers[2][oldStart], &stripPointers[2][oldPrevious], OLED_WIDTH - progress);
    memmove(&stripPointers[3][oldStart], &stripPointers[3][oldPrevious], OLED_WIDTH - progress);

    memmove(&stripPointers[0][newStart], &stripBackPointers[0][newEnd], progress);
    memmove(&stripPointers[1][newStart], &stripBackPointers[1][newEnd], progress);
    memmove(&stripPointers[2][newStart], &stripBackPointers[2][newEnd], progress);
    memmove(&stripPointers[3][newStart], &stripBackPointers[3][newEnd], progress);

    if (loopCounter % 2 == 0) {
      refresh();
    }

    vTaskDelayUntil(&startDraw, TICKS_100MS / 7);
    buttonsReleased |= getButtonState() == BUTTON_NONE;
    if (getButtonState() != BUTTON_NONE && buttonsReleased) {
      memcpy(screenBuffer, secondFrameBuffer, sizeof(screenBuffer));
      refresh();
      return;
    }
  }
  refresh();
}

void OLED::useSecondaryFramebuffer(bool useSecondary) {
  if (useSecondary) {
    setFramebuffer(secondFrameBuffer);
  } else {
    setFramebuffer(screenBuffer);
  }
}

void OLED::transitionScrollDown(const TickType_t viewEnterTime) {
  (void)viewEnterTime;
  TickType_t startDraw       = xTaskGetTickCount();
  bool       buttonsReleased = getButtonState() == BUTTON_NONE;

  for (uint8_t heightPos = 0; heightPos < OLED_HEIGHT; heightPos++) {
    for (uint8_t xPos = 0; xPos < OLED_WIDTH; xPos++) {
      const uint16_t firstStripPos  = xPos;
      const uint16_t secondStripPos = firstStripPos + OLED_WIDTH;
      const uint16_t thirdStripPos  = secondStripPos + OLED_WIDTH;
      const uint16_t fourthStripPos = thirdStripPos + OLED_WIDTH;

      screenBuffer[firstStripPos]  = (screenBuffer[firstStripPos] >> 1) | ((screenBuffer[secondStripPos] & 0x01) << 7);
      screenBuffer[secondStripPos] = (screenBuffer[secondStripPos] >> 1) | ((screenBuffer[thirdStripPos] & 0x01) << 7);
      screenBuffer[thirdStripPos]  = (screenBuffer[thirdStripPos] >> 1) | ((screenBuffer[fourthStripPos] & 0x01) << 7);
      screenBuffer[fourthStripPos] = (screenBuffer[fourthStripPos] >> 1) | ((secondFrameBuffer[firstStripPos] & 0x01) << 7);

      secondFrameBuffer[firstStripPos]  = (secondFrameBuffer[firstStripPos] >> 1) | ((secondFrameBuffer[secondStripPos] & 0x01) << 7);
      secondFrameBuffer[secondStripPos] = (secondFrameBuffer[secondStripPos] >> 1) | ((secondFrameBuffer[thirdStripPos] & 0x01) << 7);
      secondFrameBuffer[thirdStripPos]  = (secondFrameBuffer[thirdStripPos] >> 1) | ((secondFrameBuffer[fourthStripPos] & 0x01) << 7);
      secondFrameBuffer[fourthStripPos] >>= 1;
    }
    buttonsReleased |= getButtonState() == BUTTON_NONE;
    if (getButtonState() != BUTTON_NONE && buttonsReleased) {
      memcpy(screenBuffer, secondFrameBuffer, sizeof(screenBuffer));
      refresh();
      return;
    }
    if (heightPos % 2 == 0) {
      refresh();
    }
    vTaskDelayUntil(&startDraw, TICKS_100MS / 7);
  }
}

void OLED::transitionScrollUp(const TickType_t viewEnterTime) {
  (void)viewEnterTime;
  TickType_t startDraw       = xTaskGetTickCount();
  bool       buttonsReleased = getButtonState() == BUTTON_NONE;

  for (uint8_t heightPos = 0; heightPos < OLED_HEIGHT; heightPos++) {
    for (uint8_t xPos = 0; xPos < OLED_WIDTH; xPos++) {
      const uint16_t firstStripPos  = xPos;
      const uint16_t secondStripPos = firstStripPos + OLED_WIDTH;
      const uint16_t thirdStripPos  = secondStripPos + OLED_WIDTH;
      const uint16_t fourthStripPos = thirdStripPos + OLED_WIDTH;

      screenBuffer[fourthStripPos] = (screenBuffer[fourthStripPos] << 1) | ((screenBuffer[thirdStripPos] & 0x80) >> 7);
      screenBuffer[thirdStripPos]  = (screenBuffer[thirdStripPos] << 1) | ((screenBuffer[secondStripPos] & 0x80) >> 7);
      screenBuffer[secondStripPos] = (screenBuffer[secondStripPos] << 1) | ((screenBuffer[firstStripPos] & 0x80) >> 7);
      screenBuffer[firstStripPos]  = (screenBuffer[firstStripPos] << 1) | ((secondFrameBuffer[fourthStripPos] & 0x80) >> 7);

      secondFrameBuffer[fourthStripPos] = (secondFrameBuffer[fourthStripPos] << 1) | ((secondFrameBuffer[thirdStripPos] & 0x80) >> 7);
      secondFrameBuffer[thirdStripPos]  = (secondFrameBuffer[thirdStripPos] << 1) | ((secondFrameBuffer[secondStripPos] & 0x80) >> 7);
      secondFrameBuffer[secondStripPos] = (secondFrameBuffer[secondStripPos] << 1) | ((secondFrameBuffer[firstStripPos] & 0x80) >> 7);
      secondFrameBuffer[firstStripPos] <<= 1;
    }
    buttonsReleased |= getButtonState() == BUTTON_NONE;
    if (getButtonState() != BUTTON_NONE && buttonsReleased) {
      memcpy(screenBuffer, secondFrameBuffer, sizeof(screenBuffer));
      refresh();
      return;
    }
    if (heightPos % 2 == 0) {
      refresh();
    }
    vTaskDelayUntil(&startDraw, TICKS_100MS / 7);
  }
}

void OLED::print(const char *const str, FontStyle fontStyle, uint8_t len, const uint8_t soft_x_limit) {
  const uint8_t *next = reinterpret_cast<const uint8_t *>(str);
  if (next[0] == 0x01) {
    fontStyle = FontStyle::LARGE;
    next++;
  }
  while (next[0] && len--) {
    uint16_t index;
    if (next[0] <= 0xF0) {
      index = next[0];
      next++;
    } else {
      if (!next[1]) {
        return;
      }
      index = (next[0] - 0xF0) * 0xFF - 15 + next[1];
      next += 2;
    }
    drawChar(index, fontStyle, soft_x_limit);
  }
}

void OLED::printWholeScreen(const char *string) {
  setCursor(0, 0);
  if (string[0] == '\x01') {
    OLED::print(string + 1, FontStyle::LARGE);
  } else {
    OLED::print(string, FontStyle::SMALL);
  }
}

void OLED::printSymbolDeg(const FontStyle fontStyle) {
  switch (fontStyle) {
  case FontStyle::EXTRAS:
    OLED::drawSymbol(getSettingValue(SettingsOptions::TemperatureInF) ? 0 : 1);
    break;
  case FontStyle::LARGE:
    OLED::print(getSettingValue(SettingsOptions::TemperatureInF) ? LargeSymbolDegF : LargeSymbolDegC, fontStyle);
    break;
  case FontStyle::SMALL:
  default:
    OLED::print(getSettingValue(SettingsOptions::TemperatureInF) ? SmallSymbolDegF : SmallSymbolDegC, fontStyle);
    break;
  }
}

inline void stripLeaderZeros(char *buffer, uint8_t places) {
  for (int i = 0; i < (places - 1); i++) {
    if (buffer[i] == 2) {
      buffer[i] = LargeSymbolSpace[0];
    } else {
      return;
    }
  }
}

void OLED::drawHex(uint32_t x, FontStyle fontStyle, uint8_t digits) {
  for (uint_fast8_t i = 0; i < digits; i++) {
    uint16_t value = (x >> (4 * (7 - i))) & 0b1111;
    drawChar(value + 2, fontStyle, 0);
  }
}

void OLED::printNumber(uint16_t number, uint8_t places, FontStyle fontStyle, bool noLeaderZeros) {
  char buffer[7] = {0};

  if (places >= 5) {
    buffer[5] = 2 + number % 10;
    number /= 10;
  }
  if (places > 4) {
    buffer[4] = 2 + number % 10;
    number /= 10;
  }
  if (places > 3) {
    buffer[3] = 2 + number % 10;
    number /= 10;
  }
  if (places > 2) {
    buffer[2] = 2 + number % 10;
    number /= 10;
  }
  if (places > 1) {
    buffer[1] = 2 + number % 10;
    number /= 10;
  }

  buffer[0] = 2 + number % 10;
  if (noLeaderZeros) {
    stripLeaderZeros(buffer, places);
  }
  print(buffer, fontStyle);
}

void OLED::debugNumber(int32_t val, FontStyle fontStyle) {
  if (abs(val) > 99999) {
    OLED::print(LargeSymbolSpace, fontStyle);
    return;
  }
  if (val >= 0) {
    OLED::print(LargeSymbolSpace, fontStyle);
    OLED::printNumber(val, 5, fontStyle);
  } else {
    OLED::print(LargeSymbolMinus, fontStyle);
    OLED::printNumber(-val, 5, fontStyle);
  }
}

void OLED::drawSymbol(uint8_t symbolID) { drawChar(symbolID, FontStyle::EXTRAS, 0); }

void OLED::drawArea(int16_t x, int8_t y, uint8_t width, uint8_t height, const uint8_t *ptr) {
  if (x <= -width || x > OLED_WIDTH) {
    return;
  }
  uint8_t visibleStart = 0;
  uint8_t visibleEnd   = width;
  if (x < 0) {
    visibleStart -= x;
  }
  if (x + width > OLED_WIDTH) {
    visibleEnd = OLED_WIDTH - x;
  }
  uint8_t rowsDrawn = 0;
  while (height > 0) {
    for (uint8_t xx = visibleStart; xx < visibleEnd; xx++) {
      stripPointers[(y / 8) + rowsDrawn][x + xx] = ptr[xx + (rowsDrawn * width)];
    }
    height -= 8;
    rowsDrawn++;
  }
}

void OLED::drawAreaSwapped(int16_t x, int8_t y, uint8_t width, uint8_t height, const uint8_t *ptr) {
  if (x <= -width || x > OLED_WIDTH) {
    return;
  }
  uint8_t visibleStart = 0;
  uint8_t visibleEnd   = width;
  if (x < 0) {
    visibleStart -= x;
  }
  if (x + width > OLED_WIDTH) {
    visibleEnd = OLED_WIDTH - x;
  }
  uint8_t rowsDrawn = 0;
  while (height > 0) {
    for (uint8_t xx = visibleStart; xx < visibleEnd; xx += 2) {
      stripPointers[(y / 8) + rowsDrawn][x + xx]     = ptr[xx + 1 + (rowsDrawn * width)];
      stripPointers[(y / 8) + rowsDrawn][x + xx + 1] = ptr[xx + (rowsDrawn * width)];
    }
    height -= 8;
    rowsDrawn++;
  }
}

void OLED::fillArea(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t value) {
  if (x <= -wide || x > OLED_WIDTH) {
    return;
  }
  uint8_t visibleStart = 0;
  uint8_t visibleEnd   = wide;
  if (x < 0) {
    visibleStart -= x;
  }
  if (x + wide > OLED_WIDTH) {
    visibleEnd = OLED_WIDTH - x;
  }
  uint8_t rowsDrawn = 0;
  while (height > 0) {
    for (uint8_t xx = visibleStart; xx < visibleEnd; xx++) {
      stripPointers[(y / 8) + rowsDrawn][x + xx] = value;
    }
    height -= 8;
    rowsDrawn++;
  }
}

void OLED::drawFilledRect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool clear) {
  if (x0 >= OLED_WIDTH || y0 >= OLED_HEIGHT || x1 >= OLED_WIDTH || y1 >= OLED_HEIGHT) {
    return;
  }
  uint8_t startRow  = y0 / 8;
  uint8_t endRow    = y1 / 8;
  uint8_t startMask = 0xFF << (y0 % 8);
  uint8_t endMask   = 0xFF >> (7 - (y1 % 8));

  for (uint8_t row = startRow; row <= endRow; row++) {
    uint8_t mask = 0xFF;
    if (row == startRow) {
      mask &= startMask;
    }
    if (row == endRow) {
      mask &= endMask;
    }
    for (uint8_t x = x0; x <= x1; x++) {
      if (clear) {
        stripPointers[row][x] &= ~mask;
      } else {
        stripPointers[row][x] |= mask;
      }
    }
  }
}

void OLED::drawHeatSymbol(uint8_t state) {
  state /= 31;
  uint16_t cursor_x_temp = cursor_x;
  drawSymbol(14);
  drawFilledRect(cursor_x_temp, 0, cursor_x_temp + 12, 2 + (8 - state), true);
}

bool OLED::isInitDone() { return initDone; }

#endif /* COLOUR_160x40 */
