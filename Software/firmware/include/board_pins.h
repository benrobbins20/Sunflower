/* Sunflower pin map (BRD4187C prototype). Replaces Studio's Pin Tool pin_config.h.
 * Update this one file for the BGM220P PCB. */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#include "em_gpio.h"

/* I2C0 -> INA228 (expansion header) */
#define I2C_SCL_PORT   gpioPortC
#define I2C_SCL_PIN    5
#define I2C_SDA_PORT   gpioPortC
#define I2C_SDA_PIN    7

/* TIMER0 CC0/CC1 -> SG90 servos */
#define SERVO_1_PORT   gpioPortA
#define SERVO_1_PIN    5
#define SERVO_2_PORT   gpioPortA
#define SERVO_2_PIN    6

/* IADC light sensors */
#define PHOTO_1_PORT   gpioPortA
#define PHOTO_1_PIN    7
#define PHOTO_2_PORT   gpioPortD
#define PHOTO_2_PIN    2
#define PHOTO_3_PORT   gpioPortC
#define PHOTO_3_PIN    0
#define PHOTO_4_PORT   gpioPortB
#define PHOTO_4_PIN    5

#endif
