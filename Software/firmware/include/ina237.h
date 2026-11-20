/* INA237 85 V, 16-bit power monitor (TI). Driver on top of i2c_bus.
 * Fitted on the Sunflower rev2 PCB: marking 237I, DEVICE_ID 0x2381.
 * (Schematic/BOM list the pin-compatible INA228, which this driver does not support.)
 * Datasheet: Documentation/ina237.pdf (register map, 8.1.2 calibration math).
 * All registers are big-endian (MSB first). Functions return 0 (I2C_BUS_OK)
 * on success, a negative I2C_BUS_* code on bus errors, or INA237_ERR_ID. */
#ifndef INA237_H
#define INA237_H
#include <stdint.h>

#define INA237_ADDR_DEFAULT  0x40     /* A1=A0=GND, datasheet Table 7-2 */
#define INA237_ERR_ID        (-10)    /* answered, but MFG_ID != 0x5449 ("TI") or not an INA237 */

/* Check the chip, reset it, select the +-40.96 mV shunt range, program
 * SHUNT_CAL from the shunt value and expected max current, start continuous
 * conversions (16x averaging).
 *   Sunflower PCB: ina237_init(0x40, 0.100f, 0.4f)
 *   (the SparkFun breakout carries an INA228, which this driver does not support) */
int ina237_init(uint8_t addr, float shunt_ohms, float max_current_a);

/* Latest averaged results, converted to real units. */
int ina237_read_bus_mV(float *mv);        /* VBUS pin to GND               */
int ina237_read_shunt_uV(float *uv);      /* across IN+/IN- (signed)       */
int ina237_read_current_mA(float *ma);    /* signed: + = IN+ to IN- flow   */
int ina237_read_power_mW(float *mw);      /* VBUS x current, always >= 0   */
int ina237_read_die_temp_C(float *c);     /* the chip's own temperature    */

/* Debug: raw register snapshot. ina237_dump_regs() reads every register below
 * into ina237_regs; inspect it in the debugger's Watch panel (set to hex) to
 * check init's writes and decode raw counts by hand. Values are the raw bits,
 * not converted: use the read functions above for real units. */
typedef struct {
    uint16_t config;      /* 0x00  expect 0x0010 (ADCRANGE = 1)                 */
    uint16_t adc_config;  /* 0x01  expect 0xFB6A (continuous, 1052 us, avg 16)  */
    uint16_t shunt_cal;   /* 0x02  expect 4000 / 0x0FA0 (PCB: 100 mOhm, 0.4 A)  */
    uint16_t vshunt;      /* 0x04  16-bit signed, 1.25 uV/LSB (ADCRANGE = 1)    */
    uint16_t vbus;        /* 0x05  16-bit, 3.125 mV/LSB                         */
    uint16_t dietemp;     /* 0x06  signed 12-bit count in [15:4], 125 mC/LSB    */
    uint16_t current;     /* 0x07  16-bit signed, x CURRENT_LSB                 */
    uint32_t power;       /* 0x08  24-bit unsigned [23:0]                       */
    uint16_t diag_alrt;   /* 0x0B  alert config + flags                         */
    uint16_t mfg_id;      /* 0x3E  expect 0x5449 ("TI")                         */
    uint16_t dev_id;      /* 0x3F  expect 0x2381 (INA237, rev 1)                */
} ina237_regs_t;

extern volatile ina237_regs_t ina237_regs;   /* defined in ina237.c */

void ina237_dump_regs(void);

#endif
