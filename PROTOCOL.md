# HC89S003A in-circuit programming (ICP) protocol

This is Holychip's ICP protocol for the HC89S003A over JTAG, as implemented by [`icp/`](icp/).
Holychip does not document it. It was decoded with Ghidra from the HC-LINK V4 programmer's firmware
(`LINK_4_0.bin`), which ships as a plaintext STM32F103 image in Holychip's HC-PM51 V6.0.8.0
package, linked at `0x08000C00`. It was cross-checked against the host side (`LinkDriver.dll`,
`HC-PM51.exe`) and against the vendor's device database (`HC89S003AF4.dev`). `FUN_xxxxxxxx`
names in the source are Ghidra's names for functions in that image.

Much of it has since been confirmed on a real chip. §7 says exactly which parts.

---

## 1. Layering

```
vendor:  PC ──USB CDC──▶ HC-LINK V4 ──┐
                                      ├──JTAG──▶ debug port ──▶ halted 8051 core
here:    PC ──USB CDC──▶ Pico (icp/) ─┘
```

Behind the JTAG TAP (§5), the debug port has an IDCODE, a password, a control/status pair, an
instruction register, a data stream and a memory-access mode. Nothing is poked directly. Every
operation is an 8051 instruction executed on the halted core (§6).

## 2. Memory map

The HC89S003A is an enhanced 1T 8051: 16 KB flash, 256 B IRAM + 768 B XRAM, 2.0–5.5 V, 16 MHz.

| Range             |                                                                     |
| ----------------- | ------------------------------------------------------------------- |
| `0x0000`–`0x2FFF` | **user code ROM**, 12 KB. The only range `icp/` erases or programs. |
| `0x3000`–`0x3FFF` | rest of the 16 KB array. Readable, never written by `icp/`.         |

- Flash geometry: **128 B sector → 1 KB page (8 sectors) → 4 KB protection unit (4 pages).**
- **Erased flash reads `0x00`**, not `0xFF` (`.dev` `FLASH_BLANKValueInCodeArea`). On an 8051
  that makes a blank reset vector a run of `NOP`s.

**OPTION area.** This is a separate space outside the code array (datasheet §3.1.4):

| Offset              | Contents                                                                                |
| ------------------- | --------------------------------------------------------------------------------------- |
| `0x0000`–`0x0007`   | `SN_DATA0..7`, user data                                                                |
| `0x0008`–`0x000F`   | `ID_DATA0..7`, user data                                                                |
| `0x0010`–`0x0011`   | where HC-PM51 stores the CRC-16/XMODEM of the 16 KB image it programmed (observed)      |
| `0x0020`–`0x0023`   | `FLASH_SC0..3`, the 32-bit ICP password (§3)                                            |
| `0x0031`            | `ERST_ENB`: 1 makes P2.7 a GPIO instead of RST                                          |
| `0x0038`            | `WAIT_TS`                                                                               |
| `0x0039`            | `BORVS`, brown-out level                                                                |
| `0x003E` / `0x003F` | `RVCFG` / `nRVCFG` (complement). `RVCFG ≠ 00` moves the reset vector away from `0x0000` |
| `0x0100`–`0x0107`   | `CHIP_ID0..7`, factory-unique, **never erasable**                                       |

An `n`-prefixed field holds the complement of its pair. `icp/`'s `flash` reads `RVCFG` first and
refuses unless it is `00`, because with any other value the chip would not reset into the newly
written image.

## 3. Protection and unlock

- **`FLASH_SC`** is a 32-bit password that gates ICP _entry_ (datasheet §3.1.2.1). A blank chip is
  expected to take all zeros. With the wrong password the halt never completes: the first status
  read echoes the last control write (`0x014`), then the chip boots its own firmware and status
  reads turn to garbage (`7FF`, `000`, …). HC-PM51 reports this as _"Connect MCU fail! Check the
  FLASH_SC, RST, MCU name please."_
- **The SL-GlassesLed26x7 ships password-locked.** On the unit this was developed on,
  `FLASH_SC = 0x23048319` (bytes `19 83 04 23`). Whether every unit shares it is unknown.
