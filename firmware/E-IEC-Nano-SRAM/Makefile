# 64korppu Alternative E: Arduino Nano + 23LC256 SPI SRAM
#
# Requires: avr-gcc, avr-objcopy, avrdude
# Install on macOS: brew install avr-gcc avrdude
#
# Usage:
#   make           Build firmware (.hex)
#   make flash     Flash to Arduino Nano via USB
#   make size      Show code/RAM usage
#   make clean     Remove build artifacts

MCU = atmega328p
F_CPU = 16000000UL
BAUD = 115200

# avrdude settings for Arduino Nano (old bootloader)
PROGRAMMER = arduino
PORT ?= /dev/tty.usbserial-*

# Arduino IDE toolchain (prebuilt)
AVR_ROOT ?= $(HOME)/Library/Arduino15/packages/arduino/tools
AVR_GCC_DIR = $(AVR_ROOT)/avr-gcc/7.3.0-atmel3.6.1-arduino7
AVRDUDE_DIR = $(AVR_ROOT)/avrdude/6.3.0-arduino17

CC = $(AVR_GCC_DIR)/bin/avr-gcc
OBJCOPY = $(AVR_GCC_DIR)/bin/avr-objcopy
SIZE = $(AVR_GCC_DIR)/bin/avr-size
AVRDUDE = $(AVRDUDE_DIR)/bin/avrdude

CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -Wextra -std=c11
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -Iinclude
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections

TARGET = 64korppu_nano
SRCDIR = src

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/sram.c \
       $(SRCDIR)/shiftreg.c \
       $(SRCDIR)/floppy_ctrl.c \
       $(SRCDIR)/mfm_codec.c \
       $(SRCDIR)/fat12.c \
       $(SRCDIR)/iec_protocol.c \
       $(SRCDIR)/cbm_dos.c \
       $(SRCDIR)/fastload.c \
       $(SRCDIR)/fastload_jiffydos.c \
       $(SRCDIR)/fastload_burst.c \
       $(SRCDIR)/fastload_epyx.c \
       $(SRCDIR)/lz4_compress.c \
       $(SRCDIR)/compress_proto.c \
       $(SRCDIR)/led_debug.c

OBJS = $(SRCS:.c=.o)

.PHONY: all flash size clean

all: $(TARGET).hex size

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

flash: $(TARGET).hex
	$(AVRDUDE) -C $(AVRDUDE_DIR)/etc/avrdude.conf -c $(PROGRAMMER) -p $(MCU) -P $(PORT) -b $(BAUD) -U flash:w:$<:i

size: $(TARGET).elf
	@echo ""
	@echo "=== Memory Usage ==="
	$(SIZE) --mcu=$(MCU) --format=avr $<
	@echo ""

# Diagnostic: continuous floppy read for oscilloscope debugging
READER_TARGET = floppy_reader
READER_SRCS = $(SRCDIR)/floppy_reader.c \
              $(SRCDIR)/sram.c \
              $(SRCDIR)/shiftreg.c \
              $(SRCDIR)/floppy_ctrl.c \
              $(SRCDIR)/mfm_codec.c
READER_OBJS = $(READER_SRCS:.c=.o)

reader: $(READER_TARGET).hex reader-size

$(READER_TARGET).elf: $(READER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(READER_TARGET).hex: $(READER_TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

reader-size: $(READER_TARGET).elf
	@echo ""
	@echo "=== Reader Memory Usage ==="
	$(SIZE) --mcu=$(MCU) --format=avr $<
	@echo ""

reader-flash: $(READER_TARGET).hex
	$(AVRDUDE) -C $(AVRDUDE_DIR)/etc/avrdude.conf -c $(PROGRAMMER) -p $(MCU) -P $(PORT) -b $(BAUD) -U flash:w:$<:i

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).hex
	rm -f $(READER_OBJS) $(READER_TARGET).elf $(READER_TARGET).hex
