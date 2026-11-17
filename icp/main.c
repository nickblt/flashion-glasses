// HC89S003A ICP probe: a USB serial console on a Pico, talking to the stock
// chip over JTAG on the glasses' 5-hole header. The Pico is only the
// programmer; the glasses keep their own chip and run what it flashes
// (firmware/).
//
// Usage: README.md. Protocol: PROTOCOL.md. Wiring: icp_pins.h.
//
//   picocom /dev/ttyACM0                                  interactive; type `help`
//   tools/icp_console.py dump > glasses.txt                scripted capture
//   tools/icp_console.py flash firmware/build/glasses.hex  ICP_WRITE build only
//
// The default build is read-only. Erase and program exist only when built
// with -DICP_WRITE=ON, and even then `flash` refuses until `chipid` has passed
// since the probe booted (PROTOCOL.md §8). Every command except `run` leaves the
// target parked -- powered down if VDD is switched, held in reset either way --
// and every reply ends with a line starting "ok" or "error".

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "hc_icp.h"
#include "icp_hal.h"
#include "icp_pins.h"
#include "ihex.h"

#define CODE_SIZE   0x4000
#define OPTION_SIZE 0x80
#define READ_CHUNK  0x400
#define OPT_RVCFG   0x3E  // second reset vector; 0x00 = reset at 0x0000 (PROTOCOL.md §2)

static struct {
    uint32_t khz;
    icp_entry_t entry;
    uint32_t window_ms;
    uint32_t password;
    icp_unlock_t unlock;
} cfg = { ICP_DEFAULT_KHZ, ICP_ENTRY_POWER, 100, 0, ICP_UNLOCK_PASSWORD };  // ~180 JTAG tries per window

static uint8_t code[CODE_SIZE];

// The image to flash or verify, loaded as Intel HEX records. Bytes it does not
// cover are blank, which is what an erased sector reads (ICP_BLANK).
static uint8_t image[ICP_USER_ROM_SIZE];
static uint16_t image_end;   // one past the highest loaded byte
static uint32_t image_bytes;
static bool image_ready;     // EOF record seen

static bool chipid_ok;       // first contact passed since boot

static void fail(const char *what, icp_err_t e) {
    printf("error: %s: %s\n", what, icp_strerror(e));
}

static void print_entry(const icp_entry_log_t *log, uint32_t elapsed_ms) {
    printf("attempts %d in %lu ms; IDCODEs seen:", log->attempts, (unsigned long)elapsed_ms);
    if (!log->n_ids) printf(" none");
    for (int i = 0; i < log->n_ids; i++) printf(" %08lX", (unsigned long)log->ids_seen[i]);
    printf("; all-zero reads %d, all-one reads %d\n", log->all_zero, log->all_ones);
}

// --- wiring check -----------------------------------------------------------
//
// Read-only and protocol-free: every probe line stays an input throughout,
// except RST, which is only ever driven low (parked) or released. Each line is
// sampled with the Pico's pull-down and then its pull-up. A line nothing drives
// follows the pull; one on a live matrix line toggles against both.

#define CENSUS_SAMPLES 2000

static const struct { uint pin; const char *name, *where; } census_lines[] = {
    { ICP_PIN_TMS, "TMS", "hole 2, TSSOP 13" },
    { ICP_PIN_TCK, "TCK", "hole 3, TSSOP 14" },
    { ICP_PIN_TDO, "TDO", "hole 4, TSSOP 15" },
    { ICP_PIN_TDI, "TDI", "hole 5, TSSOP 16" },
    { ICP_PIN_RST, "RST", "R pad,  TSSOP 12" },
};

static unsigned census_highs(uint pin, bool pull_up) {
    gpio_set_dir(pin, GPIO_IN);
    gpio_set_pulls(pin, pull_up, !pull_up);
    sleep_us(500);
    unsigned highs = 0;
    for (unsigned i = 0; i < CENSUS_SAMPLES; i++) {
        highs += gpio_get(pin);
        busy_wait_us_32(50);  // 2000 samples over ~100 ms: several scan frames
    }
    gpio_disable_pulls(pin);
    return highs;
}