- **The factory key, `0xF0F4FF79`,** is sent in place of the password: IR `3`, then IR `4` ← 0.
  HC-PM51's _MCU ▸ Factory Reset_ connects with it. **It unlocks a chip
  regardless of `FLASH_SC`,** which is confirmed on silicon. `icp/` offers it as `unlock factory`,
  and uses it as an unlock only. The vendor's operations behind that key go on to erase and rewrite
  the factory OPTION page at `0x0100` (clearing ISP-mode markers at `0x0118`/`0x0127`). `icp/` never
  does that (§8).
- **ICP read protection**, per 4 KB unit: protected units read back as zeros, so an all-zero dump
  means protection, not an empty chip.
- **ICP erase/write protection**, per 4 KB unit: blocks erase and program. It is unknown whether
  a refused erase fails loudly or silently, so `flash` reads user ROM back after erasing and stops
  unless every byte is blank.
- **IAP read protection**, per 4 KB unit: `MOVC` from outside a protected unit reads zeros. This
  affects code running on the chip, not ICP.

The all-block erase (§6) clears `FLASH_SC` along with everything else. `icp/` does not implement it.

## 4. Entry, unlock, halt

`FUN_08008430`:

1. **Reset the target**, either by switching its VDD (power-on entry) or by pulling RST low.
2. **Catch the debug port.** Start a timeout, then loop { TAP reset; read IDCODE } until the
   expected ID comes back **twice in a row**. The port is only reachable in a short window after
   reset, before the chip's own firmware repurposes the pins.
3. **Unlock.** Write the 32-bit password to IR `6`, then a second 32-bit word to IR `7`. Its
   meaning is unknown; HC-PM51 always sends zero, and so does `icp/`. Or send the factory key
   instead (§3).
4. **Halt.** Write control ← `0x34`, then control ← `0x14`, and poll status until it is **ready:
   `(status & 0x0A) == 0x08`**. A status of `0x0000` or `0x07FF` means a floating line, i.e. no
   chip is answering.

**IDCODE.** The `.dev` file lists `08 68 6a 73`. **Silicon sends `0x736A6808`**, the stored bytes
in little-endian order. The first read after power-on is always `0x736A6800` (bit 3 clear) and
the following reads are `0x736A6808`. This is deterministic at 100, 400 and 1000 kHz, and it is why
the vendor wants two matching reads. A run of `00000000` or `FFFFFFFF` means no chip answered.
`icp/` accepts either byte order.

**Entry modes** (datasheet §3.1.6):

| `icp/` setting | Reset by                    | Wires                      | Notes                                                                                                                             |
| -------------- | --------------------------- | -------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `entry power`  | switching VDD               | 6: TMS TCK TDO TDI VDD GND | The only option on the glasses (RST is disabled, see below). The chip answers about 5 ms after power-on.                          |
| `entry rst`    | RST held low, then released | 7: the same, plus RST      | The board stays powered. RST is held low _before_ the lines are driven, because running firmware uses them. Needs `ERST_ENB = 0`. |

On the glasses `ERST_ENB = 1`, so TSSOP 12 (P2.7, the R temple pads) is a GPIO and grounding it
does nothing. Power-on entry is the only way in.

## 5. JTAG framing

Pins: **TMS = P2.6 (TSSOP 13), TCK = P2.5 (14), TDO = P2.4 (15), TDI = P2.3 (16)**, plus VDD,
GND and optionally RST. On the glasses these are holes 2–5 of the 5-hole header, with GND on
hole 1. This is plain IEEE 1149.1 with a **4-bit IR**:

- TAP reset (`FUN_08008072`) is TMS=1 ×5, then TMS=0 into Run-Test/Idle.
- IR scan (`FUN_080080a0`): TMS 1,1,0,0 → Shift-IR.
- DR scan (`FUN_0800815a`): TMS 1,0,0 → Shift-DR.
- Shifts go LSB first, with TMS=1 on the last bit, then Update and back to Idle. TDO is sampled
  before TCK↑.
