// Pin-level interface the ICP protocol core runs on.
//
// icp_hal_pico.c drives real GPIOs. The core never touches hardware directly,
// so the same framing code can also be run on a PC against a bit-level model of
// the HC89S003A's debug port.

#ifndef ICP_HAL_H
#define ICP_HAL_H

#include <stdbool.h>
#include <stdint.h>

// Leaves the target parked: RST asserted, every other line floating.
void icp_hal_init(void);

// Target supply. Off floats every signal line, so nothing back-powers the chip
// through its clamp diodes while VDD is down, and parks RST low. On drives TCK,
// TMS and TDI, gives TDO its pull-up, and leaves RST as it was. Power-on leaves
// TMS high (TAP held in reset), TCK and TDI low.
void icp_hal_power(bool on);

// RST (TSSOP 12, P2.7): true pulls it low, false releases it.
void icp_hal_rst(bool assert);

void icp_hal_tck(bool high);  // TSSOP 14, P2.5
void icp_hal_tms(bool high);  // TSSOP 13, P2.6
void icp_hal_tdi(bool high);  // TSSOP 16, P2.3
bool icp_hal_tdo(void);       // TSSOP 15, P2.4; weak pull-up: a silent target reads 1

void icp_hal_half_period(void);  // half a TCK period
void icp_hal_set_khz(uint32_t khz);

uint32_t icp_hal_us(void);
void icp_hal_sleep_ms(uint32_t ms);

#endif  // ICP_HAL_H
