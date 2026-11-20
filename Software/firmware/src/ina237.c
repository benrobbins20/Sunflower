/* Driver for the TI INA237 (16-bit power monitor) fitted on the Sunflower rev2 PCB.
 * Part marking 237I = INA237AIDGSR; its DEVICE_ID reads 0x2381 (die ID 0x238, rev 1).
 * The schematic/BOM list the pin-compatible INA228 (20-bit), which has different
 * register widths and scale factors; this driver is for the INA237 only.
 * Datasheet: Documentation/ina237.pdf (register map, section 8.1.2 calibration). */
#include "ina237.h"
#include "i2c_bus.h"


#define REG_MFG_ID              0x3E
#define REG_DEV_ID              0x3F
#define DEV_ID_INA237           0x238    // DEVICE_ID bits 15:4; bits 3:0 are the silicon revision
#define REG_CONFIG              0x00
#define CONFIG_RST              (1u << 15) // write 1: software reset, all registers to defaults (self-clears)
#define CONFIG_ADC_RANGE        (1u << 4) // 0 +-163mV, 1 +-40.96mV, use 1 for finer resolution
#define REG_SHUNT_CAL           0x02
#define REG_ADC_CONFIG          0x01 
#define ADC_MODE_CONT_ALL       (15u << 12) // [15:12] Fh = Continuous bus voltage, shunt voltage and temperature
#define ADC_CONVERSION_TIME     ((5u << 9) | (5u << 6) | (5u << 3)) // [11:9] VBUSCT, [8:6] VSCT, [5:3] TEMPCT, 1052us
#define ADC_AVG_16              (2u << 0) // [2:0] Average 16 samples
#define REG_VBUS                0x05 // 16-bit, 3.125 mV/LSB (always positive)
#define REG_CURRENT             0x07 // 16-bit SIGNED, x current_lsb
#define REG_POWER               0x08 // 24-bit UNSIGNED, all bits used (23:0), W = count x 0.2 x current_lsb
#define REG_SHUNT               0x04 // 16-bit SIGNED, 1.25 uV/LSB with ADCRANGE = 1 (5 uV/LSB with ADCRANGE = 0)
#define REG_DIETEMP             0x06 // 16-bit, SIGNED 12-bit value in bits 15:4, 125 m°C/LSB
#define REG_DIAG_ALRT           0x0B // alert config + diagnostic flags

static uint8_t dev_addr;
static float current_lsb;

// debug snapshot, declared extern in ina237.h; volatile so the debugger always shows memory
volatile ina237_regs_t ina237_regs;

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
    // every writable INA237 register is 16-bit

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

int ina237_read_bus_mV(float *mv) {
    // VBUS pin measured against ground (single-ended): 16-bit count, all bits used

    uint16_t raw;
    int rc = read16(REG_VBUS, &raw);
    if (rc == I2C_BUS_OK) {

        // step size (LSB): 3.125 mV per count
        const float step_mv = 3.125f;

        // convert the adc count to a measured value in mV
        *mv = (float)raw * step_mv;
    }

    return rc;
}

int ina237_read_current_mA(float *ma) {
    // calculated from the shunt IN+ - IN-, this can be positive or negative, can read a signed adc count value
    uint16_t raw;
    int rc = read16(REG_CURRENT, &raw);
    if (rc == I2C_BUS_OK) {

        // 16-bit two's complement filling all bits: bit 15 is the sign, so a cast sign-extends
        int16_t count = (int16_t)raw;

        // step size (LSB): Current [A] = CURRENT_LSB x CURRENT
        //   CURRENT_LSB (A per count) was chosen in init (Imax / 2^15); x 1000 for mA
        const float step_ma = current_lsb * 1000.0f;

        // signed count * multiplier
        *ma = (float)count * step_ma;
    }

    return rc;
}

int ina237_read_power_mW(float *mw) {
    // calculated by the chip from VBUS x |CURRENT|: always positive (check CURRENT's sign for direction)
    uint32_t raw;
    int rc = read24(REG_POWER, &raw);
    if (rc == I2C_BUS_OK) {

        // step size (LSB): Power [W] = 0.2 x CURRENT_LSB x POWER; x 1000 for mW
        const float step_mw = 0.2f * current_lsb * 1000.0f;

        // unsigned and uses all 24 bits [23:0]: no shift, no sign extension
        uint32_t count = raw;

        // unsigned count * multiplier
        *mw = (float)count * step_mw;
    }

    return rc;
}

int ina237_read_shunt_uV(float *uv) {
    // raw shunt ADC, IN+ - IN- (differential): signed, + = current flowing IN+ to IN-
    uint16_t raw;
    int rc = read16(REG_SHUNT, &raw);
    if (rc == I2C_BUS_OK) {
        // 16-bit two's complement filling all bits: a cast sign-extends
        int16_t count = (int16_t)raw;

        // step size (LSB): 1.25 uV per count with ADCRANGE = 1 (set in init)
        const float step_uv = 1.25f;

        // signed count * multiplier
        *uv = (float)count * step_uv;
    }

    return rc;
}

