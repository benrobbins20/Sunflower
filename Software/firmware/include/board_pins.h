/* Sunflower pin map: rev2 PCB, BGM220PC22HNA (EFR32BG22).
 * Source: rev2 schematic MCU sheet (symbol pin labels are stale; nets are authoritative).
 * App code uses only NAME_PORT / NAME_PIN from this file. */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#include "em_gpio.h"

/* I2C0 -> INA228 (10k pull-ups to V_OUT on the PCB) */
#define I2C_SDA_PORT   gpioPortC   /* module pin 28 */
#define I2C_SDA_PIN    6
#define I2C_SCL_PORT   gpioPortC   /* module pin 29 */
#define I2C_SCL_PIN    7

/* INA228 ALERT, open-drain from the INA228, 10k pull-up; input only */
#define INA_ALERT_PORT gpioPortB   /* module pin 6 (symbol says "VBAT_OK") */
#define INA_ALERT_PIN  0

/* TIMER0 CC0/CC1 -> SG90 servos (PWM_1 = PWM_A, PWM_2 = PWM_B) */
#define SERVO_1_PORT   gpioPortB   /* module pin 3, PWM_A */
#define SERVO_1_PIN    3
#define SERVO_2_PORT   gpioPortB   /* module pin 2, PWM_B */
#define SERVO_2_PIN    4

/* IADC light sensors, 2.2k pull-downs (R_PD1-4) */
#define PHOTO_1_PORT   gpioPortC   /* module pin 22 */
#define PHOTO_1_PIN    0
#define PHOTO_2_PORT   gpioPortC   /* module pin 23 */
#define PHOTO_2_PIN    1
#define PHOTO_3_PORT   gpioPortC   /* module pin 24 */
#define PHOTO_3_PIN    2
#define PHOTO_4_PORT   gpioPortC   /* module pin 25 */
#define PHOTO_4_PIN    3

/* IADC battery voltage: 300k/100k divider, V_BAT/4 (4.2 V -> 1.05 V) */
#define VBAT_SENSE_PORT gpioPortA  /* module pin 11 */
#define VBAT_SENSE_PIN  4

/* TPS22918 load switch enable (LD_SW_ON), push-pull output */
#define LOAD_SW_EN_PORT gpioPortB  /* module pin 5 */
#define LOAD_SW_EN_PIN  1

/* No BOOST_EN on rev2 (TPS61023 EN tied to VBAT; PB02 is NC).
 * No VCOM (debug header pins 4/5 NC): use RTT for logging.
 * Spare / NC: PA00, PA05-PA08, PB02, PD02, PD03. */

#endif
