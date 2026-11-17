// Holychip ICP, host side: the JTAG link layer, and the debug-port layer above
// it. See hc_icp.h and PROTOCOL.md.

#include "hc_icp.h"

#include "icp_hal.h"

#define TAP_RESET_CLOCKS   5     // FUN_08008072
#define POWER_DOWN_MS      200

// Ready timeouts. The vendor counts polls (.dev DACKMaxCount); time is the same
// limit at any clock rate. Datasheet §3.1.5: a sector erase takes 5 ms and a
// byte program 68 us, both by IAP.
#define READY_MS_EXEC      20
#define READY_MS_PROGRAM   10
#define READY_MS_ERASE     50

#define STATUS_READY_MASK  0x0A
#define STATUS_READY       0x08

#define CTRL_DEBUG_REQ     0x34  // the two connect writes, in this order (PROTOCOL.md §4)
#define CTRL_HALT          0x14
#define CTRL_EXEC          0x57  // run the latched instruction (FUN_08008a04)

#define MODE_OFF           0
#define MODE_FLASH_WRITE   2     // ICP_WRITE builds only: erase or program
#define MODE_READ_CODE     4

#define READ_CHUNK_MAX     128   // bytes per read, between A/DPTR save and restore

#define OP_MOV_DPTR_IMM    0x90  // 90 hh ll
#define OP_MOV_A_IMM       0x74  // 74 vv
#define OP_MOV_DIRECT_IMM  0x75  // 75 aa vv
#define OP_MOV_A_DIRECT    0xE5  // E5 aa

#define SFR_DPL            0x82
#define SFR_DPH            0x83
#define SFR_FLASH_IDX      0x94  // indirect flash registers, ICP-only (PROTOCOL.md §6)
#define SFR_FLASH_DAT      0x95

// JTAG (PROTOCOL.md §5): a 4-bit IR, and behind IR 0xB a register file reached by an
// 8-bit select scan followed by a scan of that register's own width.
#define IR_IDCODE          0x1
#define IR_KEY_LO          0x3   // FUN_0800835c: the factory key, then zero
#define IR_KEY_HI          0x4
#define IR_PASSWORD_LO     0x6   // FUN_08008262: the password, two 32-bit words
#define IR_PASSWORD_HI     0x7
#define IR_STREAM          0xA   // one 8-bit scan per streamed byte
#define IR_DEBUG           0xB
#define IR_UNKNOWN         0xFF

#define SEL_CTRL           0x80  // W 11
#define SEL_STATUS         0x81  // R 11
#define SEL_ACC            0x83  // R 8
#define SEL_INSN           0x86  // W 24
#define SEL_MODE           0x88  // W 8
#define JTAG_CTRL_BITS     11

// Index/value pairs the vendor sets before a read: code from LinkDriver's
// MemoryReadCode_Speedup (`86 ff 94 00 95`), OPTION from .dev IND_SFRADDR1/DATA1.
static const uint8_t area_idx[] = { [ICP_AREA_CODE] = 0xFF, [ICP_AREA_OPTION] = 0x0E };
static const uint8_t area_val[] = { [ICP_AREA_CODE] = 0x00, [ICP_AREA_OPTION] = 0xA5 };

// --- JTAG link layer (PROTOCOL.md §5) ---------------------------------------
//
// Plain IEEE 1149.1, LSB first. JTAG has no ACK and no parity, so these never
// fail at the link: a missing or wrong target shows up as a bad IDCODE (TDO's
// pull-up reads all ones) or a status that never becomes ready.

static uint8_t jtag_ir = IR_UNKNOWN;  // what the TAP holds; skips repeat IR scans

// One TCK cycle. TDO is sampled half a period after the falling edge, just
// before the rising edge, as FUN_080080a0 does.
static bool jtag_bit(bool tms, bool tdi) {
    icp_hal_tms(tms);
    icp_hal_tdi(tdi);
    icp_hal_half_period();
    bool tdo = icp_hal_tdo();
    icp_hal_tck(true);
    icp_hal_half_period();
    icp_hal_tck(false);
    return tdo;
}

// FUN_08008072: Test-Logic-Reset, then Run-Test/Idle.
static void jtag_reset(void) {
    for (int i = 0; i < TAP_RESET_CLOCKS; i++) jtag_bit(true, false);
    jtag_bit(false, false);
    jtag_ir = IR_UNKNOWN;
}

