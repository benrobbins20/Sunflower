//servo.h
#ifndef SERVO_H
#define SERVO_H
#include <stdint.h>

void servo_init(void);
void servo_set_us(uint8_t servo, uint16_t pulse_us);
void servo_set_angle(uint8_t servo, uint16_t angle_deg);

void servo_disable();

#endif // SERVO_H