#include "cbm_dos.h"
#include "iec_protocol.h"
#include "fat12.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*
 * CBM-DOS emulation layer.
 *
 * Maps Commodore disk operations to FAT12 filesystem:
 *
 * LOAD "FILENAME",8     → Opens FILENAME.PRG, reads 2-byte header + data
 * SAVE "FILENAME",8     → Creates FILENAME.PRG, writes 2-byte header + data
 * LOAD "$",8            → Generates CBM-format directory listing
 * OPEN 15,8,15,"cmd"    → Executes command on channel 15
 *
 * Directory listing format (as BASIC program):
 *   0 "DISK LABEL      " ID FAT12
 *   123  "FILENAME PRG"   PRG
 *   456  BLOCKS FREE.
 *
 * Each "block" = 256 bytes (to match CBM convention)
 */

/* File handles for open channels */
static fat12_file_t channel_files[IEC_NUM_CHANNELS];

/* Directory listing buffer (generated on LOAD "$") */
static uint8_t dir_buffer[8192];  /* Max ~8KB for directory listing */
static uint16_t dir_len = 0;
static uint16_t dir_pos = 0;
static bool dir_active = false;

void cbm_dos_init(void) {
    memset(channel_files, 0, sizeof(channel_files));
    dir_active = false;

    /* Set initial status: "73, 64KORPPU V1.0,00,00" */
    iec_set_error(CBM_ERR_DOS_MISMATCH, CBM_DOS_ID, 0, 0);
}

int cbm_dos_format_error(uint8_t code, const char *msg,
                          uint8_t track, uint8_t sector,
                          char *buf, uint8_t buflen) {
    return snprintf(buf, buflen, "%02d, %s,%02d,%02d\r",
                     code, msg, track, sector);
}

/*
 * Generate a CBM-style directory listing as a BASIC program.
 *
 * The listing is formatted as a tokenized BASIC program that can be
 * loaded and listed with the LIST command. Each line:
 *   - 2 bytes: pointer to next line (or 0x0000 for end)
 *   - 2 bytes: line number (= block count for files, 0 for header)
 *   - Variable: BASIC tokens / text
 *   - 1 byte: 0x00 line terminator
 */
