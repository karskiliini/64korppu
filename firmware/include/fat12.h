#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stdbool.h>

/*
 * FAT12 filesystem for 1.44MB floppy disk.
 *
 * Disk layout:
 *   Sector 0:      Boot sector (BPB)
 *   Sectors 1-9:   FAT #1 (9 sectors)
 *   Sectors 10-18: FAT #2 (copy)
 *   Sectors 19-32: Root directory (14 sectors, 224 entries)
 *   Sectors 33+:   Data area (clusters 2+)
 *
 * Cluster size: 1 sector (512 bytes) for 1.44MB
 * Total clusters: 2847 (fits in 12-bit FAT entries)
 */

/* 1.44MB disk parameters */
#define FAT12_BYTES_PER_SECTOR   512
#define FAT12_SECTORS_PER_CLUSTER  1
#define FAT12_RESERVED_SECTORS     1
#define FAT12_NUM_FATS             2
#define FAT12_ROOT_ENTRIES       224
#define FAT12_TOTAL_SECTORS     2880
#define FAT12_SECTORS_PER_FAT      9
#define FAT12_MEDIA_BYTE        0xF0

/* Derived constants */
#define FAT12_FAT1_START         1
#define FAT12_FAT2_START        (FAT12_FAT1_START + FAT12_SECTORS_PER_FAT)    /* 10 */
#define FAT12_ROOT_DIR_START    (FAT12_FAT2_START + FAT12_SECTORS_PER_FAT)    /* 19 */
#define FAT12_ROOT_DIR_SECTORS  ((FAT12_ROOT_ENTRIES * 32 + FAT12_BYTES_PER_SECTOR - 1) / FAT12_BYTES_PER_SECTOR) /* 14 */
#define FAT12_DATA_START        (FAT12_ROOT_DIR_START + FAT12_ROOT_DIR_SECTORS) /* 33 */
#define FAT12_TOTAL_CLUSTERS    ((FAT12_TOTAL_SECTORS - FAT12_DATA_START) / FAT12_SECTORS_PER_CLUSTER) /* 2847 */

/* FAT entry special values */
#define FAT12_FREE       0x000
#define FAT12_RESERVED   0x001
#define FAT12_EOC_MIN    0xFF8    /* End of chain (0xFF8-0xFFF) */
#define FAT12_BAD        0xFF7

/* Directory entry attributes */
#define FAT12_ATTR_READ_ONLY  0x01
#define FAT12_ATTR_HIDDEN     0x02
#define FAT12_ATTR_SYSTEM     0x04
#define FAT12_ATTR_VOLUME_ID  0x08
#define FAT12_ATTR_DIRECTORY  0x10
#define FAT12_ATTR_ARCHIVE    0x20
#define FAT12_ATTR_LFN        0x0F  /* Long filename (not used) */

/* Directory entry (32 bytes) */
typedef struct __attribute__((packed)) {
    char     name[8];        /* Filename (space-padded) */
    char     ext[3];         /* Extension (space-padded) */
    uint8_t  attr;           /* Attributes */
    uint8_t  reserved;       /* Reserved (NT) */
    uint8_t  ctime_tenths;   /* Creation time tenths of second */
    uint16_t ctime;          /* Creation time */
    uint16_t cdate;          /* Creation date */
    uint16_t adate;          /* Last access date */
    uint16_t cluster_hi;     /* High word of first cluster (0 for FAT12) */
    uint16_t mtime;          /* Last modification time */
    uint16_t mdate;          /* Last modification date */
    uint16_t cluster_lo;     /* Low word of first cluster */
    uint32_t file_size;      /* File size in bytes */
} fat12_dirent_t;

/* Boot sector / BPB (relevant fields) */
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* Extended BPB */
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_serial;
    char     volume_label[11];
    char     fs_type[8];
} fat12_bpb_t;

/* Filesystem state */
typedef struct {
    bool     mounted;
    uint8_t  fat_cache[FAT12_SECTORS_PER_FAT * FAT12_BYTES_PER_SECTOR]; /* 4608 bytes */
    bool     fat_dirty;
    uint32_t volume_serial;
    char     volume_label[12];
} fat12_state_t;

/* Error codes */
#define FAT12_OK              0
#define FAT12_ERR_IO         -1    /* Disk I/O error */
#define FAT12_ERR_NOT_MOUNT  -2    /* Not mounted */
#define FAT12_ERR_NOT_FOUND  -3    /* File not found */
#define FAT12_ERR_EXISTS     -4    /* File already exists */
#define FAT12_ERR_DIR_FULL   -5    /* Directory full */
#define FAT12_ERR_DISK_FULL  -6    /* Disk full */
#define FAT12_ERR_INVALID    -7    /* Invalid parameter */
#define FAT12_ERR_NOT_FAT12  -8    /* Not a FAT12 filesystem */

