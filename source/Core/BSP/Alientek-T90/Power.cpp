/*
 * Power.cpp
 *
 *  USB-PD power management for the Alientek T90 (Nations N32L40x).
 *
 *  The T90 sinks power through a CH224Q PD controller driven over the shared
 *  software (bit-bang) I2C bus (I2CBB2) on PB6 (SCL) / PB7 (SDA), device address
 *  0x22. Using I2CBB2 serialises CH224Q register access against QMA6100P
 *  accelerometer reads on the same pins via I2CBB2's FreeRTOS mutex.
 */

#include "Power.h"
#include "BSP.h"
#include "BSP_Power.h"
#include "I2CBB2.hpp"
#include "Pins.h"
#include "Settings.h"
#include "configuration.h"
#include "n32l40x.h"

// CH224Q register map (from the stock-firmware reverse engineering)
#define CH224Q_ADDR_7BIT  0x22                    // 7-bit device address
#define CH224Q_ADDR_8BIT  (CH224Q_ADDR_7BIT << 1) // 8-bit pre-shifted address for I2CBB2
#define CH224Q_REG_STATUS 0x09                    // negotiated-state / capability flags
#define CH224Q_REG_VSEL   0x0A                    // requested PDO index: 0=5V 1=9V 2=12V 3=15V 4=20V 5=28V

// Voltage-index table matching CH224Q_REG_VSEL. Index -> volts.
static const uint8_t ch224q_voltage_table[] = {5, 9, 12, 15, 20, 28};
#define CH224Q_VSEL_COUNT (sizeof(ch224q_voltage_table) / sizeof(ch224q_voltage_table[0]))

#ifdef CH224_SOFT_I2C

static bool ch224q_write_reg(uint8_t reg, uint8_t val) { return I2CBB2::I2C_RegisterWrite(CH224Q_ADDR_8BIT, reg, val); }

static bool ch224q_read_reg(uint8_t reg, uint8_t *out) { return I2CBB2::Mem_Read(CH224Q_ADDR_8BIT, reg, out, 1); }

#endif // CH224_SOFT_I2C

// Highest voltage index we will request (clamped to USB_PD_VMAX).
static uint8_t requested_index = 0;
// Last voltage we believe is negotiated, in volts. Starts at 0 (= "not yet negotiated") so
// preStartChecks() holds the heater off until power_check() confirms a live contract.
static uint8_t negotiated_voltage = 0;

static uint8_t voltage_to_index(uint8_t volts) {
  uint8_t best = 0;
  for (uint8_t i = 0; i < CH224Q_VSEL_COUNT; i++) {
    if (ch224q_voltage_table[i] <= volts) {
      best = i;
    }
  }
  return best;
}

void ch224q_init(void) {
#ifdef CH224_SOFT_I2C
  requested_index = voltage_to_index(USB_PD_VMAX);
  ch224q_write_reg(CH224Q_REG_VSEL, requested_index);
  // Do NOT assume the request succeeded here: leave negotiated_voltage at 0 until power_check()
  // confirms a live contract via the status register. Otherwise preStartChecks() would skip its
  // PD-settle wait and start heating while the rail is still at the 5V default.
#endif
}

// Negotiated source voltage in volts (used by preStartChecks for the PWM-speed decision).
uint16_t ch224q_source_voltage(void) { return negotiated_voltage; }

// Source current capability x100 (in centi-amps). The CH224Q does not expose a precise
// current value over this register set; assume the CH224Q maximum 5A contract. Conservative.
uint16_t ch224q_source_currentX100(void) { return 500; }

void power_check() {
#if defined(POW_PD_EXT) && (POW_PD_EXT == 3)
#ifdef CH224_SOFT_I2C
  // Re-assert the desired PDO if the negotiated voltage looks wrong (contract dropped).
  uint8_t status = 0;
  if (ch224q_read_reg(CH224Q_REG_STATUS, &status)) {
    if (status == 0) {
      // No live contract: re-request and report the conservative 5V USB default.
      ch224q_write_reg(CH224Q_REG_VSEL, requested_index);
      negotiated_voltage = 5;
    } else {
      // A non-zero status means the source accepted a PDO; trust the requested voltage.
      negotiated_voltage = ch224q_voltage_table[requested_index];
    }
  } else {
    // Bus error: re-assert blindly and stay on the conservative 5V assumption.
    ch224q_write_reg(CH224Q_REG_VSEL, requested_index);
    negotiated_voltage = 5;
  }
#endif
#endif
}

bool getIsPoweredByDCIN() { return false; }