static void census_line(int i) {
    unsigned pd = census_highs(census_lines[i].pin, false);  // highs despite pull-down
    unsigned pu = CENSUS_SAMPLES - census_highs(census_lines[i].pin, true);  // lows despite pull-up
    const char *verdict =
        pd == 0 && pu == 0                  ? "floating (follows the pull)" :
        pd == CENSUS_SAMPLES && pu == 0     ? "held high" :
        pd == 0 && pu == CENSUS_SAMPLES     ? "held low" :
                                              "ACTIVE (driven both ways)";
    printf("  %s  %-17s  high vs pull-down %4u/%u, low vs pull-up %4u/%u  %s\n",
           census_lines[i].name, census_lines[i].where, pd, CENSUS_SAMPLES, pu, CENSUS_SAMPLES,
           verdict);
}

// Both phases need the target on its own supply: under `entry power` it is
// switched off whenever it is parked, so there would be nothing to measure.
static void cmd_pins(void) {
    if (cfg.entry == ICP_ENTRY_POWER) {
        printf("error: pins needs `entry rst` wiring (VDD on 3V3(OUT), RST wired); under "
               "entry power the target is unpowered\n");
        return;
    }
    icp_release();  // parked: RST low, everything else floating
    printf("parked, RST held low (chip in reset: expect all four floating):\n");
    for (int i = 0; i < 4; i++) census_line(i);

    icp_run();  // RST released: the stock firmware scans the matrix
    sleep_ms(300);
    printf("running, RST released (stock firmware: expect all four ACTIVE):\n");
    for (int i = 0; i < 5; i++) census_line(i);
    printf("ok\n");
}

// Reset, catch, and optionally unlock + halt. Prints what the entry saw.
static bool connect(bool halt) {
    uint32_t id = 0;
    icp_entry_log_t log;
    uint32_t t0 = icp_hal_us();
    icp_err_t e = icp_enter(cfg.entry, cfg.window_ms, &id, &log);
    print_entry(&log, (icp_hal_us() - t0) / 1000u);
    if (e) {
        fail("entry", e);
        return false;
    }
    printf("IDCODE %08lX (%s byte order)\n", (unsigned long)id,
           id == ICP_IDCODE_LE ? "little-endian" : "big-endian");
    if (!halt) return true;
    if (cfg.unlock == ICP_UNLOCK_FACTORY_KEY) printf("unlocking with the factory key\n");
    e = icp_unlock_and_halt(cfg.unlock, cfg.password);
    if (e) {
        const icp_status_log_t *s = icp_status_log();
        printf("status: %d polls, values seen:", s->polls);
        for (int i = 0; i < s->n_seen; i++) printf(" %03lX", (unsigned long)s->seen[i]);
        printf(" (ready is (s & 0x0A) == 0x08)\n");
        fail("unlock/halt", e);
        return false;
    }
    printf("halted\n");
    return true;
}

static void hexdump(uint16_t base, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i += 16) {
        printf("%04X:", (unsigned)(base + i));
        for (size_t j = 0; j < 16 && i + j < n; j++) printf(" %02X", b[i + j]);
        printf("\n");
    }
}

static void intel_hex(const uint8_t *b, size_t n) {
    for (size_t a = 0; a < n; a += 16) {
        size_t len = n - a < 16 ? n - a : 16;
        uint8_t sum = (uint8_t)(len + (a >> 8) + (a & 0xFF));
        printf(":%02X%04X00", (unsigned)len, (unsigned)a);
        for (size_t i = 0; i < len; i++) {
            printf("%02X", b[a + i]);
            sum = (uint8_t)(sum + b[a + i]);
        }
        printf("%02X\n", (uint8_t)-sum);
    }
    printf(":00000001FF\n");
}

static bool read_option(uint16_t addr, uint8_t *buf, size_t n) {
    icp_err_t e = icp_read_mem(ICP_AREA_OPTION, addr, buf, n);
    if (e) fail("OPTION read", e);
    return !e;
}

static bool read_code(uint16_t addr, uint8_t *buf, size_t n) {
    for (size_t off = 0; off < n; off += READ_CHUNK) {
        size_t len = n - off < READ_CHUNK ? n - off : READ_CHUNK;
        icp_err_t e = icp_read_mem(ICP_AREA_CODE, (uint16_t)(addr + off), buf + off, len);
        if (e) {
            printf("at %04X: ", (unsigned)(addr + off));
            fail("code read", e);
            return false;
        }
    }
    return true;
}

