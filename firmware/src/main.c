#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "floppy_ctrl.h"
#include "mfm_codec.h"
#include "fat12.h"
#include "iec_protocol.h"
#include "cbm_dos.h"

/*
 * 64korppu - C64 IEC ↔ PC 1.44MB floppy bridge
 *
 * Core 0: IEC bus protocol + FAT12 + CBM-DOS
 * Core 1: Floppy drive control + MFM encoding/decoding
 */

/* Inter-core command queue */
typedef enum {
    FLOPPY_CMD_NONE = 0,
    FLOPPY_CMD_READ_SECTOR,
    FLOPPY_CMD_WRITE_SECTOR,
    FLOPPY_CMD_FORMAT_TRACK,
    FLOPPY_CMD_MOTOR_ON,
    FLOPPY_CMD_MOTOR_OFF,
    FLOPPY_CMD_RECALIBRATE,
} floppy_cmd_t;

typedef struct {
    floppy_cmd_t cmd;
    uint8_t track;
    uint8_t side;
    uint8_t sector;
    int result;
    volatile bool busy;
    volatile bool done;
} floppy_request_t;

static volatile floppy_request_t floppy_req = {0};

/* Shared sector buffer between cores */
static uint8_t shared_sector_buf[FLOPPY_SECTOR_SIZE];

/*
 * Core 1 entry point: floppy drive control loop.
 * Waits for commands from core 0 and executes them.
 */
static void core1_floppy_task(void) {
    floppy_init();
    mfm_init();

    while (true) {
        if (floppy_req.busy && !floppy_req.done) {
            int result = FLOPPY_OK;

            switch (floppy_req.cmd) {
                case FLOPPY_CMD_READ_SECTOR:
                    result = floppy_read_sector(
                        floppy_req.track, floppy_req.side,
                        floppy_req.sector, shared_sector_buf);
                    break;

                case FLOPPY_CMD_WRITE_SECTOR:
                    result = floppy_write_sector(
                        floppy_req.track, floppy_req.side,
                        floppy_req.sector, shared_sector_buf);
                    break;

                case FLOPPY_CMD_MOTOR_ON:
                    floppy_motor_on();
                    break;

                case FLOPPY_CMD_MOTOR_OFF:
                    floppy_motor_off();
                    break;

                case FLOPPY_CMD_RECALIBRATE:
                    result = floppy_recalibrate();
                    break;

                default:
                    break;
            }

            floppy_req.result = result;
            floppy_req.done = true;
            __dmb();  /* Ensure writes are visible to core 0 */
        }

        tight_loop_contents();
    }
}

/*
 * Submit a floppy command to core 1 and wait for completion.
 */
static int floppy_submit_and_wait(floppy_cmd_t cmd, uint8_t track,
                                   uint8_t side, uint8_t sector) {
    floppy_req.cmd = cmd;
    floppy_req.track = track;
    floppy_req.side = side;
    floppy_req.sector = sector;
    floppy_req.done = false;
    __dmb();
    floppy_req.busy = true;

    /* Wait for core 1 to complete */
    while (!floppy_req.done) {
        tight_loop_contents();
    }

    floppy_req.busy = false;
    return floppy_req.result;
}

/*
 * Disk I/O interface for FAT12 layer.
 * These are called from core 0 and delegate to core 1.
 */
int disk_read_sector(uint16_t lba, uint8_t *buf) {
    uint8_t track, side, sector;
    floppy_lba_to_chs(lba, &track, &side, &sector);

    int result = floppy_submit_and_wait(FLOPPY_CMD_READ_SECTOR, track, side, sector);
    if (result == FLOPPY_OK) {
        memcpy(buf, shared_sector_buf, FLOPPY_SECTOR_SIZE);
    }
    return result;
}

int disk_write_sector(uint16_t lba, const uint8_t *buf) {
    uint8_t track, side, sector;
    floppy_lba_to_chs(lba, &track, &side, &sector);

    memcpy(shared_sector_buf, buf, FLOPPY_SECTOR_SIZE);
    return floppy_submit_and_wait(FLOPPY_CMD_WRITE_SECTOR, track, side, sector);
}

int main(void) {
    stdio_init_all();

    printf("\n=== 64korppu v1.0 ===\n");
    printf("C64 IEC <-> PC 1.44MB Floppy Bridge\n\n");

    /* Launch core 1 for floppy control */
    multicore_launch_core1(core1_floppy_task);

    /* Wait for core 1 to initialize */
    sleep_ms(100);

    /* Initialize floppy drive */
    printf("Initializing floppy drive...\n");
    int result = floppy_submit_and_wait(FLOPPY_CMD_MOTOR_ON, 0, 0, 0);
    if (result != FLOPPY_OK) {
        printf("Motor start failed: %d\n", result);
    }

    result = floppy_submit_and_wait(FLOPPY_CMD_RECALIBRATE, 0, 0, 0);
    if (result == FLOPPY_OK) {
        printf("Drive recalibrated OK\n");
    } else {
        printf("Recalibrate failed: %d\n", result);
    }

    /* Mount FAT12 filesystem */
    printf("Mounting FAT12...\n");
    result = fat12_mount();
    if (result == FAT12_OK) {
        printf("FAT12 mounted OK\n");
        uint32_t free = fat12_free_space();
        printf("Free space: %lu bytes\n", (unsigned long)free);
    } else {
        printf("FAT12 mount failed: %d (insert formatted disk)\n", result);
    }

    /* Initialize IEC bus and CBM-DOS */
    printf("Starting IEC device #8...\n");
    iec_init(IEC_DEFAULT_DEVICE);
    cbm_dos_init();
    printf("Ready. Waiting for C64...\n\n");

    /* Main loop: service IEC bus */
    while (true) {
        iec_service();
    }

    return 0;
}