void cbm_dos_generate_directory(iec_channel_t *channel) {
    uint16_t pos = 0;
    uint8_t *buf = dir_buffer;

    /* Load address: $0401 (BASIC start) */
    buf[pos++] = 0x01;
    buf[pos++] = 0x04;

    /* Header line: 0 "DISK LABEL" ID FAT12 */
    /* Next line pointer (placeholder, will fix later) */
    uint16_t line_start = pos;
    buf[pos++] = 0x00;  /* Next line pointer low (placeholder) */
    buf[pos++] = 0x00;  /* Next line pointer high (placeholder) */

    /* Line number = 0 */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    /* Reverse video on */
    buf[pos++] = 0x12;  /* RVS ON */

    /* "DISK LABEL      " */
    buf[pos++] = '"';

    /* Get volume label from FAT12 or use default */
    const char *label = "64KORPPU        ";
    for (int i = 0; i < 16 && label[i]; i++) {
        buf[pos++] = label[i];
    }
    buf[pos++] = '"';
    buf[pos++] = ' ';

    /* Disk ID */
    buf[pos++] = '6';
    buf[pos++] = '4';
    buf[pos++] = ' ';
    buf[pos++] = 'F';
    buf[pos++] = 'A';
    buf[pos++] = 'T';

    buf[pos++] = 0x00;  /* End of line */

    /* Fix next line pointer */
    buf[line_start] = (pos + 0x0401) & 0xFF;
    buf[line_start + 1] = ((pos + 0x0401) >> 8) & 0xFF;

    /* File entries */
    uint16_t dir_index = 0;
    fat12_dirent_t entry;

    while (fat12_readdir(&dir_index, &entry) == FAT12_OK) {
        /* Skip volume labels */
        if (entry.attr & FAT12_ATTR_VOLUME_ID) continue;

        line_start = pos;
        buf[pos++] = 0x00;  /* Next line pointer (placeholder) */
        buf[pos++] = 0x00;

        /* Line number = blocks (file_size / 256, rounded up, min 1) */
        uint16_t blocks = (entry.file_size + 255) / 256;
        if (blocks == 0) blocks = 1;
        buf[pos++] = blocks & 0xFF;
        buf[pos++] = (blocks >> 8) & 0xFF;

        /* Spacing for alignment */
        if (blocks < 10) buf[pos++] = ' ';
        if (blocks < 100) buf[pos++] = ' ';
        if (blocks < 1000) buf[pos++] = ' ';

        /* "FILENAME EXT" */
        buf[pos++] = '"';

        /* Filename (8 chars, trim trailing spaces) */
        int name_len = 8;
        while (name_len > 0 && entry.name[name_len - 1] == ' ') name_len--;
        for (int i = 0; i < name_len; i++) {
            buf[pos++] = entry.name[i];
        }

        /* Add dot and extension if not spaces */
        int ext_len = 3;
        while (ext_len > 0 && entry.ext[ext_len - 1] == ' ') ext_len--;
        if (ext_len > 0) {
            buf[pos++] = '.';
            for (int i = 0; i < ext_len; i++) {
                buf[pos++] = entry.ext[i];
            }
        }

        buf[pos++] = '"';

        /* Pad to column for type */
        int total_name = name_len + (ext_len > 0 ? 1 + ext_len : 0);
        for (int i = total_name; i < 16; i++) {
            buf[pos++] = ' ';
        }

        /* File type */
        if (entry.attr & FAT12_ATTR_DIRECTORY) {
            buf[pos++] = 'D'; buf[pos++] = 'I'; buf[pos++] = 'R';
        } else if (ext_len >= 3 && memcmp(entry.ext, "PRG", 3) == 0) {
            buf[pos++] = 'P'; buf[pos++] = 'R'; buf[pos++] = 'G';
        } else if (ext_len >= 3 && memcmp(entry.ext, "SEQ", 3) == 0) {
            buf[pos++] = 'S'; buf[pos++] = 'E'; buf[pos++] = 'Q';
        } else {
            buf[pos++] = 'P'; buf[pos++] = 'R'; buf[pos++] = 'G';
        }

        buf[pos++] = 0x00;  /* End of line */

        /* Fix next line pointer */
        buf[line_start] = (pos + 0x0401) & 0xFF;
        buf[line_start + 1] = ((pos + 0x0401) >> 8) & 0xFF;

        if (pos > sizeof(dir_buffer) - 64) break;  /* Safety margin */
    }

    /* "Blocks free" line */
    line_start = pos;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    uint32_t free_bytes = fat12_free_space();
    uint16_t free_blocks = free_bytes / 256;
    buf[pos++] = free_blocks & 0xFF;
    buf[pos++] = (free_blocks >> 8) & 0xFF;

    const char *free_msg = "BLOCKS FREE.";
    for (const char *p = free_msg; *p; p++) {
        buf[pos++] = *p;
    }
    buf[pos++] = 0x00;

    /* End of program: two zero bytes */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    dir_len = pos;
    dir_pos = 0;
    dir_active = true;
}

void cbm_dos_open(uint8_t sa, const char *filename, uint8_t len) {
    char name8[9] = {0};
    char ext3[4] = {0};

    if (sa == IEC_SA_LOAD || sa == IEC_SA_SAVE) {
        /* LOAD/SAVE: filename comes with the command */

        /* Check for directory listing */
        if (sa == IEC_SA_LOAD && len == 1 && filename[0] == '$') {
            cbm_dos_generate_directory(NULL);
            iec_set_error(CBM_ERR_OK, "OK", 0, 0);
            return;
        }

        /* Parse filename */
        fat12_parse_filename(filename, name8, ext3);

        if (sa == IEC_SA_LOAD) {
            /* Open for reading */
            int rc = fat12_open_read(name8, ext3, &channel_files[sa]);
            if (rc != FAT12_OK) {
                iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                return;
            }
        } else {
            /* Open for writing (SAVE) */
            int rc = fat12_create(name8, ext3, &channel_files[sa]);
            if (rc != FAT12_OK) {
                if (rc == FAT12_ERR_DISK_FULL) {
                    iec_set_error(CBM_ERR_DISK_FULL, "DISK FULL", 0, 0);
                } else {
                    iec_set_error(CBM_ERR_WRITE_ERROR, "WRITE ERROR", 0, 0);
                }
                return;
            }
        }

        iec_set_error(CBM_ERR_OK, "OK", 0, 0);
    }
}

void cbm_dos_close(uint8_t sa) {
    if (channel_files[sa].active) {
        fat12_close(&channel_files[sa]);
    }
    dir_active = false;
}