// Compare user ROM with the loaded image (blank where nothing was loaded).
static bool compare_image(void) {
    if (!read_code(0, code, ICP_USER_ROM_SIZE)) return false;
    unsigned bad = 0, first = 0;
    for (unsigned a = 0; a < ICP_USER_ROM_SIZE; a++) {
        if (code[a] == image[a]) continue;
        if (!bad++) first = a;
    }
    if (bad) {
        printf("error: %u bytes differ, first at %04X: chip %02X, image %02X\n", bad, first,
               code[first], image[first]);
        return false;
    }
    printf("user ROM 0000-%04X matches the image\n", ICP_USER_ROM_SIZE - 1);
    return true;
}

// --- read-only commands -----------------------------------------------------

static void cmd_id(void) {
    if (connect(false)) printf("ok\n");
}

// First contact: CHIP_ID twice, identical, and not blank.
static void cmd_chipid(void) {
    if (!connect(true)) return;
    uint8_t a[8], b[8];
    if (!read_option(0x0100, a, sizeof a) || !read_option(0x0100, b, sizeof b)) return;
    printf("CHIP_ID");
    for (int i = 0; i < 8; i++) printf(" %02X", a[i]);
    printf("\n");
    bool same = memcmp(a, b, sizeof a) == 0;
    bool blank = true;
    for (int i = 0; i < 8; i++) blank &= a[i] == 0x00 || a[i] == 0xFF;
    if (!same) {
        printf("error: two reads disagree\n");
    } else if (blank) {
        printf("error: CHIP_ID reads blank, suspicious\n");
    } else {
        chipid_ok = true;
        printf("ok\n");
    }
}

static void cmd_option(void) {
    if (!connect(true)) return;
    uint8_t buf[OPTION_SIZE];
    if (!read_option(0x0000, buf, sizeof buf)) return;
    printf("OPTION 0x0000 (user: SN, ID, FLASH_SC at 0x20, config at 0x30, RVCFG at 0x3E)\n");
    hexdump(0x0000, buf, sizeof buf);
    if (!read_option(0x0100, buf, sizeof buf)) return;
    printf("OPTION 0x0100 (factory: CHIP_ID at 0x100)\n");
    hexdump(0x0100, buf, sizeof buf);
    printf("ok\n");
}

// Read the factory image before anything is ever erased. All zeros means read
// protection, not an empty chip (PROTOCOL.md §3).
static void cmd_dump(void) {
    if (!connect(true) || !read_code(0, code, CODE_SIZE)) return;
    intel_hex(code, CODE_SIZE);
    size_t zeros = 0;
    for (size_t i = 0; i < CODE_SIZE; i++) zeros += code[i] == 0;
    printf("%u of %u bytes are zero\n", (unsigned)zeros, (unsigned)CODE_SIZE);
    printf("ok\n");
}

static void cmd_verify(void) {
    if (!image_ready) {
        printf("error: no image loaded (new, then Intel HEX records)\n");
        return;
    }
    if (connect(true) && compare_image()) printf("ok\n");
}

// --- image loading ----------------------------------------------------------

static void cmd_new(void) {
    memset(image, ICP_BLANK, sizeof image);
    image_end = 0;
    image_bytes = 0;
    image_ready = false;
    printf("ok\n");
}

// One Intel HEX record. Only types 00, 01 and a zero 04 are accepted. Data
// past user ROM is accepted only if it is blank, and then ignored: `flash`
// never writes there, and a full `dump` (0x0000-0x3FFF) loads as it is.
static void cmd_record(const char *line) {
    ihex_rec_t rec;
    const char *why = ihex_parse(line, &rec);
    if (why) {
        printf("error: %s\n", why);
        return;
    }
    if (image_ready) {
        printf("error: image already complete; `new` to start another\n");
        return;
    }
    if (rec.type == 0x00) {
        unsigned in_rom = rec.addr >= ICP_USER_ROM_SIZE ? 0
                        : rec.addr + rec.len > ICP_USER_ROM_SIZE ? ICP_USER_ROM_SIZE - rec.addr
                        : rec.len;
        for (unsigned i = in_rom; i < rec.len; i++) {
            if (rec.data[i] != ICP_BLANK) {
                printf("error: data at %04X is past user ROM (%04X) and not blank\n",
                       (unsigned)rec.addr + i, ICP_USER_ROM_SIZE);
                return;
            }
        }
        if (in_rom) {
            memcpy(&image[rec.addr], rec.data, in_rom);
            image_bytes += in_rom;
            if (rec.addr + in_rom > image_end) image_end = (uint16_t)(rec.addr + in_rom);
        }
    } else if (rec.type == 0x01) {
        image_ready = true;
        printf("image: %lu bytes, 0000-%04X\n", (unsigned long)image_bytes,
               image_end ? image_end - 1 : 0);
    } else if (!(rec.type == 0x04 && rec.len == 2 && rec.data[0] == 0 && rec.data[1] == 0)) {
        printf("error: record type %02X not supported\n", rec.type);
        return;
    }
    printf("ok\n");
}

