#include "eyes.h"

#include <stddef.h>
#include <stdint.h>

#include "glasses_matrix.h"

// --- art --------------------------------------------------------------------
//
// Nine columns wide, seven tall, one eye. The two eyes sit at x=0 and x=17,
// which leaves columns 9..16 between them -- exactly the span the nose bridge
// removes from the bottom row, so each eye lands squarely on one lens.
//
// Edit the art freely; only '#' matters, so trailing spaces are decoration.
// Art is drawn for the left eye. The right eye gets the same art, mirrored only
// where the shape is lopsided (the angry brows).

#define EYE_W      9
#define EYE_LEFT_X 0
#define EYE_RIGHT_X 17

// The round eye, level 0 wide open to level 3 the closed lid line. Blink and
// Sleepy draw it; Blink blinks through these hand-drawn levels. Every other
// mode closes its own art with close_lid() instead.
#define EYE_LEVELS 4
#define LID_SHUT   3   // the closed line
#define LID_DARK   4   // nothing lit: asleep

static const char *const eye_art[EYE_LEVELS][GL_ROWS] = {
    {   // 0 - open
        "  #####  ",
        " ####### ",
        "#########",
        "#########",
        "#########",
        " ####### ",
        "  #####  ",
    },
    {   // 1 - squashed
        "         ",
        " ####### ",
        "#########",
        "#########",
        "#########",
        " ####### ",
        "         ",
    },
    {   // 2 - narrowed
        "         ",
        "         ",
        " ####### ",
        "#########",
        " ####### ",
        "         ",
        "         ",
    },
    {   // 3 - shut
        "         ",
        "         ",
        "         ",
        "#########",
        "         ",
        "         ",
        "         ",
    },
};

// Happy: a ^ ^ smile. Drawn one row down, bobbing up a row.
#define HAPPY_ROWS 5
static const char *const happy_art[HAPPY_ROWS] = {
    "         ",
    " ####### ",
    "#########",
    "##     ##",
    "         ",
};

static const char *const heart_art[2][GL_ROWS] = {
    {   // 0 - between beats
        "         ",
        "  ## ##  ",
        " ####### ",
        "  #####  ",
        "   ###   ",
        "    #    ",
        "         ",
    },
    {   // 1 - beat
        " ### ### ",
        "#########",
        "#########",
        " ####### ",
        "  #####  ",
        "   ###   ",
        "    #    ",
    },
};

// Left eye: the brow slopes down toward the nose. Mirrored for the right.
static const char *const angry_art[GL_ROWS] = {
    "##       ",
    "#####    ",
    "######## ",
    "#########",
    "#########",
    " ####### ",
    "  #####  ",
};

// --- timing -----------------------------------------------------------------
//
// A blink closes faster than it opens, which is what reads as organic rather
// than mechanical. Whole thing lands around 220 ms. Mode switches and power-off
// close the eyes at the same speed.

#define CLOSE_STEP_MS 22
#define SHUT_MS       45
#define OPEN_STEP_MS  32
#define WAKE_SHUT_MS  260   // powers on with the eyes shut, then wakes up
#define SWITCH_SHUT_MS 60   // shut between one mode and the next
#define SLEEP_SHUT_MS 300   // the closed line lingers before power-off

#define BLINK_MIN_MS   2200
#define BLINK_MAX_MS   6000
#define DOUBLE_PERCENT 22   // chance a blink is followed by a second one
#define DOUBLE_GAP_MS  170

// The other modes run a loop of beats: a pose, held so long. What a pose means
// is up to the mode -- see draw_eye(). REST holds a random while.
#define REST        0
#define REST_MIN_MS 1200
#define REST_MAX_MS 3200

typedef struct {
    uint8_t pose;
    uint16_t ms;
} beat_t;

// Pose: 1 = bobbed up a row.
static const beat_t happy_beats[] = {{0, REST}, {1, 110}, {0, 110}, {1, 110}};
// Pose: 1 = the big heart. Lub-dub, rest.
static const beat_t heart_beats[] = {{1, 140}, {0, 110}, {1, 140}, {0, 650}};
// Pose: rows hidden under the top lid. Droops, dozes, jolts wide, settles.
static const beat_t sleepy_beats[] = {
    {3, REST}, {4, 500}, {5, 700}, {6, 1500}, {1, 150}, {2, 150},
};
// Pose: 1 = glaring, the bottom of the eye squeezed up.
static const beat_t angry_beats[] = {{0, REST}, {1, 900}};

