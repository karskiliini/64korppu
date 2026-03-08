/*
 * Mock FAT12 implementation for host-side testing.
 *
 * Stores files in memory. Tests pre-register file content,
 * then d64.c calls fat12_open_read/read/close to load D64 images,
 * and fat12_create/write/close to flush them back.
 */
#include "fat12.h"
#include "mock_fat12.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct {
    bool     active;
    char     name[8];
    char     ext[3];
    uint8_t  data[MOCK_MAX_DATA];
    uint32_t size;
} mock_file_t;

static mock_file_t mock_files[MOCK_MAX_FILES];

/* Track which mock file is associated with each fat12_file_t */
typedef struct {
    bool     in_use;
    int      file_idx;   /* Index into mock_files[] */
    uint32_t position;
    bool     write_mode;
} mock_handle_t;

#define MOCK_MAX_HANDLES 8
static mock_handle_t mock_handles[MOCK_MAX_HANDLES];

/* Find a mock file by 8.3 name */
static int find_mock_file(const char *name, const char *ext) {
    for (int i = 0; i < MOCK_MAX_FILES; i++) {
        if (mock_files[i].active &&
            memcmp(mock_files[i].name, name, 8) == 0 &&
            memcmp(mock_files[i].ext, ext, 3) == 0) {
            return i;
        }
    }
    return -1;
}

/* Find a free handle slot */
static int alloc_handle(void) {
    for (int i = 0; i < MOCK_MAX_HANDLES; i++) {
        if (!mock_handles[i].in_use) return i;
    }
    return -1;
}

void mock_fat12_reset(void) {
    memset(mock_files, 0, sizeof(mock_files));
    memset(mock_handles, 0, sizeof(mock_handles));
}

void mock_fat12_add_file(const char *name8, const char *ext3,
                          const uint8_t *data, uint32_t size) {
    for (int i = 0; i < MOCK_MAX_FILES; i++) {
        if (!mock_files[i].active) {
            mock_files[i].active = true;
            memcpy(mock_files[i].name, name8, 8);
            memcpy(mock_files[i].ext, ext3, 3);
            if (size > MOCK_MAX_DATA) size = MOCK_MAX_DATA;
            memcpy(mock_files[i].data, data, size);
            mock_files[i].size = size;
            return;
        }
    }
    fprintf(stderr, "mock_fat12: too many files\n");
}

uint32_t mock_fat12_get_file(const char *name8, const char *ext3,
                              uint8_t *data, uint32_t max_size) {
    int idx = find_mock_file(name8, ext3);
    if (idx < 0) return 0;

    uint32_t copy_size = mock_files[idx].size;
    if (copy_size > max_size) copy_size = max_size;
    memcpy(data, mock_files[idx].data, copy_size);
    return mock_files[idx].size;
}

/* --- FAT12 API implementation (mock) --- */

int fat12_open_read(const char *name, const char *ext, fat12_file_t *file) {
    int idx = find_mock_file(name, ext);
    if (idx < 0) return FAT12_ERR_NOT_FOUND;

    int h = alloc_handle();
    if (h < 0) return FAT12_ERR_INVALID;

    mock_handles[h].in_use = true;
    mock_handles[h].file_idx = idx;
    mock_handles[h].position = 0;
    mock_handles[h].write_mode = false;

    memset(file, 0, sizeof(*file));
    file->active = true;
    file->file_size = mock_files[idx].size;
    file->position = 0;
    file->write_mode = false;
    /* Abuse first_cluster as handle index for mock */
    file->first_cluster = (uint16_t)h;
    file->current_cluster = (uint16_t)h;

    return FAT12_OK;
}

int fat12_read(fat12_file_t *file, uint8_t *buf, uint16_t count) {
    if (!file->active) return FAT12_ERR_INVALID;

    int h = file->first_cluster;
    if (h < 0 || h >= MOCK_MAX_HANDLES || !mock_handles[h].in_use)
        return FAT12_ERR_INVALID;

    int idx = mock_handles[h].file_idx;
    uint32_t pos = mock_handles[h].position;
    uint32_t avail = mock_files[idx].size - pos;
    if (count > avail) count = avail;
    if (count == 0) return 0;

    memcpy(buf, &mock_files[idx].data[pos], count);
    mock_handles[h].position += count;
    file->position = mock_handles[h].position;

    return count;
}