// --- writing (ICP_WRITE builds only) ----------------------------------------

#ifdef ICP_WRITE

// Erase all user ROM, check it took, program the image, verify every byte.
static void cmd_flash(void) {
    if (!image_ready) {
        printf("error: no image loaded (new, then Intel HEX records)\n");
        return;
    }
    if (!chipid_ok) {
        printf("error: run chipid first; flash only follows a passed first contact\n");
        return;
    }
    if (!connect(true)) return;

    // A moved reset vector would leave our image unreachable.
    uint8_t opt[OPTION_SIZE];
    if (!read_option(0x0000, opt, sizeof opt)) return;
    if (opt[OPT_RVCFG] != 0x00) {
        printf("error: RVCFG = %02X, so reset does not start at 0x0000; flashing here would "
               "not run. Stopping (PROTOCOL.md §2)\n", opt[OPT_RVCFG]);
        return;
    }

    uint32_t t0 = icp_hal_us();
    for (uint16_t a = 0; a < ICP_USER_ROM_SIZE; a += ICP_SECTOR_SIZE) {
        icp_err_t e = icp_erase_sector(a);
        if (e) {
            printf("at %04X: ", a);
            fail("erase", e);
            return;
        }
    }
    printf("erased 0000-%04X in %lu ms\n", ICP_USER_ROM_SIZE - 1,
           (unsigned long)((icp_hal_us() - t0) / 1000u));

    // If erase was silently refused, programming would OR into old code.
    if (!read_code(0, code, ICP_USER_ROM_SIZE)) return;
    for (unsigned a = 0; a < ICP_USER_ROM_SIZE; a++) {
        if (code[a] != ICP_BLANK) {
            printf("error: %04X reads %02X after erase; ICP erase protection? (PROTOCOL.md §3)\n", a,
                   code[a]);
            return;
        }
    }

    t0 = icp_hal_us();
    icp_err_t e = icp_program(0, image, image_end);
    if (e) {
        fail("program", e);
        return;
    }
    printf("programmed %u bytes in %lu ms\n", image_end,
           (unsigned long)((icp_hal_us() - t0) / 1000u));

    if (compare_image()) printf("ok\n");
}

#endif  // ICP_WRITE

// --- console ----------------------------------------------------------------

static void cmd_status(void) {
#ifdef ICP_WRITE
    printf("build: ICP_WRITE (erase/program enabled)\n");
#else
    printf("build: read-only\n");
#endif
    printf("khz %lu, entry %s, window %lu ms, unlock %s, password %08lX\n",
           (unsigned long)cfg.khz, cfg.entry == ICP_ENTRY_POWER ? "power" : "rst",
           (unsigned long)cfg.window_ms,
           cfg.unlock == ICP_UNLOCK_FACTORY_KEY ? "factory" : "password",
           (unsigned long)cfg.password);
    printf("chipid passed: %s; image: %s\n", chipid_ok ? "yes" : "no",
           image_ready ? "loaded" : "none");
    printf("ok\n");
}

