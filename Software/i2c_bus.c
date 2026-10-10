#include "i2c_bus.h"

#include "em_device.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "pin_config.h"
#include "em_i2c.h"
#include <stdint.h>

#define I2C_BUS_PERIPH          I2C0
#define I2C_BUS_TIMEOUT_LOOPS   300000u

void i2c_bus_init(void) {
    CMU_ClockEnable(cmuClock_I2C0, true);
    CMU_ClockEnable(cmuClock_GPIO, true);

    // open drain ~ Logical AND, must release both pins, external pull ups on dev boards/pcb
    GPIO_PinModeSet((GPIO_Port_TypeDef)I2C_SCL_PORT, I2C_SCL_PIN, gpioModeWiredAnd, 1);
    GPIO_PinModeSet((GPIO_Port_TypeDef)I2C_SDA_PORT, I2C_SDA_PIN, gpioModeWiredAnd, 1);

    uint32_t sda_port = (uint32_t)I2C_SDA_PORT << _GPIO_I2C_SDAROUTE_PORT_SHIFT; // port c 0x2
    uint32_t sda_pin = (uint32_t)I2C_SDA_PIN << _GPIO_I2C_SDAROUTE_PIN_SHIFT; // pin 7, 19:16, 0111
    GPIO->I2CROUTE[0].SDAROUTE = sda_pin | sda_port; // 0x00070002
    
    uint32_t scl_port = (uint32_t)I2C_SCL_PORT << _GPIO_I2C_SCLROUTE_PORT_SHIFT; // 0x2
    uint32_t scl_pin = (uint32_t)I2C_SCL_PIN << _GPIO_I2C_SCLROUTE_PIN_SHIFT; // pin 5, 19:16, 0101, 0x00050002
    GPIO->I2CROUTE[0].SCLROUTE = scl_pin | scl_port;

    GPIO->I2CROUTE[0].ROUTEEN = GPIO_I2C_ROUTEEN_SDAPEN | GPIO_I2C_ROUTEEN_SCLPEN;

    // I2C_INIT_DEFAULT
    // .enable  = true                    -> turn I2C0 on when init finishes
    // .master  = true                    -> we drive SCL
    // .refFreq = 0                       -> measure current I2C0 input clock
    // .freq    = I2C_FREQ_STANDARD_MAX   -> 100 kHz
    // .clhr    = i2cClockHLRStandard     -> SCL high/low ratio 4:4 for standard mode
    I2C_Init_TypeDef i2c_init = I2C_INIT_DEFAULT; 
    I2C_Init(I2C_BUS_PERIPH, &i2c_init);
}

static int run_transfer(I2C_TransferSeq_TypeDef *seq) {
    I2C_TransferReturn_TypeDef ret;
    uint32_t loops = I2C_BUS_TIMEOUT_LOOPS;
    
    // loops are much faster than i2c bus, many loops and clock cycles between each IF register data received. 
    ret = I2C_TransferInit(I2C_BUS_PERIPH, seq);
    while (ret == i2cTransferInProgress && loops > 0) {
        ret = I2C_Transfer(I2C_BUS_PERIPH); 
        loops--;
    }

    if (ret == i2cTransferDone) {
        return I2C_BUS_OK;
    }

    // timeout on long transfers or bus clock is stuck or something
    if (ret == i2cTransferInProgress) {
        return I2C_BUS_TIMEOUT;
    }
    
    if (ret == i2cTransferNack) {
        return I2C_BUS_NACK;
    }
    
    return I2C_BUS_ERROR; // catch-all for other errors
}

// START, addr+W, tx bytes, repeated START, addr+R, rx bytes (NACK last), STOP
int i2c_bus_write_read(uint8_t addr, const uint8_t *tx, uint16_t tx_len,
                       uint8_t *rx, uint16_t rx_len) {
    I2C_TransferSeq_TypeDef seq;

    // do not need to manually set W/R, seq state machine handles it
    seq.addr  = (uint16_t)(addr << 1);   
    seq.flags = I2C_FLAG_WRITE_READ;

    // I2C_FLAG_WRITE_READ - Data written from buf[0].data and read into buf[1].data
    seq.buf[0].data = (uint8_t *)tx;
    seq.buf[0].len = tx_len;

    seq.buf[1].data = rx;                
    seq.buf[1].len = rx_len;

    return run_transfer(&seq);
}

// read n bytes starting at register reg: write 1 byte (reg), read n bytes, one transaction
int i2c_bus_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t n) {
    return i2c_bus_write_read(addr, &reg, 1, data, n);
}

// START, addr+W, reg byte, tx bytes, STOP
int i2c_bus_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len) {
    I2C_TransferSeq_TypeDef seq;

    seq.addr  = (uint16_t)(addr << 1);
    seq.flags = I2C_FLAG_WRITE_WRITE;

    seq.buf[0].data = &reg;
    seq.buf[0].len = 1;

    seq.buf[1].data = (uint8_t *)data;
    seq.buf[1].len = len;

    return run_transfer(&seq);
}

// Plain write: START, addr+W, tx bytes, STOP.
int i2c_bus_write(uint8_t addr, const uint8_t *tx, uint16_t tx_len) {
    I2C_TransferSeq_TypeDef seq;

    seq.addr  = (uint16_t)(addr << 1);
    seq.flags = I2C_FLAG_WRITE;

    seq.buf[0].data = (uint8_t *)tx;
    seq.buf[0].len  = tx_len;

    return run_transfer(&seq);
}

// i2c_bus_deinit
// Undo i2c_bus_init: stop I2C0, release pins, gate the clock.
// Call i2c_bus_init() again to wake it. Only call between transfers.
void i2c_bus_deinit(void) {
    I2C_Enable(I2C_BUS_PERIPH, false);

    GPIO->I2CROUTE[0].ROUTEEN = 0; // clear out the whole register 

    // pins high-Z: no driver, no input leakage; board pull-ups hold lines idle high
    GPIO_PinModeSet(I2C_SCL_PORT, I2C_SCL_PIN, gpioModeDisabled, 0);
    GPIO_PinModeSet(I2C_SDA_PORT, I2C_SDA_PIN, gpioModeDisabled, 0);

    CMU_ClockEnable(cmuClock_I2C0, false);     // gate the I2C0 clock
}