// FUN_080080a0 (IR) / FUN_0800815a (DR): from Run-Test/Idle to Shift, n bits
// with TMS raised on the last, then Update and back to Run-Test/Idle.
static uint32_t jtag_scan(bool ir, uint32_t v, unsigned n) {
    jtag_bit(true, false);              // Select-DR-Scan
    if (ir) jtag_bit(true, false);      // Select-IR-Scan
    jtag_bit(false, false);             // Capture
    jtag_bit(false, false);             // Shift
    uint32_t out = 0;
    for (unsigned i = 0; i < n; i++) {
        if (jtag_bit(i + 1 == n, (v >> i) & 1u)) out |= 1u << i;
    }
    jtag_bit(true, false);              // Update
    jtag_bit(false, false);             // Run-Test/Idle
    return out;
}

static void jtag_set_ir(uint8_t ir) {
    if (jtag_ir == ir) return;
    jtag_scan(true, ir, 4);
    jtag_ir = ir;
}

static uint32_t jtag_debug(uint8_t sel, uint32_t v, unsigned n) {
    jtag_set_ir(IR_DEBUG);
    jtag_scan(false, sel, 8);
    return jtag_scan(false, v, n);
}

static icp_err_t jt_idcode(uint32_t *id) {
    jtag_set_ir(IR_IDCODE);
    *id = jtag_scan(false, 0, 32);
    return ICP_OK;
}

// FUN_08008262. The second word's meaning is unknown (PROTOCOL.md §7). It is
// sent as zero, as HC-PM51 always does: its password dialog fills only the first.
static icp_err_t jt_password(uint32_t pw) {
    jtag_set_ir(IR_PASSWORD_LO);
    jtag_scan(false, pw, 32);
    jtag_set_ir(IR_PASSWORD_HI);
    jtag_scan(false, 0, 32);
    return ICP_OK;
}

// FUN_0800835c
static icp_err_t jt_factory_key(void) {
    jtag_set_ir(IR_KEY_LO);
    jtag_scan(false, ICP_FACTORY_KEY, 32);
    jtag_set_ir(IR_KEY_HI);
    jtag_scan(false, 0, 32);
    return ICP_OK;
}

static icp_err_t jt_ctrl(uint32_t v) {
    jtag_debug(SEL_CTRL, v, JTAG_CTRL_BITS);
    return ICP_OK;
}

static icp_err_t jt_status(uint32_t *s) {
    *s = jtag_debug(SEL_STATUS, 0, JTAG_CTRL_BITS);
    return ICP_OK;
}

static icp_err_t jt_insn(uint32_t insn) {
    jtag_debug(SEL_INSN, insn, 24);
    return ICP_OK;
}

static icp_err_t jt_acc(uint8_t *a) {
    *a = (uint8_t)jtag_debug(SEL_ACC, 0, 8);
    return ICP_OK;
}

static icp_err_t jt_mode(uint8_t m) {
    jtag_debug(SEL_MODE, m, 8);
    return ICP_OK;
}

// FUN_08008e10: mode 4, then one 8-bit scan per byte under IR 0xA.
static icp_err_t jt_read_stream(uint8_t *buf, size_t n) {
    jt_mode(MODE_READ_CODE);
    jtag_set_ir(IR_STREAM);
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)jtag_scan(false, 0, 8);
    return jt_mode(MODE_OFF);
}

#ifdef ICP_WRITE
// FUN_08008f56, less its poll (the caller's).
static icp_err_t jt_flash_write(uint8_t b) {
    jtag_set_ir(IR_STREAM);
    jtag_scan(false, b, 8);
    jtag_set_ir(IR_DEBUG);
    return ICP_OK;
}
#endif

// --- debug port -------------------------------------------------------------

static icp_status_log_t status_log;

static void log_status(uint32_t s) {
    status_log.polls++;
    status_log.last = s;
    for (int i = 0; i < status_log.n_seen; i++) {
        if (status_log.seen[i] == s) return;
    }
    if (status_log.n_seen < (int)(sizeof status_log.seen / sizeof status_log.seen[0])) {
        status_log.seen[status_log.n_seen++] = s;
    }
}

const icp_status_log_t *icp_status_log(void) { return &status_log; }

static icp_err_t wait_ready(uint32_t timeout_ms) {
    status_log = (icp_status_log_t){0};
    uint32_t start = icp_hal_us();
    do {
        uint32_t s;
        icp_err_t e = jt_status(&s);
        if (e) return e;
        log_status(s);
        if ((s & STATUS_READY_MASK) == STATUS_READY) return ICP_OK;
    } while ((uint32_t)(icp_hal_us() - start) < timeout_ms * 1000u);
    return ICP_E_TIMEOUT;
}

icp_err_t icp_exec(uint32_t insn) {
    icp_err_t e = jt_insn(insn);
    if (!e) e = jt_ctrl(CTRL_EXEC);
    if (!e) e = wait_ready(READY_MS_EXEC);
    return e;
}

static icp_err_t mov_direct(uint8_t sfr, uint8_t v) {
    return icp_exec((uint32_t)OP_MOV_DIRECT_IMM << 16 | (uint32_t)sfr << 8 | v);
}

