#include "mfm_codec.h"
#include "config.h"
#include "sram.h"
#include "shiftreg.h"
#include "uart.h"

#ifdef __AVR__

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>

/*
 * MFM codec for ATmega328P.
 *
 * Read:   Timer1 ICP1 (D8) captures flux transitions → SRAM.
 * Decode: Reconstruct MFM bitstream from SRAM, extract sectors.
 * Write:  Poll ICP1 to find sector position, then generate MFM
 *         pulses on /WDATA (D7) using Timer1 OC1A for bit-cell timing.
 */

/* ---- Capture state (used by ISR) ---- */

static volatile uint16_t prev_capture;
static volatile uint32_t capture_count;
static volatile uint8_t pulse_pack;
static volatile uint8_t pulse_in_pack;
static volatile bool capture_done;

/* ---- Write state ---- */

static uint8_t write_prev_bit;

/* ---- CRC ---- */

uint16_t mfm_crc16(const uint8_t *data, uint16_t length, uint16_t crc) {
    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ MFM_CRC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* ---- Init ---- */

void mfm_init(void) {
    /* Timer1: Normal mode, no prescaler (16MHz clock) */
    TCCR1A = 0;
    TCCR1B = (1 << CS10);  /* No prescaler */

    /* ICP1 input on PB0 (D8), falling edge trigger, noise canceler on */
    TCCR1B &= ~(1 << ICES1);  /* Falling edge */
    TCCR1B |= (1 << ICNC1);   /* 4-sample noise canceler (debounce) */

    /* Disable capture interrupt initially */
    TIMSK1 &= ~(1 << ICIE1);
}

/* ---- Capture (ISR-driven, track → SRAM) ---- */

ISR(TIMER1_CAPT_vect) {
    uint16_t interval = ICR1 - prev_capture;
    prev_capture = ICR1;

    uint8_t code;
    if (interval < MFM_THRESHOLD_SHORT) {
        code = 0;  /* 2T */
    } else if (interval < MFM_THRESHOLD_MEDIUM) {
        code = 1;  /* 3T */
    } else if (interval < MFM_THRESHOLD_LONG) {
        code = 2;  /* 4T */
    } else {
        code = 3;  /* Invalid/gap */
    }

    pulse_pack = (pulse_pack << 2) | code;
    pulse_in_pack++;

    if (pulse_in_pack >= 4) {
        SPDR = pulse_pack;
        while (!(SPSR & (1 << SPIF)));
        capture_count++;
        pulse_in_pack = 0;
    }

    if (capture_count >= (SRAM_MFM_TRACK_SIZE - 16)) {
        TIMSK1 &= ~(1 << ICIE1);
        capture_done = true;
    }
}

int mfm_capture_track(void) {
    TRACE("[MFM] capture start\r\n");

    capture_count = 0;
    pulse_pack = 0;
    pulse_in_pack = 0;
    capture_done = false;

    sram_begin_seq_write(SRAM_MFM_TRACK);
    TIFR1 |= (1 << ICF1);
    prev_capture = ICR1;
    TIMSK1 |= (1 << ICIE1);
    sei();

    uint16_t timeout = 0;
    while (!capture_done && timeout < 500) {
        _delay_ms(1);
        timeout++;
    }

    TIMSK1 &= ~(1 << ICIE1);
    sram_end_seq();

    TRACE("[MFM] capture: ");
    uart_putdec((uint16_t)capture_count);
    TRACE(" packs, ");
    uart_putdec(timeout);
    TRACE("ms\r\n");

    if (timeout >= 500) {
        TRACE("[MFM] capture TIMEOUT!\r\n");
        return FLOPPY_ERR_TIMEOUT;
    }

    /* Pulse code histogram: count 2T/3T/4T/invalid distribution */
    {
        uint16_t hist[4] = {0, 0, 0, 0};
        sram_begin_seq_read(SRAM_MFM_TRACK);
        uint16_t scan = (capture_count > 500) ? 500 : (uint16_t)capture_count;
        for (uint16_t i = 0; i < scan; i++) {
            uint8_t packed = sram_seq_read_byte();
            for (int p = 3; p >= 0; p--) {
                hist[(packed >> (p * 2)) & 0x03]++;
            }
        }
        sram_end_seq();
        TRACE("[MFM] hist 2T=");
        uart_putdec(hist[0]);
        TRACE(" 3T=");
        uart_putdec(hist[1]);
        TRACE(" 4T=");
        uart_putdec(hist[2]);
        TRACE(" inv=");
        uart_putdec(hist[3]);
        TRACE("\r\n");
    }

    return FLOPPY_OK;
}

/* ---- Decode (SRAM → sector data) ---- */

int mfm_decode_sector(uint8_t sector, uint8_t *data_out) {
    TRACE("[MFM] decode sector ");
    uart_putdec(sector);
    TRACE(", scan ");
    uart_putdec((uint16_t)capture_count);
    TRACE(" packs\r\n");

    uint32_t read_pos = SRAM_MFM_TRACK;
    (void)capture_count;

    uint16_t shift_reg = 0;
    uint8_t bit_count = 0;
    uint8_t byte_val = 0;
    bool in_sync = false;
    uint8_t sync_count = 0;
    bool reading_id = false;
    bool reading_data = false;
    uint8_t field_bytes[6];
    uint16_t field_pos = 0;
    uint16_t data_pos = 0;
    bool found_sector = false;

    sram_begin_seq_read(read_pos);

    for (uint32_t i = 0; i < capture_count; i++) {
        uint8_t packed = sram_seq_read_byte();

        for (int p = 3; p >= 0; p--) {
            uint8_t code = (packed >> (p * 2)) & 0x03;
            uint8_t bits_to_add;

            switch (code) {
                case 0: bits_to_add = 2; shift_reg = (shift_reg << 2) | 0x01; break;
                case 1: bits_to_add = 3; shift_reg = (shift_reg << 3) | 0x01; break;
                case 2: bits_to_add = 4; shift_reg = (shift_reg << 4) | 0x01; break;
                default: bits_to_add = 0; shift_reg = 0; bit_count = 0;
                         in_sync = false; sync_count = 0;
                         reading_id = false; reading_data = false;
                         continue;
            }

            bit_count += bits_to_add;

            while (bit_count >= 2) {
                bit_count -= 2;
                uint8_t data_bit = (shift_reg >> bit_count) & 0x01;
                byte_val = (byte_val << 1) | data_bit;

                if (++field_pos % 8 == 0) {
                    if (!in_sync) {
                        if (byte_val == MFM_SYNC_BYTE) {
                            sync_count++;
                            if (sync_count >= 3) in_sync = true;
                        } else {
                            sync_count = 0;
                        }
                    } else if (reading_data) {
                        if (data_pos < 512) {
                            data_out[data_pos++] = byte_val;
                        }
                        if (data_pos >= 512) {
                            TRACE("[MFM] decode OK, 512 bytes\r\n");
                            sram_end_seq();
                            return 0;
                        }
                    } else if (reading_id) {
                        field_bytes[data_pos++] = byte_val;
                        if (data_pos >= 4) {
                            TRACE("[MFM] IDAM: T=");
                            uart_putdec(field_bytes[0]);
                            TRACE(" S=");
                            uart_putdec(field_bytes[1]);
                            TRACE(" R=");
                            uart_putdec(field_bytes[2]);
                            TRACE(" N=");
                            uart_putdec(field_bytes[3]);
                            if (field_bytes[2] == sector) {
                                TRACE(" MATCH!\r\n");
                                found_sector = true;
                            } else {
                                TRACE("\r\n");
                            }
                            reading_id = false;
                            data_pos = 0;
                            in_sync = false;
                            sync_count = 0;
                        }
                    } else {
                        if (byte_val == MFM_IDAM) {
                            reading_id = true;
                            data_pos = 0;
                        } else if (byte_val == MFM_DAM && found_sector) {
                            reading_data = true;
                            data_pos = 0;
                        } else {
                            in_sync = false;
                            sync_count = 0;
                        }
                    }
                    byte_val = 0;
                }
            }
        }
    }

    sram_end_seq();
    TRACE("[MFM] decode FAIL: sector ");
    uart_putdec(sector);
    TRACE(" not found\r\n");
    return FLOPPY_ERR_NO_SECTOR;
}

/* ---- Find sectors (SRAM → ID list) ---- */

int mfm_find_sectors(mfm_sector_id_t *ids_out, int max_ids) {
    uint32_t read_pos = SRAM_MFM_TRACK;
    int found = 0;

    uint16_t shift_reg = 0;
    uint8_t bit_count = 0;
    uint8_t byte_val = 0;
    bool in_sync = false;
    uint8_t sync_count = 0;
    bool reading_id = false;
    uint8_t id_bytes[4];
    uint8_t id_pos = 0;
    uint16_t field_pos = 0;

    sram_begin_seq_read(read_pos);

    for (uint32_t i = 0; i < capture_count; i++) {
        uint8_t packed = sram_seq_read_byte();

        for (int p = 3; p >= 0; p--) {
            uint8_t code = (packed >> (p * 2)) & 0x03;
            uint8_t bits_to_add;

            switch (code) {
                case 0: bits_to_add = 2; shift_reg = (shift_reg << 2) | 0x01; break;
                case 1: bits_to_add = 3; shift_reg = (shift_reg << 3) | 0x01; break;
                case 2: bits_to_add = 4; shift_reg = (shift_reg << 4) | 0x01; break;
                default: bits_to_add = 0; shift_reg = 0; bit_count = 0;
                         in_sync = false; sync_count = 0;
                         reading_id = false; continue;
            }

            bit_count += bits_to_add;

            while (bit_count >= 2) {
                bit_count -= 2;
                uint8_t data_bit = (shift_reg >> bit_count) & 0x01;
                byte_val = (byte_val << 1) | data_bit;

                if (++field_pos % 8 == 0) {
                    if (!in_sync) {
                        if (byte_val == MFM_SYNC_BYTE) {
                            sync_count++;
                            if (sync_count >= 3) in_sync = true;
                        } else {
                            sync_count = 0;
                        }
                    } else if (reading_id) {
                        id_bytes[id_pos++] = byte_val;
                        if (id_pos >= 4) {
                            if (found < max_ids) {
                                ids_out[found].track = id_bytes[0];
                                ids_out[found].side = id_bytes[1];
                                ids_out[found].sector = id_bytes[2];
                                ids_out[found].size_code = id_bytes[3];
                                ids_out[found].crc = 0;
                                found++;
                            }
                            reading_id = false;
                            in_sync = false;
                            sync_count = 0;
                        }
                    } else {
                        if (byte_val == MFM_IDAM) {
                            reading_id = true;
                            id_pos = 0;
                        } else {
                            in_sync = false;
                            sync_count = 0;
                        }
                    }
                    byte_val = 0;
                }
            }
        }
    }

    sram_end_seq();

    TRACE("[MFM] find_sectors: ");
    uart_putdec(found);
    TRACE(" found\r\n");

    return found;
}

/* ---- Write helpers ---- */

/*
 * /WDATA pulse: short negative pulse (~300ns).
 * Drive detects falling edge as flux transition.
 */
static inline void wdata_pulse(void) {
    FLOPPY_WDATA_PORT &= ~(1 << FLOPPY_WDATA_PIN);
    __builtin_avr_delay_cycles(5);  /* ~300ns at 16MHz */
    FLOPPY_WDATA_PORT |= (1 << FLOPPY_WDATA_PIN);
}

/*
 * Wait for one MFM bit cell (2µs = 32 Timer1 ticks at 16MHz).
 * Uses OC1A compare match for jitter-free timing.
 */
static inline void wait_bit_cell(void) {
    while (!(TIFR1 & (1 << OCF1A)));
    TIFR1 = (1 << OCF1A);
    OCR1A += MFM_TICKS_PER_CELL_HD;
}

/*
 * Write one data byte as MFM, tracking clock insertion.
 * MFM encoding: clock bit inserted when prev_data=0 AND current_data=0.
 */
static void mfm_write_data_byte(uint8_t byte) {
    for (int8_t i = 7; i >= 0; i--) {
        uint8_t data_bit = (byte >> i) & 1;

        /* Clock bit cell */
        wait_bit_cell();
        if (!write_prev_bit && !data_bit) {
            wdata_pulse();
        }

        /* Data bit cell */
        wait_bit_cell();
        if (data_bit) {
            wdata_pulse();
        }

        write_prev_bit = data_bit;
    }
}

/*
 * Write MFM sync byte 0xA1 with missing clock bit.
 * Standard MFM for 0xA1: 0100_0100_1010_1001
 * Sync variant:           0100_0100_1000_1001 (clock bit 5 missing)
 * Hex: 0x4489
 */
static void mfm_write_sync_a1(void) {
    uint16_t pattern = 0x4489;
    for (int8_t i = 15; i >= 0; i--) {
        wait_bit_cell();
        if (pattern & (1U << i)) {
            wdata_pulse();
        }
    }
    write_prev_bit = 1;  /* Last data bit of 0xA1 is 1 */
}

/*
 * Poll ICP1 for one flux transition, classify interval.
 * Returns pulse code (0=2T, 1=3T, 2=4T, 3=invalid).
 * Used for real-time sector search during write positioning.
 */
static uint8_t mfm_poll_pulse(void) {
    TIFR1 = (1 << ICF1);
    uint16_t timeout = 0;
    while (!(TIFR1 & (1 << ICF1))) {
        if (++timeout > 50000) return 3;
    }
    uint16_t interval = ICR1 - prev_capture;
    prev_capture = ICR1;

    if (interval < MFM_THRESHOLD_SHORT) return 0;
    if (interval < MFM_THRESHOLD_MEDIUM) return 1;
    if (interval < MFM_THRESHOLD_LONG) return 2;
    return 3;
}

/*
 * Scan /RDATA in real-time (polling) for target sector IDAM.
 * Returns FLOPPY_OK when positioned right after the 4-byte sector ID.
 * Caller should then wait through CRC+gap2 before writing.
 */
static int mfm_wait_for_sector(uint8_t target_sector) {
    TRACE("[MFM] scan for sector ");
    uart_putdec(target_sector);
    TRACE("...\r\n");

    prev_capture = ICR1;

    uint16_t shift_reg = 0;
    uint8_t bit_count = 0;
    uint8_t byte_val = 0;
    uint8_t byte_bits = 0;
    uint8_t sync_count = 0;
    bool in_sync = false;
    bool reading_id = false;
    uint8_t id_bytes[4];
    uint8_t id_pos = 0;

    /* Scan up to ~2 revolutions (80000 pulses) */
    for (uint32_t n = 0; n < 80000; n++) {
        uint8_t code = mfm_poll_pulse();

        uint8_t bits_to_add;
        switch (code) {
            case 0: bits_to_add = 2; shift_reg = (shift_reg << 2) | 0x01; break;
            case 1: bits_to_add = 3; shift_reg = (shift_reg << 3) | 0x01; break;
            case 2: bits_to_add = 4; shift_reg = (shift_reg << 4) | 0x01; break;
            default: in_sync = false; sync_count = 0; reading_id = false;
                     bit_count = 0; continue;
        }
        bit_count += bits_to_add;

        while (bit_count >= 2) {
            bit_count -= 2;
            uint8_t data_bit = (shift_reg >> bit_count) & 0x01;
            byte_val = (byte_val << 1) | data_bit;
            byte_bits++;

            if (byte_bits >= 8) {
                byte_bits = 0;

                if (!in_sync) {
                    if (byte_val == MFM_SYNC_BYTE) {
                        sync_count++;
                        if (sync_count >= 3) in_sync = true;
                    } else {
                        sync_count = 0;
                    }
                } else if (reading_id) {
                    id_bytes[id_pos++] = byte_val;
                    if (id_pos >= 4) {
                        if (id_bytes[2] == target_sector) {
                            TRACE("[MFM] found sector ");
                            uart_putdec(target_sector);
                            TRACE(" at T=");
                            uart_putdec(id_bytes[0]);
                            TRACE("\r\n");
                            return FLOPPY_OK;
                        }
                        reading_id = false;
                        in_sync = false;
                        sync_count = 0;
                    }
                } else {
                    if (byte_val == MFM_IDAM) {
                        reading_id = true;
                        id_pos = 0;
                    } else {
                        in_sync = false;
                        sync_count = 0;
                    }
                }
                byte_val = 0;
            }
        }
    }

    TRACE("[MFM] sector scan timeout\r\n");
    return FLOPPY_ERR_TIMEOUT;
}

/* ---- Write sector ---- */

int mfm_write_sector(const mfm_sector_id_t *id, const uint8_t *data) {
    /*
     * Write data field of an existing sector:
     * 1. Poll /RDATA to find target IDAM in real-time
     * 2. Wait through ID CRC (2 bytes) + gap2 (22 bytes) = 768µs
     * 3. Assert /WGATE, disable interrupts
     * 4. Write: 12×0x00 + 3×sync_A1 + DAM + 512×data + CRC + gap3
     * 5. Release /WGATE, re-enable interrupts
     */
    TRACE("[MFM] write T=");
    uart_putdec(id->track);
    TRACE(" S=");
    uart_putdec(id->side);
    TRACE(" R=");
    uart_putdec(id->sector);
    TRACE("\r\n");

    /* Step 1: Find sector IDAM by polling /RDATA */
    int rc = mfm_wait_for_sector(id->sector);
    if (rc != FLOPPY_OK) return rc;

    /* Step 2: Wait through ID CRC (2 bytes) + gap2 (22 bytes)
     * = 24 data bytes × 32µs/byte = 768µs */
    _delay_us(768);

    /* Step 3: Assert /WGATE (via shift register) */
    shiftreg_assert_bit(SR_BIT_WGATE);

    cli();

    /* Initialize Timer1 OC1A for precise bit-cell timing */
    write_prev_bit = 0;
    OCR1A = TCNT1 + MFM_TICKS_PER_CELL_HD;
    TIFR1 = (1 << OCF1A);

    /* Step 4: Write data field */
    /* Preamble: 12 × 0x00 */
    for (uint8_t i = 0; i < 12; i++) {
        mfm_write_data_byte(0x00);
    }

    /* Sync: 3 × 0xA1 (with missing clock) */
    mfm_write_sync_a1();
    mfm_write_sync_a1();
    mfm_write_sync_a1();

    /* DAM */
    mfm_write_data_byte(MFM_DAM);

    /* CRC starts from sync bytes + DAM */
    uint16_t crc = MFM_CRC_INIT;
    uint8_t sync_byte = MFM_SYNC_BYTE;
    crc = mfm_crc16(&sync_byte, 1, crc);
    crc = mfm_crc16(&sync_byte, 1, crc);
    crc = mfm_crc16(&sync_byte, 1, crc);
    uint8_t dam_byte = MFM_DAM;
    crc = mfm_crc16(&dam_byte, 1, crc);

    /* 512 data bytes */
    for (uint16_t i = 0; i < 512; i++) {
        mfm_write_data_byte(data[i]);
        crc = mfm_crc16(&data[i], 1, crc);
    }

    /* CRC (2 bytes, MSB first) */
    uint8_t crc_hi = (crc >> 8) & 0xFF;
    uint8_t crc_lo = crc & 0xFF;
    mfm_write_data_byte(crc_hi);
    mfm_write_data_byte(crc_lo);

    /* Gap3: 54 × 0x4E (HD standard) */
    for (uint8_t i = 0; i < 54; i++) {
        mfm_write_data_byte(0x4E);
    }

    sei();

    /* Step 5: Release /WGATE */
    shiftreg_release_bit(SR_BIT_WGATE);

    TRACE("[MFM] write OK\r\n");
    return FLOPPY_OK;
}

#endif /* __AVR__ */