typedef struct {
    const beat_t *beats;  // NULL: no beat loop
    uint8_t n_beats;
    uint8_t blinks;       // runs the automatic blinker
} mode_def_t;

#define BEATS(b) b, sizeof b / sizeof b[0]

// Indexed by eyes_mode_t.
static const mode_def_t modes[EYES_MODE_COUNT] = {
    {NULL, 0, 1},              // EYES_BLINK
    {BEATS(happy_beats), 0},   // EYES_HAPPY
    {BEATS(heart_beats), 0},   // EYES_HEARTS
    {BEATS(sleepy_beats), 0},  // EYES_SLEEPY
    {BEATS(angry_beats), 1},   // EYES_ANGRY
};

// --- state ------------------------------------------------------------------
//
// On the 8051 (SDCC) all of it lives in external RAM. SDCC's small model would
// otherwise put every static, and every parameter of a function that calls
// another, in the 8051's 128 bytes of direct RAM, which the scan interrupt
// needs and this file alone would fill.

#ifdef __SDCC
#define EYES_XDATA __xdata
#else
#define EYES_XDATA
#endif

static EYES_XDATA uint8_t cur_mode;
static EYES_XDATA bool dirty;

// The lid over the whole display: mode switches, power-off and power-on.
typedef enum { LID_STILL, LID_CLOSING, LID_HOLD, LID_OPENING } lid_phase_t;

static EYES_XDATA uint8_t lid;            // 0 open .. LID_SHUT, or LID_DARK
static EYES_XDATA uint8_t lid_phase;
static EYES_XDATA uint32_t lid_deadline;
static EYES_XDATA uint8_t next_mode;      // what to open into
static EYES_XDATA bool going_dark;        // closing to power off, not to switch

// The current mode's own animation.
static EYES_XDATA uint8_t beat;
static EYES_XDATA uint32_t anim_deadline;

// The automatic blinker, per eye so it can wink.
typedef enum { BL_IDLE, BL_CLOSING, BL_SHUT, BL_OPENING } blink_phase_t;

static EYES_XDATA uint8_t blink_phase;
static EYES_XDATA uint8_t level[2];       // 0 = left eye, 1 = right
static EYES_XDATA bool animating[2];      // which eyes this blink moves (a wink moves one)
static EYES_XDATA uint32_t blink_deadline;
static EYES_XDATA bool pending_double;
static EYES_XDATA bool force_blink;

// Read once per call into the api rather than handed down as a parameter,
// which on SDCC would cost 4 bytes of direct RAM in every function taking it.
static EYES_XDATA uint32_t now;

static void read_clock(void) { now = gl_millis(); }