static icp_err_t read_direct(uint8_t sfr, uint8_t *v) {
    *v = 0;
    icp_err_t e = icp_exec((uint32_t)OP_MOV_A_DIRECT << 16 | (uint32_t)sfr << 8);
    return e ? e : jt_acc(v);
}

typedef struct { uint8_t a, dpl, dph; } saved_t;

// FUN_08008cf2. ACC reads directly.
static icp_err_t save(saved_t *s) {
    icp_err_t e = jt_acc(&s->a);
    if (!e) e = read_direct(SFR_DPL, &s->dpl);
    if (!e) e = read_direct(SFR_DPH, &s->dph);
    return e;
}

// FUN_08008cce
static icp_err_t restore(const saved_t *s) {
    icp_err_t e = icp_exec((uint32_t)OP_MOV_DPTR_IMM << 16 | (uint32_t)s->dph << 8 | s->dpl);
    if (!e) e = icp_exec((uint32_t)OP_MOV_A_IMM << 16 | (uint32_t)s->a << 8);
    return e;
}

// FUN_08008e10
static icp_err_t read_chunk(icp_area_t area, uint16_t addr, uint8_t *buf, size_t n) {
    saved_t s;
    icp_err_t e = save(&s);
    if (e) return e;  // nothing changed yet, and nothing valid to restore
    e = mov_direct(SFR_FLASH_IDX, area_idx[area]);
    if (!e) e = mov_direct(SFR_FLASH_DAT, area_val[area]);
    if (!e) e = icp_exec((uint32_t)OP_MOV_DPTR_IMM << 16 | addr);
    if (!e) e = jt_read_stream(buf, n);
    // Close the window even after a failed stream, then put A and DPTR back.
    icp_err_t c = mov_direct(SFR_FLASH_IDX, area_idx[area]);
    if (!c) c = mov_direct(SFR_FLASH_DAT, 0x00);
    if (!c) c = restore(&s);
    return e ? e : c;
}

icp_err_t icp_read_mem(icp_area_t area, uint16_t addr, uint8_t *buf, size_t n) {
    while (n) {
        size_t chunk = n < READ_CHUNK_MAX ? n : READ_CHUNK_MAX;
        icp_err_t e = read_chunk(area, addr, buf, chunk);
        if (e) return e;
        addr = (uint16_t)(addr + chunk);
        buf += chunk;
        n -= chunk;
    }
    return ICP_OK;
}

static bool is_ours(uint32_t id) {
    return id == ICP_IDCODE_LE || id == ICP_IDCODE_BE;
}

static void log_id(icp_entry_log_t *log, uint32_t id) {
    if (!log || id == 0 || id == 0xFFFFFFFFu) return;
    for (int i = 0; i < log->n_ids; i++) {
        if (log->ids_seen[i] == id) return;
    }
    if (log->n_ids < (int)(sizeof log->ids_seen / sizeof log->ids_seen[0])) {
        log->ids_seen[log->n_ids++] = id;
    }
}

// FUN_08008430: reset the target, then hammer TAP reset + IDCODE until the
// same good ID comes back twice running.
icp_err_t icp_enter(icp_entry_t how, uint32_t window_ms, uint32_t *idcode,
                    icp_entry_log_t *log) {
    if (log) *log = (icp_entry_log_t){0};

    if (how == ICP_ENTRY_POWER) {
        icp_release();
        icp_hal_sleep_ms(POWER_DOWN_MS);
        icp_hal_power(true);
        icp_hal_rst(false);  // power-off parked it; POR entry needs it free
    } else {
        // The target is already running and driving the programming pins as
        // matrix lines: hold it in reset (pins back to inputs) before driving
        // them ourselves.
        icp_hal_rst(true);
        icp_hal_sleep_ms(1);
        icp_hal_power(true);
        icp_hal_sleep_ms(10);
        icp_hal_rst(false);
    }
    icp_hal_tck(false);
    jtag_ir = IR_UNKNOWN;

    uint32_t start = icp_hal_us();
    uint32_t last = 0;
    int same = 0;
    bool saw_ours = false, saw_other = false;
    while ((uint32_t)(icp_hal_us() - start) < window_ms * 1000u) {
        uint32_t id = 0;
        jtag_reset();
        icp_err_t e = jt_idcode(&id);
        if (log) log->attempts++;
        if (e) {
            same = 0;
            continue;
        }
        if (log && id == 0) log->all_zero++;
        if (log && id == 0xFFFFFFFFu) log->all_ones++;
        log_id(log, id);
        if (is_ours(id)) {
            saw_ours = true;
            same = (id == last) ? same + 1 : 1;
        } else {
            saw_other |= id != 0 && id != 0xFFFFFFFFu;
            same = 0;
        }
        last = id;
        if (same >= 2) {
            *idcode = id;
            return ICP_OK;
        }
    }
    // Our ID seen but never twice running is a flaky link, not a wrong chip.
    return (saw_other && !saw_ours) ? ICP_E_ID : ICP_E_NO_TARGET;
}

