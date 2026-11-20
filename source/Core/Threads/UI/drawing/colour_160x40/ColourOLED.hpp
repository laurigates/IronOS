/*
 * ColourOLED.hpp
 *
 *  Display driver interface for 160x40 RGB565 colour panels (COLOUR_160x40).
 *  Replaces the SSD1306/SSD1307 I2C OLED driver when COLOUR_160x40 is defined
 *  and talks directly to the hardware panel driver (GC9Display) without I2C
 *  framing or SSD1306 command buffers.
 */

#ifndef UI_DRAWING_COLOUR_160X40_COLOUROLED_HPP_
#define UI_DRAWING_COLOUR_160X40_COLOUROLED_HPP_

#include "configuration.h"
#ifdef COLOUR_160x40

#include "Font.h"
#include "cmsis_os.h"
#include <BSP.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS.h"
#ifdef __cplusplus
}
#endif

// Logical 1bpp canvas dimensions (centered inside the 160x40 colour panel for
// menus, warnings, boot logos and fallback screens).
#define OLED_WIDTH  128
#define OLED_HEIGHT 32

enum class FontStyle {
  SMALL,
  LARGE,
  EXTRAS,
  TINY,
};

class OLED {
public:
  enum DisplayState : bool { OFF = false, ON = true };

  static void initialize();
  static bool isInitDone();
  static void refresh();
  static void setDisplayState(DisplayState state);

  // Set the rotation for the screen
  static void setRotation(bool leftHanded);

  // True left/right-hand orientation state (used by the full-frame transpose and
  // by rotation-aware button indicators in draw_temperature_change).
  static bool getRawRotation() {
#ifdef OLED_FLIP
    return !inLeftHandedMode;
#else
    return inLeftHandedMode;
#endif
  }

  // Because ColourOLED rotates the entire 160x40 frame during the panel blit,
  // 1bpp UI layout code draws a single un-rotated layout.
  static bool getRotation() { return false; }

  static void    setBrightness(uint8_t contrast);
  static void    setInverseDisplay(bool inverted);
  static int16_t getCursorX() { return cursor_x; }
  static void    print(const char *string, FontStyle fontStyle, uint8_t length = 255, const uint8_t soft_x_limit = 0);
  static void    printWholeScreen(const char *string);
  static void    printSymbolDeg(FontStyle fontStyle = FontStyle::LARGE);
  static void    setCursor(int16_t x, int16_t y) {
    cursor_x = x;
    cursor_y = y;
  }
  static void        drawImage(const uint8_t *buffer, uint8_t x, uint8_t width) { drawArea(x, 0, width, 16, buffer); }
  static void        printNumber(uint16_t number, uint8_t places, FontStyle fontStyle, bool noLeaderZeros = true);
  static void        clearScreen();
  static void        drawBattery(uint8_t state) { drawSymbol(3 + (state > 10 ? 10 : state)); }
  static void        drawCheckbox(bool state) { drawSymbol((state) ? 16 : 17); }
  inline static void drawUnavailableIcon() { drawArea(OLED_WIDTH - OLED_HEIGHT - 2, 0, OLED_HEIGHT, OLED_HEIGHT, UnavailableIcon); }
  static void        debugNumber(int32_t val, FontStyle fontStyle);
  static void        drawHex(uint32_t x, FontStyle fontStyle, uint8_t digits);
  static void        drawSymbol(uint8_t symbolID);
  static void        drawArea(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t *ptr);
  static void        drawAreaSwapped(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t *ptr);
  static void        fillArea(int16_t x, int8_t y, uint8_t wide, uint8_t height, const uint8_t value);
  static void        drawFilledRect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool clear);
  static void        drawHeatSymbol(uint8_t state);
  static void        drawScrollIndicator(uint8_t p, uint8_t h);
  static void        maskScrollIndicatorOnOLED();
  static void        transitionSecondaryFramebuffer(const bool forwardNavigation, const TickType_t viewEnterTime);
  static void        useSecondaryFramebuffer(bool useSecondary);
  static void        transitionScrollDown(const TickType_t viewEnterTime);
  static void        transitionScrollUp(const TickType_t viewEnterTime);

private:
  static bool         checkDisplayBufferChecksum();
  static void         blitFrame();
  static void         drawChar(uint16_t charCode, FontStyle fontStyle, const uint8_t soft_x_limit);
  static void         setFramebuffer(uint8_t *buffer);
  static uint8_t     *stripPointers[4];
  static bool         inLeftHandedMode;
  static bool         initDone;
  static DisplayState displayState;
  static int16_t      cursor_x, cursor_y;
  static uint32_t     displayChecksum;
  static uint8_t      screenBuffer[OLED_WIDTH * (OLED_HEIGHT / 8)];
  static uint8_t      secondFrameBuffer[OLED_WIDTH * (OLED_HEIGHT / 8)];
};

#endif /* COLOUR_160x40 */
#endif /* UI_DRAWING_COLOUR_160X40_COLOUROLED_HPP_ */
