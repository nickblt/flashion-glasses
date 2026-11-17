// SL-GlassesLed26x7 charlieplexed matrix: board pin numbering and geometry,
// independent of the chip driving it. Board pins are the 1..16 numbering of
// DrSapling's reverse engineering (README.md, "Credit"); pinmap.h maps them to
// HC89S003A port pins.

#ifndef GLASSES_PINS_H
#define GLASSES_PINS_H

#include <stdint.h>

// ---- pin block -------------------------------------------------------------

#define GL_PIN_COUNT       16      // board pins 1..16; 1..13 are the anodes

// Cathode-only pins. 15 and 16 stand in when an LED's cathode would collide
// with the section's own anode.
#define GL_PIN_LOW_ONLY    14
#define GL_PIN_SUB_EVEN    15    // used when section index is even
#define GL_PIN_SUB_ODD     16    // used when section index is odd

// ---- matrix geometry -------------------------------------------------------

#define GL_SECTIONS        13    // scanned one at a time -> 1/13 duty
#define GL_SECTION_COLS    2
#define GL_ROWS            7
#define GL_COLS            26    // 13 sections x 2 columns

// Anode (driven HIGH) for a section. s = 0..12.
#define GL_SECTION_PIN(s)  ((s) + 1)

// Substitute cathode for section s.
#define GL_SECTION_SUB(s)  (((s) & 1) ? GL_PIN_SUB_ODD : GL_PIN_SUB_EVEN)

// Cathode board pin for the LED at (col, row) inside a section, col 0..1, row 0..6.
// Yields 1..14 in reading order down the 2x7 block.
#define GL_CELL_PIN(col, row) (2 * (row) + (col) + 1)

// Nose bridge: these display coords carry no LED.
static inline int gl_cell_absent(int x, int y) {
    return (y == 4 && x >= 11 && x <= 14)
        || (y == 5 && x >= 10 && x <= 15)
        || (y == 6 && x >= 9  && x <= 16);
}

// Resolve a display pixel to the (anode, cathode) board pins that light it.
// Not for SDCC: it compiles an unused static inline into every file anyway,
// and gives each copy 13 bytes of the 8051's scarce direct RAM.
#ifndef __SDCC
static inline void gl_resolve(int x, int y, int *section, int *anode, int *cathode) {
    int s   = x / GL_SECTION_COLS;
    int col = x % GL_SECTION_COLS;
    int c   = GL_CELL_PIN(col, y);

    *section = s;
    *anode   = GL_SECTION_PIN(s);
    *cathode = (c == *anode) ? GL_SECTION_SUB(s) : c;
}
#endif

// ---- drive -----------------------------------------------------------------
// A frame is GL_SECTIONS scan slots. In slot s, the anode GL_SECTION_PIN(s) is
// driven HIGH, every cathode to light is pulled LOW, and every other pin is
// high-Z. Scan at ~1 kHz per section.
//
// No resistors exist on the glasses board. Verify current on an all-on frame
// before running it unattended -- one anode can be sourcing 14 LEDs at once.

#endif  // GLASSES_PINS_H
