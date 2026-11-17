// Holychip HC89S003A in-circuit programming over JTAG (the glasses' 5-hole
// header).
//
// The protocol is PROTOCOL.md, decoded from the HC-LINK V4 firmware
// (LINK_4_0.bin, linked at 0x08000C00). FUN_xxxxxxxx names in comments below
// are Ghidra's names for that image's functions, so each step can be checked
// against a decompile of it.
//
// Read-only by construction unless built with ICP_WRITE: then, and only then,
// it gains sector erase and byte program, both confined to user code ROM
// (0x0000-0x2FFF). In no build is there an OPTION-area write or an all-block
// erase. The factory key is available as an unlock only (icp_unlock_t). The
// indirect flash index/data pair is only ever set to values the vendor's own
// code uses (PROTOCOL.md §8).

#ifndef HC_ICP_H
#define HC_ICP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ICP_OK = 0,
    ICP_E_TIMEOUT,    // status never became ready
    ICP_E_NO_TARGET,  // nothing answered inside the entry window
    ICP_E_ID,         // something answered, but not with our IDCODE
    ICP_E_RANGE,      // erase/program outside user code ROM
} icp_err_t;

#define ICP_USER_ROM_SIZE  0x3000  // PROTOCOL.md §2: 12 KB usable; 0x3000-0x3FFF untouched
#define ICP_SECTOR_SIZE    0x80
#define ICP_BLANK          0x00    // .dev FLASH_BLANKValueInCodeArea: erased reads 00

// Both byte orders of the .dev file's `08 68 6A 73` (PROTOCOL.md §4). Silicon
// sends ICP_IDCODE_LE; both are still accepted.
#define ICP_IDCODE_LE  0x736A6808u
#define ICP_IDCODE_BE  0x08686A73u

typedef enum { ICP_AREA_CODE, ICP_AREA_OPTION } icp_area_t;

typedef enum { ICP_ENTRY_POWER, ICP_ENTRY_RST } icp_entry_t;

typedef struct {
    uint32_t ids_seen[8];  // distinct non-trivial IDCODE values, for diagnosis
    int n_ids;
    int attempts;
    int all_zero;          // reads of 00000000: something holding the data line low
    int all_ones;          // reads of FFFFFFFF: nothing driving it (the pull-up wins)
} icp_entry_log_t;

const char *icp_strerror(icp_err_t e);

// --- debug port -------------------------------------------------------------

// Reset the target and catch its debug port before user code starts. Succeeds
// on the same IDCODE twice running (FUN_08008430). `log` may be NULL. JTAG has
// no ACK: a missing target shows as TDO's pull-up, all ones.
icp_err_t icp_enter(icp_entry_t how, uint32_t window_ms, uint32_t *idcode,
                    icp_entry_log_t *log);

// How to unlock. FACTORY_KEY sends Holychip's fixed key 0xF0F4FF79 in place of
// the password: IR 3, then IR 4 <- 0 (FUN_0800835c). HC-PM51 uses it for
// "Factory Reset" (menu 0x9C96 sets project bit 0x40; FUN_08008430 then picks
// the key), after which its normal erase /
// program / verify run in that session. The key only unlocks: the factory
// OPTION-page rewrite that Factory Reset also does is not implemented here.
typedef enum { ICP_UNLOCK_PASSWORD, ICP_UNLOCK_FACTORY_KEY } icp_unlock_t;

#define ICP_FACTORY_KEY  0xF0F4FF79u

// Unlock, then control 0x34, 0x14, then wait for ready (PROTOCOL.md §4).
icp_err_t icp_unlock_and_halt(icp_unlock_t how, uint32_t password);

// What the most recent wait for ready saw: for diagnosing ICP_E_TIMEOUT.
typedef struct {
    int polls;
    uint32_t last;
    uint32_t seen[6];  // distinct status values, in order of appearance
    int n_seen;
} icp_status_log_t;
const icp_status_log_t *icp_status_log(void);

// Run one 8051 instruction on the halted core: opcode<<16 | b1<<8 | b2.
icp_err_t icp_exec(uint32_t insn);

// Read flash through the streaming engine in mode 4 (FUN_08008e10). Saves and
// restores A and DPTR around every chunk, as the vendor does.
icp_err_t icp_read_mem(icp_area_t area, uint16_t addr, uint8_t *buf, size_t n);

#ifdef ICP_WRITE
// On ICP_E_TIMEOUT from either call the target is left exactly as it was when
// the flash operation stalled: call icp_release() next, nothing else.

// Erase the 128-byte code sector holding `addr` (FUN_080090c8, indirect
// 0x0D <- 0xD2). Code area only.
icp_err_t icp_erase_sector(uint16_t addr);

// Program n bytes of code at addr into erased flash (FUN_08008fac, indirect
// 0x00 <- 0x00, streaming mode 2: write one byte, poll ready, repeat).
icp_err_t icp_program(uint16_t addr, const uint8_t *buf, size_t n);
#endif

// Power the target down, float every line, and park RST low. With VDD wired
// straight to 3V3 (`entry rst`), that leaves the chip held in reset: dark, and
// drawing almost nothing.
void icp_release(void);

// Let a parked target run its firmware (RST released, lines still floating).
// Only meaningful with VDD on 3V3 (`entry rst` wiring): with VDD switched, the
// target stays powered down.
void icp_run(void);

#endif  // HC_ICP_H
