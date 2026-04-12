#ifndef UART_H
#define UART_H

#include <stdint.h>

#ifdef __AVR__

void uart_init(void);
void uart_putchar(char c);
void uart_puts(const char *s);
void uart_puthex8(uint8_t val);
void uart_puthex16(uint16_t val);
void uart_putdec(uint16_t val);

#include <avr/pgmspace.h>
void uart_puts_P(const char *s);
#define TRACE(s) uart_puts_P(PSTR(s))

#else

/* No-op stubs for host builds (unit tests) */
static inline void uart_init(void) {}
static inline void uart_putchar(char c) { (void)c; }
static inline void uart_puts(const char *s) { (void)s; }
static inline void uart_puthex8(uint8_t val) { (void)val; }
static inline void uart_puthex16(uint16_t val) { (void)val; }
static inline void uart_putdec(uint16_t val) { (void)val; }
#define TRACE(s) ((void)0)

#endif

#endif /* UART_H */
