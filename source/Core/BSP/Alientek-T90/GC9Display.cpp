/*
 * GC9Display.cpp
 *
 *  Alientek T90 (Nations N32L40x) GC9-family color LCD hardware driver over SPI1.
 *  Pure hardware panel driver against the Nations std-periph driver; all UI
 *  drawing and colourisation live in source/Core/Threads/UI/drawing/colour_160x40.
 */

#include "GC9Display.hpp"

#ifdef OLED_GC9D01

#include "BSP.h"      // delay_ms
#include "FreeRTOS.h" // display mutex: serialise full-frame SPI writes across GUI and MOV tasks
#include "Pins.h"     // LCD_* pin macros + n32l40x.h
#include "semphr.h"
#include "task.h" // xTaskGetSchedulerState

// GC9 command opcodes.
#define GC9_CMD_SLPOUT  0x11
#define GC9_CMD_INVOFF  0x20
#define GC9_CMD_INVON   0x21
#define GC9_CMD_DISPOFF 0x28
#define GC9_CMD_DISPON  0x29
#define GC9_CMD_CASET   0x2A
#define GC9_CMD_RASET   0x2B
#define GC9_CMD_RAMWR   0x2C
#define GC9_CMD_MADCTL  0x36
#define GC9_CMD_COLMOD  0x3A

bool GC9Display::initialised = false;

// Serialises the full-frame render path across GUI and MOV tasks.
static StaticSemaphore_t displayMutexBuffer;
static SemaphoreHandle_t displayMutex = NULL;

// ---------------------------------------------------------------------------
// Low-level SPI1 helpers (polled, blocking; Mode 3, half-duplex TX-only)
// ---------------------------------------------------------------------------

#define GC9_SPI_TIMEOUT 100000U

// Wait until the TX register is empty and the bus is idle, then change DFF.
// DFF must only be toggled while SPI is not busy (silicon requirement).
static inline void gc9_wait_idle(void) {
  uint32_t to = GC9_SPI_TIMEOUT;
  while (SPI_I2S_GetStatus(SPI1, SPI_I2S_TE_FLAG) == RESET && --to) {
  }
  to = GC9_SPI_TIMEOUT;
  while (SPI_I2S_GetStatus(SPI1, SPI_I2S_BUSY_FLAG) != RESET && --to) {
  }
}

static inline void gc9_set_datalen(uint16_t len) {
  gc9_wait_idle();
  SPI_ConfigDataLen(SPI1, len);
}

// Push one byte (assumes 8-bit DFF). Caller frames CS/DC.
static inline void gc9_spi_byte(uint8_t b) {
  uint32_t to = GC9_SPI_TIMEOUT;
  while (SPI_I2S_GetStatus(SPI1, SPI_I2S_TE_FLAG) == RESET && --to) {
  }
  SPI_I2S_TransmitData(SPI1, b);
}

// Push one 16-bit word (assumes 16-bit DFF). Caller frames CS.
static inline void gc9_spi_word(uint16_t w) {
  uint32_t to = GC9_SPI_TIMEOUT;
  while (SPI_I2S_GetStatus(SPI1, SPI_I2S_TE_FLAG) == RESET && --to) {
  }
  SPI_I2S_TransmitData(SPI1, w);
}

// Write a command byte: DC low, CS low, send (8-bit), CS high, DC high.
static void gc9_cmd(uint8_t cmd) {
  GPIO_ResetBits(LCD_DC_GPIO_Port, LCD_DC_Pin); // command
  GPIO_ResetBits(LCD_CS_GPIO_Port, LCD_CS_Pin); // select
  gc9_spi_byte(cmd);
  gc9_wait_idle();
  GPIO_SetBits(LCD_CS_GPIO_Port, LCD_CS_Pin);
  GPIO_SetBits(LCD_DC_GPIO_Port, LCD_DC_Pin); // back to data idle
}

// Write a data byte: DC high, CS low, send (8-bit), CS high.
static void gc9_data(uint8_t d) {
  GPIO_SetBits(LCD_DC_GPIO_Port, LCD_DC_Pin); // data
  GPIO_ResetBits(LCD_CS_GPIO_Port, LCD_CS_Pin);
  gc9_spi_byte(d);
  gc9_wait_idle();
  GPIO_SetBits(LCD_CS_GPIO_Port, LCD_CS_Pin);
}

