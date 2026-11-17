#include "ihex.h"

#include <string.h>

static int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

const char *ihex_parse(const char *line, ihex_rec_t *rec) {
    uint8_t raw[IHEX_MAX_DATA + 5];
    size_t chars = strlen(line);

    if (line[0] != ':' || chars % 2 == 0) return "malformed record";
    size_t n = (chars - 1) / 2;
    if (n < 5 || n > sizeof raw) return "record length out of range";

    uint8_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        int hi = nibble(line[1 + 2 * i]), lo = nibble(line[2 + 2 * i]);
        if (hi < 0 || lo < 0) return "not hex";
        raw[i] = (uint8_t)(hi << 4 | lo);
        sum = (uint8_t)(sum + raw[i]);
    }
    if (n != (size_t)raw[0] + 5) return "length byte disagrees with the record";
    if (sum) return "bad checksum";

    rec->len = raw[0];
    rec->addr = (uint16_t)(raw[1] << 8 | raw[2]);
    rec->type = raw[3];
    memcpy(rec->data, &raw[4], rec->len);
    return NULL;
}
