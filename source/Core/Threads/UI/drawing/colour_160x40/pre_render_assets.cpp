#include "ui_drawing.hpp"
#ifdef COLOUR_160x40

// ColourOLED rotates the entire 160x40 frame in software during the SPI blit,
// so no pre-flipped RAM copies of buttonA/buttonB/disconnectedTip are needed.
void ui_pre_render_assets(void) {}
#endif