// Write a 16-bit data value big-endian (hi byte then lo byte), 8-bit DFF.
static void gc9_data16(uint16_t v) {
  gc9_data((uint8_t)(v >> 8));
  gc9_data((uint8_t)(v & 0xFF));
}

// Send a command followed by N data bytes (init stream helper).
static void gc9_cmd_data(uint8_t cmd, const uint8_t *data, uint8_t n) {
  gc9_cmd(cmd);
  for (uint8_t i = 0; i < n; i++) {
    gc9_data(data[i]);
  }
}

// ---------------------------------------------------------------------------
// GC9 init command stream (verified vendor table)
// ---------------------------------------------------------------------------

static void gc9_run_init_sequence(void) {
  // Power-on settle before the software unlock (the T90 stock firmware waits ~200 ms here before
  // 0xFE/0xEF; without it the unlock can land before the panel's internal regulators are ready).
  delay_ms(120);
  // Inner register enable (GC9-family software unlock).
  gc9_cmd(0xFE);
  gc9_cmd(0xEF);

  // Inter-register page-enable block: 0x80..0x8F each = 0xFF.
  for (uint8_t reg = 0x80; reg <= 0x8F; reg++) {
    static const uint8_t ff = 0xFF;
    gc9_cmd_data(reg, &ff, 1);
  }

  // Pixel format = 16 bpp RGB565 (mandatory, matches the RGB565 blit).
  {
    static const uint8_t d[] = {0x05};
    gc9_cmd_data(GC9_CMD_COLMOD, d, sizeof(d));
  }

  // Vendor charge-pump / timing.
  {
    static const uint8_t d[] = {0x11};
    gc9_cmd_data(0xEC, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x7A};
    gc9_cmd_data(0x7E, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x02, 0x0E, 0x00, 0x00, 0x28, 0x00, 0x00};
    gc9_cmd_data(0x74, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x3E};
    gc9_cmd_data(0x98, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x3E};
    gc9_cmd_data(0x99, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x0E, 0x0E};
    gc9_cmd_data(0xB5, d, sizeof(d));
  }

  // Gate / source timing.
  {
    static const uint8_t d[] = {0x38, 0x09, 0x6D, 0x67};
    gc9_cmd_data(0x60, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x38, 0xAD, 0x6D, 0x67, 0x05};
    gc9_cmd_data(0x63, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x38, 0x0B, 0x70, 0xAB, 0x6D, 0x67};
    gc9_cmd_data(0x64, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x38, 0x0F, 0x70, 0xAF, 0x6D, 0x67};
    gc9_cmd_data(0x66, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x00, 0x00};
    gc9_cmd_data(0x6A, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x3B, 0x08, 0x04, 0x00, 0x04, 0x64, 0x67};
    gc9_cmd_data(0x68, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x22, 0x02, 0x22, 0x02, 0x22, 0x22, 0x50};
    gc9_cmd_data(0x6C, d, sizeof(d));
  }

  // Gamma / gate LUT (32 bytes).
  {
    static const uint8_t d[] = {0x00, 0x00, 0x00, 0x00, 0x07, 0x01, 0x13, 0x11, 0x0B, 0x09, 0x16, 0x15, 0x1D, 0x1E, 0x00, 0x00,
                                0x00, 0x00, 0x1E, 0x1D, 0x15, 0x16, 0x0A, 0x0C, 0x12, 0x14, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00};
    gc9_cmd_data(0x6E, d, sizeof(d));
  }

  // Power / VREG.
  {
    static const uint8_t d[] = {0x1B};
    gc9_cmd_data(0xA9, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x6B};
    gc9_cmd_data(0xA8, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x6D};
    gc9_cmd_data(0xA8, d, sizeof(d));
  } // re-write
  {
    static const uint8_t d[] = {0x40};
    gc9_cmd_data(0xA7, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x47};
    gc9_cmd_data(0xAD, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x73};
    gc9_cmd_data(0xAF, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x73};
    gc9_cmd_data(0xAF, d, sizeof(d));
  } // re-write
  {
    static const uint8_t d[] = {0x44};
    gc9_cmd_data(0xAC, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x6C};
    gc9_cmd_data(0xA3, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x00};
    gc9_cmd_data(0xCB, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x22};
    gc9_cmd_data(0xCD, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x10};
    gc9_cmd_data(0xC2, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x00};
    gc9_cmd_data(0xC5, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x0E};
    gc9_cmd_data(0xC6, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x1F};
    gc9_cmd_data(0xC7, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x0E};
    gc9_cmd_data(0xC8, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x00};
    gc9_cmd_data(0xBF, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x20};
    gc9_cmd_data(0xF9, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x3B};
    gc9_cmd_data(0x9B, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x33, 0x7F, 0x00};
    gc9_cmd_data(0x93, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x0E, 0x0F, 0x03, 0x0E, 0x0F, 0x03};
    gc9_cmd_data(0x70, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x0E, 0x16, 0x03};
    gc9_cmd_data(0x71, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x0E, 0x09};
    gc9_cmd_data(0x91, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x2C};
    gc9_cmd_data(0xC3, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x1A};
    gc9_cmd_data(0xC4, d, sizeof(d));
  }

  // Split gamma.
  {
    static const uint8_t d[] = {0x51, 0x13, 0x0C, 0x06, 0x00, 0x2F};
    gc9_cmd_data(0xF0, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x51, 0x13, 0x0C, 0x06, 0x00, 0x33};
    gc9_cmd_data(0xF2, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x3C, 0x94, 0x4F, 0x33, 0x34, 0xCF};
    gc9_cmd_data(0xF1, d, sizeof(d));
  }
  {
    static const uint8_t d[] = {0x4D, 0x94, 0x4F, 0x33, 0x34, 0xCF};
    gc9_cmd_data(0xF3, d, sizeof(d));
  }

  // Orientation default; rotation is done in software (the transpose).
  {
    static const uint8_t d[] = {0x00};
    gc9_cmd_data(GC9_CMD_MADCTL, d, sizeof(d));
  }

  // Sleep out + mandatory 200 ms wait, display on, start memory write.
  gc9_cmd(GC9_CMD_SLPOUT);
  delay_ms(200);
  gc9_cmd(GC9_CMD_DISPON);
  gc9_cmd(GC9_CMD_RAMWR);
}

