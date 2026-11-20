/*
 * GC9Display.hpp
 *
 *  Alientek T90 (Nations N32L40x) GC9-family color LCD hardware driver over SPI1.
 *  Pure hardware panel interface (init, power/inversion commands, and RGB565
 *  pixel streaming); all UI drawing and colour theming live in UI/drawing/colour_160x40.
 */

#ifndef BSP_ALIENTEK_T90_GC9DISPLAY_HPP_
#define BSP_ALIENTEK_T90_GC9DISPLAY_HPP_

#include "configuration.h"
#ifdef OLED_GC9D01

#include <stdbool.h>
#include <stdint.h>

// Default background fill color during initial hardware clear.
#define COLOR_BG ((uint16_t)0x0000)

// Native GC9 panel GRAM geometry (portrait), zero start offset on both axes.
#define GC9_PANEL_W 40  // columns (CASET 0..39)
#define GC9_PANEL_H 160 // rows    (RASET 0..159)

// Landscape canvas is 160x40; the 128x32 mono canvas is centered inside it.
#define GC9_X_OFF 16 // (160 - 128) / 2 -> left/right margin in landscape x
#define GC9_Y_OFF 4  // ( 40 -  32) / 2 -> top/bottom margin in landscape y

class GC9Display {
public:
  // Lifecycle: both perform the full (idempotent) panel + SPI + GPIO bringup.
  static void init();
  static void FRToSInit();

  // Panel power + backlight state (true = display on + backlight on).
  static void setDisplayOn(bool on);

  // Panel hardware colour inversion (GC9 INVON/INVOFF).
  static void setInverted(bool inverted);

  // Serialise full-frame SPI transactions across the GUI and MOV tasks.
  static bool lockFrame();
  static void unlockFrame(bool locked);

  // Full-panel RGB565 pixel stream (row 0..159 outer, col 0..39 inner).
  static void beginFrame();
  static void pushPixel(uint16_t color);
  static void endFrame();

private:
  static bool initialised;
};

#endif // OLED_GC9D01
#endif /* BSP_ALIENTEK_T90_GC9DISPLAY_HPP_ */