// Signed comparison so the ms counter wrapping at ~49 days is harmless.
static bool due(uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

// xorshift32. The seed is fixed, so the blink rhythm repeats from each
// power-on; seed it from the CHIP_ID if that ever matters.
static EYES_XDATA uint32_t rand_state = 0x2545F491ul;

static uint16_t random16(void) {
    rand_state ^= rand_state << 13;
    rand_state ^= rand_state >> 17;
    rand_state ^= rand_state << 5;
    return (uint16_t)rand_state;
}

static uint16_t rand_between(uint16_t lo, uint16_t hi) {
    return lo + random16() % (uint16_t)(hi - lo);
}

// --- drawing ----------------------------------------------------------------

static void draw_art(int8_t x0, int8_t y0, const char *const *art, uint8_t rows, bool mirror) {
    for (uint8_t r = 0; r < rows; r++) {
        const char *line = art[r];
        for (uint8_t c = 0; c < EYE_W && line[c]; c++) {
            if (line[c] != '#') continue;
            gl_set_px(x0 + (mirror ? EYE_W - 1 - c : c), y0 + r, true);
        }
    }
}

static void fill_rows(int8_t x0, uint8_t y_from, uint8_t y_to, bool on) {
    for (uint8_t y = y_from; y < y_to; y++) {
        for (uint8_t c = 0; c < EYE_W; c++) gl_set_px(x0 + c, y, on);
    }
}

// Closes the lid to `lvl` over whatever the eye shows: rows outside the open
// band go dark, and shut leaves the middle row as a line, like eye_art[3].
static void close_lid(int8_t x0, uint8_t lvl) {
    if (lvl == 0) return;
    if (lvl >= LID_SHUT) {
        fill_rows(x0, 0, GL_ROWS, false);
        if (lvl == LID_SHUT) fill_rows(x0, GL_ROWS / 2, GL_ROWS / 2 + 1, true);
        return;
    }
    fill_rows(x0, 0, lvl, false);
    fill_rows(x0, GL_ROWS - lvl, GL_ROWS, false);
}

static void draw_eye(uint8_t e) {
    int8_t x0 = e ? EYE_RIGHT_X : EYE_LEFT_X;
    const mode_def_t *m = &modes[cur_mode];
    uint8_t pose = m->beats ? m->beats[beat].pose : 0;

    uint8_t lvl = lid;  // the lid closes on top of any blink in progress
    if (m->blinks && level[e] > lvl) lvl = level[e];
    if (lvl >= LID_DARK) return;

    switch (cur_mode) {
        case EYES_BLINK:
            // The round eye has hand-drawn lid levels; use them, not close_lid().
            draw_art(x0, 0, eye_art[lvl], GL_ROWS, false);
            return;
        case EYES_HAPPY:
            draw_art(x0, pose ? 0 : 1, happy_art, HAPPY_ROWS, false);
            break;
        case EYES_HEARTS:
            draw_art(x0, 0, heart_art[pose], GL_ROWS, false);
            break;
        case EYES_SLEEPY:
            draw_art(x0, 0, eye_art[0], GL_ROWS, false);
            fill_rows(x0, 0, pose, false);
            break;
        case EYES_ANGRY:
            draw_art(x0, 0, angry_art, GL_ROWS, e == 1);
            if (pose) fill_rows(x0, GL_ROWS - 2, GL_ROWS, false);
            break;
    }
    close_lid(x0, lvl);
}

static void redraw(void) {
    gl_clear();
    draw_eye(0);
    draw_eye(1);
    gl_present();
    dirty = false;
}

// --- blinker ----------------------------------------------------------------

static void set_level(uint8_t e, uint8_t v) {
    if (level[e] == v) return;
    level[e] = v;
    dirty = true;
}

static void schedule_blink(void) {
    blink_phase = BL_IDLE;
    if (pending_double) {
        pending_double = false;
        blink_deadline = now + DOUBLE_GAP_MS;
    } else {
        blink_deadline = now + rand_between(BLINK_MIN_MS, BLINK_MAX_MS);
    }
}

static void start_blink(bool left, bool right) {
    animating[0] = left;
    animating[1] = right;
    pending_double = (random16() % 100) < DOUBLE_PERCENT;
    blink_phase = BL_CLOSING;
    blink_deadline = now + CLOSE_STEP_MS;
}

static void blink_tick(void) {
    if (force_blink && blink_phase == BL_IDLE) {
        force_blink = false;
        start_blink(true, true);
    }
    if (!due(blink_deadline)) return;

    switch (blink_phase) {
        case BL_IDLE:
            start_blink(true, true);
            break;

        case BL_CLOSING: {
            bool done = true;
            for (uint8_t e = 0; e < 2; e++) {
                if (animating[e] && level[e] < LID_SHUT) {
                    set_level(e, level[e] + 1);
                    done = false;
                }
            }
            if (done) {
                blink_phase = BL_SHUT;
                blink_deadline = now + SHUT_MS;
            } else {
                blink_deadline = now + CLOSE_STEP_MS;
            }
            break;
        }

        case BL_SHUT:
            blink_phase = BL_OPENING;
            blink_deadline = now + OPEN_STEP_MS;
            break;

        case BL_OPENING: {
            bool done = true;
            for (uint8_t e = 0; e < 2; e++) {
                if (level[e] > 0) {
                    set_level(e, level[e] - 1);
                    done = false;
                }
            }
            if (done) {
                schedule_blink();
            } else {
                blink_deadline = now + OPEN_STEP_MS;
            }
            break;
        }
    }
}

// --- the mode's own animation -----------------------------------------------

static void start_beat(void) {
    uint16_t ms = modes[cur_mode].beats[beat].ms;
    anim_deadline = now + (ms != REST ? ms : rand_between(REST_MIN_MS, REST_MAX_MS));
    dirty = true;
}

static void anim_tick(void) {
    if (!modes[cur_mode].beats || !due(anim_deadline)) return;
    if (++beat >= modes[cur_mode].n_beats) beat = 0;
    start_beat();
}

static void enter_mode(uint8_t mode) {
    cur_mode = mode;
    level[0] = level[1] = 0;
    force_blink = pending_double = false;
    schedule_blink();
    beat = 0;
    if (modes[mode].beats) start_beat();
    dirty = true;
}

// --- the lid over everything ------------------------------------------------

static void lid_tick(void) {
    if (lid_phase == LID_STILL || !due(lid_deadline)) return;

    switch (lid_phase) {
        case LID_CLOSING:
            if (lid < LID_SHUT) {
                lid++;
                dirty = true;
            }
            if (lid < LID_SHUT) {
                lid_deadline = now + CLOSE_STEP_MS;
                break;
            }
            // Shut. Swap the mode behind the closed lid, or linger, then go dark.
            if (!going_dark) enter_mode(next_mode);
            lid_phase = LID_HOLD;
            lid_deadline = now + (going_dark ? SLEEP_SHUT_MS : SWITCH_SHUT_MS);
            break;

        case LID_HOLD:
            lid_phase = going_dark ? LID_STILL : LID_OPENING;
            lid = going_dark ? LID_DARK : LID_SHUT - 1;
            lid_deadline = now + OPEN_STEP_MS;
            dirty = true;
            break;

        case LID_OPENING:
            lid--;
            dirty = true;
            if (lid == 0) {
                lid_phase = LID_STILL;
            } else {
                lid_deadline = now + OPEN_STEP_MS;
            }
            break;
    }
}

// --- api --------------------------------------------------------------------

void eyes_init(void) {
    cur_mode = EYES_BLINK;
    eyes_wake();
    redraw();
}

void eyes_update(void) {
    if (lid == LID_DARK) return;  // asleep: nothing moves until eyes_wake()

    read_clock();
    lid_tick();
    anim_tick();
    if (modes[cur_mode].blinks) blink_tick();

    if (dirty) redraw();
}

void eyes_set_mode(eyes_mode_t mode) {
    if (going_dark || (uint8_t)mode >= EYES_MODE_COUNT) return;
    read_clock();
    next_mode = (uint8_t)mode;
    if (lid_phase == LID_HOLD) {
        enter_mode(next_mode);  // already shut: swap right here
    } else if (lid_phase != LID_CLOSING) {
        lid_phase = LID_CLOSING;
        lid_deadline = now;
    }
}

void eyes_next_mode(void) {
    // A second press while the lids are still closing goes one further.
    uint8_t from = lid_phase == LID_CLOSING ? next_mode : cur_mode;
    eyes_set_mode((eyes_mode_t)((from + 1) % EYES_MODE_COUNT));
}

eyes_mode_t eyes_mode(void) { return (eyes_mode_t)cur_mode; }

void eyes_sleep(void) {
    if (lid == LID_DARK) return;
    read_clock();
    going_dark = true;
    lid_phase = LID_CLOSING;
    lid_deadline = now;
}

bool eyes_asleep(void) { return lid == LID_DARK; }

void eyes_wake(void) {
    read_clock();
    going_dark = false;
    enter_mode(cur_mode);
    lid = LID_SHUT;
    lid_phase = LID_HOLD;
    lid_deadline = now + WAKE_SHUT_MS;
}

void eyes_blink_now(void) { force_blink = true; }

void eyes_wink(bool left_eye) {
    if (blink_phase != BL_IDLE) return;
    read_clock();
    start_blink(left_eye, !left_eye);
}
