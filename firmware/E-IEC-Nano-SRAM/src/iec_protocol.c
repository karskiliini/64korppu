#include "iec_protocol.h"
#include "cbm_dos.h"
#include "config.h"
#include "fastload.h"
#include "fastload_jiffydos.h"
#include "floppy_ctrl.h"
#include "fat12.h"
#include "uart.h"
#include "led_debug.h"

#ifdef __AVR__

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>

/*
 * IEC serial bus protocol for ATmega328P.
 *
 * Direct 5V GPIO — no level shifters needed.
 * Open-collector emulation:
 *   Assert: set as OUTPUT, drive LOW
 *   Release: set as INPUT (external/internal pull-up → HIGH)
 */

static iec_device_t device = {0};
static volatile bool atn_pending = false;
static bool reset_held = false;  /* Edge detection for /RESET */

/* Pin manipulation macros.
 * Assert order matters: clear PORT BEFORE setting DDR — the other way
 * round the pin drives HIGH for a moment (PORT still 1 from the
 * pull-up), actively fighting any device pulling the line low. */
#define IEC_ASSERT(pin)   do { IEC_PORT &= ~(1 << (pin)); IEC_DDR |= (1 << (pin)); } while(0)
#define IEC_RELEASE(pin)  do { IEC_DDR &= ~(1 << (pin)); IEC_PORT |= (1 << (pin)); } while(0)
#define IEC_IS_LOW(pin)   (!(IEC_PINR & (1 << (pin))))

/*
 * INT0 ISR: immediate ATN acknowledge.
 *
 * When C64 asserts ATN, this ISR fires within ~1µs and pulls DATA low.
 * This tells C64 "device #8 is present on the bus" and prevents
 * "?DEVICE NOT PRESENT" errors even during blocking floppy operations.
 *
 * The full IEC command handling happens later in iec_service() or iec_poll().
 */
ISR(INT0_vect) {
    IEC_ASSERT(IEC_PIN_DATA);
    atn_pending = true;
}

void iec_init(uint8_t device_num) {
    TRACE("[IEC] init dev #");
    uart_putdec(device_num);
    TRACE("\r\n");

    device.device_number = device_num;
    device.state = IEC_STATE_IDLE;
    atn_pending = false;

    /* All IEC pins as inputs with pull-ups */
    IEC_DDR &= ~((1 << IEC_PIN_ATN) | (1 << IEC_PIN_CLK) |
                  (1 << IEC_PIN_DATA) | (1 << IEC_PIN_RESET));
    IEC_PORT |= (1 << IEC_PIN_ATN) | (1 << IEC_PIN_CLK) |
                (1 << IEC_PIN_DATA) | (1 << IEC_PIN_RESET);

    /* INT0 (ATN on PD2): falling edge trigger */
    EICRA = (EICRA & ~((1 << ISC01) | (1 << ISC00))) |
            (1 << ISC01);   /* Falling edge */
    EIFR  = (1 << INTF0);  /* Clear pending */
    EIMSK |= (1 << INT0);  /* Enable INT0 */

    /* Read initial bus state */
    uint8_t pins = IEC_PINR;
    TRACE("[IEC] bus: ATN=");
    uart_putchar((pins & (1 << IEC_PIN_ATN)) ? 'H' : 'L');
    TRACE(" CLK=");
    uart_putchar((pins & (1 << IEC_PIN_CLK)) ? 'H' : 'L');
    TRACE(" DATA=");
    uart_putchar((pins & (1 << IEC_PIN_DATA)) ? 'H' : 'L');
    TRACE(" RST=");
    uart_putchar((pins & (1 << IEC_PIN_RESET)) ? 'H' : 'L');
    TRACE("\r\n");

    memset(device.channels, 0, sizeof(device.channels));
    iec_set_error(CBM_ERR_DOS_MISMATCH, CBM_DOS_ID, 0, 0);
}

/*
 * Lightweight IEC poll: call from inside blocking waits (motor spin-up,
 * step settle, etc.). If ATN was acknowledged by INT0 ISR, process the
 * full IEC command sequence now while we have CPU time.
 */
void iec_poll(void) {
    if (atn_pending) {
        atn_pending = false;
        TRACE("[IEC] poll: ATN during wait\r\n");
        iec_service();
    }
}

void iec_release_all(void) {
    IEC_RELEASE(IEC_PIN_CLK);
    IEC_RELEASE(IEC_PIN_DATA);
}

