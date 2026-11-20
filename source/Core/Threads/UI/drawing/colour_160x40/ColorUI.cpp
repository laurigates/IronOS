/*
 * ColorUI.cpp
 *
 *  Native RGB565 "hero" screen renderer for 160x40 colour displays (see ColorUI.hpp).
 */

#include "ColorUI.hpp"

#ifdef COLOUR_160x40

#include "BSP.h"          // getInputVoltageX10, powerPWM, isTipDisconnected
#include "ColorTheme.hpp" // rgb565, heatColorForTempC, heatBarColorAt
#include "Font.h"         // ExtraFontChars
#include "GC9Display.hpp" // beginFrame/pushPixel/endFrame + panel geometry
#include "IRQ.h"          // pendingPWM
#include "OLED.hpp"       // OLED::getRawRotation
#include "Settings.h"     // getSettingValue, SettingsOptions
#include "SolderingCommon.h"
#include "TipThermoModel.h" // TipThermoModel::getTipInC
#include "Translation.h"    // FontSectionInfo
#include "power.hpp"        // x10WattHistory

#define LCD_W GC9_PANEL_H // 160 (landscape width)
#define LCD_H GC9_PANEL_W // 40  (landscape height)

namespace {

bool     g_activeScene = false;
bool     g_boostMode   = false;
uint32_t g_sceneHash   = 0;

// --- 1bpp glyph bit access (column-major, LSB = top pixel of each 8px strip; matches OLED.cpp) ---
bool fontBit(const uint8_t *font, uint8_t index, uint8_t w, uint8_t h, int gx, int gy) {
  if (gx < 0 || gy < 0 || gx >= w || gy >= h) {
    return false;
  }
  const uint8_t *g = font + ((uint16_t)w * (h / 8)) * index;
  return (g[(gy >> 3) * w + gx] >> (gy & 7)) & 1;
}

uint8_t smGlyph(const char *smallSymbol) { return (uint8_t)smallSymbol[0] - 2; }

struct Placement {
  const uint8_t *font;
  uint8_t        index, w, h, scale;
  int16_t        x, y;
  uint16_t       color;
};

Placement g_place[40];
int       g_nplace  = 0;
uint16_t  g_bg      = 0;
bool      g_bar     = false;
int       g_barFill = 0; // lit length of the bottom bar, 0..LCD_W-1

const uint8_t *font12() { return FontSectionInfo.font12_start_ptr; }
const uint8_t *font06() { return FontSectionInfo.font06_start_ptr; }

void put(const uint8_t *font, uint8_t index, uint8_t w, uint8_t h, uint8_t scale, int x, int y, uint16_t color) {
  if (g_nplace < (int)(sizeof(g_place) / sizeof(g_place[0]))) {
    g_place[g_nplace++] = {font, index, w, h, scale, (int16_t)x, (int16_t)y, color};
  }
}

int putBigNumber(int value, int x, int y, uint8_t scale, uint16_t color) {
  uint8_t digits[5];
  int     n = 0;
  if (value < 0) {
    value = 0;
  }
  do {
    digits[n++] = value % 10;
    value /= 10;
  } while (value > 0 && n < 5);
  for (int i = n - 1; i >= 0; i--) {
    put(font12(), digits[i], 12, 16, scale, x, y, color);
    x += 12 * scale;
  }
  return x;
}

int putSmallNumber(int value, int x, int y, uint16_t color) {
  uint8_t digits[5];
  int     n = 0;
  if (value < 0) {
    value = 0;
  }
  do {
    digits[n++] = value % 10;
    value /= 10;
  } while (value > 0 && n < 5);
  for (int i = n - 1; i >= 0; i--) {
    put(font06(), digits[i], 6, 8, 1, x, y, color);
    x += 6;
  }
  return x;
}

void putSmallChar(uint8_t idx, int x, int y, uint16_t color) { put(font06(), idx, 6, 8, 1, x, y, color); }

uint16_t scenePixel(int x, int y) {
  if (g_bar && y >= LCD_H - 3) {
    if (x <= g_barFill) {
      return T90Theme::heatBarColorAt((uint8_t)((uint32_t)x * 255u / (LCD_W - 1)));
    }
    return rgb565(26, 30, 36);
  }
  for (int i = 0; i < g_nplace; i++) {
    const Placement &p  = g_place[i];
    const int        pw = p.w * p.scale, ph = p.h * p.scale;
    if (x >= p.x && x < p.x + pw && y >= p.y && y < p.y + ph) {
      if (fontBit(p.font, p.index, p.w, p.h, (x - p.x) / p.scale, (y - p.y) / p.scale)) {
        return p.color;
      }
    }
  }
  return g_bg;
}

} // namespace

