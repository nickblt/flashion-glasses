// Charlieplex scan-out for the SL-GlassesLed26x7 on its own HC89S003A.
//
// Implements glasses_matrix.h, the API eyes.c draws through.
// gl_present() compiles each section into the exact twelve register values the
// scan interrupt writes (three port latches, nine mode registers), so the
// interrupt only copies bytes: no shifts, no multiplies, no library calls,
// which on SDCC would be non-reentrant helpers shared with main().
//
// int is 16 bits here. Every shift that can reach bit 16 is done in uint32_t.

#include "glasses_matrix.h"

#include <string.h>

#include "matrix_hc89.h"
#include "pinmap.h"

#define MODE_INPUT       0x0  // plain input, no pull: high-Z
#define MODE_PULLUP_ST   0x6  // input, pull-up, Schmitt trigger
#define MODE_PUSH_PULL   0x8
#define MODE_OPEN_DRAIN  0xD
#define N_MODE_REGS      9    // P0M0..P0M3, P1M0, P2M0..P2M3

// BTN is P0.7 (pinmap.h): the high nibble of P0M3, and the only pin that is
// never a plain input. The button shorts it to ground, so it keeps its pull-up
// through every write below, as the stock firmware's does.
#define P0M3_IDLE        (MODE_PULLUP_ST << 4)
#define MODE_REG_P0M3    3    // its index in section_regs_t.mode

// Timer 3 on Fosc = 16 MHz / 8 = 2 MHz: two ticks per microsecond.
#define T3CON_FOSC_DIV8  0x10
#define T3CON_TR3        0x04
#define T3CON_TF3        0x80
#define T3_RELOAD        (65536ul - 2ul * GL_SLOT_US)

// Every matrix pin back to input. RST is a plain input too; BTN keeps its
// pull-up.
#define BLANK_MODES()                                          \
    do {                                                       \
        P0M0 = MODE_INPUT; P0M1 = MODE_INPUT;                  \
        P0M2 = MODE_INPUT; P0M3 = P0M3_IDLE;                   \
        P1M0 = MODE_INPUT;                                     \
        P2M0 = MODE_INPUT; P2M1 = MODE_INPUT;                  \
        P2M2 = MODE_INPUT; P2M3 = MODE_INPUT;                  \
    } while (0)

typedef struct {
    uint8_t port[3];             // P0, P1, P2
    uint8_t mode[N_MODE_REGS];   // P0M0..P0M3, P1M0, P2M0..P2M3
} section_regs_t;

// Two compiled frames. The scanner latches `front` at the start of each frame
// and reports it in `shown`, so a frame is never shown half old, half new, and
// gl_present() never writes the frame on screen.
static HC_XDATA section_regs_t frames[2][GL_SECTIONS];
static volatile uint8_t front;
static volatile uint8_t shown;
static bool running;

static HC_XDATA uint32_t fb[GL_ROWS];  // bit x of row y = pixel (x, y)

// Interrupt-only state.
static HC_XDATA section_regs_t *cur;
static uint8_t section;
static uint16_t us_acc;

static volatile uint32_t ms;

// --- scan -------------------------------------------------------------------

void gl_scan_isr(void) HC_ISR(T3_VECTOR) {
    T3CON &= (uint8_t)~T3CON_TF3;

    // Blank before the next section's levels go out: that is what stops one
    // section ghosting into the next.
    BLANK_MODES();

    if (section == 0) {
        shown = front;
        cur = shown ? frames[1] : frames[0];  // no multiply in here
    }

    // Latches while the pins are still inputs, then the modes.
    P0 = cur->port[0];
    P1 = cur->port[1];
    P2 = cur->port[2];
    P0M0 = cur->mode[0];
    P0M1 = cur->mode[1];
    P0M2 = cur->mode[2];
    P0M3 = cur->mode[3];
    P1M0 = cur->mode[4];
    P2M0 = cur->mode[5];
    P2M1 = cur->mode[6];
    P2M2 = cur->mode[7];
    P2M3 = cur->mode[8];

    cur++;
    if (++section == GL_SECTIONS) section = 0;

    us_acc += GL_SLOT_US;
    if (us_acc >= 1000) {
        us_acc -= 1000;
        ms++;
    }
}

uint32_t gl_millis(void) {
    uint8_t enabled = ET3;
    ET3 = 0;  // a 32-bit read is four instructions; keep the timer out of it
    uint32_t t = ms;
    ET3 = enabled;
    return t;
}

// --- framebuffer ------------------------------------------------------------

void gl_clear(void) { memset(fb, 0, sizeof fb); }

void gl_set_px(int x, int y, bool on) {
    if (x < 0 || x >= GL_COLS || y < 0 || y >= GL_ROWS) return;
    if (gl_cell_absent(x, y)) return;
    uint32_t bit = (uint32_t)1 << x;
    if (on) {
        fb[y] |= bit;
    } else {
        fb[y] &= ~bit;
    }
}

