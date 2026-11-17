// Which HC89S003A port pin drives each of the glasses' 16 matrix lines.
//
// It is DrSapling's "pin numbers.jpg" (README.md, "Credit") read pad by pad,
// with the datasheet's TSSOP-20 numbering (pin 1 bottom-left, notch on the
// left). The photo agrees with the datasheet wherever the two can be compared:
// GND on 7, VCC on 9, and an unlabelled pad on 12.
//
// One correction, found on the glasses: the open eyes looked right, but they
// light nearly every LED in each section, which hides two swapped cathode
// lines. The first sparse frame (the Sleepy doze) showed it: every pin-14 LED
// dark, ghosts on pin 15's. Modelling every single swap against what the
// glasses showed left one fit, 14 <-> 15, which this table now has. TSSOP
// 17/18 are adjacent; the reading had them transposed. Confirmed on the
// glasses: the doze frame and the open eyes both draw clean.
//
// Every GPIO comes out used exactly once: 16 matrix lines + BTN, with P2.7 left
// alone. On these glasses P2.7 is a GPIO, not RST (OPTION ERST_ENB = 1), and is
// wired to the R temple pads. The JTAG pins P2.3-P2.6 are matrix lines too.
//
//   TSSOP  port  board          TSSOP  port  board
//     1    P2.0    6              20    P0.7   BTN
//     2    P0.5    5              19    P0.6   16
//     3    P0.4    2              18    P2.1   15
//     4    P0.3    1              17    P2.2   14
//     5    P0.2    3              16    P2.3   10   (TDI)
//     6    P0.1    4              15    P2.4   12   (TDO)
//     7    GND                    14    P2.5   11   (TCK)
//     8    P0.0    7              13    P2.6   13   (TMS)
//     9    VDD                    12    P2.7   RST
//    10    P1.1    8              11    P1.0    9

#ifndef PINMAP_H
#define PINMAP_H

#include <stdint.h>

#ifdef __SDCC
#define PINMAP_CODE __code
#else
#define PINMAP_CODE
#endif

// A port pin, packed: port in bits 4-5, bit number in bits 0-2.
#define HC_PIN(port, bit)  ((uint8_t)((port) << 4 | (bit)))
#define HC_PORT(pin)       ((pin) >> 4)
#define HC_BIT(pin)        ((pin) & 7)

// Index = board pin (1..16). Entry 0 is unused.
static const PINMAP_CODE uint8_t board_pin_to_hc[17] = {
    0,
    HC_PIN(0, 3),   //  1
    HC_PIN(0, 4),   //  2
    HC_PIN(0, 2),   //  3
    HC_PIN(0, 1),   //  4
    HC_PIN(0, 5),   //  5
    HC_PIN(2, 0),   //  6
    HC_PIN(0, 0),   //  7
    HC_PIN(1, 1),   //  8
    HC_PIN(1, 0),   //  9
    HC_PIN(2, 3),   // 10
    HC_PIN(2, 5),   // 11
    HC_PIN(2, 4),   // 12
    HC_PIN(2, 6),   // 13
    HC_PIN(2, 2),   // 14  (was P2.1: swapped with 15, see top)
    HC_PIN(2, 1),   // 15
    HC_PIN(0, 6),   // 16
};

// Never driven high. The button shorts P0.7 to ground: the stock firmware gives
// it a pull-up, wakes from power-down on its falling edge, and reads 0 as
// pressed (its image at 0x0037, 0x004d, 0x1c9e). P2.7 goes to the R pads,
// whatever they are for; it is a plain input while running and is parked low
// for power-down, as stock does.
#define HC_PIN_BTN  HC_PIN(0, 7)
#define HC_PIN_RST  HC_PIN(2, 7)

#endif  // PINMAP_H