void ColorUI::resetScene() {
  g_activeScene = false;
  g_sceneHash   = 0;
}

void ColorUI::prepareSoldering(bool boostModeOn) {
  if (isTipDisconnected()) {
    g_activeScene = false;
    return;
  }
  g_activeScene = true;
  g_boostMode   = boostModeOn;

  const bool     inF     = getSettingValue(SettingsOptions::TemperatureInF) != 0;
  const int32_t  tipC    = TipThermoModel::getTipInC();
  const int32_t  tipShow = getTipTemp();
  const int32_t  setShow = boostModeOn ? getSettingValue(SettingsOptions::BoostTemp) : getSettingValue(SettingsOptions::SolderingTemp);
  const uint16_t vX10    = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0);
  const uint32_t wX10    = x10WattHistory.average();

  // Compute a deterministic hash over all displayed colour soldering parameters so ColourOLED::refresh()
  // updates the panel whenever tip temperature, setpoint/boost, voltage, wattage, PWM duty, or orientation changes.
  uint32_t h  = 0x811C9DC5u;
  h           = (h ^ (uint32_t)tipC) * 16777619u;
  h           = (h ^ (uint32_t)tipShow) * 16777619u;
  h           = (h ^ (uint32_t)setShow) * 16777619u;
  h           = (h ^ (uint32_t)vX10) * 16777619u;
  h           = (h ^ (uint32_t)wX10) * 16777619u;
  h           = (h ^ (uint32_t)pendingPWM) * 16777619u;
  h           = (h ^ (uint32_t)inF) * 16777619u;
  h           = (h ^ (uint32_t)boostModeOn) * 16777619u;
  g_sceneHash = h ? h : 1u;
}

bool ColorUI::hasActiveScene() { return g_activeScene; }

uint32_t ColorUI::getSceneHash() { return g_sceneHash; }

void ColorUI::renderScene() {
  const bool     inF     = getSettingValue(SettingsOptions::TemperatureInF) != 0;
  const int32_t  tipC    = TipThermoModel::getTipInC();
  const int32_t  tipShow = getTipTemp();
  const int32_t  setShow = g_boostMode ? getSettingValue(SettingsOptions::BoostTemp) : getSettingValue(SettingsOptions::SolderingTemp);
  const uint16_t vX10    = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0);
  const uint32_t wX10    = x10WattHistory.average();
  const uint16_t heat    = T90Theme::heatColorForTempC(tipC);

  g_nplace = 0;
  g_bg     = rgb565(0, 0, 0);
  g_bar    = false;

  // Big heat-mapped temperature number (2x large font) + degree/unit glyph.
  int x = putBigNumber((int)tipShow, 6, 4, 2, heat);
  put(ExtraFontChars, inF ? 0 : 1, 12, 16, 1, x + 3, 4, heat);

  // Right column: voltage (teal), wattage (white), setpoint (amber, or red-orange during boost).
  const int      rx    = 100;
  const uint16_t teal  = rgb565(0, 200, 210);
  const uint16_t white = rgb565(235, 235, 235);
  const uint16_t amber = g_boostMode ? rgb565(255, 90, 30) : rgb565(255, 170, 40);
  {
    int xx = putSmallNumber(vX10 / 10, rx, 3, teal);
    putSmallChar(smGlyph(SmallSymbolDot), xx, 3, teal);
    xx = putSmallNumber(vX10 % 10, xx + 6, 3, teal);
    putSmallChar(smGlyph(SmallSymbolVolts), xx, 3, teal);
  }
  {
    int xx = putSmallNumber(wX10 / 10, rx, 15, white);
    putSmallChar(smGlyph(SmallSymbolDot), xx, 15, white);
    xx = putSmallNumber(wX10 % 10, xx + 6, 15, white);
    putSmallChar(smGlyph(SmallSymbolWatts), xx, 15, white);
  }
  putSmallNumber((int)setShow, rx, 27, amber);

  // Bottom power bar from the live heater duty.
  const uint16_t denom = powerPWM ? powerPWM : 255;
  int            pct   = (int)((uint32_t)pendingPWM * 100u / denom);
  if (pct > 100) {
    pct = 100;
  }
  g_bar     = true;
  g_barFill = (LCD_W - 1) * pct / 100;

  const bool leftHanded = OLED::getRawRotation();
  GC9Display::beginFrame();
  for (int row = 0; row < GC9_PANEL_H; row++) {
    const int lx = leftHanded ? (GC9_PANEL_H - 1 - row) : row;
    for (int col = 0; col < GC9_PANEL_W; col++) {
      const int ly = leftHanded ? col : (GC9_PANEL_W - 1 - col);
      GC9Display::pushPixel(scenePixel(lx, ly));
    }
  }
  GC9Display::endFrame();
}

#endif // COLOUR_160x40
