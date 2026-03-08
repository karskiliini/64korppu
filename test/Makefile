CC = gcc
CFLAGS = -Wall -Wextra -g -O0
INCLUDES = -I../firmware/include -I.
FIRMWARE_SRC = ../firmware/src

.PHONY: all test clean

all: test_d64 vice/iec_bridge

# Unit tests for D64 module
test_d64: test_d64.c mock_fat12.c $(FIRMWARE_SRC)/d64.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

# VICE IEC bridge (host build of CBM-DOS + D64)
vice/iec_bridge: vice/iec_bridge.c vice/mock_iec.c mock_fat12.c \
                  $(FIRMWARE_SRC)/d64.c $(FIRMWARE_SRC)/cbm_dos.c
	$(CC) $(CFLAGS) $(INCLUDES) -Ivice -o $@ $^

test: test_d64
	./test_d64

clean:
	rm -f test_d64 vice/iec_bridge