static void cmd_help(void) {
    printf("HC89S003A ICP probe (README.md)\n"
           "  id            power-cycle, catch the debug port, report IDCODE\n"
           "  chipid        + unlock, halt, read CHIP_ID twice  <- first milestone\n"
           "  option        + dump both OPTION pages\n"
           "  dump          + dump code 0x0000-0x3FFF as Intel HEX\n"
           "  new           start loading an image; then send its Intel HEX records\n"
           "  verify        compare user ROM with the loaded image\n"
#ifdef ICP_WRITE
           "  flash         erase user ROM, program the image, verify (needs chipid)\n"
#endif
           "  off           park the target: held in reset, all other lines floating\n"
           "  khz <n>       clock rate, 10-1000 (default %d)\n"
           "  entry power|rst   (default power: the glasses)\n"
           "  pins          entry rst only: line levels parked, then with the firmware running\n"
           "  run           entry rst only: release reset and let the target run its firmware\n"
           "  window <ms>   how long to keep trying after reset\n"
           "  pwd <hex>     FLASH_SC to unlock with (default 0)\n"
           "  unlock password|factory   factory = Holychip's key, for a locked chip\n"
           "  status\n"
           "ok\n", ICP_DEFAULT_KHZ);
}

static void run(char *line) {
    if (line[0] == ':') {
        cmd_record(line);
        return;  // the target is untouched; nothing to release
    }
    char *cmd = strtok(line, " \t");
    char *arg = strtok(NULL, " \t");
    if (!cmd) return;

    if (!strcmp(cmd, "help")) cmd_help();
    else if (!strcmp(cmd, "status")) cmd_status();
    else if (!strcmp(cmd, "id")) cmd_id();
    else if (!strcmp(cmd, "pins")) cmd_pins();
    else if (!strcmp(cmd, "chipid")) cmd_chipid();
    else if (!strcmp(cmd, "option")) cmd_option();
    else if (!strcmp(cmd, "dump")) cmd_dump();
    else if (!strcmp(cmd, "new")) cmd_new();
    else if (!strcmp(cmd, "verify")) cmd_verify();
#ifdef ICP_WRITE
    else if (!strcmp(cmd, "flash")) cmd_flash();
#else
    else if (!strcmp(cmd, "flash")) printf("error: read-only build; rebuild with -DICP_WRITE=ON\n");
#endif
    else if (!strcmp(cmd, "off")) printf("ok\n");
    else if (!strcmp(cmd, "run")) {
        if (cfg.entry == ICP_ENTRY_POWER) {
            // GP19 would be powering a running matrix: not what it is for.
            printf("error: run needs `entry rst` wiring (VDD on 3V3(OUT)); under entry power "
                   "the target is unpowered\n");
        } else {
            icp_run();  // the one command that leaves the target running
            printf("target running its firmware; `off` parks it again\n");
            printf("ok\n");
            return;
        }
    }
    else if (!strcmp(cmd, "khz") && arg) {
        uint32_t k = strtoul(arg, NULL, 0);
        if (k < 10 || k > 1000) {
            printf("error: khz out of range\n");
        } else {
            cfg.khz = k;
            icp_hal_set_khz(k);
            printf("ok\n");
        }
    } else if (!strcmp(cmd, "entry") && arg && (!strcmp(arg, "power") || !strcmp(arg, "rst"))) {
        cfg.entry = !strcmp(arg, "power") ? ICP_ENTRY_POWER : ICP_ENTRY_RST;
        printf("ok\n");
    } else if (!strcmp(cmd, "window") && arg) {
        cfg.window_ms = strtoul(arg, NULL, 0);
        printf("ok\n");
    } else if (!strcmp(cmd, "unlock") && arg &&
               (!strcmp(arg, "password") || !strcmp(arg, "factory"))) {
        cfg.unlock = !strcmp(arg, "factory") ? ICP_UNLOCK_FACTORY_KEY : ICP_UNLOCK_PASSWORD;
        printf("ok\n");
    } else if (!strcmp(cmd, "pwd") && arg) {
        cfg.password = strtoul(arg, NULL, 16);
        printf("ok\n");
    } else {
        printf("error: unknown command, try help\n");
    }
    icp_release();
}

int main(void) {
    // Target parked in reset before USB comes up: see icp_hal_init().
    icp_hal_init();
    icp_release();
    stdio_init_all();
    memset(image, ICP_BLANK, sizeof image);

    char line[128];
    size_t n = 0;
    printf("> ");
    while (true) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[n] = 0;
            run(line);
            n = 0;
            printf("> ");
        } else if ((c == 0x7F || c == 0x08) && n) {
            n--;
            printf("\b \b");
        } else if (c >= ' ' && n < sizeof line - 1) {
            line[n++] = (char)c;
            putchar(c);
        }
    }
}
