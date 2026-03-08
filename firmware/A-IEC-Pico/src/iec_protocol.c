#include "iec_protocol.h"
#include "cbm_dos.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include <string.h>
#include <stdio.h>

/*
 * Commodore IEC serial bus protocol implementation.
 *
 * The IEC bus is a active-low open-collector bus. To drive a line:
 *   Assert (LOW):  set GPIO as output, drive LOW
 *   Release (HIGH): set GPIO as input (external pull-up brings HIGH)
 *
 * The 74LVC245 level shifter handles 3.3V (Pico) ↔ 5V (C64) conversion.
 * Direction control on the 245 must be handled carefully for bidirectional
 * signals (ATN, CLK, DATA).
 *
 * Protocol timing (standard IEC, non-JiffyDOS):
 *   Bit time:   ~70us per bit (CLK-driven)
 *   Byte time:  ~560us + overhead
 *   Throughput: ~1-3 KB/s
 */

/* IEC bus timing constants (microseconds) */
#define IEC_TIMING_ATN_RESPONSE     1000   /* Max time to respond to ATN */
#define IEC_TIMING_LISTENER_HOLD      80   /* Listener hold-off time */
#define IEC_TIMING_TALKER_SETUP       80   /* Talker bit setup time */
#define IEC_TIMING_CLK_LOW            60   /* Clock low time for bit */
#define IEC_TIMING_CLK_HIGH           60   /* Clock high time for bit */
#define IEC_TIMING_EOI_TIMEOUT       200   /* EOI detection timeout */
#define IEC_TIMING_EOI_ACK            60   /* EOI acknowledge pulse */
#define IEC_TIMING_BETWEEN_BYTES     100   /* Time between bytes */
#define IEC_TIMEOUT_US            10000    /* General timeout */

static iec_device_t device = {0};

/* Inline bus operations */
static inline void iec_assert(uint8_t pin) {
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
}

static inline void iec_release(uint8_t pin) {
    gpio_set_dir(pin, GPIO_IN);
    /* Pull-up brings line HIGH */
}

static inline bool iec_is_asserted(uint8_t pin) {
    return !gpio_get(pin);  /* Active low: LOW = asserted */
}

void iec_init(uint8_t device_num) {
    device.device_number = device_num;
    device.state = IEC_STATE_IDLE;

    /* Configure IEC pins as inputs with pull-ups initially */
    uint8_t pins[] = {IEC_PIN_ATN, IEC_PIN_CLK, IEC_PIN_DATA, IEC_PIN_RESET};
    for (int i = 0; i < 4; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }

    /* Initialize all channels */
    memset(device.channels, 0, sizeof(device.channels));

    /* Set initial status */
    iec_set_error(CBM_ERR_DOS_MISMATCH, CBM_DOS_ID, 0, 0);
}

void iec_release_all(void) {
    iec_release(IEC_PIN_CLK);
    iec_release(IEC_PIN_DATA);
}

/*
 * Wait for ATN to be asserted (C64 wants to send a command).
 * Returns true if ATN is asserted.
 */
static bool wait_for_atn(void) {
    return iec_is_asserted(IEC_PIN_ATN);
}

