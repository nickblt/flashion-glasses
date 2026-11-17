# The glasses firmware for the stock HC89S003A, built with SDCC.
#
#   make           build/glasses.hex
#   make check     clean build, then print ROM/RAM use
#
# Flash it with the ICP probe (README.md, "Flashing"):
#   tools/icp_console.py unlock factory && tools/icp_console.py chipid &&
#   tools/icp_console.py flash firmware/build/glasses.hex

CC      = sdcc
CFLAGS  = -mmcs51 --std-c99 --model-small -I. -Iinclude
# 12 KB user ROM (PROTOCOL.md §2), 768 B XRAM, 256 B IRAM.
LDFLAGS = --code-size 0x3000 --xram-size 0x300 --iram-size 0x100

OBJ = build/main.rel build/matrix_hc89.rel build/button.rel build/eyes.rel
DEPS = $(wildcard *.h include/*.h)

.PHONY: all check clean
all: build/glasses.hex

check:
	$(MAKE) clean
	$(MAKE) build/glasses.hex
	@grep -E '^ *(ROM/EPROM/FLASH|EXTERNAL RAM) |^Stack starts' build/glasses.mem || cat build/glasses.mem

build:
	mkdir -p build

build/main.rel: main.c $(DEPS) | build
	$(CC) $(CFLAGS) -c $< -o $@
build/matrix_hc89.rel: matrix_hc89.c $(DEPS) | build
	$(CC) $(CFLAGS) -c $< -o $@
build/button.rel: button.c $(DEPS) | build
	$(CC) $(CFLAGS) -c $< -o $@
build/eyes.rel: eyes.c $(DEPS) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/glasses.ihx: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) -o $@

build/glasses.hex: build/glasses.ihx
	packihx $< > $@

clean:
	rm -rf build
