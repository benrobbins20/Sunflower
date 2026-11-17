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
 *   SparkFun breakout: ina228_init(0x40, 0.015f, 1.0f)
 *   Sunflower PCB:     ina228_init(0x40, 0.100f, 0.4f) */
int ina228_init(uint8_t addr, float shunt_ohms, float max_current_a);

/* Latest averaged results, converted to real units. */
int ina228_read_bus_mV(float *mv);        /* VBUS pin to GND               */
int ina228_read_shunt_uV(float *uv);      /* across IN+/IN- (signed)       */
int ina228_read_current_mA(float *ma);    /* signed: + = IN+ to IN- flow   */
int ina228_read_power_mW(float *mw);      /* VBUS x current, always >= 0   */
int ina228_read_die_temp_C(float *c);     /* INA228's own temperature      */

/* Debug: copy the raw registers into ina228_regs (see ina228.c) so they can
 * be inspected in the debugger's Watch panel. */
void ina228_dump_regs(void);

#endif
