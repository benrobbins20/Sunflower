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
#define REG_VBUS                0x05 // 7.6.1.6: 24-bit, value in bits 23:4, 195.3125 uV/LSB
#define REG_CURRENT             0x07 // 7.6.1.8: 24-bit, SIGNED 20-bit value in bits 23:4, x current_lsb
#define REG_POWER               0x08 // 7.6.1.9: 24-bit UNSIGNED, all bits used (23:0), W = count x 3.2 x current_lsb
#define REG_SHUNT               0x04 // 7.6.1.5 Shunt Voltage Measurement signed 20 bit * 78.125 nV/LSB
#define REG_DIETEMP             0x06 // 7.6.1.7: 16-bit SIGNED, all bits used (15:0), 7.8125 m°C/LSB
#define REG_DIAG_ALRT           0x0B // 7.6.1.12: alert config [15:12] + diagnostic flags [11:0], reset 0x0001

static uint8_t dev_addr;
static float current_lsb;

// debug snapshot, declared extern in ina228.h; volatile so the debugger always shows memory
volatile ina228_regs_t ina228_regs;

static int read16(uint8_t reg, uint16_t *out) {
    // 16-bit register: write pointer, read 2 bytes (datasheet 7.5.1.1)
    uint8_t buf[2];
    int rc = i2c_bus_read_reg(dev_addr, reg, buf, 2);
    if (rc == I2C_BUS_OK) {

        // bytes arrive MSB first: first byte is the high byte
        *out = (uint16_t)((buf[0] << 8) | buf[1]);
    }

    return rc;
}

static int write16(uint8_t reg, uint16_t val) {
    // every writable INA228 register is 16-bit

    // split MSB first: high byte, then low byte (uint8_t cast keeps only the low 8 bits)
    uint8_t buf[2] = { (uint8_t)(val >> 8), (uint8_t)val };

    return i2c_bus_write_reg(dev_addr, reg, buf, 2);
}

static int read24(uint8_t reg, uint32_t *out) {
    // 24-bit measurement registers: VSHUNT, VBUS, CURRENT, POWER
    uint8_t buf[3];
    int rc = i2c_bus_read_reg(dev_addr, reg, buf, 3);
    if (rc == I2C_BUS_OK) {

        // MSB first into bits 23:0; cast before shifting so the math is done in 32 bits
        *out = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    }

    return rc;
}

int ina228_read_bus_mV(float *mv) {
    // VBUS pin measured against ground (single-ended): 20-bit UNSIGNED count, 0 to 2^20 - 1

    uint32_t raw;
    int rc = read24(REG_VBUS, &raw);
    if (rc == I2C_BUS_OK) {

        // step size (LSB): datasheet 7.6.1.6 conversion factor 195.3125 uV/LSB = 0.1953125 mV per count
        const float step_mv = 0.1953125f;

        // [23:4] >> [19:0] int
        uint32_t count = raw >> 4;

        // convert the adc count to a measured value in mV
        *mv = (float)count * step_mv;
    }

    return rc;
}

int ina228_read_current_mA(float *ma) {
    // calculated from the shunt IN+ - IN-, this can be positive or negative, can read a signed adc count value
    uint32_t raw;
    int rc = read24(REG_CURRENT, &raw);
    if (rc == I2C_BUS_OK) {

        // current is signed 20 bit adc count in [23:4]
        // first shift << 8 to place the sign bit in MSB
        // cast the current value to signed then shift >> 12 to sign extend and place the 20-bit value in LSB
        uint32_t top = raw << 8;
        int32_t count = (int32_t)top >> 12;

        // step size (LSB): datasheet 8.1.2 eq 4, Current [A] = CURRENT_LSB x CURRENT
        //   CURRENT_LSB (A per count) was chosen in init (eq 3: Imax / 2^19); x 1000 for mA
        const float step_ma = current_lsb * 1000.0f;

        // signed count * multiplier
        *ma = (float)count * step_ma;
    }

    return rc;
}

int ina228_read_power_mW(float *mw) {
    // calculated by the chip from VBUS x |CURRENT|: always positive (check CURRENT's sign for direction)
    uint32_t raw;
    int rc = read24(REG_POWER, &raw);
    if (rc == I2C_BUS_OK) {

        // step size (LSB): datasheet 8.1.2 eq 5, Power [W] = 3.2 x CURRENT_LSB x POWER; x 1000 for mW
        const float step_mw = 3.2f * current_lsb * 1000.0f;

        // unsigned and uses all 24 bits [23:0]: no shift, no sign extension
        uint32_t count = raw;

        // unsigned count * multiplier
        *mw = (float)count * step_mw;
    }

    return rc;
}

int ina228_read_shunt_uV(float *uv) {
    // raw shunt ADC, IN+ - IN- (differential): signed, + = current flowing IN+ to IN-
    uint32_t raw;
    int rc = read24(REG_SHUNT, &raw);
    if (rc == I2C_BUS_OK) {
        uint32_t top = raw << 8;
        int32_t count = (int32_t)top >> 12;

        // step size (LSB): datasheet 7.6.1.5 conversion factor 78.125 nV/LSB with ADCRANGE = 1
        const float step_uv = 0.078125f;

        // signed count * multiplier
        *uv = (float)count * step_uv;
    }

    return rc;
}

int ina228_read_die_temp_C(float *c) {
    // INA228's own die temperature, signed (can read below 0 C)
    uint16_t raw;
    int rc = read16(REG_DIETEMP, &raw);
    if (rc == I2C_BUS_OK) {

        // value fills all 16 bits [15:0]: bit 15 is already the sign, so a cast sign-extends
        int16_t count = (int16_t)raw;

        // step size (LSB): datasheet 7.6.1.7 conversion factor 7.8125 m°C/LSB = 0.0078125 °C per count
        const float step_c = 0.0078125f;

        // signed count * multiplier
        *c = (float)count * step_c;
    }

    return rc;
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

void ina228_dump_regs(void) {
    // raw register snapshot for the debugger: every field keeps its last good read
    uint16_t r16;
    uint32_t r24;

    // configuration: confirms init's writes landed (defaults would be 0x0000, 0xFB68, 0x1000)
    if (read16(REG_CONFIG, &r16)     == I2C_BUS_OK) ina228_regs.config     = r16;
    if (read16(REG_ADC_CONFIG, &r16) == I2C_BUS_OK) ina228_regs.adc_config = r16;
    if (read16(REG_SHUNT_CAL, &r16)  == I2C_BUS_OK) ina228_regs.shunt_cal  = r16;

    // measurements: raw bits, not converted (decode by hand: shift, sign, x step)
    if (read24(REG_SHUNT, &r24)      == I2C_BUS_OK) ina228_regs.vshunt     = r24;
    if (read24(REG_VBUS, &r24)       == I2C_BUS_OK) ina228_regs.vbus       = r24;
    if (read16(REG_DIETEMP, &r16)    == I2C_BUS_OK) ina228_regs.dietemp    = r16;
    if (read24(REG_CURRENT, &r24)    == I2C_BUS_OK) ina228_regs.current    = r24;
    if (read24(REG_POWER, &r24)      == I2C_BUS_OK) ina228_regs.power      = r24;

    // status and identity
    if (read16(REG_DIAG_ALRT, &r16)  == I2C_BUS_OK) ina228_regs.diag_alrt  = r16;
    if (read16(REG_MFG_ID, &r16)     == I2C_BUS_OK) ina228_regs.mfg_id     = r16;
    if (read16(REG_DEV_ID, &r16)     == I2C_BUS_OK) ina228_regs.dev_id     = r16;
}
