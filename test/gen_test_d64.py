#!/usr/bin/env python3
"""Generate a test .D64 image with a few PRG files."""
import struct

IMAGE_SIZE = 174848
SECTOR_SIZE = 256

SPT = [0,
    21,21,21,21,21,21,21,21,21,21,  # 1-10
    21,21,21,21,21,21,21,            # 11-17
    19,19,19,19,19,19,19,            # 18-24
    18,18,18,18,18,18,               # 25-30
    17,17,17,17,17                   # 31-35
]

def ts_offset(track, sector):
    off = 0
    for t in range(1, track):
        off += SPT[t] * SECTOR_SIZE
    off += sector * SECTOR_SIZE
    return off

def create_d64():
    image = bytearray(IMAGE_SIZE)

    # BAM at track 18, sector 0
    bam_off = ts_offset(18, 0)
    image[bam_off + 0] = 18   # First dir track
    image[bam_off + 1] = 1    # First dir sector
    image[bam_off + 2] = 0x41 # DOS version 'A'

    # Initialize BAM entries for all tracks
    for t in range(1, 36):
        offset = bam_off + 4 * t
        nsec = SPT[t]
        b0 = b1 = b2 = 0
        for s in range(nsec):
            if s < 8: b0 |= (1 << s)
            elif s < 16: b1 |= (1 << (s - 8))
            else: b2 |= (1 << (s - 16))
        image[offset + 0] = nsec
        image[offset + 1] = b0
        image[offset + 2] = b1
        image[offset + 3] = b2

    # Allocate BAM sector and first dir sector
    image[bam_off + 4*18 + 0] -= 2
    image[bam_off + 4*18 + 1] &= ~0x03

    # Disk name
    name = b"TEST DISK"
    for i in range(16):
        image[bam_off + 0x90 + i] = name[i] if i < len(name) else 0xA0

    image[bam_off + 0xA0] = 0xA0
    image[bam_off + 0xA1] = 0xA0
    image[bam_off + 0xA2] = ord('6')
    image[bam_off + 0xA3] = ord('4')
    image[bam_off + 0xA4] = 0xA0
    image[bam_off + 0xA5] = ord('2')
    image[bam_off + 0xA6] = ord('A')

    # First directory sector
    dir_off = ts_offset(18, 1)
    image[dir_off + 0] = 0     # No next dir sector
    image[dir_off + 1] = 0xFF

    return image

def bam_alloc(image, track, sector):
    bam_off = ts_offset(18, 0)
    offset = bam_off + 4 * track
    byte_idx = 1 + (sector // 8)
    bit = 1 << (sector % 8)
    if image[offset + byte_idx] & bit:
        image[offset + byte_idx] &= ~bit
        image[offset] -= 1

def add_file(image, name, data):
    """Add a PRG file to the D64 image."""
    bam_off = ts_offset(18, 0)
    dir_off = ts_offset(18, 1)

    # Find free dir slot
    slot = -1
    for i in range(8):
        if image[dir_off + i * 32 + 2] == 0:
            slot = i
            break
    assert slot >= 0, "Directory full"

    # Allocate blocks and write data
    first_t = first_s = 0
    prev_t = prev_s = 0
    offset = 0
    blocks = 0

    while offset < len(data):
        # Find free sector
        alloc_t = alloc_s = None
        for t in range(1, 36):
            if t == 18: continue
            for s in range(SPT[t]):
                byte_idx = 1 + (s // 8)
                bit = 1 << (s % 8)
                if image[bam_off + 4*t + byte_idx] & bit:
                    alloc_t, alloc_s = t, s
                    break
            if alloc_t: break
        assert alloc_t, "Disk full"
        bam_alloc(image, alloc_t, alloc_s)

        if first_t == 0:
            first_t, first_s = alloc_t, alloc_s

        if prev_t:
            prev_off = ts_offset(prev_t, prev_s)
            image[prev_off + 0] = alloc_t
            image[prev_off + 1] = alloc_s

        block_off = ts_offset(alloc_t, alloc_s)
        chunk = min(254, len(data) - offset)
        image[block_off + 2:block_off + 2 + chunk] = data[offset:offset + chunk]

        if offset + chunk >= len(data):
            image[block_off + 0] = 0
            image[block_off + 1] = chunk + 1
        else:
            image[block_off + 0] = 0
            image[block_off + 1] = 0

        prev_t, prev_s = alloc_t, alloc_s
        offset += chunk
        blocks += 1

    # Directory entry
    entry_off = dir_off + slot * 32
    image[entry_off + 2] = 0x82  # PRG + closed
    image[entry_off + 3] = first_t
    image[entry_off + 4] = first_s

    name_bytes = name.encode('ascii')[:16]
    for i in range(16):
        image[entry_off + 5 + i] = name_bytes[i] if i < len(name_bytes) else 0xA0

    image[entry_off + 30] = blocks & 0xFF
    image[entry_off + 31] = (blocks >> 8) & 0xFF

image = create_d64()

# Add test files
# Simple "Hello World" PRG (load addr $0801)
hello = bytes([0x01, 0x08]) + b"HELLO FROM D64 IMAGE!"
add_file(image, "HELLO", hello)

# Bigger file (500 bytes)
big = bytes([0x01, 0x08]) + bytes(range(256)) + bytes(range(256)) [:242]
add_file(image, "BIGFILE", big)

with open("TEST.D64", "wb") as f:
    f.write(image)

print(f"Created TEST.D64 ({len(image)} bytes)")