bool iec_receive_byte_atn(uint8_t *byte) {
    /*
     * Receive a byte while ATN is asserted (command byte from C64).
     *
     * Protocol:
     * 1. Controller (C64) asserts ATN and CLK
     * 2. We (device) assert DATA to signal "ready"
     * 3. Controller releases CLK
     * 4. For each bit (LSB first):
     *    a. Controller asserts CLK (data is valid)
     *    b. We read DATA
     *    c. Controller releases CLK
     * 5. After 8 bits, we release DATA
     */

    *byte = 0;

    /* Signal we're ready by asserting DATA */
    iec_assert(IEC_PIN_DATA);
    sleep_us(IEC_TIMING_LISTENER_HOLD);

    /* Wait for controller to release CLK (ready to send) */
    uint64_t timeout = time_us_64() + IEC_TIMEOUT_US;
    while (iec_is_asserted(IEC_PIN_CLK)) {
        if (time_us_64() > timeout) return false;
    }

    /* Release DATA to signal we're ready to receive */
    iec_release(IEC_PIN_DATA);

    /* Receive 8 bits, LSB first */
    for (int bit = 0; bit < 8; bit++) {
        /* Wait for CLK to be asserted (data valid) */
        timeout = time_us_64() + IEC_TIMEOUT_US;
        while (!iec_is_asserted(IEC_PIN_CLK)) {
            if (time_us_64() > timeout) return false;
        }

        /* Read data bit */
        if (!iec_is_asserted(IEC_PIN_DATA)) {
            *byte |= (1 << bit);
        }

        /* Wait for CLK to be released */
        timeout = time_us_64() + IEC_TIMEOUT_US;
        while (iec_is_asserted(IEC_PIN_CLK)) {
            if (time_us_64() > timeout) return false;
        }
    }

    /* Acknowledge by asserting DATA */
    iec_assert(IEC_PIN_DATA);
    sleep_us(IEC_TIMING_BETWEEN_BYTES);

    return true;
}

bool iec_receive_byte(uint8_t *byte, bool *eoi) {
    /*
     * Receive a byte during data phase (no ATN).
     *
     * EOI detection: if talker doesn't assert CLK within 200us,
     * it signals EOI (last byte). We acknowledge by pulsing DATA.
     */

    *byte = 0;
    *eoi = false;

    /* Release DATA to signal ready */
    iec_release(IEC_PIN_DATA);

    /* Wait for talker (C64) to assert CLK, with EOI detection */
    uint64_t start = time_us_64();
    while (!iec_is_asserted(IEC_PIN_CLK)) {
        if (time_us_64() - start > IEC_TIMING_EOI_TIMEOUT) {
            /* EOI: talker is signaling last byte */
            *eoi = true;

            /* Acknowledge EOI by pulsing DATA */
            iec_assert(IEC_PIN_DATA);
            sleep_us(IEC_TIMING_EOI_ACK);
            iec_release(IEC_PIN_DATA);

            /* Now wait for CLK normally */
            uint64_t timeout = time_us_64() + IEC_TIMEOUT_US;
            while (!iec_is_asserted(IEC_PIN_CLK)) {
                if (time_us_64() > timeout) return false;
            }
            break;
        }
        if (iec_is_asserted(IEC_PIN_ATN)) return false;  /* ATN abort */
    }

    /* Receive 8 bits, LSB first */
    for (int bit = 0; bit < 8; bit++) {
        /* Wait for CLK asserted */
        uint64_t timeout = time_us_64() + IEC_TIMEOUT_US;
        while (!iec_is_asserted(IEC_PIN_CLK)) {
            if (time_us_64() > timeout) return false;
        }

        /* Read data bit */
        if (!iec_is_asserted(IEC_PIN_DATA)) {
            *byte |= (1 << bit);
        }

        /* Wait for CLK released */
        timeout = time_us_64() + IEC_TIMEOUT_US;
        while (iec_is_asserted(IEC_PIN_CLK)) {
            if (time_us_64() > timeout) return false;
        }
    }

    /* Acknowledge by asserting DATA */
    iec_assert(IEC_PIN_DATA);

    return true;
}