- There is no ACK and no parity. A missing target reads as TDO's pull-up, all ones; a locked one
  shows as a status that never becomes ready.

| IR       | DR                                                                           |
| -------- | ---------------------------------------------------------------------------- |
| `1`      | IDCODE, 32 bits                                                              |
| `3`, `4` | factory key pair, 32 bits each (§3)                                          |
| `6`, `7` | password pair, 32 bits each (§4)                                             |
| `A`      | data stream: one 8-bit scan per byte, read in mode 4, written in mode 2 (§6) |
| `B`      | debug registers, below                                                       |

**Debug registers.** Each access under IR `B` is two DR scans: an 8-bit select, then a scan of
that register's own width.

| Select | Width | Register                                                                    |
| ------ | ----- | --------------------------------------------------------------------------- |
| `80`   | 11    | control (write): `0x34` then `0x14` to connect (§4), `0x57` to execute (§6) |
| `81`   | 11    | status (read): ready is `(status & 0x0A) == 0x08`                           |
| `83`   | 8     | ACC, read after an instruction                                              |
| `85`   | 16    | PC                                                                          |
| `86`   | 24    | instruction to execute (§6)                                                 |
| `88`   | 8     | memory-access mode (§6)                                                     |
| `8C`+  |       | breakpoints, at addresses from a table in the firmware. Not needed here.    |

## 6. The instruction port, and flash

Every operation is an 8051 instruction executed on the halted core (`FUN_08008a04`):

1. instruction register ← `opcode<<16 | b1<<8 | b2`. Two-byte instructions pad `b2` with `00`.
2. control ← `0x57` (execute and wait; `0x17` executes without waiting).
3. Poll status until ready.

Afterwards ACC can be read through debug select `83`. Around reads and programs, the vendor firmware
saves A, DPL and DPH first and restores them after; its sector erase does not. `icp/` does the same.

| Want                    | Instruction(s)                                      |
| ----------------------- | --------------------------------------------------- |
| write SFR / direct byte | `75 aa vv`, `MOV direct,#data`                      |
| read SFR / direct byte  | `E5 aa 00`, `MOV A,direct`, then read ACC           |
| read / write IRAM       | `78 aa 00` `MOV R0,#`, then `E6 00 00` / `76 vv 00` |
| set DPTR                | `90 hh ll`                                          |
| set A                   | `74 vv 00`                                          |
| jump                    | `02 hh ll`                                          |

**Memory goes through a streaming engine.** The mode register selects it, DPTR is the
auto-incrementing address, and each byte is one 8-bit scan under IR `A`:

| Mode |                                                                                               |
| ---- | --------------------------------------------------------------------------------------------- |
| 0    | off                                                                                           |
| 2    | flash write: each streamed byte is programmed (or triggers a latched erase), then poll status |
| 3    | XDATA write                                                                                   |
| 4    | code / OPTION read                                                                            |
| 5    | XDATA read                                                                                    |

**Flash is steered through an indirect register pair** behind the undocumented SFRs `0x94`
(index) and `0x95` (data), set with `MOV direct,#data`:

| Index ← value | Meaning                                                                        |
| ------------- | ------------------------------------------------------------------------------ |
| `FF` ← `00`   | select the code array for reading (`MemoryReadCode_Speedup`)                   |
| `0E` ← `A5`   | map the OPTION area, in 128-byte sectors                                       |
| `0D` ← `D2`   | latch **sector erase** (128 B)                                                 |
| `0D` ← `00`   | clear the latched erase command                                                |
| `0C` ← `C3`   | latch **all-block erase**, which clears `FLASH_SC` too. Not in `icp/`.         |
| `00` ← `00`   | plain program, nothing latched (`MemoryWriteCode(buf, 0x00, 0x00, addr, len)`) |

These pairs are the `.dev` file's `IND_SFR` tables, read as "this value → that SFR".

- **Read** (`FUN_08008e10`): save A/DPTR; set the index/value pair; `MOV DPTR,#addr`; mode ← 4;
  one scan per byte; mode ← 0; write the pair's value back to `00`; restore A/DPTR.
