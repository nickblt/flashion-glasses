# Reflashing the SL-GlassesLed26x7 LED glasses in place

Cheap "LED shutter" party glasses with a 26×7 matrix are often the `SL-GlassesLed26x7` board,
driven by a Holychip **HC89S003A**, a small 8051 microcontroller. This repo puts your own
firmware on that chip, **without desoldering anything**, using a Raspberry Pi Pico as the
programmer.

- **`icp/`** — Pico firmware that speaks Holychip's undocumented in-circuit programming (ICP)
  protocol over the glasses' 5-hole JTAG header. It can read, back up, erase, program and verify.
- **`tools/icp_console.py`** — drives the probe from the shell. Python 3, stdlib only.
- **`firmware/`** — example firmware for the glasses: animated eyes that blink, change mood at
  the press of a button, and power down on a long press. Built with SDCC.
- **[`PROTOCOL.md`](PROTOCOL.md)** — the ICP protocol, decoded from Holychip's own programmer
  firmware, with what has been confirmed on silicon.

```
|..#####............#####..|      |..........................|
|.#######..........#######.|      |..........................|
|#########........#########|      |..........................|
|#########........#########|  ->  |#########........#########|
|#########..    ..#########|      |...........    ...........|
|.#######..      ..#######.|      |..........      ..........|
|..#####..        ..#####..|      |.........        .........|
```

The 26×7 matrix, open eyes and mid-blink. Spaces are the nose bridge, which has no LEDs.

## ⚠ Read this before connecting anything

- **The glasses have no current-limiting resistors.** Feed them **3.3 V only**, and never from two
  supplies at once.
- **Disconnect the battery first.** Lift the LiPo's + lead at the `V` pad. A charged cell is
  4.2 V, which is above what the Pico's pins tolerate and would fight its supply.
