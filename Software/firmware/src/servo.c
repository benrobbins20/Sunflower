/* SG90 servo driver for the Sunflower rev2 PCB.
 * Ported from the old EFR32MG24 project (git show 4720d3d^:Software/servo.c).
 * Changes: pins come from board_pins.h (SERVO_1 = PB03, SERVO_2 = PB04), and
 * TIMER1 replaces TIMER0, because the demo's pwm led0 instance owns TIMER0. */
#include "servo.h"

#include "em_device.h"
#include "board_pins.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_timer.h"
#include "sl_power_manager.h"
#include <stdint.h>

#define SERVO_MIN_US 500
#define SERVO_CENTER_US 1500
#define SERVO_MAX_US 2500
#define SERVO_PERIOD_US 20000
#define SERVO_RAMP_STEP_DEG 1

/* TIMER1 is 16-bit on the EFR32BG22 (TIMER0 is 32-bit): TOP must fit in 65535.
 * 38.4 MHz HFXO / 16 prescale = 2.4 MHz, so 20 ms = 48000 ticks: fits. */
#define SERVO_TIMER        TIMER1
#define SERVO_TIMER_CLOCK  cmuClock_TIMER1
#define SERVO_TIMER_INDEX  1   /* GPIO->TIMERROUTE[n] */

static uint32_t timer_frequency_hz;
static uint8_t current_angle[SERVO_COUNT] = {90, 90};
static uint8_t target_angle[SERVO_COUNT]  = {90, 90};


void servo_init(void) {
    // 1. Enable/configure TIMER1
    // 2. Configure CC0 and CC1 for PWM
    // 3. Route CC0 -> SERVO_1 pin
    // 4. Route CC1 -> SERVO_2 pin
    // 5. Set PWM period to 20 ms / 50 Hz
    // 6. Start both servos at 1500 us
    // The timer needs the high-frequency clock, which stops in EM2: stay in EM1 or above.
    sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);

    CMU_ClockEnable(cmuClock_GPIO, true);
    CMU_ClockEnable(SERVO_TIMER_CLOCK, true);

    GPIO_PinModeSet(SERVO_1_PORT, SERVO_1_PIN, gpioModePushPull, 0);
    GPIO_PinModeSet(SERVO_2_PORT, SERVO_2_PIN, gpioModePushPull, 0);

    TIMER_Init_TypeDef timer_init = TIMER_INIT_DEFAULT;
    timer_init.prescale = timerPrescale16;
    timer_init.enable = false;
    TIMER_Init(SERVO_TIMER, &timer_init);

    uint32_t timer_input_hz = CMU_ClockFreqGet(SERVO_TIMER_CLOCK);
    timer_frequency_hz = timer_input_hz / 16;

    TIMER_InitCC_TypeDef cc_init = TIMER_INITCC_DEFAULT;
    cc_init.mode = timerCCModePWM;
    TIMER_InitCC(SERVO_TIMER, 0, &cc_init);
    TIMER_InitCC(SERVO_TIMER, 1, &cc_init);

    // PWM period in timer ticks (must stay <= 65535 on 16-bit TIMER1)
    uint32_t period_ticks = ((uint64_t)SERVO_PERIOD_US * timer_frequency_hz) / 1000000UL;
    TIMER_TopSet(SERVO_TIMER, period_ticks - 1);

    // Series 2: any timer channel can go to any GPIO; the route registers pick the pin
    GPIO->TIMERROUTE[SERVO_TIMER_INDEX].CC0ROUTE = ((uint32_t)SERVO_1_PORT << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT) | (SERVO_1_PIN << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);
    GPIO->TIMERROUTE[SERVO_TIMER_INDEX].CC1ROUTE = ((uint32_t)SERVO_2_PORT << _GPIO_TIMER_CC1ROUTE_PORT_SHIFT) | (SERVO_2_PIN << _GPIO_TIMER_CC1ROUTE_PIN_SHIFT);
    GPIO->TIMERROUTE[SERVO_TIMER_INDEX].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN | GPIO_TIMER_ROUTEEN_CC1PEN;

    servo_set_us(SERVO_1, SERVO_CENTER_US);
    servo_set_us(SERVO_2, SERVO_CENTER_US);

    TIMER_Enable(SERVO_TIMER, true);
}

void servo_set_us(servo_t servo, uint16_t pulse_us) {
    if (pulse_us < SERVO_MIN_US) {
        pulse_us = SERVO_MIN_US;
    }
    if (pulse_us > SERVO_MAX_US) {
        pulse_us = SERVO_MAX_US;
    }

    uint32_t ticks = ((uint64_t)pulse_us * timer_frequency_hz) / 1000000UL;

    if (servo == SERVO_1) {
        TIMER_CompareSet(SERVO_TIMER, 0, ticks);
    }
    if (servo == SERVO_2) {
        TIMER_CompareSet(SERVO_TIMER, 1, ticks);
    }
}

void servo_set_angle(servo_t servo, uint8_t angle_deg) {
    // clamp angle (unsigned) within 180 full sweep
    if (angle_deg > 180) {
        angle_deg = 180;
    }
    // full sweep in terms of PWM range
    uint16_t pulse_range = SERVO_MAX_US - SERVO_MIN_US;
    // angle scaled to us range
    uint32_t pulse_scaled = (uint32_t)angle_deg * pulse_range;
    // ratio of requested angle versus full sweep
    uint16_t pulse_offset = pulse_scaled / 180;
    // offset the pulse width from the minimum pulse width
    uint16_t pulse_us = SERVO_MIN_US + pulse_offset;

    servo_set_us(servo, pulse_us);
}

void servo_set_target_angle(servo_t servo, uint8_t angle_deg) {
	if (angle_deg > 180) {
		angle_deg = 180;
	}
	if (servo < SERVO_COUNT) {
		target_angle[servo] = angle_deg;
	}
}

void servo_ramp_step(void) {
	for (servo_t i = 0; i < SERVO_COUNT; i++) {
		// nothing to do
		if (current_angle[i] == target_angle[i]) {
			continue;
		}
		// move up to target angle
		if (current_angle[i] < target_angle[i]) {
			uint8_t remaining = target_angle[i] - current_angle[i];
			uint8_t step = (remaining < SERVO_RAMP_STEP_DEG) ? remaining : SERVO_RAMP_STEP_DEG;
			current_angle[i] += step;
		}
		// move down to target angle
		else {
			uint8_t remaining = current_angle[i] - target_angle[i];
			uint8_t step = (remaining < SERVO_RAMP_STEP_DEG) ? remaining : SERVO_RAMP_STEP_DEG;
			current_angle[i] -= step;
		}
		servo_set_angle(i, current_angle[i]);
	}
}

void servo_disable(void) {
    TIMER_Enable(SERVO_TIMER, false);
    GPIO_PinOutClear(SERVO_1_PORT, SERVO_1_PIN);
    GPIO_PinOutClear(SERVO_2_PORT, SERVO_2_PIN);
    sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
}