- **Program** (`FUN_08008fac`): the same with pair `00`/`00` and mode 2, then one streamed byte
  and a status poll per byte. The vendor batches up to 1 KB (one page) per call.
- **Erase sector** (`FUN_080090c8`): `MOV DPTR,#addr`; `0D` ← `D2`; mode ← 2; write one dummy
  `00` (the latched command turns the write into an erase); poll; mode ← 0; `0D` ← `00`. Any
  address inside the sector works. HC-PM51 erases each selected 1 KB page this way, one 128 B
  sector at a time.
- **OPTION erase** is for reference only, and **`icp/` never does it**. HC-PM51 latches `0D` ← `D2`
  directly, then makes a one-byte write with `0E` ← `A5` at `0x00` and again at `0x80`. That erases
  both user OPTION sectors, `FLASH_SC` and `RVCFG` included.

## 7. What silicon has confirmed, and what is still inference

**Confirmed** on an HC89S003A on the glasses, over JTAG with power-on entry:

- JTAG framing, the 4-bit IR, and the debug-register file behind IR `B`.
- Power-on entry, the IDCODE value and byte order, and the first-read quirk (§4).
- The wrong-password failure, and the factory-key unlock (§3).
- Halt, the instruction port, and OPTION reads (CHIP_ID read twice, identical).
- A full 16 KB code read. Its CRC-16/XMODEM matched the CRC HC-PM51 stored in OPTION `0x10` when
  the factory programmed the chip, so the read is bit-exact.
- Sector erase of `0x0000`–`0x2FFF` (about 1.0 s), a 4.9 KB program (about 3.3 s), and a full
  verify. A second verify after a fresh power cycle also matched. DPTR does auto-increment in the
  streaming modes.
- `RVCFG = 00`, `ERST_ENB = 1`, `BORVS = 4` (3.0 V), no ICP erase protection on user ROM.

**Implemented and tested only against a bit-level model** of the debug port, not on silicon:

- `entry rst`. The glasses have RST disabled.
- Unlocking with the correct `FLASH_SC` (`pwd 23048319`) rather than the factory key.

**Still inference:**

- What JTAG's second password word (IR `7`) holds.
- Control values other than `0x34`/`0x14` (connect), `0x57` and `0x17`. `0x54`, `0x50`, `0x56` and
  `0x74` appear in the vendor's halt/run/reset paths.
- The meanings of the `IND_SFR` indices other than those in §6.
- Whether the all-block erase works _without_ the password.
- How long the post-reset window stays open. The chip answers about 5 ms after power-on.
- Whether ICP erase protection refuses an erase loudly or silently.

## 8. Safety rules

`icp/` is built around these rules, and anything that extends it should keep to them.

1. **The glasses board has no current-limiting resistors.** Feed it 3.3 V only, and never from
   two supplies at once. Lift the battery's + lead before connecting the probe: a charged LiPo is
   4.2 V, which is above the RP2040's pin limit and would fight the Pico's supply.
2. **The programming lines are also LED matrix lines.** The LEDs load them and join them to each
   other, so the probe drives them at 12 mA. Start at the default 100 kHz and raise the clock only
   once something works.
3. **Read-only until first contact passes.** The default `icp/` build contains no erase or program
   code at all (`nm` confirms it). The `ICP_WRITE` build's `flash` refuses until `chipid` has
   passed since the probe booted.
4. **Back up first.** `dump` the chip before the first `flash`. The glasses' stock firmware is not
   distributed here, and your own dump is the only way back to it.
5. **No all-block erase and no OPTION writes.** Either one rewrites `FLASH_SC`, `RVCFG` and the
   config bytes together. A mistake there can leave a chip that does not boot or will not unlock.
6. **Never erase or write the factory OPTION page, `0x0100`–`0x017F`.** It holds CHIP_ID, which
   cannot be restored. The factory key is used only as an unlock, never for the vendor's
   page-rewriting operations behind it.
7. **The indirect flash registers only ever get values the vendor's own code uses** (the table
   in §6).
