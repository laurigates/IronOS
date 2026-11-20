/*
 * ColorUI.hpp
 *
 *  Native RGB565 "hero" screen renderer for 160x40 colour displays (COLOUR_160x40).
 *  Invoked directly from UI/drawing/colour_160x40/draw_soldering_*.cpp and streamed
 *  by ColourOLED::refresh().
 */

#ifndef UI_DRAWING_COLOUR_160X40_COLORUI_HPP_
#define UI_DRAWING_COLOUR_160X40_COLORUI_HPP_

#include "configuration.h"
#ifdef COLOUR_160x40

#include <stdbool.h>
#include <stdint.h>

namespace ColorUI {
// Clear any pending native colour scene (called on OLED::clearScreen(), e.g. at the
// start of each frame or when a full-screen warning replaces the soldering screen).
void resetScene();

// Prepare the native RGB565 soldering screen for the current frame.
void prepareSoldering(bool boostModeOn);

// True if a native colour scene was prepared for this frame and not cancelled by a warning.
bool hasActiveScene();

// Hash of the active colour scene parameters for frame-change detection.
uint32_t getSceneHash();

// Stream the prepared RGB565 scene to GC9Display (caller holds GC9Display::lockFrame()).
void renderScene();
} // namespace ColorUI

#endif // COLOUR_160x40
#endif /* UI_DRAWING_COLOUR_160X40_COLORUI_HPP_ */
