// Charlieplex scan-out driver API for the SL-GlassesLed26x7 board. eyes.c
// draws through it; matrix_hc89.c implements it on the stock HC89S003A.
// Pin algebra: glasses_pins.h. Port pins: pinmap.h.

#ifndef GLASSES_MATRIX_H
#define GLASSES_MATRIX_H

#include <stdbool.h>
#include <stdint.h>
#include "glasses_pins.h"

// Microseconds each of the 13 sections is held lit. 13 * 400us = 5.2ms/frame,
// ~192 Hz refresh. Shortening this reduces flicker but not duty: every section
// is lit 1/13 of the time regardless, which is the display's brightness ceiling.
#define GL_SLOT_US 400

// Blanks the display: every matrix pin back to an input. Call before anything
// else.
void gl_matrix_init(void);

// Starts the scan timer. Nothing lights up until the first gl_present().
bool gl_matrix_start(void);
void gl_matrix_stop(void);

// Milliseconds counted by the scan interrupt: it stands still while the scan
// is stopped, and so across power-down.
uint32_t gl_millis(void);

// --- framebuffer (not live until gl_present) --------------------------------

void gl_clear(void);
void gl_set_px(int x, int y, bool on);
bool gl_get_px(int x, int y);

// Draws '#' as lit, everything else as transparent. `art` is `rows` strings of
// equal length, top row first. Clipped to the display and to the nose bridge.
void gl_draw_art(int x0, int y0, const char *const *art, int rows);

// Compiles the framebuffer into section masks and hands them to the scanner.
void gl_present(void);

#endif  // GLASSES_MATRIX_H