bool gl_get_px(int x, int y) {
    if (x < 0 || x >= GL_COLS || y < 0 || y >= GL_ROWS) return false;
    return (fb[y] >> x) & 1u;
}

void gl_draw_art(int x0, int y0, const char *const *art, int rows) {
    for (int r = 0; r < rows; r++) {
        const char *line = art[r];
        for (int c = 0; line[c]; c++) {
            if (line[c] == '#') gl_set_px(x0 + c, y0 + r, true);
        }
    }
}

// --- compile ----------------------------------------------------------------

// Make board pin `board_pin` a push-pull output at `level` in section `r`.
static void drive(HC_XDATA section_regs_t *r, uint8_t board_pin, uint8_t level) {
    uint8_t pin = board_pin_to_hc[board_pin];
    uint8_t port = HC_PORT(pin), bit = HC_BIT(pin);
    uint8_t reg = port == 0 ? bit >> 1 : port == 1 ? 4 : 5 + (bit >> 1);
    uint8_t shift = (bit & 1) ? 4 : 0;

    if (level) r->port[port] |= (uint8_t)(1u << bit);
    r->mode[reg] = (uint8_t)((r->mode[reg] & ~(0x0Fu << shift)) | (MODE_PUSH_PULL << shift));
}

void gl_present(void) {
    // The frame not on screen is the one we may write. If the last present
    // has not been picked up yet, wait for it: at most one frame, 5.2 ms.
    if (running && EA) {
        while (shown != front) {
        }
    }
    uint8_t back = front ^ 1;
    HC_XDATA section_regs_t *f = back ? frames[1] : frames[0];

    for (uint8_t s = 0; s < GL_SECTIONS; s++, f++) {
        uint16_t cathodes = 0;  // bit n = board pin n + 1
        for (uint8_t p = 0; p < 14; p++) {
            uint8_t x = 2 * s + (p & 1), y = p >> 1;
            if (!((fb[y] >> x) & 1u)) continue;

            // Cathode board pin is p+1. Where that collides with the section's
            // own anode, the board routes the LED to pin 15/16 instead.
            uint8_t pin = (p == s) ? GL_SECTION_SUB(s) : p + 1;
            cathodes |= (uint16_t)1 << (pin - 1);
        }
        uint8_t anode = GL_SECTION_PIN(s);
        cathodes &= (uint16_t)~((uint16_t)1 << (anode - 1));

        memset(f, 0, sizeof *f);
        f->mode[MODE_REG_P0M3] = P0M3_IDLE;
        if (!cathodes) continue;  // nothing lit: the section stays all-input

        drive(f, anode, 1);
        for (uint8_t q = 1; q <= GL_PIN_COUNT; q++) {
            if (cathodes & ((uint16_t)1 << (q - 1))) drive(f, q, 0);
        }
    }
    front = back;
}

// --- lifecycle --------------------------------------------------------------

void gl_matrix_init(void) {
    BLANK_MODES();
    P0 = 0;
    P1 = 0;
    P2 = 0;
    memset(frames, 0, sizeof frames);
    gl_clear();
    front = shown = 0;
    section = 0;
    cur = frames[0];
    running = false;
}

// The caller sets EA once everything else is ready.
bool gl_matrix_start(void) {
    T3CON = T3CON_FOSC_DIV8;              // stopped, so the reload also loads the count
    TH3 = (uint8_t)(T3_RELOAD >> 8);      // datasheet §9.3: high byte first
    TL3 = (uint8_t)T3_RELOAD;
    ET3 = 1;
    T3CON |= T3CON_TR3;
    running = true;
    return true;
}

void gl_matrix_stop(void) {
    T3CON &= (uint8_t)~T3CON_TR3;
    ET3 = 0;
    running = false;
    BLANK_MODES();
}

// The stock firmware's power-down pin state, read out of its image: every pin
// open-drain low but BTN, which keeps its pull-up to wake the chip. Nothing
// floats, which would leak in power-down, and with both ends of every LED
// grounded none can conduct. That includes P2.7, the R temple pads.
#define OD_PAIR ((uint8_t)(MODE_OPEN_DRAIN << 4 | MODE_OPEN_DRAIN))

void gl_matrix_park(void) {
    P0 = 0;
    P1 = 0;
    P2 = 0;
    P0M0 = OD_PAIR; P0M1 = OD_PAIR; P0M2 = OD_PAIR;
    P0M3 = P0M3_IDLE | MODE_OPEN_DRAIN;
    P1M0 = OD_PAIR;
    P2M0 = OD_PAIR; P2M1 = OD_PAIR; P2M2 = OD_PAIR; P2M3 = OD_PAIR;
}
