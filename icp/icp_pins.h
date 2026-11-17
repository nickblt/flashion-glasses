// Pico wiring for the ICP probe: JTAG on the glasses' 5-hole header.
//
// GP16-GP21, leaving GP0-GP15 free. Holes are counted left to right with the
// board held C1 to the right of U1 (chip pin 1 bottom-left). Hole 1 is the one
// that beeps to GND; go by that, not by position (README.md, "Wiring"):
//
//   Pico                         glasses
//   GP20 (phys 26)  TMS     ->   hole 2   TSSOP 13  P2.6
//   GP16 (phys 21)  TCK     ->   hole 3   TSSOP 14  P2.5
//   GP17 (phys 22)  TDO     <-   hole 4   TSSOP 15  P2.4
//   GP21 (phys 27)  TDI     ->   hole 5   TSSOP 16  P2.3
//   GND  (phys 38)          --   hole 1   TSSOP 7
//   GP19 (phys 25)  PWR     ->   V pad    TSSOP 9   powers the glasses directly
//
// Two entry modes (PROTOCOL.md §4, datasheet §3.1.6.1):
//
//   entry power  The default, and the only one that works on the glasses. GP19
//                powers VDD directly, and the probe catches the debug port in
//                the moment after power-on. The chip draws a few mA there,
//                which a 12 mA pad can supply. If the catch is missed, the
//                stock firmware tries to light LEDs from GP19; the pad's drive
//                limit caps that, and the short default window (100 ms) keeps
//                it brief. A target that draws more can have VDD switched
//                through a P-MOSFET or a load switch instead, with
//                ICP_PWR_ON_LEVEL set to suit.
//   entry rst    For boards whose RST pin still works: Pico 3V3(OUT) (phys 36)
//                straight to VDD, and GP18 (phys 24) to RST (TSSOP 12, P2.7).
//                The board stays powered; RST is held low before the JTAG
//                lines are driven. GP19 is unused.
//
// On the glasses RST does nothing: the R temple pads are TSSOP 12, but the
// chip runs on with them grounded, because OPTION ERST_ENB makes P2.7 a GPIO.
// Leave R unwired.
//
// The board has no series resistors (PROTOCOL.md §8). Never feed it from two
// supplies at once: disconnect the glasses' own supply while this is wired.

#ifndef ICP_PINS_H
#define ICP_PINS_H

#define ICP_PIN_TCK       16
#define ICP_PIN_TDO       17
#define ICP_PIN_RST       18
#define ICP_PIN_PWR       19
#define ICP_PIN_TMS       20
#define ICP_PIN_TDI       21

// Level on ICP_PIN_PWR that turns the target supply on: 1 when GP19 powers the
// target directly (the glasses) or drives an NPN; 0 for a P-MOSFET gate driven
// directly.
#define ICP_PWR_ON_LEVEL  1

// PROTOCOL.md §8: start at 100 kHz. The vendor dongle runs near 1 MHz.
#define ICP_DEFAULT_KHZ   100

#endif  // ICP_PINS_H
