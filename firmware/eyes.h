// Eye animations for the 26x7 glasses matrix: big round blinking eyes, and a
// handful of other moods to cycle through.

#ifndef EYES_H
#define EYES_H

#include <stdbool.h>

// In the order eyes_next_mode() steps through them.
typedef enum {
    EYES_BLINK,   // wide open, blinks on its own
    EYES_HAPPY,   // ^ ^ arcs that giggle now and then
    EYES_HEARTS,  // beating hearts
    EYES_SLEEPY,  // heavy lids droop, nod off, jolt awake
    EYES_ANGRY,   // slanted brows, a glare every so often
    EYES_MODE_COUNT
} eyes_mode_t;

// Blink mode, eyes shut and about to open. Draws the first frame; call after
// gl_matrix_init().
void eyes_init(void);

// Advances the animation. Call freely from the main loop; it does its own
// timing and only redraws when a frame actually changes.
void eyes_update(void);

// Closes the eyes, swaps the animation behind the lids, and opens them again.
// Calling again before they reopen skips ahead another mode.
void eyes_next_mode(void);
void eyes_set_mode(eyes_mode_t mode);
eyes_mode_t eyes_mode(void);

// Power-off and power-on. eyes_sleep() closes the eyes and then goes dark;
// eyes_asleep() turns true once the dark frame is out. eyes_wake() opens them
// from shut, in the mode they were in.
void eyes_sleep(void);
bool eyes_asleep(void);
void eyes_wake(void);

// Blinks are automatic in the modes that have them. These force one at the
// next update; a wink blinks one eye. true = the wearer's left.
void eyes_blink_now(void);
void eyes_wink(bool left_eye);

#endif  // EYES_H
