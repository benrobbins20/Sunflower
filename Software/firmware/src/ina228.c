#include "ina228.h"
#include "i2c_bus.h"


#define REG_MFG_ID              0x3E
#define REG_DEV_ID              0x3F
#define REG_CONFIG              0x00
#define CONFIG_RST              (1u << 15) // write 1: software reset, all registers to defaults (self-clears)
#define CONFIG_ADC_RANGE        (1u << 4) // 0 +-163mV, 1 +-40.96mV, use 1 for finer resolution
#define REG_SHUNT_CAL           0x02
#define REG_ADC_CONFIG          0x01 
#define ADC_MODE_CONT_ALL       (15u << 12) // [15:12] Fh = Continuous bus voltage, shunt voltage and temperature
#define ADC_CONVERSION_TIME     ((5u << 9) | (5u << 6) | (5u << 3)) // [11:9] VBUSCT, [8:6] VSCT, [5:3] TEMPCT, 1052us
#define ADC_AVG_16              (2u << 0) // [2:0] Average 16 samples

static uint8_t dev_addr;
static float current_lsb;

static int read16(uint8_t reg, uint16_t *out) {
    uint8_t buf[2];
    int rc = i2c_bus_read_reg(dev_addr, reg, buf, 2);
    if (rc == I2C_BUS_OK) {
        // received in MSB, shift data to left to display 16 bit val in correctly
        *out = (uint16_t)((buf[0] << 8) | buf[1]);
    }
    return rc;
}

static int write16(uint8_t reg, uint16_t val) {
    uint8_t buf[2] = { (uint8_t)(val >> 8), (uint8_t)val }; // cast of 1 byte on val retains low byte only
    return i2c_bus_write_reg(dev_addr, reg, buf, 2);
}

int ina228_init(uint8_t addr, float shunt_ohms, float max_current_a) {
    uint16_t id;
    int rc;

    dev_addr = addr;

    // MFG_ID (0x3E) is read-only and must read 0x5449 = "TI"
    rc = read16(REG_MFG_ID, &id);
    if (rc != I2C_BUS_OK) return rc;
    if (id != 0x5449) return INA228_ERR_ID;

    // confirm die ID, bits 15:4
    rc = read16(REG_DEV_ID, &id);
    if (rc != I2C_BUS_OK) return rc;
    if ((id >> 4) != 0x228) return INA228_ERR_ID;

    // software reset: all registers back to datasheet defaults
    rc = write16(REG_CONFIG, CONFIG_RST);
    if (rc != I2C_BUS_OK) return rc;

    // set adc range
    rc = write16(REG_CONFIG, CONFIG_ADC_RANGE);
    if (rc != I2C_BUS_OK) return rc;

    // max_current_a: pick above the real max (PCB: BQ25570 limit 285 mA -> 0.4 A, breakout bench -> 0.5 A)
    // CURRENT is 20-bit signed: 2^20 codes split +/-, so 2^19 = 524288 counts each way
    // CURRENT_LSB = Imax / 2^19  (datasheet eq 3)
    current_lsb = max_current_a / 524288.0f;

    // must multiply by 4 when using adcrange=1
    // 13107.2e6f fixed constant in datasheet eq 2 to set scaling
    uint16_t cal = (uint16_t)(13107.2e6f * current_lsb * shunt_ohms * 4.0f + 0.5f); // round up and truncate
    rc = write16(REG_SHUNT_CAL, cal & 0x7FFFu); // bit 15 is reserved, mask it out
    if (rc != I2C_BUS_OK) return rc;


    // configure ADC: continuous mode, conversion time, averaging
    rc = write16(REG_ADC_CONFIG, ADC_MODE_CONT_ALL | ADC_CONVERSION_TIME | ADC_AVG_16);
    if (rc != I2C_BUS_OK) return rc;

    return I2C_BUS_OK;
}


