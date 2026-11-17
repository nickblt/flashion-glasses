// HC89S003A backend for glasses_matrix.h: the glasses driven by their
// own stock chip. Everything else lives in the shared API header; this adds
// what only this backend has.

#ifndef MATRIX_HC89_H
#define MATRIX_HC89_H

#include <stdint.h>

#include "HC89S003F4.h"

#ifdef __SDCC
#define HC_XDATA __xdata
#define HC_ISR(vector) __interrupt(vector)
#else  // host build, e.g. a PC-side test harness: plain C
#define HC_XDATA
#define HC_ISR(vector)
#endif

// Timer 3 scan interrupt. SDCC only emits a vector for an ISR whose prototype
// is visible in the file holding main(), so main.c includes this header.
void gl_scan_isr(void) HC_ISR(T3_VECTOR);

// Pins for power-down, after gl_matrix_stop(). gl_matrix_init() undoes it.
void gl_matrix_park(void);

#endif  // MATRIX_HC89_H
