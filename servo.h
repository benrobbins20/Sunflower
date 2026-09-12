//servo.h
#ifndef SERVO_H
#define SERVO_H
#include <stdint.h>

typedef enum {
	SERVO_A = 0,
	SERVO_B = 1,
	SERVO_COUNT,
} servo_t;

void servo_init(void);
void servo_set_us(servo_t servo, uint16_t pulse_us);
void servo_set_angle(servo_t servo, uint8_t angle_deg);
void servo_set_target_angle(servo_t servo, uint8_t angle_deg);
void servo_ramp_step(void);
void servo_disable(void);



#endif // SERVO_H