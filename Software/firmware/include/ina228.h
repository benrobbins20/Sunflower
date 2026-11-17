/* INA228 85 V, 20-bit power monitor (TI). Driver on top of i2c_bus.
 * Datasheet: Documentation/ina228.pdf (SLYS021A, revised May 2022)
 *   Table 7-2 address pins, Table 7-3 register map, 8.1.2 calibration math.
 * All registers are big-endian (MSB first). Functions return 0 (I2C_BUS_OK)
 * on success, a negative I2C_BUS_* code on bus errors, or INA228_ERR_ID. */
#ifndef INA228_H
#define INA228_H
#include <stdint.h>

#define INA228_ADDR_DEFAULT  0x40     /* A1=A0=GND, datasheet Table 7-2 */
#define INA228_ERR_ID        (-10)    /* answered, but MFG_ID != 0x5449 ("TI") */

/* Check the chip, reset it, select the +-40.96 mV shunt range, program
 * SHUNT_CAL from the shunt value and expected max current, start continuous
 * conversions (16x averaging).
 *   SparkFun breakout: ina228_init(0x40, 0.015f, 0.5f)
 *   Sunflower PCB:     ina228_init(0x40, 0.100f, 0.4f) */
int ina228_init(uint8_t addr, float shunt_ohms, float max_current_a);

/* Latest averaged results, converted to real units. */
int ina228_read_bus_mV(float *mv);        /* VBUS pin to GND               */
int ina228_read_shunt_uV(float *uv);      /* across IN+/IN- (signed)       */
int ina228_read_current_mA(float *ma);    /* signed: + = IN+ to IN- flow   */
int ina228_read_power_mW(float *mw);      /* VBUS x current, always >= 0   */
int ina228_read_die_temp_C(float *c);     /* INA228's own temperature      */

/* Debug: raw register snapshot. ina228_dump_regs() reads every register below
 * into ina228_regs; inspect it in the debugger's Watch panel (set to hex) to
 * check init's writes and decode raw counts by hand. Values are the raw bits,
 * not converted: use the read functions above for real units. */
typedef struct {
    uint16_t config;      /* 0x00  expect 0x0010 (ADCRANGE = 1)                 */
    uint16_t adc_config;  /* 0x01  expect 0xFB6A (continuous, 1052 us, avg 16)  */
    uint16_t shunt_cal;   /* 0x02  expect 750 / 0x02EE (breakout), 4000 (PCB)   */
    uint32_t vshunt;      /* 0x04  24-bit, signed 20-bit count in [23:4]        */
    uint32_t vbus;        /* 0x05  24-bit, unsigned 20-bit count in [23:4]      */
    uint16_t dietemp;     /* 0x06  16-bit signed                                */
    uint32_t current;     /* 0x07  24-bit, signed 20-bit count in [23:4]        */
    uint32_t power;       /* 0x08  24-bit unsigned [23:0]                       */
    uint16_t diag_alrt;   /* 0x0B  alert config + flags; bit 1 CNVRF toggles    */
    uint16_t mfg_id;      /* 0x3E  expect 0x5449 ("TI")                         */
    uint16_t dev_id;      /* 0x3F  expect 0x2281 (INA228, rev 1)                */
} ina228_regs_t;

extern volatile ina228_regs_t ina228_regs;   /* defined in ina228.c */

void ina228_dump_regs(void);

#endif
