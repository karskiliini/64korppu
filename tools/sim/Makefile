# 64korppu SimAVR end-to-end floppy read test
#
# Requires: simavr (brew install osx-cross/avr/simavr)
#           avr-gcc (brew install osx-cross/avr/avr-gcc@14 avr-binutils)
#
# Usage:
#   make                    Build simulator
#   make firmware           Build floppy_reader.elf (AVR)
#   make test               Run the full scenario matrix (PASS/FAIL)
#   make run                Single nominal run
#   make clean

CC = cc
CFLAGS = -Wall -Wextra -O2 -g
CFLAGS += -I/opt/homebrew/include -I../../test
LDFLAGS = -L/opt/homebrew/lib -lsimavr -lelf -lm

# AVR toolchain (override like the firmware Makefile)
AVR_GCC_DIR ?= /opt/homebrew/opt/avr-gcc@14
OBJCOPY ?= /opt/homebrew/opt/avr-binutils/bin/avr-objcopy
SIZE ?= /opt/homebrew/opt/avr-binutils/bin/avr-size

TARGET = sim_64korppu
FIRMWARE = ../../firmware/E-IEC-Nano-SRAM/floppy_reader.elf

.PHONY: all run test firmware clean

all: $(TARGET)

$(TARGET): sim_64korppu.c ../../test/mfm_track_gen.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

firmware:
	$(MAKE) -C ../../firmware/E-IEC-Nano-SRAM reader \
		AVR_GCC_DIR=$(AVR_GCC_DIR) OBJCOPY=$(OBJCOPY) SIZE=$(SIZE)

run: $(TARGET) firmware
	./$(TARGET) $(FIRMWARE) -t 15000 -reads 3

# Scenario matrix: nominal, 23LC256, slow/fast motor, jitter
test: $(TARGET) firmware
	@echo "=== [1/5] nominal (23LC512) ==="
	./$(TARGET) $(FIRMWARE) -t 15000 -reads 3
	@echo ""
	@echo "=== [2/5] 23LC256 (32KB, address aliasing) ==="
	./$(TARGET) $(FIRMWARE) -t 60000 -reads 2 -allow-err 3 -sram256
	@echo ""
	@echo "=== [3/5] motor +1.5% ==="
	./$(TARGET) $(FIRMWARE) -t 30000 -reads 2 -allow-err 1 -speed-ppm 15000
	@echo ""
	@echo "=== [4/5] motor -1.5% ==="
	./$(TARGET) $(FIRMWARE) -t 30000 -reads 2 -allow-err 1 -speed-ppm -15000
	@echo ""
	@echo "=== [5/5] jitter ±2 ticks (±125ns) ==="
	./$(TARGET) $(FIRMWARE) -t 30000 -reads 2 -allow-err 1 -jitter 2
	@echo ""
	@echo "=== SimAVR scenario matrix: ALL PASS ==="

clean:
	rm -f $(TARGET)
