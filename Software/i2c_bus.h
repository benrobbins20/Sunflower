// i2c_bus.h
// Bare I2C0 master driver (polled). Knows nothing about specific chips.
// Device drivers (ina228.c, etc.) sit on top of this.
//
// Pins come from the Pin Tool (config/pin_config.h): I2C_SDA, I2C_SCL
// Addresses are 7-bit (e.g. 0x41), the driver shifts them for the hardware.

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdint.h>

// Return codes for every transfer
#define I2C_BUS_OK        0   // transfer finished, STOP sent
#define I2C_BUS_NACK     -1   // device didn't ACK (wrong address / not connected / not powered)
#define I2C_BUS_ERROR    -2   // bus error, arbitration lost, usage fault
#define I2C_BUS_TIMEOUT  -3   // no progress before timeout (bus stuck, SDA held low)

// Clocks, open-drain pins, I2C0 routing, 100 kHz master mode
void i2c_bus_init(void);

// Stop I2C0, release pins, gate clock. Call i2c_bus_init() again to wake.
void i2c_bus_deinit(void);

// START, addr+W, tx bytes, STOP
int i2c_bus_write(uint8_t addr, const uint8_t *tx, uint16_t tx_len);

// START, addr+W, tx bytes, repeated START, addr+R, rx bytes (NACK last), STOP
int i2c_bus_write_read(uint8_t addr, const uint8_t *tx, uint16_t tx_len,
                       uint8_t *rx, uint16_t rx_len);

// Register helpers (STM32 burstread / burstwrite equivalents)
// read n bytes starting at register reg
int i2c_bus_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t n);

// write n bytes starting at register reg (reg byte + data in one transaction)
int i2c_bus_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t n);

#endif // I2C_BUS_H
