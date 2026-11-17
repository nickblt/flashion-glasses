// icp_hal.h on RP2040 GPIOs. Wiring: icp_pins.h.

#include "icp_hal.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "icp_pins.h"

static uint32_t half_cycles;

static void float_pin(uint pin) {
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);
}

// Called first thing in main(), before USB starts: the glasses run off the
// Pico's 3V3 in `entry rst` wiring, and their stock firmware lighting the
// matrix sagged the rail enough to break USB enumeration. Parking the target
// in reset keeps every one of its pins an input, so it draws almost nothing.
void icp_hal_init(void) {
    gpio_init(ICP_PIN_RST);
    icp_hal_rst(true);

    const uint lines[] = { ICP_PIN_TCK, ICP_PIN_TDO, ICP_PIN_TMS, ICP_PIN_TDI };
    for (unsigned i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        gpio_init(lines[i]);
        // These are LED matrix lines, and the matrix joins them through LEDs
        // with no series resistors (TMS to TDI is LED (25,4), for one). Drive
        // hard enough to hold a logic level against an LED; slow edges still.
        gpio_set_drive_strength(lines[i], GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(lines[i], GPIO_SLEW_RATE_SLOW);
        float_pin(lines[i]);
    }
    gpio_set_drive_strength(ICP_PIN_RST, GPIO_DRIVE_STRENGTH_2MA);
    // GP19 may power the target directly (icp_pins.h): the strongest setting.
    gpio_init(ICP_PIN_PWR);
    gpio_set_drive_strength(ICP_PIN_PWR, GPIO_DRIVE_STRENGTH_12MA);
    gpio_put(ICP_PIN_PWR, !ICP_PWR_ON_LEVEL);
    gpio_set_dir(ICP_PIN_PWR, GPIO_OUT);
    icp_hal_set_khz(ICP_DEFAULT_KHZ);
}

// Lines float before the supply drops and stay floating until it is back, so
// nothing ever back-powers the target through its clamp diodes. RST is the
// exception: off parks it low (0 V cannot back-power anything), and it is
// left alone on the way up -- icp_enter() releases it when it means to.
void icp_hal_power(bool on) {
    if (on) {
        gpio_put(ICP_PIN_PWR, ICP_PWR_ON_LEVEL);
        gpio_put(ICP_PIN_TCK, 0);
        gpio_set_dir(ICP_PIN_TCK, GPIO_OUT);
        gpio_pull_up(ICP_PIN_TDO);
        gpio_set_dir(ICP_PIN_TDO, GPIO_IN);
        gpio_put(ICP_PIN_TMS, 1);  // TAP held in Test-Logic-Reset until used
        gpio_set_dir(ICP_PIN_TMS, GPIO_OUT);
        gpio_put(ICP_PIN_TDI, 0);
        gpio_set_dir(ICP_PIN_TDI, GPIO_OUT);
    } else {
        float_pin(ICP_PIN_TCK);
        float_pin(ICP_PIN_TDO);
        float_pin(ICP_PIN_TMS);
        float_pin(ICP_PIN_TDI);
        icp_hal_rst(true);
        gpio_put(ICP_PIN_PWR, !ICP_PWR_ON_LEVEL);
    }
}

void icp_hal_rst(bool assert) {
    if (assert) {
        gpio_put(ICP_PIN_RST, 0);
        gpio_set_dir(ICP_PIN_RST, GPIO_OUT);
    } else {
        float_pin(ICP_PIN_RST);
    }
}

void icp_hal_tck(bool high) { gpio_put(ICP_PIN_TCK, high); }

void icp_hal_tms(bool high) { gpio_put(ICP_PIN_TMS, high); }

void icp_hal_tdi(bool high) { gpio_put(ICP_PIN_TDI, high); }

bool icp_hal_tdo(void) { return gpio_get(ICP_PIN_TDO); }

void icp_hal_half_period(void) { busy_wait_at_least_cycles(half_cycles); }

void icp_hal_set_khz(uint32_t khz) {
    uint32_t c = clock_get_hz(clk_sys) / (2000u * khz);
    half_cycles = c ? c : 1;
}

uint32_t icp_hal_us(void) { return time_us_32(); }

void icp_hal_sleep_ms(uint32_t ms) { sleep_ms(ms); }