/*
 * Receive one command byte while ATN is asserted (we are listener,
 * C64 is talker).
 *
 * IEC bit timing: the talker changes DATA while CLK is LOW and the
 * bit is valid when CLK is RELEASED (high) — so sample on the CLK
 * high phase. (The old code sampled on the CLK low phase and caught
 * a phantom first bit, garbling every command byte.)
 */
bool iec_receive_byte_atn(uint8_t *byte) {
    *byte = 0;

    /* Hold DATA (device-present ack) until the talker is ready */
    IEC_ASSERT(IEC_PIN_DATA);

    /* Wait for talker to release CLK = ready-to-send.
     * If ATN is released here, the command phase is over. */
    uint16_t timeout = 0;
    while (IEC_IS_LOW(IEC_PIN_CLK)) {
        _delay_us(1);
        if (!IEC_IS_LOW(IEC_PIN_ATN)) return false;
        if (++timeout > IEC_TIMEOUT_US) return false;
    }

    /* Signal ready-for-data */
    IEC_RELEASE(IEC_PIN_DATA);

    /* Receive 8 bits, LSB first: wait CLK low (talker sets the bit),
     * wait CLK high (bit valid), sample. */
    for (int bit = 0; bit < 8; bit++) {
        timeout = 0;
        while (!IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) { iec_release_all(); return false; }
        }
        timeout = 0;
        while (IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) { iec_release_all(); return false; }
        }
        if (!IEC_IS_LOW(IEC_PIN_DATA)) {
            *byte |= (1 << bit);
        }
    }

    /* Talker pulls CLK low to end the byte; ack within 1ms */
    timeout = 0;
    while (!IEC_IS_LOW(IEC_PIN_CLK)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) { iec_release_all(); return false; }
    }
    IEC_ASSERT(IEC_PIN_DATA);

    return true;
}

/*
 * Receive one data byte (we are listener, C64 is talker, ATN high).
 *
 * Sequence: the talker RELEASES CLK when ready to send (between bytes
 * it holds CLK low). We release DATA = ready-for-data, then wait for
 * CLK to go low = transfer starting. If that takes > 200µs, it is the
 * EOI signal: ack with a >=60µs DATA pulse, then keep waiting.
 * Bits are valid on the CLK high phase (see iec_receive_byte_atn).
 *
 * The old code had this backwards: it waited for "CLK asserted" at
 * entry — but CLK is ALREADY low between bytes, so the wait fell
 * straight through, EOI could never be detected, and the bit loop
 * sampled on the wrong phase.
 */
bool iec_receive_byte(uint8_t *byte, bool *eoi) {
    *byte = 0;
    *eoi = false;

    /* Wait for talker ready-to-send (CLK released) */
    uint16_t timeout = 0;
    while (IEC_IS_LOW(IEC_PIN_CLK)) {
        _delay_us(1);
        if (IEC_IS_LOW(IEC_PIN_ATN)) { iec_release_all(); return false; }
        if (++timeout > IEC_TIMEOUT_US) { iec_release_all(); return false; }
    }

    /* Signal ready-for-data */
    IEC_RELEASE(IEC_PIN_DATA);

    /* Wait for transfer start (CLK low), detecting EOI at 200µs */
    uint16_t wait = 0;
    while (!IEC_IS_LOW(IEC_PIN_CLK)) {
        _delay_us(1);
        if (++wait > IEC_TIMING_EOI_TIMEOUT) {
            *eoi = true;
            IEC_ASSERT(IEC_PIN_DATA);
            _delay_us(IEC_TIMING_EOI_ACK);
            IEC_RELEASE(IEC_PIN_DATA);

            timeout = 0;
            while (!IEC_IS_LOW(IEC_PIN_CLK)) {
                _delay_us(1);
                if (++timeout > IEC_TIMEOUT_US) { iec_release_all(); return false; }
            }
            break;
        }
        if (IEC_IS_LOW(IEC_PIN_ATN)) { iec_release_all(); return false; }
    }

    /* Receive 8 bits, LSB first: bit valid on CLK high phase */
    for (int bit = 0; bit < 8; bit++) {
        timeout = 0;
        while (IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) { iec_release_all(); return false; }
        }
        if (!IEC_IS_LOW(IEC_PIN_DATA)) {
            *byte |= (1 << bit);
        }
        timeout = 0;
        while (!IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) { iec_release_all(); return false; }
        }
    }

    /* Byte-complete ack */
    IEC_ASSERT(IEC_PIN_DATA);
    return true;
}

