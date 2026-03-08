#!/usr/bin/env python3
"""
IEC Bridge test client.

Connects to the iec_bridge TCP server and simulates C64 IEC operations.
Can be used standalone or as a library for automated testing.

Usage:
    # Run all tests against a running bridge:
    python3 vice_test.py [host] [port]

    # Default: localhost:6464
    python3 vice_test.py
"""
import socket
import struct
import sys
import time
import subprocess
import os
import signal

# Protocol constants
CMD_OPEN    = 0x01
CMD_CLOSE   = 0x02
CMD_READ    = 0x03
CMD_WRITE   = 0x04
CMD_EXEC    = 0x05
CMD_QUIT    = 0xFF

RSP_OK      = 0x00
RSP_BYTE    = 0x01
RSP_NO_DATA = 0x02
RSP_ERROR   = 0xFF

SA_LOAD    = 0
SA_SAVE    = 1
SA_COMMAND = 15


class IECClient:
    """Simulates a C64 talking to the IEC bridge."""

    def __init__(self, host='localhost', port=6464):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
        self.sock.settimeout(5.0)

    def close(self):
        try:
            self.sock.sendall(bytes([CMD_QUIT]))
            self.sock.recv(1)
        except:
            pass
        self.sock.close()

    def _recv(self, n):
        data = b''
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("Connection closed")
            data += chunk
        return data

    def open_channel(self, sa, filename):
        """OPEN sa, 8, sa, "filename" """
        fn = filename.encode('ascii')
        self.sock.sendall(bytes([CMD_OPEN, sa, len(fn)]) + fn)
        rsp = self._recv(1)[0]
        if rsp == RSP_OK:
            return True, 0, "OK"
        elif rsp == RSP_ERROR:
            code = self._recv(1)[0]
            mlen = self._recv(1)[0]
            msg = self._recv(mlen).decode('ascii')
            return False, code, msg
        return False, -1, "Unknown response"

    def close_channel(self, sa):
        """CLOSE sa"""
        self.sock.sendall(bytes([CMD_CLOSE, sa]))
        self._recv(1)  # RSP_OK

    def read_byte(self, sa):
        """Read one byte from channel. Returns (byte, eoi) or None on no data."""
        self.sock.sendall(bytes([CMD_READ, sa]))
        rsp = self._recv(1)[0]
        if rsp == RSP_BYTE:
            data = self._recv(2)
            return data[0], bool(data[1])
        return None

    def read_all(self, sa):
        """Read all bytes from channel until EOI."""
        result = bytearray()
        while True:
            r = self.read_byte(sa)
            if r is None:
                break
            byte, eoi = r
            result.append(byte)
            if eoi:
                break
        return bytes(result)

    def write_byte(self, sa, byte):
        """Write one byte to channel."""
        self.sock.sendall(bytes([CMD_WRITE, sa, byte]))
        self._recv(1)  # RSP_OK

    def write_bytes(self, sa, data):
        """Write multiple bytes to channel."""
        for b in data:
            self.write_byte(sa, b)

    def execute_command(self, cmd):
        """Send command on channel 15. E.g. "CD:GAME.D64" """
        c = cmd.encode('ascii')
        self.sock.sendall(bytes([CMD_EXEC, len(c)]) + c)
        rsp = self._recv(1)[0]
        if rsp == RSP_OK:
            return True, 0, "OK"
        elif rsp == RSP_ERROR:
            code = self._recv(1)[0]
            mlen = self._recv(1)[0]
            msg = self._recv(mlen).decode('ascii')
            return False, code, msg
        return False, -1, "Unknown"

    # --- High-level C64 operations ---

    def load_directory(self):
        """LOAD "$",8 — returns raw directory listing bytes."""
        ok, code, msg = self.open_channel(SA_LOAD, "$")
        if not ok:
            print(f"  DIR failed: {code} {msg}")
            return None
        data = self.read_all(SA_LOAD)
        self.close_channel(SA_LOAD)
        return data

    def load_file(self, filename):
        """LOAD "filename",8 — returns file bytes (including load address)."""
        ok, code, msg = self.open_channel(SA_LOAD, filename)
        if not ok:
            print(f"  LOAD failed: {code} {msg}")
            return None
        data = self.read_all(SA_LOAD)
        self.close_channel(SA_LOAD)
        return data

    def save_file(self, filename, data):
        """SAVE "filename",8 — sends data bytes (including load address)."""
        ok, code, msg = self.open_channel(SA_SAVE, filename)
        if not ok:
            print(f"  SAVE failed: {code} {msg}")
            return False
        self.write_bytes(SA_SAVE, data)
        self.close_channel(SA_SAVE)
        return True

    def mount_d64(self, filename):
        """OPEN 15,8,15,"CD:filename": mount D64."""
        return self.execute_command(f"CD:{filename}")

    def unmount_d64(self):
        """OPEN 15,8,15,"CD:..": unmount D64."""
        return self.execute_command("CD:..")

    def scratch(self, filename):
        """OPEN 15,8,15,"S:filename": delete file."""
        return self.execute_command(f"S:{filename}")


