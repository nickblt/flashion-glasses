// SL-GlassesLed26x7 on its own HC89S003A: animated eyes (eyes.c), built for
// the stock chip so the board needs no rework. README.md has the flashing
// route; pinmap.h has the pins.
//
// The button: a short press steps to the next animation. Holding it for
// BTN_LONG_MS closes the eyes and powers down; holding it again powers back
// up. Across power-down the chip draws next to nothing, and RAM survives, so
// the eyes come back in the mode they left in.

#include "HC89S003F4.h"

#include "button.h"
#include "eyes.h"
#include "glasses_matrix.h"
#include "matrix_hc89.h"  // the scan ISR prototype must be visible here

#define PCON_PD     0x02
#define INT7_BIT    0x80  // P0.7's bit in PINTE0 and PINTF0
#define PITS1_IT7   0xC0  // INT7's trigger field in PITS1
#define IT7_FALLING 0x40

// After a wake, a press has to register within this long, or the wake was a
// glitch.
#define WAKE_GRACE_MS 100

static bool button_down(void) { return !P0_7; }  // pinmap.h: pressed reads 0

// INT2..INT7 share this vector, and only INT7 is enabled. It exists to wake the
// chip, so all it does is clear the flag, which these lines leave to software.
void button_wake_isr(void) HC_ISR(INT2_7_VECTOR) { PINTF0 &= (uint8_t)~INT7_BIT; }

// Powers down until the button is held for BTN_LONG_MS. A shorter press wakes
// the chip only long enough to time it.
static void power_down(void) {
    gl_matrix_stop();
    for (;;) {
        gl_matrix_park();
        PINTF0 &= (uint8_t)~INT7_BIT;  // stale from presses while running
        EX2_7 = 1;
        if (!button_down()) {
            PCON |= PCON_PD;  // stops here until P0.7 falls
            __asm
                nop
                nop
            __endasm;
        }
        EX2_7 = 0;

        // Time the press on the scan timer. The frames are blank, so the
        // display stays dark while it does.
        gl_matrix_init();
        gl_matrix_start();
        uint32_t woke = gl_millis();
        btn_init(woke, false);
        for (;;) {
            uint32_t t = gl_millis();
            btn_event_t ev = btn_update(t, button_down());
            if (ev == BTN_LONG) return;
            if (ev == BTN_SHORT) break;
            if (btn_is_up() && t - woke >= WAKE_GRACE_MS) break;
        }
        gl_matrix_stop();
    }
}

void main(void) {
    // Clock bring-up exactly as the vendor's register examples do it.
    WDTCCR = 0x00;    // watchdog off
    CLKSWR = 0x51;    // internal RC 32 MHz / 2: Fosc = 16 MHz
    CLKDIV = 0x01;    // Fcpu = Fosc
    FREQ_CLK = 0x10;  // the CPU clock in MHz; only IAP reads it, but keep it true

    // INT7 on P0.7's falling edge is the wake source, as in the stock firmware.
    // It reaches the CPU only across power-down, while EX2_7 is set.
    PITS1 = (PITS1 & (uint8_t)~PITS1_IT7) | IT7_FALLING;
    PINTE0 |= INT7_BIT;

    gl_matrix_init();
    eyes_init();
    gl_matrix_start();
    EA = 1;
    btn_init(gl_millis(), button_down());

    while (1) {
        switch (btn_update(gl_millis(), button_down())) {
            case BTN_SHORT:
                eyes_next_mode();
                break;
            case BTN_LONG:
                eyes_sleep();
                break;
            default:
                break;
        }
        eyes_update();

        // Off once the eyes have closed and the finger is off the button, so
        // the release cannot be taken for a wake.
        if (eyes_asleep() && btn_is_up()) {
            power_down();
            eyes_wake();
            btn_init(gl_millis(), true);  // the power-on hold: ignored to its release
        }
    }
}