/* File handle for open files */
typedef struct {
    bool     active;
    uint16_t first_cluster;
    uint16_t current_cluster;
    uint32_t file_size;
    uint32_t position;           /* Current read/write position */
    uint16_t dir_entry_sector;   /* Sector containing directory entry */
    uint16_t dir_entry_offset;   /* Offset within sector */
    bool     write_mode;
    bool     modified;
} fat12_file_t;

#define FAT12_MAX_OPEN_FILES  4

/**
 * Mount a FAT12 filesystem by reading the boot sector and FAT.
 * @return FAT12_OK on success, error code on failure.
 */
int fat12_mount(void);

/**
 * Unmount filesystem, flushing any dirty FAT data.
 */
void fat12_unmount(void);

/**
 * Read a FAT12 entry for a given cluster.
 * @param cluster  Cluster number.
 * @return FAT entry value (12-bit).
 */
uint16_t fat12_read_fat_entry(uint16_t cluster);

/**
 * Write a FAT12 entry for a given cluster.
 * @param cluster  Cluster number.
 * @param value    FAT entry value (12-bit).
 */
void fat12_write_fat_entry(uint16_t cluster, uint16_t value);

/**
 * Flush dirty FAT cache to disk (writes both FAT copies).
 * @return FAT12_OK on success, error code on failure.
 */
int fat12_flush_fat(void);

/**
 * Find a file in the root directory by 8.3 name.
 * @param name     Filename (8 chars, space-padded).
 * @param ext      Extension (3 chars, space-padded).
 * @param entry    Output: directory entry if found.
 * @return FAT12_OK if found, FAT12_ERR_NOT_FOUND if not.
 */
int fat12_find_file(const char *name, const char *ext, fat12_dirent_t *entry);

/**
 * Open a file for reading.
 * @param name     8.3 filename (e.g., "HELLO   PRG").
 * @param file     File handle to initialize.
 * @return FAT12_OK on success.
 */
int fat12_open_read(const char *name, const char *ext, fat12_file_t *file);

/**
 * Read data from an open file.
 * @param file     File handle.
 * @param buf      Output buffer.
 * @param count    Bytes to read.
 * @return Number of bytes actually read, or negative error code.
 */
int fat12_read(fat12_file_t *file, uint8_t *buf, uint16_t count);

/**
 * Create a new file for writing.
 * @param name     Filename (8 chars).
 * @param ext      Extension (3 chars).
 * @param file     File handle to initialize.
 * @return FAT12_OK on success.
 */
int fat12_create(const char *name, const char *ext, fat12_file_t *file);

/**
 * Write data to an open file.
 * @param file     File handle.
 * @param buf      Data to write.
 * @param count    Bytes to write.
 * @return Number of bytes written, or negative error code.
 */
int fat12_write(fat12_file_t *file, const uint8_t *buf, uint16_t count);

/**
 * Close a file, flushing any pending writes.
 * @param file     File handle.
 * @return FAT12_OK on success.
 */
int fat12_close(fat12_file_t *file);

/**
 * Delete a file from the root directory and free its clusters.
 * @param name     Filename.
 * @param ext      Extension.
 * @return FAT12_OK on success.
 */
int fat12_delete(const char *name, const char *ext);

/**
 * Rename a file.
 * @param old_name, old_ext  Current name.
 * @param new_name, new_ext  New name.
 * @return FAT12_OK on success.
 */
int fat12_rename(const char *old_name, const char *old_ext,
                  const char *new_name, const char *new_ext);

/**
 * Iterate over root directory entries.
 * Call with *index = 0 to start. Returns next valid entry.
 * @param index    In/out: entry index.
 * @param entry    Output: directory entry.
 * @return FAT12_OK if entry returned, FAT12_ERR_NOT_FOUND when done.
 */
int fat12_readdir(uint16_t *index, fat12_dirent_t *entry);

/**
 * Get free space on disk.
 * @return Free space in bytes.
 */
uint32_t fat12_free_space(void);

/**
 * Format a disk with FAT12 filesystem.
 * @param label    Volume label (11 chars, space-padded). Can be NULL.
 * @return FAT12_OK on success.
 */
int fat12_format(const char *label);

/**
 * Parse a CBM-style filename into 8.3 format.
 * E.g., "HELLO.PRG" → name="HELLO   ", ext="PRG"
 * If no extension, defaults to "PRG".
 * @param cbm_name  Input filename string (null-terminated).
 * @param name8     Output: 8-char padded name.
 * @param ext3      Output: 3-char padded extension.
 */
void fat12_parse_filename(const char *cbm_name, char *name8, char *ext3);

#endif /* FAT12_H */
