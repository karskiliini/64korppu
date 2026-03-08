#ifndef CBM_DOS_H
#define CBM_DOS_H

#include <stdint.h>
#include <stdbool.h>
#include "iec_protocol.h"
#include "fat12.h"

/*
 * CBM-DOS emulation layer.
 *
 * Translates CBM disk commands to FAT12 operations:
 * - LOAD "filename",8     → Open file for reading (SA 0)
 * - SAVE "filename",8     → Create/overwrite file (SA 1)
 * - LOAD "$",8            → Directory listing in CBM format
 * - OPEN 15,8,15,"S:file" → Scratch (delete) file
 * - OPEN 15,8,15,"R:new=old" → Rename file
 * - OPEN 15,8,15,"N:label" → Format disk
 * - OPEN 15,8,15          → Read error channel
 *
 * File type handling:
 * - .PRG files: 2-byte load address header + data (default)
 * - .SEQ files: sequential data files
 */

/* CBM-DOS error codes */
#define CBM_ERR_OK               0   /* 00, OK,00,00 */
#define CBM_ERR_FILES_SCRATCHED  1   /* 01, FILES SCRATCHED,xx,00 */
#define CBM_ERR_READ_ERROR      20   /* 20, READ ERROR,TT,SS */
#define CBM_ERR_WRITE_ERROR     25   /* 25, WRITE ERROR,TT,SS */
#define CBM_ERR_WRITE_PROTECT   26   /* 26, WRITE PROTECT ON,TT,SS */
#define CBM_ERR_SYNTAX_ERROR    30   /* 30, SYNTAX ERROR,00,00 */
#define CBM_ERR_FILE_NOT_FOUND  62   /* 62, FILE NOT FOUND,00,00 */
#define CBM_ERR_FILE_EXISTS     63   /* 63, FILE EXISTS,00,00 */
#define CBM_ERR_DISK_FULL       72   /* 72, DISK FULL,00,00 */
#define CBM_ERR_DOS_MISMATCH    73   /* 73, 64KORPPU V1.0,00,00 */
#define CBM_ERR_DRIVE_NOT_READY 74   /* 74, DRIVE NOT READY,00,00 */

/* Firmware identification string */
#define CBM_DOS_ID  "64KORPPU V1.0"

/**
 * Initialize CBM-DOS layer.
 */
void cbm_dos_init(void);

/**
 * Handle an OPEN command for a channel.
 * Called when C64 sends OPEN with a filename and secondary address.
 *
 * @param sa        Secondary address (0-15).
 * @param filename  Filename from OPEN command.
 * @param len       Filename length.
 */
void cbm_dos_open(uint8_t sa, const char *filename, uint8_t len);

/**
 * Handle a CLOSE command for a channel.
 * @param sa  Secondary address.
 */
void cbm_dos_close(uint8_t sa);

/**
 * Provide the next data byte for a TALK operation.
 * Called repeatedly when C64 reads from a channel.
 *
 * @param sa    Secondary address.
 * @param byte  Output: next byte.
 * @param eoi   Output: true if this is the last byte.
 * @return true if a byte is available, false if channel is empty/error.
 */
bool cbm_dos_talk_byte(uint8_t sa, uint8_t *byte, bool *eoi);

/**
 * Accept a data byte from a LISTEN operation.
 * Called repeatedly when C64 writes to a channel.
 *
 * @param sa    Secondary address.
 * @param byte  Byte received.
 */
void cbm_dos_listen_byte(uint8_t sa, uint8_t byte);

/**
 * Generate a CBM-style directory listing.
 * Formats FAT12 directory as a BASIC program listing:
 *   0 "DISK LABEL      " 64K FAT12
 *   123  "FILENAME PRG"   PRG
 *   456  BLOCKS FREE.
 *
 * @param channel  Channel buffer to fill with listing data.
 */
void cbm_dos_generate_directory(iec_channel_t *channel);

/**
 * Execute a command channel command.
 * Parses and executes commands sent to SA 15:
 *   S:filename  - Scratch (delete)
 *   R:new=old   - Rename
 *   N:label     - Format/New
 *   I           - Initialize (re-read disk)
 *
 * @param cmd  Command string.
 * @param len  Command length.
 */
void cbm_dos_execute_command(const char *cmd, uint8_t len);

/**
 * Format the error channel string.
 * @param code    Error code.
 * @param msg     Error message.
 * @param track   Track number.
 * @param sector  Sector number.
 * @param buf     Output buffer.
 * @param buflen  Buffer size.
 * @return Length of formatted string.
 */
int cbm_dos_format_error(uint8_t code, const char *msg,
                          uint8_t track, uint8_t sector,
                          char *buf, uint8_t buflen);

#endif /* CBM_DOS_H */
