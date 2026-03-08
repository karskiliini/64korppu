#ifndef IEC_PROTOCOL_H
#define IEC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Commodore IEC serial bus protocol implementation.
 *
 * IEC bus signals (active low, open collector):
 *   ATN  - Attention (active when controller sends commands)
 *   CLK  - Clock
 *   DATA - Data
 *   SRQ  - Service Request (not used here)
 *   RESET - Bus reset
 *
 * This module emulates a CBM disk drive (device #8 by default).
 * All bus signals are active low and open-collector:
 * - To assert: drive pin LOW (pull to ground)
 * - To release: set pin as input (pull-up brings it HIGH)
 * - To read: read pin state (LOW = asserted by someone)
 */

/* GPIO pin assignments for IEC bus (accent through 74LVC245 level shifter) */
#define IEC_PIN_ATN     2
#define IEC_PIN_CLK     3
#define IEC_PIN_DATA    4
#define IEC_PIN_RESET   5

/* Default device number */
#define IEC_DEFAULT_DEVICE  8

/* IEC bus commands (under ATN) */
#define IEC_CMD_LISTEN      0x20    /* LISTEN + device (0x20-0x3E) */
#define IEC_CMD_UNLISTEN    0x3F
#define IEC_CMD_TALK        0x40    /* TALK + device (0x40-0x5E) */
#define IEC_CMD_UNTALK      0x5F
#define IEC_CMD_OPEN        0x60    /* OPEN/secondary address (0x60-0x6F) */
#define IEC_CMD_CLOSE       0xE0    /* CLOSE (0xE0-0xEF) */
#define IEC_CMD_DATA        0x60    /* Data channel (same as OPEN) */

/* Secondary addresses */
#define IEC_SA_LOAD         0       /* LOAD uses SA 0 */
#define IEC_SA_SAVE         1       /* SAVE uses SA 1 */
#define IEC_SA_COMMAND     15       /* Command/error channel */

/* Bus states */
typedef enum {
    IEC_STATE_IDLE,
    IEC_STATE_LISTENER,          /* We are addressed as listener */
    IEC_STATE_TALKER,            /* We are addressed as talker */
} iec_state_t;

/* Channel state for secondary addresses 0-15 */
#define IEC_NUM_CHANNELS    16
typedef struct {
    bool     open;
    char     filename[42];       /* Filename from OPEN command */
    uint8_t  filename_len;
    uint8_t  buffer[256];        /* Channel data buffer */
    uint16_t buf_len;            /* Data length in buffer */
    uint16_t buf_pos;            /* Current position in buffer */
    bool     eof;                /* End of file reached */
} iec_channel_t;

/* IEC device state */
typedef struct {
    uint8_t       device_number;
    iec_state_t   state;
    uint8_t       current_sa;     /* Current secondary address */
    bool          atn_active;
    iec_channel_t channels[IEC_NUM_CHANNELS];
    /* Error channel (SA 15) status */
    uint8_t       error_code;
    char          error_msg[64];
} iec_device_t;

/**
 * Initialize IEC bus GPIO pins and device state.
 * @param device_num  Device number (typically 8).
 */
void iec_init(uint8_t device_num);

/**
 * Main IEC bus service loop. Call this repeatedly from core 0.
 * Handles ATN, receives commands, dispatches to channel handlers.
 */
void iec_service(void);

/**
 * Release all IEC bus signals (set as inputs).
 */
void iec_release_all(void);

/* Low-level bus operations */

/**
 * Assert (pull low) a bus signal.
 */
static inline void iec_assert(uint8_t pin);

/**
 * Release (set high-Z) a bus signal.
 */
static inline void iec_release(uint8_t pin);

/**
 * Read the state of a bus signal.
 * @return true if line is LOW (asserted).
 */
static inline bool iec_is_asserted(uint8_t pin);

/**
 * Receive a byte under ATN from the bus controller (C64).
 * @param byte  Output: received byte.
 * @return true on success, false on error/timeout.
 */
bool iec_receive_byte_atn(uint8_t *byte);

/**
 * Receive a byte (data phase, not under ATN).
 * @param byte  Output: received byte.
 * @param eoi   Output: true if EOI was signaled.
 * @return true on success, false on error/timeout.
 */
bool iec_receive_byte(uint8_t *byte, bool *eoi);

/**
 * Send a byte as talker.
 * @param byte  Byte to send.
 * @param eoi   If true, signal EOI (last byte).
 * @return true on success, false on error/timeout.
 */
bool iec_send_byte(uint8_t byte, bool eoi);

/**
 * Set error status on the error channel (SA 15).
 * @param code  CBM-DOS error code.
 * @param msg   Error message text.
 * @param track Track number (or 0).
 * @param sector Sector number (or 0).
 */
void iec_set_error(uint8_t code, const char *msg, uint8_t track, uint8_t sector);

#endif /* IEC_PROTOCOL_H */
