#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);
void uart_putchar(char c);
void uart_puts(const char *s);
void uart_puthex8(uint8_t val);
void uart_puthex16(uint16_t val);
void uart_putdec(uint16_t val);

#ifdef __AVR__
#include <avr/pgmspace.h>
void uart_puts_P(const char *s);
/* Put trace string literals in flash, not RAM */
#define TRACE(s) uart_puts_P(PSTR(s))
#else
#define TRACE(s) uart_puts(s)
#endif

#endif /* UART_H */