int fat12_create(const char *name, const char *ext, fat12_file_t *file) {
    /* Delete existing if present */
    int existing = find_mock_file(name, ext);
    if (existing >= 0) {
        mock_files[existing].active = false;
    }

    /* Find free slot */
    int idx = -1;
    for (int i = 0; i < MOCK_MAX_FILES; i++) {
        if (!mock_files[i].active) { idx = i; break; }
    }
    if (idx < 0) return FAT12_ERR_DIR_FULL;

    mock_files[idx].active = true;
    memcpy(mock_files[idx].name, name, 8);
    memcpy(mock_files[idx].ext, ext, 3);
    mock_files[idx].size = 0;

    int h = alloc_handle();
    if (h < 0) return FAT12_ERR_INVALID;

    mock_handles[h].in_use = true;
    mock_handles[h].file_idx = idx;
    mock_handles[h].position = 0;
    mock_handles[h].write_mode = true;

    memset(file, 0, sizeof(*file));
    file->active = true;
    file->file_size = 0;
    file->position = 0;
    file->write_mode = true;
    file->first_cluster = (uint16_t)h;
    file->current_cluster = (uint16_t)h;

    return FAT12_OK;
}

int fat12_write(fat12_file_t *file, const uint8_t *buf, uint16_t count) {
    if (!file->active || !file->write_mode) return FAT12_ERR_INVALID;

    int h = file->first_cluster;
    if (h < 0 || h >= MOCK_MAX_HANDLES || !mock_handles[h].in_use)
        return FAT12_ERR_INVALID;

    int idx = mock_handles[h].file_idx;
    uint32_t pos = mock_handles[h].position;
    if (pos + count > MOCK_MAX_DATA) return FAT12_ERR_DISK_FULL;

    memcpy(&mock_files[idx].data[pos], buf, count);
    mock_handles[h].position += count;
    mock_files[idx].size = mock_handles[h].position;
    file->position = mock_handles[h].position;
    file->file_size = mock_files[idx].size;

    return count;
}

int fat12_close(fat12_file_t *file) {
    if (!file->active) return FAT12_OK;

    int h = file->first_cluster;
    if (h >= 0 && h < MOCK_MAX_HANDLES) {
        mock_handles[h].in_use = false;
    }
    file->active = false;
    return FAT12_OK;
}

int fat12_delete(const char *name, const char *ext) {
    int idx = find_mock_file(name, ext);
    if (idx < 0) return FAT12_ERR_NOT_FOUND;
    mock_files[idx].active = false;
    return FAT12_OK;
}

void fat12_parse_filename(const char *cbm_name, char *name8, char *ext3) {
    memset(name8, ' ', 8);
    memset(ext3, ' ', 3);
    memcpy(ext3, "PRG", 3);

    if (!cbm_name || !cbm_name[0]) return;

    const char *dot = NULL;
    for (const char *p = cbm_name; *p; p++) {
        if (*p == '.') { dot = p; break; }
    }

    int len = dot ? (dot - cbm_name) : (int)strlen(cbm_name);
    if (len > 8) len = 8;
    for (int i = 0; i < len; i++) {
        name8[i] = toupper((unsigned char)cbm_name[i]);
    }

    if (dot && dot[1]) {
        int elen = strlen(dot + 1);
        if (elen > 3) elen = 3;
        for (int i = 0; i < elen; i++) {
            ext3[i] = toupper((unsigned char)dot[i + 1]);
        }
    }
}

/* Stubs for functions not used by d64.c but declared in fat12.h */
int fat12_mount(void) { return FAT12_OK; }
void fat12_unmount(void) {}
uint16_t fat12_read_fat_entry(uint16_t cluster) { return 0; }
void fat12_write_fat_entry(uint16_t cluster, uint16_t value) {}
int fat12_flush_fat(void) { return FAT12_OK; }
int fat12_find_file(const char *name, const char *ext, fat12_dirent_t *entry) {
    return FAT12_ERR_NOT_FOUND;
}
int fat12_rename(const char *old_name, const char *old_ext,
                  const char *new_name, const char *new_ext) {
    return FAT12_ERR_NOT_FOUND;
}
int fat12_readdir(uint16_t *index, fat12_dirent_t *entry) {
    return FAT12_ERR_NOT_FOUND;
}
uint32_t fat12_free_space(void) { return 0; }
int fat12_format(const char *label) { return FAT12_OK; }