bool cbm_dos_talk_byte(uint8_t sa, uint8_t *byte, bool *eoi) {
    *eoi = false;

    if (sa == IEC_SA_COMMAND) {
        /* Error channel: read status message */
        iec_channel_t *ch = &((iec_device_t *)0)->channels[sa]; /* TODO: fix access */
        /* For now, handled in iec_protocol directly */
        return false;
    }

    if (sa == IEC_SA_LOAD) {
        /* Check for directory listing */
        if (dir_active) {
            if (dir_pos < dir_len) {
                *byte = dir_buffer[dir_pos++];
                if (dir_pos >= dir_len) {
                    *eoi = true;
                    dir_active = false;
                }
                return true;
            }
            *eoi = true;
            return false;
        }

        /* Read from file */
        if (channel_files[sa].active) {
            uint8_t buf;
            int rc = fat12_read(&channel_files[sa], &buf, 1);
            if (rc == 1) {
                *byte = buf;
                /* Check if next read would be EOF */
                if (channel_files[sa].position >= channel_files[sa].file_size) {
                    *eoi = true;
                }
                return true;
            }
            *eoi = true;
            return false;
        }
    }

    *eoi = true;
    return false;
}

void cbm_dos_listen_byte(uint8_t sa, uint8_t byte) {
    if (sa == IEC_SA_SAVE && channel_files[sa].active) {
        fat12_write(&channel_files[sa], &byte, 1);
    }
}

void cbm_dos_execute_command(const char *cmd, uint8_t len) {
    if (len == 0) return;

    char name8[9], ext3[4];
    char cmd_buf[42];

    /* Copy command to working buffer */
    uint8_t cmd_len = len < sizeof(cmd_buf) - 1 ? len : sizeof(cmd_buf) - 1;
    memcpy(cmd_buf, cmd, cmd_len);
    cmd_buf[cmd_len] = '\0';

    /* Convert to uppercase */
    for (int i = 0; i < cmd_len; i++) {
        cmd_buf[i] = toupper((unsigned char)cmd_buf[i]);
    }

    switch (cmd_buf[0]) {
        case 'S': {
            /* S:filename - Scratch (delete) */
            if (cmd_len > 2 && cmd_buf[1] == ':') {
                fat12_parse_filename(&cmd_buf[2], name8, ext3);
                int rc = fat12_delete(name8, ext3);
                if (rc == FAT12_OK) {
                    iec_set_error(CBM_ERR_FILES_SCRATCHED, "FILES SCRATCHED", 1, 0);
                } else {
                    iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                }
            } else {
                iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
            }
            break;
        }

        case 'R': {
            /* R:newname=oldname - Rename */
            if (cmd_len > 2 && cmd_buf[1] == ':') {
                /* Find the '=' separator */
                char *eq = strchr(&cmd_buf[2], '=');
                if (eq) {
                    *eq = '\0';
                    char new_name8[9], new_ext3[4];
                    char old_name8[9], old_ext3[4];
                    fat12_parse_filename(&cmd_buf[2], new_name8, new_ext3);
                    fat12_parse_filename(eq + 1, old_name8, old_ext3);

                    int rc = fat12_rename(old_name8, old_ext3, new_name8, new_ext3);
                    if (rc == FAT12_OK) {
                        iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                    } else if (rc == FAT12_ERR_NOT_FOUND) {
                        iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                    } else {
                        iec_set_error(CBM_ERR_FILE_EXISTS, "FILE EXISTS", 0, 0);
                    }
                } else {
                    iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
                }
            }
            break;
        }

        case 'N': {
            /* N:label - Format/New disk */
            if (cmd_len > 2 && cmd_buf[1] == ':') {
                /* Parse volume label (pad to 11 chars) */
                char label[12];
                memset(label, ' ', 11);
                label[11] = '\0';
                int llen = strlen(&cmd_buf[2]);
                if (llen > 11) llen = 11;
                memcpy(label, &cmd_buf[2], llen);

                int rc = fat12_format(label);
                if (rc == FAT12_OK) {
                    iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                } else {
                    iec_set_error(CBM_ERR_WRITE_ERROR, "WRITE ERROR", 0, 0);
                }
            } else {
                /* N without label */
                int rc = fat12_format(NULL);
                if (rc == FAT12_OK) {
                    iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                } else {
                    iec_set_error(CBM_ERR_WRITE_ERROR, "WRITE ERROR", 0, 0);
                }
            }
            break;
        }

        case 'I': {
            /* I - Initialize (re-read disk) */
            fat12_unmount();
            int rc = fat12_mount();
            if (rc == FAT12_OK) {
                iec_set_error(CBM_ERR_OK, "OK", 0, 0);
            } else {
                iec_set_error(CBM_ERR_DRIVE_NOT_READY, "DRIVE NOT READY", 0, 0);
            }
            break;
        }

        default:
            iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
            break;
    }
}
