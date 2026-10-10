// uart_console.c
// EUSART0 as a plain UART for debug text. Same pattern as i2c_bus.c:
// clocks -> pin modes -> route EUSART0 to pins -> peripheral init.

#include "uart_console.h"

#include "em_device.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_eusart.h"
#include "pin_config.h"
#include <stdarg.h>
#include <stdio.h>

#if defined(__has_include)
#if __has_include("sl_board_control_config.h")
#include "sl_board_control_config.h"   // Pro Kit only: SL_BOARD_ENABLE_VCOM_PORT/PIN (PB00)
#endif
#endif

#define UART_CONSOLE_PERIPH   EUSART0   // EUSART1 is used by the kit flash-shutdown driver

// Pins: Pin Tool names VCOM_TX / VCOM_RX win if they exist, else Pro Kit VCOM pins.
#ifndef VCOM_TX_PORT
#define VCOM_TX_PORT          gpioPortA // PA08 -> board controller VCOM RX
#define VCOM_TX_PIN           8
#endif
#ifndef VCOM_RX_PORT
#define VCOM_RX_PORT          gpioPortA // PA09 <- board controller VCOM TX
#define VCOM_RX_PIN           9
#endif

void uart_console_init(void) {
    CMU_ClockEnable(cmuClock_GPIO, true);
    CMU_ClockEnable(cmuClock_EUSART0, true);

#ifdef SL_BOARD_ENABLE_VCOM_PORT
    // Pro Kit: PB00 high tells the board controller to connect PA08/PA09 to the USB serial port
    GPIO_PinModeSet((GPIO_Port_TypeDef)SL_BOARD_ENABLE_VCOM_PORT, SL_BOARD_ENABLE_VCOM_PIN, gpioModePushPull, 1);
#endif

    // TX idles high (UART idle = mark), RX input with pull-up so it doesn't float
    GPIO_PinModeSet((GPIO_Port_TypeDef)VCOM_TX_PORT, VCOM_TX_PIN, gpioModePushPull, 1);
    GPIO_PinModeSet((GPIO_Port_TypeDef)VCOM_RX_PORT, VCOM_RX_PIN, gpioModeInputPull, 1);

    // route EUSART0 TX/RX to the pins (same field layout as I2CROUTE: PORT 1:0, PIN 19:16)
    uint32_t tx_port = (uint32_t)VCOM_TX_PORT << _GPIO_EUSART_TXROUTE_PORT_SHIFT;
    uint32_t tx_pin  = (uint32_t)VCOM_TX_PIN  << _GPIO_EUSART_TXROUTE_PIN_SHIFT;
    GPIO->EUSARTROUTE[0].TXROUTE = tx_pin | tx_port;               // 0x00080000 for PA08

    uint32_t rx_port = (uint32_t)VCOM_RX_PORT << _GPIO_EUSART_RXROUTE_PORT_SHIFT;
    uint32_t rx_pin  = (uint32_t)VCOM_RX_PIN  << _GPIO_EUSART_RXROUTE_PIN_SHIFT;
    GPIO->EUSARTROUTE[0].RXROUTE = rx_pin | rx_port;               // 0x00090000 for PA09

    GPIO->EUSARTROUTE[0].ROUTEEN = GPIO_EUSART_ROUTEEN_TXPEN | GPIO_EUSART_ROUTEEN_RXPEN;

    // EUSART_UART_INIT_DEFAULT_HF: enable TX+RX, 115200 baud, 8 data bits, no parity, 1 stop bit
    EUSART_UartInit_TypeDef init = EUSART_UART_INIT_DEFAULT_HF;
    EUSART_UartInitHf(UART_CONSOLE_PERIPH, &init);
}

void uart_console_write(const char *s) {
    while (*s) {
        if (*s == '\n') {
            EUSART_Tx(UART_CONSOLE_PERIPH, '\r');   // terminals want CR+LF
        }
        EUSART_Tx(UART_CONSOLE_PERIPH, (uint8_t)*s++); // waits for FIFO space, then queues the byte
    }
}

void uart_printf(const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    uart_console_write(buf);
}
