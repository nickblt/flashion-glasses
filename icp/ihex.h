// One Intel HEX record. Split out of main.c so it can be tested on a PC: `flash`
// verifies the chip against the image as parsed here, so a parse bug would
// otherwise program the wrong bytes and still report a match.

#ifndef IHEX_H
#define IHEX_H

#include <stdint.h>

#define IHEX_MAX_DATA 32

typedef struct {
    uint8_t type;   // 0x00 data, 0x01 EOF, 0x04 extended linear address
    uint8_t len;
    uint16_t addr;
    uint8_t data[IHEX_MAX_DATA];
} ihex_rec_t;

// Parses ":LLAAAATT<data>CC". Returns NULL on success, else why not.
const char *ihex_parse(const char *line, ihex_rec_t *rec);

#endif  // IHEX_H
