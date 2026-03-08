CC = gcc
CFLAGS = -Wall -Wextra -g -O0
INCLUDES = -I../firmware/include -I.
FIRMWARE_SRC = ../firmware/src

.PHONY: all test clean

all: test_d64 test_cbmfs test_format vice/iec_bridge

# Unit tests for D64 module
test_d64: test_d64.c mock_fat12.c $(FIRMWARE_SRC)/d64.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

# Unit tests for CBMFS (1581) module
test_cbmfs: test_cbmfs.c $(FIRMWARE_SRC)/cbmfs.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

# Unit tests for format detection and geometry
test_format: test_format.c $(FIRMWARE_SRC)/format_detect.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

# VICE IEC bridge (host build of CBM-DOS + D64)
vice/iec_bridge: vice/iec_bridge.c vice/mock_iec.c mock_fat12.c \
                  $(FIRMWARE_SRC)/d64.c $(FIRMWARE_SRC)/cbm_dos.c
	$(CC) $(CFLAGS) $(INCLUDES) -Ivice -o $@ $^

test: test_d64 test_cbmfs test_format
	@echo "=== Running D64 tests ==="
	./test_d64
	@echo ""
	@echo "=== Running CBMFS tests ==="
	./test_cbmfs
	@echo ""
	@echo "=== Running Format Detection tests ==="
	./test_format

clean:
	rm -f test_d64 test_cbmfs test_format vice/iec_bridge
