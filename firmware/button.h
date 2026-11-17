// The glasses' one button: short press and long hold, debounced.
//
// Pure logic: the caller supplies the time and the pin level, so it runs
// unchanged in a test harness on a PC.

#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#define BTN_DEBOUNCE_MS 30    // a level must hold this long to count
#define BTN_LONG_MS     1000  // hold this long for BTN_LONG

typedef enum {
    BTN_NONE,
    BTN_SHORT,  // released before BTN_LONG_MS
    BTN_LONG,   // still held at BTN_LONG_MS; fires then, not on release
} btn_event_t;

// Starts tracking from `down`. A press already in progress belongs to whoever
// caused it -- the power-on hold -- so it produces nothing, not even on release.
void btn_init(uint32_t now_ms, bool down);

// Feed it the raw level every few ms. At most one event per press.
btn_event_t btn_update(uint32_t now_ms, bool down);

// Debounced: the button is up and has stayed up.
bool btn_is_up(void);

#endif  // BUTTON_H
