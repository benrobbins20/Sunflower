#include "servo.h"

#include "bgm220pc22hna.h"
#include "bgm22_gpio.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_timer.h"
#include "sl_power_manager.h"

#define SERVO_MIN_US 500
#define SERVO_CENTER_US 1500
#define SERVO_MAX_US 2500
#define SERVO_PERIOD_US 20000

static uint32_t timer_frequency_hz;


void servo_init(void)
{
    // 1. Enable/configure TIMER0
    // 2. Configure CC0 and CC1 for PWM
    // 3. Route CC0 -> PB3
    // 4. Route CC1 -> PB4
    // 5. Set PWM period to 20 ms / 50 Hz
    // 6. Start both servos at 1500 us
    sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);

    CMU_ClockEnable(cmuClock_GPIO, true);
    CMU_ClockEnable(cmuClock_TIMER0, true);

    GPIO_PinModeSet(gpioPortB, 3, gpioModePushPull, 0);
    GPIO_PinModeSet(gpioPortB, 4, gpioModePushPull, 0);

    TIMER_Init_TypeDef timer_init = TIMER_INIT_DEFAULT;
    timer_init.prescale = timerPrescale16;
    timer_init.enable = false;
    TIMER_Init(TIMER0, &timer_init);

    uint32_t timer_input_hz = CMU_ClockFreqGet(cmuClock_TIMER0);
    timer_frequency_hz = timer_input_hz / 16;

    TIMER_InitCC_TypeDef cc_init = TIMER_INITCC_DEFAULT;
    cc_init.mode = timerCCModePWM;
    TIMER_InitCC(TIMER0, 0, &cc_init);
    TIMER_InitCC(TIMER0, 1, &cc_init);

    // PWM period in terms of seconds
    uint32_t period_ticks = ((uint64_t)SERVO_PERIOD_US * timer_frequency_hz) / 1000000UL;
    TIMER_TopSet(TIMER0, period_ticks - 1);

    GPIO->TIMERROUTE[0].CC0ROUTE = (gpioPortB << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT) | (3 << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);
    GPIO->TIMERROUTE[0].CC1ROUTE = (gpioPortB << _GPIO_TIMER_CC1ROUTE_PORT_SHIFT) | (4 << _GPIO_TIMER_CC1ROUTE_PIN_SHIFT);
    GPIO->TIMERROUTE[0].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN | GPIO_TIMER_ROUTEEN_CC1PEN;

    servo_set_us(0, 2400);
    servo_set_us(1, 2400);

    TIMER_Enable(TIMER0, true);

}

void servo_set_us(uint8_t servo, uint16_t pulse_us) {
    if (pulse_us < SERVO_MIN_US) {
        pulse_us = SERVO_MIN_US;
    }
    if (pulse_us > SERVO_MAX_US) {
        pulse_us = SERVO_MAX_US;
    }

    uint32_t ticks = ((uint64_t)pulse_us * timer_frequency_hz) / 1000000UL;

    if (servo == 0) {
        TIMER_CompareSet(TIMER0, 0, ticks);
    }
    if (servo == 1) {
        TIMER_CompareSet(TIMER0, 1, ticks);
    }
}

void servo_set_angle(uint8_t servo, uint16_t angle_deg) {
    // clamp angle (unsigned) within 180 full sweep
    if (angle_deg > 180) {
        angle_deg = 180;
    }

    // ratio of requested angle versus full sweep
    float angle_ratio = angle_deg / 180.0f;
    // full sweep in terms of PWM range
    float pulse_range = SERVO_MAX_US - SERVO_MIN_US;
    // offset from 0 degrees
    float pulse_offset = angle_ratio * pulse_range;

    uint16_t pulse_us = SERVO_MIN_US + pulse_offset;
    servo_set_us(servo, pulse_us);
}

void servo_disable(void) {
    TIMER_Enable(TIMER0, false);

    GPIO_PinOutClear(gpioPortB, 3);
    GPIO_PinOutClear(gpioPortB, 4);
}