// ---------------------------------------------------------------------------
// GPIO + SPI1 bringup
// ---------------------------------------------------------------------------

static void gc9_init_hw(void) {
  RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOB | RCC_APB2_PERIPH_GPIOD | RCC_APB2_PERIPH_SPI1, ENABLE);

  GPIO_InitType io;
  GPIO_InitStruct(&io);

  // CS = PA10, BL = PA6, DC = PD0: plain push-pull GPIO outputs.
  io.Pin            = LCD_CS_Pin | LCD_BL_Pin;
  io.GPIO_Mode      = GPIO_Mode_Out_PP;
  io.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
  io.GPIO_Pull      = GPIO_No_Pull;
  io.GPIO_Current   = GPIO_DC_8mA;
  GPIO_InitPeripheral(LCD_CS_GPIO_Port, &io); // PA10, PA6 (same port)

  GPIO_InitStruct(&io);
  io.Pin            = LCD_DC_Pin;
  io.GPIO_Mode      = GPIO_Mode_Out_PP;
  io.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
  io.GPIO_Pull      = GPIO_No_Pull;
  io.GPIO_Current   = GPIO_DC_8mA;
  GPIO_InitPeripheral(LCD_DC_GPIO_Port, &io); // PD0

  // SCK = PB3 (AF1), MOSI = PB5 (AF0): per-pin SPI1 AF on the N32L40x.
  GPIO_InitStruct(&io);
  io.GPIO_Mode      = GPIO_Mode_AF_PP;
  io.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
  io.GPIO_Pull      = GPIO_No_Pull;
  io.GPIO_Current   = GPIO_DC_8mA;
  io.Pin            = LCD_SCK_Pin;
  io.GPIO_Alternate = GPIO_AF1_SPI1;
  GPIO_InitPeripheral(LCD_SCK_GPIO_Port, &io); // PB3
  io.Pin            = LCD_MOSI_Pin;
  io.GPIO_Alternate = GPIO_AF0_SPI1;
  GPIO_InitPeripheral(LCD_MOSI_GPIO_Port, &io); // PB5

  // Idle levels: CS high (deselected), DC high (data), BL high (backlight off).
  GPIO_SetBits(LCD_CS_GPIO_Port, LCD_CS_Pin);
  GPIO_SetBits(LCD_DC_GPIO_Port, LCD_DC_Pin);
  GPIO_SetBits(LCD_BL_GPIO_Port, LCD_BL_Pin);

  // SPI1: Mode 3, master, software NSS, half-duplex TX-only, baud PCLK/2, MSB.
  SPI_InitType s;
  SPI_InitStruct(&s);
  s.DataDirection = SPI_DIR_SINGLELINE_TX;
  s.SpiMode       = SPI_MODE_MASTER;
  s.DataLen       = SPI_DATA_SIZE_8BITS;
  s.CLKPOL        = SPI_CLKPOL_HIGH;        // CPOL=1
  s.CLKPHA        = SPI_CLKPHA_SECOND_EDGE; // CPHA=1 -> Mode 3
  s.NSS           = SPI_NSS_SOFT;
  s.BaudRatePres  = SPI_BR_PRESCALER_2; // fPCLK/2
  s.FirstBit      = SPI_FB_MSB;
  s.CRCPoly       = 7; // unused, must be >= 1
  SPI_Init(SPI1, &s);
  SPI_SetNssLevel(SPI1, SPI_NSS_HIGH); // SSI high, avoid MODF
  SPI_Enable(SPI1, ENABLE);
}

