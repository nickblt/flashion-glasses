#include "button.h"

static bool raw;           // the level last fed in
static uint32_t raw_since; // when it last changed
static bool held;          // debounced level
static uint32_t press_at;  // when the debounced press began
static bool armed;         // this press may still produce an event

void btn_init(uint32_t now_ms, bool down) {
    raw = held = down;
    raw_since = press_at = now_ms;
    armed = false;
}

btn_event_t btn_update(uint32_t now_ms, bool down) {
    if (down != raw) {
        raw = down;
        raw_since = now_ms;
    }

    if (raw != held && now_ms - raw_since >= BTN_DEBOUNCE_MS) {
        held = raw;
        if (held) {
            press_at = raw_since;  // timed from the edge it settled on
            armed = true;
        } else if (armed) {
            armed = false;
            return BTN_SHORT;
        }
    }

    if (held && armed && now_ms - press_at >= BTN_LONG_MS) {
        armed = false;
        return BTN_LONG;
    }
    return BTN_NONE;
}

bool btn_is_up(void) { return !held && !raw; }
