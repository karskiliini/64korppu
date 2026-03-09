# 64korppu SimAVR Simulation
#
# Requires: simavr (brew install simavr)
#
# Usage:
#   make                    Build simulator
#   make run                Build + run (1 second boot simulation)
#   make run-verbose        Build + run with SPI trace
#   make run-long           Build + run 5 seconds
#   make clean              Remove build artifacts

CC = cc
CFLAGS = -Wall -Wextra -O2 -g
CFLAGS += -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lsimavr -lelf -lm

TARGET = sim_64korppu
FIRMWARE = ../../firmware/E-IEC-Nano-SRAM/64korppu_nano_debug.elf

.PHONY: all run run-verbose run-long firmware clean

all: $(TARGET)

$(TARGET): sim_64korppu.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

firmware:
	cd ../../firmware/E-IEC-Nano-SRAM && \
	avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL -Os -g -Wall -std=c11 \
		-ffunction-sections -fdata-sections -Iinclude -Wl,--gc-sections \
		-o 64korppu_nano_debug.elf \
		src/main.c src/sram.c src/shiftreg.c src/floppy_ctrl.c \
		src/mfm_codec.c src/fat12.c src/iec_protocol.c src/cbm_dos.c \
		src/fastload.c src/fastload_jiffydos.c src/fastload_burst.c \
		src/fastload_epyx.c

run: $(TARGET) firmware
	./$(TARGET) $(FIRMWARE) -t 1000

run-verbose: $(TARGET) firmware
	./$(TARGET) $(FIRMWARE) -t 1000 -v

run-long: $(TARGET) firmware
	./$(TARGET) $(FIRMWARE) -t 5000

clean:
	rm -f $(TARGET)