/*
 * Send one data byte (we are talker, C64 is listener).
 *
 * Entry state: we hold CLK low (from turnaround or the previous byte).
 * Ready handshake: RELEASE CLK = ready-to-send; the listener answers
 * by releasing DATA. (The old code kept CLK asserted while waiting
 * for DATA — the C64 waits for CLK high before releasing DATA, so
 * both sides deadlocked and every talk-direction byte timed out.)
 *
 * EOI: before the last byte, simply don't pull CLK low — after 200µs
 * the listener recognizes EOI and acks with a DATA low pulse.
 * Non-EOI: pull CLK low well within the 200µs window.
 *
 * Bits: while CLK low set DATA, release CLK (bit valid), pull CLK low.
 */
bool iec_send_byte(uint8_t byte, bool eoi) {
    /* Ready-to-send */
    IEC_RELEASE(IEC_PIN_CLK);

    /* Wait for listener ready (DATA released) */
    uint16_t timeout = 0;
    while (IEC_IS_LOW(IEC_PIN_DATA)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) return false;
        if (IEC_IS_LOW(IEC_PIN_ATN)) return false;
    }

    if (eoi) {
        /* Let the 200µs EOI window elapse; listener acks by pulsing
         * DATA low and releasing it again. */
        timeout = 0;
        while (!IEC_IS_LOW(IEC_PIN_DATA)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }
        timeout = 0;
        while (IEC_IS_LOW(IEC_PIN_DATA)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }
    }

    /* Begin transfer (must happen < 200µs after DATA high if !eoi) */
    IEC_ASSERT(IEC_PIN_CLK);
    _delay_us(IEC_TIMING_TALKER_SETUP);

    /* Send 8 bits, LSB first */
    for (int bit = 0; bit < 8; bit++) {
        if (byte & (1 << bit)) {
            IEC_RELEASE(IEC_PIN_DATA);
        } else {
            IEC_ASSERT(IEC_PIN_DATA);
        }
        _delay_us(IEC_TIMING_CLK_LOW);

        IEC_RELEASE(IEC_PIN_CLK);
        _delay_us(IEC_TIMING_CLK_HIGH);

        IEC_ASSERT(IEC_PIN_CLK);
    }

    IEC_RELEASE(IEC_PIN_DATA);

    /* Wait for listener acknowledge (DATA low within ~1ms) */
    timeout = 0;
    while (!IEC_IS_LOW(IEC_PIN_DATA)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) return false;
    }

    _delay_us(IEC_TIMING_BETWEEN_BYTES);
    return true;
}

void iec_set_error(uint8_t code, const char *msg, uint8_t track, uint8_t sector) {
    device.error_code = code;
    int len = cbm_dos_format_error(code, msg, track, sector,
                                    device.error_msg, sizeof(device.error_msg));

    /* Store in channel buffer for SA 15 */
    iec_channel_t *ch = &device.channels[1];  /* Index 1 = command channel */
    uint8_t copy_len = (len < (int)sizeof(ch->buffer)) ? (uint8_t)len : sizeof(ch->buffer);
    memcpy(ch->buffer, device.error_msg, copy_len);
    ch->buf_len = copy_len;
    ch->buf_pos = 0;
    ch->eof = false;
}

bool iec_error_talk_byte(uint8_t *byte, bool *eoi) {
    iec_channel_t *ch = &device.channels[1];
    if (ch->buf_pos >= ch->buf_len) {
        *eoi = true;
        return false;
    }
    *byte = ch->buffer[ch->buf_pos++];
    *eoi = (ch->buf_pos >= ch->buf_len);
    return true;
}

static void iec_service_inner(void);

/*
 * Reentrancy guard: iec_poll() is called from blocking floppy waits
 * (seek settle, motor spin-up) which themselves run inside an
 * in-progress iec_service() transfer. Re-entering the non-reentrant
 * state machine mid-transfer corrupts it (e.g. ATN during seek), so
 * nested calls are dropped — INT0 has already asserted DATA, and the
 * outer service loop handles the command when it returns.
 */
static bool in_service = false;

void iec_service(void) {
    if (in_service) return;
    in_service = true;
    iec_service_inner();
    in_service = false;
}

