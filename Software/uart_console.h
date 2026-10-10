// uart_console.h
// Bare EUSART0 UART console (polled, TX only for now). 115200 8N1.
// Pro Kit: PA08 TX / PA09 RX = board VCOM -> USB serial port (JLink CDC UART Port).
// PCB: name the pins VCOM_TX / VCOM_RX in the Pin Tool and they override the defaults.

#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

void uart_console_init(void);

// blocking: returns when every byte is in the TX FIFO
void uart_console_write(const char *s);

// printf-style, lines longer than 128 chars get truncated
void uart_printf(const char *fmt, ...);

#endif // UART_CONSOLE_H
