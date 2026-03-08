/*
 * Mock FAT12 layer for host-side testing.
 *
 * Instead of reading from a real floppy disk, this mock stores
 * "files" in memory. Tests can pre-register files with their content
 * before calling d64_mount() etc.
 */
#ifndef MOCK_FAT12_H
#define MOCK_FAT12_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum files the mock can hold */
#define MOCK_MAX_FILES  16
#define MOCK_MAX_DATA   (256 * 1024)  /* 256 KB max per file */

/**
 * Reset the mock filesystem (clear all registered files).
 */
void mock_fat12_reset(void);

/**
 * Register a "file" in the mock FAT12 filesystem.
 * @param name8  8-char space-padded filename.
 * @param ext3   3-char space-padded extension.
 * @param data   File content.
 * @param size   File size in bytes.
 */
void mock_fat12_add_file(const char *name8, const char *ext3,
                          const uint8_t *data, uint32_t size);

/**
 * Retrieve written file data after a d64_flush() / fat12_create+write+close.
 * @param name8  8-char space-padded filename.
 * @param ext3   3-char space-padded extension.
 * @param data   Output buffer.
 * @param max_size  Buffer size.
 * @return Actual file size, or 0 if not found.
 */
uint32_t mock_fat12_get_file(const char *name8, const char *ext3,
                              uint8_t *data, uint32_t max_size);

#endif /* MOCK_FAT12_H */