int ina237_read_die_temp_C(float *c) {
    // INA237's own die temperature, signed (can read below 0 C)
    uint16_t raw;
    int rc = read16(REG_DIETEMP, &raw);
    if (rc == I2C_BUS_OK) {

        // signed 12-bit value in [15:4]: cast keeps the sign in bit 15, >> 4 sign-extends down to [11:0]
        int16_t count = (int16_t)((int16_t)raw >> 4);

        // step size (LSB): 125 m°C per count = 0.125 °C
        const float step_c = 0.125f;

        // signed count * multiplier
        *c = (float)count * step_c;
    }

    return rc;
}

int ina237_init(uint8_t addr, float shunt_ohms, float max_current_a) {
    uint16_t id;
    int rc;

    dev_addr = addr;

    // MFG_ID (0x3E) is read-only and must read 0x5449 = "TI"
    rc = read16(REG_MFG_ID, &id);
    if (rc != I2C_BUS_OK) return rc;
    if (id != 0x5449) return INA237_ERR_ID;

    // confirm die ID, bits 15:4
    rc = read16(REG_DEV_ID, &id);
    if (rc != I2C_BUS_OK) return rc;
    if ((id >> 4) != DEV_ID_INA237) return INA237_ERR_ID;

    // software reset: all registers back to datasheet defaults
    rc = write16(REG_CONFIG, CONFIG_RST);
    if (rc != I2C_BUS_OK) return rc;

    // set adc range
    rc = write16(REG_CONFIG, CONFIG_ADC_RANGE);
    if (rc != I2C_BUS_OK) return rc;

    // max_current_a: pick above the real max (PCB: BQ25570 limit 285 mA -> 0.4 A, breakout bench -> 0.5 A)
    // CURRENT is 16-bit signed: 2^16 codes split +/-, so 2^15 = 32768 counts each way
    // CURRENT_LSB = Imax / 2^15
    current_lsb = max_current_a / 32768.0f;

    // must multiply by 4 when using adcrange=1
    // 819.2e6f fixed constant in the INA237 SHUNT_CAL equation to set scaling
    // Sunflower: 819.2e6 x (0.4 / 32768) x 0.100 x 4 = 4000
    uint16_t cal = (uint16_t)(819.2e6f * current_lsb * shunt_ohms * 4.0f + 0.5f); // round to nearest
    rc = write16(REG_SHUNT_CAL, cal & 0x7FFFu); // bit 15 is reserved, mask it out
    if (rc != I2C_BUS_OK) return rc;

    // configure ADC: continuous mode, conversion time, averaging
    rc = write16(REG_ADC_CONFIG, ADC_MODE_CONT_ALL | ADC_CONVERSION_TIME | ADC_AVG_16);
    if (rc != I2C_BUS_OK) return rc;

    return I2C_BUS_OK;
}

void ina237_dump_regs(void) {
    // raw register snapshot for the debugger: every field keeps its last good read
    uint16_t r16;
    uint32_t r24;

    // configuration: confirms init's writes landed
    if (read16(REG_CONFIG, &r16)     == I2C_BUS_OK) ina237_regs.config     = r16;
    if (read16(REG_ADC_CONFIG, &r16) == I2C_BUS_OK) ina237_regs.adc_config = r16;
    if (read16(REG_SHUNT_CAL, &r16)  == I2C_BUS_OK) ina237_regs.shunt_cal  = r16;

    // measurements: raw bits, not converted (decode by hand: sign, shift, x step)
    if (read16(REG_SHUNT, &r16)      == I2C_BUS_OK) ina237_regs.vshunt     = r16;
    if (read16(REG_VBUS, &r16)       == I2C_BUS_OK) ina237_regs.vbus       = r16;
    if (read16(REG_DIETEMP, &r16)    == I2C_BUS_OK) ina237_regs.dietemp    = r16;
    if (read16(REG_CURRENT, &r16)    == I2C_BUS_OK) ina237_regs.current    = r16;
    if (read24(REG_POWER, &r24)      == I2C_BUS_OK) ina237_regs.power      = r24;

    // status and identity
    if (read16(REG_DIAG_ALRT, &r16)  == I2C_BUS_OK) ina237_regs.diag_alrt  = r16;
    if (read16(REG_MFG_ID, &r16)     == I2C_BUS_OK) ina237_regs.mfg_id     = r16;
    if (read16(REG_DEV_ID, &r16)     == I2C_BUS_OK) ina237_regs.dev_id     = r16;
}