static void iec_service_inner(void) {
    /*
     * Bus reset: edge-triggered, not level-triggered.
     * /RESET LOW  → shutdown once, then idle (no reboot loop)
     * /RESET HIGH → reinit once (when C64 turns on)
     * C64 off (/RESET stuck LOW) → controller stays idle, no action
     */
    if (IEC_IS_LOW(IEC_PIN_RESET)) {
        if (!reset_held) {
            /* Falling edge: /RESET just went LOW → shutdown */
            reset_held = true;
            TRACE("[IEC] *** BUS RESET ***\r\n");
            device.state = IEC_STATE_IDLE;
            iec_release_all();
            floppy_motor_off();
            fat12_unmount();
            cbm_dos_init();
            fastload_reset();
        }
        return;  /* Stay idle while /RESET is held LOW */
    }

    /* /RESET is HIGH — was it just released? */
    if (reset_held) {
        reset_held = false;
        TRACE("[IEC] reset released, reinit floppy\r\n");

        /* Full floppy reinit (same as boot) */
        floppy_motor_on();
        int rc = floppy_recalibrate();
        if (rc != FLOPPY_OK) {
            TRACE("[IEC] no drive!\r\n");
            iec_set_error(CBM_ERR_DRIVE_NOT_READY, "DRIVE NOT READY", 0, 0);
            led_debug_blink(DBG_FLOPPY_ERROR);
            floppy_motor_off();
            return;
        }
        TRACE("[IEC] drive OK\r\n");

        if (!floppy_check_disk()) {
            TRACE("[IEC] no disk in drive\r\n");
            iec_set_error(CBM_ERR_DRIVE_NOT_READY, "DRIVE NOT READY", 0, 0);
            led_debug_blink(DBG_NO_DISK);
            floppy_motor_off();
            return;
        }

        TRACE("[IEC] mount FAT12...\r\n");
        rc = fat12_mount();
        if (rc == FAT12_OK) {
            TRACE("[IEC] FAT12 OK\r\n");
        } else {
            TRACE("[IEC] mount failed\r\n");
            iec_set_error(CBM_ERR_DRIVE_NOT_READY, "DRIVE NOT READY", 0, 0);
            led_debug_blink(DBG_NO_DISK);
        }

        floppy_motor_off();
        TRACE("[IEC] reinit done, ready\r\n");
        led_debug_blink(DBG_BOOT_OK);
        return;
    }

    /* Check for ATN */
    if (!IEC_IS_LOW(IEC_PIN_ATN)) {
        if (device.state == IEC_STATE_TALKER) {
            uint8_t byte;
            bool eoi;
            if (cbm_dos_talk_byte(device.current_sa, &byte, &eoi)) {
                const fastload_protocol_t *fl = fastload_active();
                bool ok = fl && fl->send_byte ? fl->send_byte(byte, eoi)
                                               : iec_send_byte(byte, eoi);
                if (!ok) {
                    TRACE("[IEC] talk: send failed\r\n");
                    device.state = IEC_STATE_IDLE;
                    iec_release_all();
                }
                if (eoi) {
                    TRACE("[IEC] talk: EOI, done\r\n");
                    device.state = IEC_STATE_IDLE;
                    iec_release_all();
                }
            } else {
                /* No data to send — release bus so C64 doesn't hang */
                TRACE("[IEC] talk: no data, release\r\n");
                device.state = IEC_STATE_IDLE;
                iec_release_all();
            }
        }
        return;
    }

    /* ATN asserted - receive commands */
    led_green_on();
    TRACE("[IEC] ATN\r\n");
    IEC_ASSERT(IEC_PIN_DATA);

    uint8_t cmd;
    while (IEC_IS_LOW(IEC_PIN_ATN)) {
        if (!iec_receive_byte_atn(&cmd)) {
            TRACE("[IEC] ATN byte recv fail\r\n");
            break;
        }

        TRACE("[IEC] cmd=0x");
        uart_puthex8(cmd);

        uint8_t device_num = cmd & 0x1F;

        if (cmd == IEC_CMD_UNLISTEN) {
            TRACE(" UNLISTEN\r\n");
            if (device.state == IEC_STATE_LISTENER) {
                iec_channel_t *ch = &device.channels[0];
                if (device.current_sa == IEC_SA_COMMAND && ch->buf_len > 0) {
                    ch->buffer[ch->buf_len] = '\0';
                    cbm_dos_execute_command((char *)ch->buffer, ch->buf_len);
                    ch->buf_len = 0;
                } else if (device.current_sa == IEC_SA_SAVE) {
                    cbm_dos_close(device.current_sa);
                }
                device.state = IEC_STATE_IDLE;
                iec_release_all();
                fastload_reset();
            }
        } else if (cmd == IEC_CMD_UNTALK) {
            TRACE(" UNTALK\r\n");
            if (device.state == IEC_STATE_TALKER) {
                device.state = IEC_STATE_IDLE;
                iec_release_all();
                fastload_reset();
            }
        } else if ((cmd & 0xE0) == IEC_CMD_LISTEN) {
            TRACE(" LISTEN #");
            uart_putdec(device_num);
            TRACE("\r\n");
            if (device_num == device.device_number) {
                device.state = IEC_STATE_LISTENER;
                TRACE("[IEC] -> LISTENER\r\n");
            }
        } else if ((cmd & 0xE0) == IEC_CMD_TALK) {
            TRACE(" TALK #");
            uart_putdec(device_num);
            TRACE("\r\n");
            if (device_num == device.device_number) {
                device.state = IEC_STATE_TALKER;
                TRACE("[IEC] -> TALKER\r\n");
            }
        } else if ((cmd & 0xF0) == IEC_CMD_SECOND) {
            /* 0x60-0x6F: data channel select after LISTEN/TALK */
            uint8_t sa = cmd & 0x0F;
            TRACE(" SECOND SA=");
            uart_putdec(sa);
            TRACE("\r\n");
            device.current_sa = sa;
        } else if ((cmd & 0xF0) == IEC_CMD_OPEN) {
            /* 0xF0-0xFF: OPEN — filename follows as data bytes.
             * Reset the channel so a fresh filename is collected;
             * the old code never cleared `open`/`filename_len`, so
             * every LOAD after the first was misrouted. */
            uint8_t sa = cmd & 0x0F;
            TRACE(" OPEN SA=");
            uart_putdec(sa);
            TRACE("\r\n");
            device.current_sa = sa;

            if (device.state == IEC_STATE_LISTENER) {
                iec_channel_t *ch = &device.channels[0];
                ch->open = false;
                ch->filename_len = 0;
                ch->buf_len = 0;
                ch->buf_pos = 0;
            }
        } else if ((cmd & 0xF0) == (IEC_CMD_CLOSE & 0xF0)) {
            uint8_t sa = cmd & 0x0F;
            TRACE(" CLOSE SA=");
            uart_putdec(sa);
            TRACE("\r\n");
            cbm_dos_close(sa);
            device.channels[0].open = false;
            device.channels[0].filename_len = 0;
        } else {
            TRACE(" ??\r\n");
        }
    }

    led_green_off();

    if (device.state == IEC_STATE_TALKER) {
        /* Talk-attention turnaround (after ATN release): roles swap —
         * we take CLK as the new talker and release DATA; the C64
         * holds DATA low as the new listener. */
        IEC_ASSERT(IEC_PIN_CLK);
        IEC_RELEASE(IEC_PIN_DATA);
        _delay_us(IEC_TIMING_TALKER_SETUP);

        TRACE("[IEC] ATN released, detect fastload\r\n");
        fastload_detect();
        const fastload_protocol_t *fl = fastload_active();
        if (fl && fl->on_atn_end) {
            TRACE("[IEC] fastload active\r\n");
            fl->on_atn_end();
        }
    } else if (device.state == IEC_STATE_IDLE) {
        /* Command was for another device (or UNLISTEN) — stop holding
         * DATA from the INT0 device-present ack, or the whole bus
         * stays jammed low. */
        iec_release_all();
    }

    if (device.state == IEC_STATE_LISTENER) {
        TRACE("[IEC] listening for data SA=");
        uart_putdec(device.current_sa);
        TRACE("\r\n");
        uint8_t byte;
        bool eoi;
        iec_channel_t *ch = &device.channels[0];

        while (1) {
            if (IEC_IS_LOW(IEC_PIN_ATN)) {
                TRACE("[IEC] ATN during listen\r\n");
                break;
            }
            if (!iec_receive_byte(&byte, &eoi)) {
                TRACE("[IEC] listen recv fail\r\n");
                break;
            }

            if (!ch->open && device.current_sa != IEC_SA_COMMAND) {
                if (ch->filename_len < sizeof(ch->filename) - 1) {
                    ch->filename[ch->filename_len++] = byte;
                }
                if (eoi) {
                    ch->filename[ch->filename_len] = '\0';
                    TRACE("[IEC] filename='");
                    uart_puts(ch->filename);
                    TRACE("'\r\n");
                    cbm_dos_open(device.current_sa, ch->filename, ch->filename_len);
                    ch->open = true;
                }
            } else {
                /* Reserve one slot for the '\0' the UNLISTEN handler
                 * appends before cbm_dos_execute_command(). */
                if (ch->buf_len < sizeof(ch->buffer) - 1) {
                    ch->buffer[ch->buf_len++] = byte;
                }
                cbm_dos_listen_byte(device.current_sa, byte);
            }

            if (eoi) {
                TRACE("[IEC] listen EOI\r\n");
                break;
            }
        }
    }
}

#endif /* __AVR__ */