icp_err_t icp_unlock_and_halt(icp_unlock_t how, uint32_t password) {
    icp_err_t e = how == ICP_UNLOCK_FACTORY_KEY ? jt_factory_key() : jt_password(password);
    if (!e) e = jt_ctrl(CTRL_DEBUG_REQ);
    if (!e) e = jt_ctrl(CTRL_HALT);
    if (!e) e = wait_ready(READY_MS_EXEC);
    return e;
}

#ifdef ICP_WRITE

// Indirect flash pairs, from the vendor's own call sites in HC-PM51.exe
// (FlashErase / MemoryWriteCode) and the .dev IND_SFR tables.
#define IND_FLASH_CMD      0x0D
#define CMD_SECTOR_ERASE   0xD2
#define IND_NONE           0x00  // MemoryWriteCode(..., 0x00, 0x00, ...): plain program

static bool in_user_rom(uint16_t addr, size_t n) {
    return n <= ICP_USER_ROM_SIZE && addr <= ICP_USER_ROM_SIZE - n;
}

// One mode-2 data write, then poll: FUN_08008f56.
static icp_err_t flash_byte(uint8_t v, uint32_t timeout_ms) {
    icp_err_t e = jt_flash_write(v);
    return e ? e : wait_ready(timeout_ms);
}

// FUN_080090c8: DPTR first, then the command pair, then a dummy
// write in mode 2 performs the erase. No A/DPTR save here, as in the vendor's.
icp_err_t icp_erase_sector(uint16_t addr) {
    addr &= (uint16_t)~(ICP_SECTOR_SIZE - 1);
    if (!in_user_rom(addr, ICP_SECTOR_SIZE)) return ICP_E_RANGE;

    icp_err_t e = icp_exec((uint32_t)OP_MOV_DPTR_IMM << 16 | addr);
    if (!e) e = mov_direct(SFR_FLASH_IDX, IND_FLASH_CMD);
    if (!e) e = mov_direct(SFR_FLASH_DAT, CMD_SECTOR_ERASE);
    if (!e) e = jt_mode(MODE_FLASH_WRITE);
    if (!e) e = flash_byte(0x00, READY_MS_ERASE);
    // A flash op that never finished: touch nothing more. The caller powers
    // down, which is the only cleanup a wedged controller is sure to take.
    if (e == ICP_E_TIMEOUT) return e;
    // Otherwise drop the erase command whatever happened, so nothing later erases.
    icp_err_t c = jt_mode(MODE_OFF);
    if (!c) c = mov_direct(SFR_FLASH_IDX, IND_FLASH_CMD);
    if (!c) c = mov_direct(SFR_FLASH_DAT, 0x00);
    return e ? e : c;
}

// FUN_08008fac
icp_err_t icp_program(uint16_t addr, const uint8_t *buf, size_t n) {
    if (!in_user_rom(addr, n)) return ICP_E_RANGE;
    if (!n) return ICP_OK;

    saved_t s;
    icp_err_t e = save(&s);
    if (e) return e;
    e = mov_direct(SFR_FLASH_IDX, IND_NONE);
    if (!e) e = mov_direct(SFR_FLASH_DAT, IND_NONE);
    if (!e) e = icp_exec((uint32_t)OP_MOV_DPTR_IMM << 16 | addr);
    if (!e) e = jt_mode(MODE_FLASH_WRITE);
    for (size_t i = 0; !e && i < n; i++) e = flash_byte(buf[i], READY_MS_PROGRAM);
    if (e == ICP_E_TIMEOUT) return e;  // as in icp_erase_sector
    icp_err_t c = jt_mode(MODE_OFF);
    if (!c) c = mov_direct(SFR_FLASH_IDX, IND_NONE);
    if (!c) c = mov_direct(SFR_FLASH_DAT, 0x00);
    if (!c) c = restore(&s);
    return e ? e : c;
}

#endif  // ICP_WRITE

void icp_release(void) {
    icp_hal_power(false);
    jtag_ir = IR_UNKNOWN;
}

void icp_run(void) {
    icp_release();
    icp_hal_rst(false);
}

const char *icp_strerror(icp_err_t e) {
    switch (e) {
    case ICP_OK:          return "ok";
    case ICP_E_TIMEOUT:   return "status never became ready";
    case ICP_E_NO_TARGET: return "nothing answered in the entry window";
    case ICP_E_ID:        return "an IDCODE answered, but not the HC89S003A's";
    case ICP_E_RANGE:     return "outside user code ROM 0x0000-0x2FFF";
    }
    return "?";
}