// Open a full-panel pixel write: window 0..39 x 0..159, RAMWR, then assert CS and switch to 16-bit
// pixel framing. Pixels are streamed by the caller (no full-frame buffer).
static void gc9_begin_frame(void) {
  gc9_cmd(GC9_CMD_CASET);
  gc9_data16(0);
  gc9_data16(GC9_PANEL_W - 1);
  gc9_cmd(GC9_CMD_RASET);
  gc9_data16(0);
  gc9_data16(GC9_PANEL_H - 1);
  gc9_cmd(GC9_CMD_RAMWR);
  GPIO_SetBits(LCD_DC_GPIO_Port, LCD_DC_Pin); // data
  GPIO_ResetBits(LCD_CS_GPIO_Port, LCD_CS_Pin);
  gc9_set_datalen(SPI_DATA_SIZE_16BITS);
}

// Close a pixel write: drain the bus, restore 8-bit framing, deselect.
static void gc9_end_frame(void) {
  gc9_wait_idle();
  gc9_set_datalen(SPI_DATA_SIZE_8BITS);
  GPIO_SetBits(LCD_CS_GPIO_Port, LCD_CS_Pin);
}

// Fill the whole panel with the background colour (streamed, no buffer).
static void gc9_clear(void) {
  gc9_begin_frame();
  for (int i = 0; i < GC9_PANEL_W * GC9_PANEL_H; i++) {
    gc9_spi_word(COLOR_BG);
  }
  gc9_end_frame();
}

// ---------------------------------------------------------------------------
// Public lifecycle & hardware primitives
// ---------------------------------------------------------------------------

void GC9Display::init() {
  if (initialised) {
    return;
  }
  if (displayMutex == NULL) {
    displayMutex = xSemaphoreCreateMutexStatic(&displayMutexBuffer);
  }
  gc9_init_hw();
  gc9_run_init_sequence();
  gc9_clear();
  // Backlight on (PA6 active-low) once the panel shows background.
  GPIO_ResetBits(LCD_BL_GPIO_Port, LCD_BL_Pin);
  initialised = true;
}

void GC9Display::FRToSInit() { init(); }

void GC9Display::setDisplayOn(bool on) {
  const bool locked = lockFrame();
  if (on) {
    gc9_cmd(GC9_CMD_DISPON);
    GPIO_ResetBits(LCD_BL_GPIO_Port, LCD_BL_Pin); // backlight on (active-low)
  } else {
    gc9_cmd(GC9_CMD_DISPOFF);
    GPIO_SetBits(LCD_BL_GPIO_Port, LCD_BL_Pin); // backlight off, saves power
  }
  unlockFrame(locked);
}

void GC9Display::setInverted(bool inverted) {
  const bool locked = lockFrame();
  gc9_cmd(inverted ? GC9_CMD_INVON : GC9_CMD_INVOFF);
  unlockFrame(locked);
}

bool GC9Display::lockFrame() {
  const bool lock = (displayMutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
  if (lock) {
    xSemaphoreTake(displayMutex, portMAX_DELAY);
  }
  return lock;
}

void GC9Display::unlockFrame(bool locked) {
  if (locked) {
    xSemaphoreGive(displayMutex);
  }
}

void GC9Display::beginFrame() { gc9_begin_frame(); }
void GC9Display::pushPixel(uint16_t color) { gc9_spi_word(color); }
void GC9Display::endFrame() { gc9_end_frame(); }

#endif // OLED_GC9D01