bool iec_send_byte(uint8_t byte, bool eoi) {
    /*
     * Send a byte as talker.
     *
     * 1. We assert CLK, release DATA
     * 2. Wait for listener to release DATA (ready)
     * 3. If EOI: release CLK, wait for listener's EOI ack (DATA pulse)
     * 4. For each bit (LSB first):
     *    a. Set DATA to bit value
     *    b. Release CLK (data valid signal)
     *    c. Wait, then assert CLK
     */

    /* Hold CLK, release DATA */
    iec_assert(IEC_PIN_CLK);
    iec_release(IEC_PIN_DATA);
    sleep_us(IEC_TIMING_TALKER_SETUP);

    /* Wait for listener to be ready (DATA released) */
    uint64_t timeout = time_us_64() + IEC_TIMEOUT_US;
    while (iec_is_asserted(IEC_PIN_DATA)) {
        if (time_us_64() > timeout) return false;
        if (iec_is_asserted(IEC_PIN_ATN)) return false;
    }

    /* EOI signaling */
    if (eoi) {
        /* Release CLK to signal EOI */
        iec_release(IEC_PIN_CLK);

        /* Wait for listener's EOI acknowledge (DATA pulse) */
        timeout = time_us_64() + IEC_TIMEOUT_US;
        while (!iec_is_asserted(IEC_PIN_DATA)) {
            if (time_us_64() > timeout) return false;
        }
        timeout = time_us_64() + IEC_TIMEOUT_US;
        while (iec_is_asserted(IEC_PIN_DATA)) {
            if (time_us_64() > timeout) return false;
        }

        /* Re-assert CLK */
        iec_assert(IEC_PIN_CLK);
        sleep_us(IEC_TIMING_TALKER_SETUP);
    }

    /* Send 8 bits, LSB first */
    for (int bit = 0; bit < 8; bit++) {
        /* Set data bit */
        if (byte & (1 << bit)) {
            iec_release(IEC_PIN_DATA);
        } else {
            iec_assert(IEC_PIN_DATA);
        }
        sleep_us(IEC_TIMING_CLK_LOW);

        /* Clock the bit: release CLK */
        iec_release(IEC_PIN_CLK);
        sleep_us(IEC_TIMING_CLK_HIGH);

        /* Re-assert CLK */
        iec_assert(IEC_PIN_CLK);
    }

    /* Release DATA */
    iec_release(IEC_PIN_DATA);

    /* Wait for listener acknowledge (DATA asserted) */
    timeout = time_us_64() + IEC_TIMEOUT_US;
    while (!iec_is_asserted(IEC_PIN_DATA)) {
        if (time_us_64() > timeout) return false;
    }

    sleep_us(IEC_TIMING_BETWEEN_BYTES);
    return true;
}

void iec_set_error(uint8_t code, const char *msg, uint8_t track, uint8_t sector) {
    device.error_code = code;

    /* Format error string: "XX, MESSAGE,TT,SS\r" */
    int len = cbm_dos_format_error(code, msg, track, sector,
                                    device.error_msg, sizeof(device.error_msg));

    /* Store in channel 15 buffer */
    iec_channel_t *ch = &device.channels[IEC_SA_COMMAND];
    memcpy(ch->buffer, device.error_msg, len);
    ch->buf_len = len;
    ch->buf_pos = 0;
    ch->eof = false;
}

/*
 * Main IEC bus service routine.
 * Monitors ATN and handles commands from the C64.
 */