- **Back up the stock firmware before your first flash** ([End to end](#end-to-end-back-up-build-flash),
  step 4). It is not distributed here, so your own dump is the only way back to it.

The full rules the flasher is built around are in [PROTOCOL.md §8](PROTOCOL.md#8-safety-rules).

## What you need

**Hardware**

- The glasses. Check the board is marked `SL-GlassesLed26x7` and the 20-pin chip `HC89S003A`.
- A Raspberry Pi Pico (RP2040).
- Six wires, and some way to contact the 5-hole header. Its pitch is **1.6 mm**, which no
  standard header or programming clip fits. Soldering thin leads straight into the holes works.
- A multimeter with continuity, to find the header's ground hole.
- A soldering iron, for the leads and to lift the battery's + lead.

**Software** (tested versions)

| Tool                                                      | For                                 | Tested |
| --------------------------------------------------------- | ----------------------------------- | ------ |
| [pico-sdk](https://github.com/raspberrypi/pico-sdk)       | `icp/`                              | 2.1.1  |
| `arm-none-eabi-gcc`                                       | `icp/`                              | 16.2.0 |
| CMake ≥ 3.13, plus Ninja or Make                          | `icp/`                              | 4.4.4  |
| [SDCC](https://sdcc.sourceforge.net/) (`sdcc`, `packihx`) | `firmware/`                         | 4.6.0  |
| Python 3, stdlib only                                     | `tools/icp_console.py`              | 3.14   |
| `picotool` (optional)                                     | reloading the probe without BOOTSEL |        |

`icp_console.py` uses `termios`, so it runs on Linux and macOS. On Windows, or anywhere else, any
serial terminal can drive the probe by hand (see [Console commands](#console-commands)).

## Wiring

Hold the board with the chip's pin 1 at bottom left (C1 to the right of the chip). Count the
header holes 1–5 from left to right, but **identify hole 1 by continuity, not by position.** It
is the one that beeps to ground (chip pin 7, or the `G` pad).

| Pico           |         | Glasses | Chip pin |
| -------------- | ------- | ------- | -------- |
| GND (phys 38)  | —       | hole 1  | 7, GND   |
| GP20 (phys 26) | TMS →   | hole 2  | 13, P2.6 |
| GP16 (phys 21) | TCK →   | hole 3  | 14, P2.5 |
| GP17 (phys 22) | ← TDO   | hole 4  | 15, P2.4 |
| GP21 (phys 27) | TDI →   | hole 5  | 16, P2.3 |
| GP19 (phys 25) | power → | `V` pad | 9, VDD   |

- **GP19 powers the glasses.** The probe power-cycles the chip and catches its debug port in the
  few milliseconds after power-on, before the stock firmware takes the pins over.
- **Leave the `R` pads unwired.** On these glasses that pin is configured as a GPIO, so reset
  does nothing (PROTOCOL.md §4).
- Boards whose reset pin still works can use reset entry instead (GP18 → RST, VDD on 3V3(OUT));
  see [`icp/icp_pins.h`](icp/icp_pins.h).

## End to end: back up, build, flash

The whole run, from stock glasses to the example firmware, from the repo root. Every probe
command ends in a line starting `ok` or `error`. **Stop at the first error.** The message says
what failed. [Build](#build) and [Flashing](#flashing) below explain each step in more detail.

**1. Build the flasher and load it** (once). Hold BOOTSEL while plugging the Pico in, then copy
the `.uf2` onto the `RPI-RP2` drive that appears.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S icp -B icp/build -DICP_WRITE=ON
cmake --build icp/build                       # -> icp/build/hc_icp_writer.uf2
```

**2. Compile the new firmware.** This needs SDCC (`sdcc` and `packihx`, see
[What you need](#what-you-need)). To make the animations your own, edit `firmware/eyes.c` first
(see [The example firmware](#the-example-firmware)).

```sh
make -C firmware check                        # clean build -> firmware/build/glasses.hex
```

A good build prints the `sdcc` commands with no errors or warnings, then ends with the memory
summary:

```
packihx: read 219 lines, wrote 408: OK.
Stack starts at: 0x61 (sp set to 0x60) with 159 bytes available.
   EXTERNAL RAM     0x0001   0x0176     374      768
   ROM/EPROM/FLASH  0x0000   0x186d    6254    12288
```

The last two columns are bytes used and the maximum: ROM must fit in 12288 and external RAM in
768, and the linker stops with an error if either overflows. "Bytes available" is what the
256-byte internal RAM has left for the stack. If an edit shrinks that sharply, move new variables
to `__xdata` (see **Memory** under [The example firmware](#the-example-firmware)). After the
first build, plain `make -C firmware` rebuilds only what changed.

**3. Wire up and make first contact.** Lift the battery's + lead first, then wire as in
[Wiring](#wiring). Nothing is written to the chip in this step or the next.

```sh
tools/icp_console.py unlock factory
tools/icp_console.py chipid                   # IDCODE 736A6808, then CHIP_ID, then ok
```

**4. Back up the stock firmware.**

```sh
tools/icp_console.py dump > stock-dump.txt
tail -2 stock-dump.txt                        # "N of 16384 bytes are zero", then "ok"
grep '^:' stock-dump.txt > stock-full.hex     # all 16 KB, as Intel HEX
tools/icp_console.py option > stock-option.txt
tools/icp_console.py verify stock-full.hex    # must say "matches the image", then ok
```

The last line checks the backup before anything is erased. `verify` loads the file through the
same path `flash` uses and compares it with the chip, so a pass means `stock-full.hex` is both
correct and flashable. If the dump says all 16384 bytes are zero, the chip is read-protected, and
you have no backup. Do not continue unless you are prepared to lose the stock firmware.

Keep the `stock-*` files somewhere safe. The repo's `.gitignore` keeps them out of commits.

**5. Flash and check.**

```sh
tools/icp_console.py flash firmware/build/glasses.hex
tools/icp_console.py verify firmware/build/glasses.hex
```

`flash` erases user ROM, checks that it is blank, programs the image, and verifies every byte.
The second `verify` is an independent re-read after a fresh power cycle.

**6. Run it.** Unplug the probe from the glasses and reconnect the battery, or power them from
the Pico on the bench (see [Flashing](#flashing)). A short press of the button steps through the
animations (see [The example firmware](#the-example-firmware)).

**To go back to stock later:** wire up again, then

```sh
tools/icp_console.py unlock factory && tools/icp_console.py chipid
tools/icp_console.py flash stock-full.hex
```

## Build

**The flasher**

```sh
export PICO_SDK_PATH=/path/to/pico-sdk        # or: export PICO_SDK_FETCH_FROM_GIT=on
cmake -S icp -B icp/build -DICP_WRITE=ON
cmake --build icp/build                       # -> icp/build/hc_icp_writer.uf2
```

Without `-DICP_WRITE=ON` you get `hc_icp_probe.uf2`, a **read-only** build. It has every command
except `flash`, and contains no erase or program code at all. The two images have different names
so they cannot be mixed up. Start with either; you need the writer to flash.

To load it, hold BOOTSEL while plugging the Pico in, then copy the `.uf2` onto the `RPI-RP2`
drive. Once a probe image is running, `picotool load -f -x <file>.uf2` swaps images without
BOOTSEL.

**The firmware**

```sh
make -C firmware          # -> firmware/build/glasses.hex
make -C firmware check    # clean build, then ROM/RAM use
```

## Flashing

The steps are in [End to end](#end-to-end-back-up-build-flash). This section is what they do.

The probe is a USB serial device (`/dev/ttyACM0` on Linux; pass `--port` for another).
`tools/icp_console.py` sends one command, prints the reply, and exits 0 for `ok` or 1 for an
error. Settings such as `unlock` stay on the Pico until it is unplugged or reloaded.

**First contact.** The glasses ship password-locked, so `unlock factory` comes first
(PROTOCOL.md §3). Then `chipid` prints something like:

```
attempts 13 in 207 ms; IDCODEs seen: 736A6800 736A6808; all-zero reads 0, all-one reads 9
IDCODE 736A6808 (little-endian byte order)
unlocking with the factory key
halted
CHIP_ID .. .. .. .. .. .. .. ..
ok
```

The CHIP_ID is unique to your chip. `chipid` has to pass after every power-up of the probe before
it will `flash`.

**`flash`** erases user ROM (`0x0000`–`0x2FFF`), reads it back to check it really is blank,
programs the image, and verifies every byte. That takes a few seconds for the example firmware,
and roughly twice as long for a full 12 KB image. It never touches the password, the OPTION area
or `0x3000`–`0x3FFF`, **so the chip stays locked:** run `unlock factory` again after every
power-up of the probe. An image may carry data above `0x2FFF` only if it is blank (`00`), which
is what lets a full `dump` be flashed back as it is.

**`verify FILE`** compares the chip with a file, read-only, in either build.

**Restoring the stock firmware** is flashing the backup, `stock-full.hex`. If `flash` refuses it
with `data at 3000 is past user ROM (3000) and not blank`, your chip had something above user
ROM. `flash` never erases that range, so it is still on the chip, and only the records below
`0x3000` need restoring:

```sh
{ grep -E '^:10[0-2]' stock-full.hex; echo ':00000001FF'; } > stock-restore.hex
tools/icp_console.py flash stock-restore.hex
```

**Bench power.** To run the glasses without the battery, connect their `V` pad to the Pico's
3V3(OUT) (phys 36) and hole 1 to a Pico GND, with GP19 disconnected and the battery still off.

## Console commands

These are the probe's own commands, so they also work interactively (`picocom /dev/ttyACM0`, then
type `help`). Every reply ends with a line starting `ok` or `error`.

| Command                    |                                                                                       |
| -------------------------- | ------------------------------------------------------------------------------------- |
| `id`                       | power-cycle, catch the debug port, report the IDCODE                                  |
| `chipid`                   | + unlock, halt, read CHIP_ID twice. Required before `flash`.                          |
| `option`                   | + dump both OPTION pages                                                              |
| `dump`                     | + dump code `0x0000`–`0x3FFF` as Intel HEX                                            |
| `verify FILE`              | compare user ROM with an image                                                        |
| `flash FILE`               | erase user ROM, program, verify (`ICP_WRITE` build only)                              |
| `off`                      | park the target                                                                       |
| `unlock password\|factory` | how to unlock: the `pwd` value, or Holychip's factory key                             |
| `pwd <hex>`                | password (`FLASH_SC`) for `unlock password`, default 0                                |
| `khz <n>`                  | clock rate, 10–1000 kHz, default 100                                                  |
| `entry power\|rst`         | how to reset the target. Default `power`, the only one that works on the glasses      |
| `window <ms>`              | how long to keep trying after reset (default 100)                                     |
| `pins`                     | `entry rst` wiring only: each line's level against the Pico's pull-down, then pull-up |
| `run`                      | `entry rst` wiring only: release reset and leave the target running                   |
| `status`                   | build type and current settings                                                       |

`verify` and `flash` take a file only through `icp_console.py`, which uploads it first. On the
console itself, send `new`, paste the Intel HEX records, then send `verify` or `flash` on their own.

## The example firmware

- **Short press:** the eyes blink closed and reopen as the next animation: blink, happy,
  hearts, sleepy, angry, and round again.
- **Hold 1 s:** the eyes close and the glasses power down, using the stock firmware's own sleep
  pin state. **Hold 1 s again** to wake, in the animation you left.
- Disconnecting the battery resets it to blink.

**Changing the animation.** Every eye shape is ASCII art at the top of
[`firmware/eyes.c`](firmware/eyes.c), nine columns by seven rows, for one eye. Only `#` matters.
Each mode's rhythm is a short table of beats (pose, milliseconds) under the art, and blink timing
is the `#define` block above those. The hold time is `BTN_LONG_MS` in
[`firmware/button.h`](firmware/button.h).

| File                                 |                                                                      |
| ------------------------------------ | -------------------------------------------------------------------- |
| `main.c`                             | main loop: button, animation, power-down and wake                    |
| `eyes.c`, `eyes.h`                   | the animations. Portable C, drawn through `glasses_matrix.h`         |
| `glasses_matrix.h`, `glasses_pins.h` | drawing API, and the matrix's charlieplex geometry                   |
| `matrix_hc89.c`                      | the HC89 backend: a Timer 3 interrupt scans 13 sections at ~190 Hz   |
| `pinmap.h`                           | board pin → chip port pin                                            |
| `button.c`                           | debounce, short and long press                                       |
| `include/HC89S003F4.h`               | a hand-written SDCC register header for the chip, from the datasheet |

**Memory.** The image is about 6.1 KB of the 12 KB ROM. The real limit is the 8051's 128 bytes of
direct RAM: SDCC's small model gives every non-leaf function's locals fixed addresses there. Put
new state in `__xdata`, as `eyes.c` does.

**Known gaps:** there is no watchdog, the blink randomness repeats from each power-on (fixed
seed), and neither sleep current nor all-on current has been measured on battery power.

## How it works

Holychip's programmers reach the chip through an on-chip debug port, over JTAG.
The protocol is undocumented. It was recovered from the firmware of Holychip's own HC-LINK V4
programmer, which ships as a plain Intel HEX file inside their HC-PM51 software package. After
unlocking and halting the core, every operation is a single 8051 instruction fed to the halted
CPU, and flash is reached through a small streaming engine. [PROTOCOL.md](PROTOCOL.md) covers it
down to the bit.

## Credit

- **[DrSapling/LED-Glasses-Matrix-26x7-SL-GlassesLed26x7](https://github.com/DrSapling/LED-Glasses-Matrix-26x7-SL-GlassesLed26x7)**
  (GPL-3.0) reverse-engineered the matrix, the board's pin numbering, and the chip's pad
  connections that `pinmap.h` is built from. None of this would be approachable without it.
- **[yiyidijiyi/smartpad_hc89s003](https://github.com/yiyidijiyi/smartpad_hc89s003)**, a working
  HC89S003 peripheral library, confirmed the GPIO model and the IAP sequence.
- Holychip's HC89S003A datasheet is the source for the register header. Vendor documents and
  software are not redistributed here.

## License

GPL-3.0-or-later. The full text is in [LICENSE](LICENSE).
