#ifndef MFM_CODEC_H
#define MFM_CODEC_H

#include <stdint.h>
#include <stdbool.h>

/*
 * MFM encoding/decoding for Arduino Nano.
 *
 * Read: Timer1 ICP captures flux transitions → SRAM track buffer.
 * Decode: Process SRAM buffer to extract sectors.
 * Write: Generate MFM from sector data → /WDATA via direct GPIO.
 */

typedef struct {
    uint8_t track;
    uint8_t side;
    uint8_t sector;
    uint8_t size_code;
    uint16_t crc;
} mfm_sector_id_t;

void mfm_init(void);
uint16_t mfm_crc16(const uint8_t *data, uint16_t length, uint16_t crc);

/* Capture raw MFM track from floppy to SRAM track buffer */
int mfm_capture_track(void);

/* Common (host-testable) pieces of the read path: threshold defaults,
 * histogram-based recalibration and capture-count accessors. Host
 * tests fill the mock SRAM with an interval stream, set the count and
 * run the exact same calibrate+decode code the firmware uses. */
void mfm_set_default_thresholds(void);
void mfm_calibrate_capture(void);
void mfm_set_capture_count(uint16_t n);
uint16_t mfm_get_capture_count(void);

/* Decode a sector from the captured track in SRAM */
int mfm_decode_sector(uint8_t sector, uint8_t *data_out);

/* Encode and write a sector to floppy */
int mfm_write_sector(const mfm_sector_id_t *id, const uint8_t *data);

/* Find all sector IDs in captured track */
int mfm_find_sectors(mfm_sector_id_t *ids_out, int max_ids);

#endif /* MFM_CODEC_H */