void iec_service(void) {
    /* Check for bus reset */
    if (iec_is_asserted(IEC_PIN_RESET)) {
        device.state = IEC_STATE_IDLE;
        iec_release_all();
        /* Wait for reset to be released */
        while (iec_is_asserted(IEC_PIN_RESET)) {
            tight_loop_contents();
        }
        sleep_ms(100);
        return;
    }

    /* Check for ATN */
    if (!wait_for_atn()) {
        /* No ATN - handle ongoing talker/listener operations */

        if (device.state == IEC_STATE_TALKER) {
            /* We're talking: send data bytes */
            uint8_t byte;
            bool eoi;
            if (cbm_dos_talk_byte(device.current_sa, &byte, &eoi)) {
                if (!iec_send_byte(byte, eoi)) {
                    device.state = IEC_STATE_IDLE;
                    iec_release_all();
                }
                if (eoi) {
                    device.state = IEC_STATE_IDLE;
                    iec_release_all();
                }
            }
        }

        return;
    }

    /*
     * ATN is asserted - receive command bytes.
     * All devices must listen when ATN is asserted.
     */
    iec_assert(IEC_PIN_DATA);  /* Acknowledge ATN */

    uint8_t cmd;
    while (iec_is_asserted(IEC_PIN_ATN)) {
        if (!iec_receive_byte_atn(&cmd)) {
            break;
        }

        /* Decode command */
        uint8_t device_num = cmd & 0x1F;

        if (cmd == IEC_CMD_UNLISTEN) {
            if (device.state == IEC_STATE_LISTENER) {
                /* Process received data */
                iec_channel_t *ch = &device.channels[device.current_sa];
                if (device.current_sa == IEC_SA_COMMAND && ch->buf_len > 0) {
                    /* Command channel: execute command */
                    ch->buffer[ch->buf_len] = '\0';
                    cbm_dos_execute_command((char *)ch->buffer, ch->buf_len);
                    ch->buf_len = 0;
                } else if (device.current_sa == IEC_SA_SAVE) {
                    /* SAVE: close file */
                    cbm_dos_close(device.current_sa);
                }
                device.state = IEC_STATE_IDLE;
                iec_release_all();
            }
        } else if (cmd == IEC_CMD_UNTALK) {
            if (device.state == IEC_STATE_TALKER) {
                device.state = IEC_STATE_IDLE;
                iec_release_all();
            }
        } else if ((cmd & 0xE0) == IEC_CMD_LISTEN) {
            /* LISTEN command */
            if (device_num == device.device_number) {
                device.state = IEC_STATE_LISTENER;
            }
        } else if ((cmd & 0xE0) == IEC_CMD_TALK) {
            /* TALK command */
            if (device_num == device.device_number) {
                device.state = IEC_STATE_TALKER;
            }
        } else if ((cmd & 0xF0) == IEC_CMD_OPEN) {
            /* Secondary address / OPEN */
            uint8_t sa = cmd & 0x0F;
            device.current_sa = sa;

            if (device.state == IEC_STATE_LISTENER) {
                /* Prepare to receive filename/data */
                iec_channel_t *ch = &device.channels[sa];
                ch->buf_len = 0;
                ch->buf_pos = 0;
            } else if (device.state == IEC_STATE_TALKER) {
                /* Prepare to send data - turnaround */
                iec_release(IEC_PIN_DATA);
                sleep_us(IEC_TIMING_TALKER_SETUP);
                iec_assert(IEC_PIN_CLK);  /* We now control CLK as talker */
            }
        } else if ((cmd & 0xF0) == IEC_CMD_CLOSE) {
            uint8_t sa = cmd & 0x0F;
            cbm_dos_close(sa);
        }
    }

    /* ATN released */
    if (device.state == IEC_STATE_LISTENER) {
        /* Continue receiving data bytes */
        uint8_t byte;
        bool eoi;
        iec_channel_t *ch = &device.channels[device.current_sa];

        while (true) {
            if (iec_is_asserted(IEC_PIN_ATN)) break;  /* New ATN */

            if (!iec_receive_byte(&byte, &eoi)) break;

            /* First bytes after OPEN are the filename */
            if (!ch->open && device.current_sa != IEC_SA_COMMAND) {
                if (ch->filename_len < sizeof(ch->filename) - 1) {
                    ch->filename[ch->filename_len++] = byte;
                }
                if (eoi) {
                    ch->filename[ch->filename_len] = '\0';
                    cbm_dos_open(device.current_sa, ch->filename, ch->filename_len);
                    ch->open = true;
                }
            } else {
                /* Data byte for an open channel */
                if (ch->buf_len < sizeof(ch->buffer)) {
                    ch->buffer[ch->buf_len++] = byte;
                }
                cbm_dos_listen_byte(device.current_sa, byte);
            }

            if (eoi) break;
        }
    }
}