def parse_directory(raw):
    """Parse raw CBM directory listing into human-readable format."""
    if len(raw) < 2:
        return []

    lines = []
    pos = 2  # Skip load address

    while pos < len(raw) - 1:
        # Next line pointer
        if raw[pos] == 0 and raw[pos+1] == 0:
            break
        pos += 2

        # Line number (= block count)
        if pos + 2 > len(raw):
            break
        blocks = raw[pos] | (raw[pos+1] << 8)
        pos += 2

        # Line content until 0x00
        line = ""
        while pos < len(raw) and raw[pos] != 0:
            ch = raw[pos]
            if ch == 0x12:
                pass  # RVS ON, skip
            elif 32 <= ch < 127:
                line += chr(ch)
            else:
                line += f"[{ch:02X}]"
            pos += 1
        pos += 1  # Skip terminator

        lines.append((blocks, line))

    return lines


# ---- Test suite ----

def test_directory_listing(c):
    """Test LOAD "$",8 in FAT12 mode."""
    print("Test: FAT12 directory listing")
    data = c.load_directory()
    assert data is not None, "Failed to load directory"
    assert len(data) > 4, "Directory too short"
    assert data[0] == 0x01 and data[1] == 0x04, "Wrong load address"

    lines = parse_directory(data)
    print(f"  Got {len(lines)} lines")
    for blocks, line in lines:
        print(f"    {blocks:4d} {line}")
    print("  PASS")


def test_mount_d64(c):
    """Test CD:filename.D64 to mount a D64 image."""
    print("Test: Mount D64")
    ok, code, msg = c.mount_d64("TEST.D64")
    if not ok:
        print(f"  SKIP (no TEST.D64 available): {code} {msg}")
        return False

    print("  Mounted. Reading D64 directory...")
    data = c.load_directory()
    assert data is not None, "Failed to read D64 directory"

    lines = parse_directory(data)
    print(f"  D64 directory ({len(lines)} lines):")
    for blocks, line in lines:
        print(f"    {blocks:4d} {line}")

    print("  PASS")
    return True


def test_load_from_d64(c):
    """Test loading a file from a mounted D64."""
    print("Test: Load file from D64")

    # First, get directory to find a filename
    data = c.load_directory()
    lines = parse_directory(data)

    # Find first PRG file (skip header and "blocks free")
    filename = None
    for blocks, line in lines:
        if 'PRG' in line and '"' in line:
            # Extract filename between quotes
            start = line.index('"') + 1
            end = line.index('"', start)
            filename = line[start:end].strip()
            break

    if not filename:
        print("  SKIP (no PRG files in D64)")
        return

    print(f"  Loading \"{filename}\"...")
    file_data = c.load_file(filename)
    assert file_data is not None, f"Failed to load {filename}"
    print(f"  Got {len(file_data)} bytes")

    if len(file_data) >= 2:
        load_addr = file_data[0] | (file_data[1] << 8)
        print(f"  Load address: ${load_addr:04X}")

    print("  PASS")


def test_save_to_d64(c):
    """Test SAVE to a mounted D64 image."""
    print("Test: Save file to D64")

    # Save a small test PRG
    test_data = bytes([0x01, 0x08, 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE])
    ok = c.save_file("TESTFILE", test_data)
    assert ok, "SAVE failed"

    # Read it back
    readback = c.load_file("TESTFILE")
    assert readback is not None, "Failed to read back saved file"
    assert readback == test_data, f"Data mismatch: {readback.hex()} vs {test_data.hex()}"

    print(f"  Saved and verified {len(test_data)} bytes")
    print("  PASS")


def test_scratch_in_d64(c):
    """Test S:filename to delete a file in D64."""
    print("Test: Scratch file in D64")

    ok, code, msg = c.scratch("TESTFILE")
    assert ok or code == 1, f"Scratch failed: {code} {msg}"

    # Verify it's gone
    result = c.load_file("TESTFILE")
    assert result is None, "File still exists after scratch"

    print("  PASS")


def test_unmount_d64(c):
    """Test CD:.. to unmount."""
    print("Test: Unmount D64")
    ok, code, msg = c.unmount_d64()
    assert ok, f"Unmount failed: {code} {msg}"
    print("  PASS")


def run_all_tests(host='localhost', port=6464):
    """Run all integration tests."""
    print(f"=== IEC Bridge Integration Tests ===")
    print(f"Connecting to {host}:{port}...\n")

    c = IECClient(host, port)

    try:
        test_directory_listing(c)
        print()

        d64_available = test_mount_d64(c)
        print()

        if d64_available:
            test_load_from_d64(c)
            print()

            test_save_to_d64(c)
            print()

            test_scratch_in_d64(c)
            print()

            test_unmount_d64(c)
            print()

        print("=== All tests passed ===")

    except Exception as e:
        print(f"\n!!! TEST FAILED: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    finally:
        c.close()


if __name__ == '__main__':
    host = sys.argv[1] if len(sys.argv) > 1 else 'localhost'
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 6464
    run_all_tests(host, port)
