/* SPDX-License-Identifier: MIT */
#include "app.h"
#include "app_config.h"
#include "workload.h"
#include "kit_demo.h"
#include "em_core.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include <stdio.h>
#include "i2c_bus.h"
#include "ina228.h"

/* Timers post work; the main loop owns processing. Keep handles alive globally. */
static sl_sleeptimer_timer_handle_t period_timer;
/* volatile makes ISR updates visible; atomic sections protect compound changes. */
static volatile uint32_t pending_jobs;
static volatile bool init_failed;
volatile app_stats_t app_stats;

static int ina_rc;
static volatile float ina_bus_mv, ina_shunt_uv, ina_current_ma, ina_power_mw, ina_temp_c;

#if APP_MODE == APP_MODE_DEMO
/* Animation uses a single pending flag; workload uses a bounded event count. */
static sl_sleeptimer_timer_handle_t breath_timer;
static volatile bool breath_pending;
static uint8_t breath_duty;
static bool breath_rising = true;

static void breath_elapsed(sl_sleeptimer_timer_handle_t *timer, void *data)
{
  (void)timer;
  (void)data;
  /* Coalesce delayed updates instead of replaying them in a burst. */
  breath_pending = true;
}
#endif

static void period_elapsed(sl_sleeptimer_timer_handle_t *timer, void *data)
{
  (void)timer;
  (void)data;
  ++app_stats.timer_events;
  /* Overflow is observable in app_stats rather than silently losing all timing evidence. */
  if (pending_jobs < APP_QUEUE_LIMIT) {
    ++pending_jobs;
  } else {
    ++app_stats.dropped_events;
  }
  /* Interrupt context: no processing, printing, blocking I/O, or sleep call. */
}

void app_init(void)
{
  printf("Sunflower boot\r\n");
  /* Expose the compiled mode so the debugger can confirm the flashed image. */
  app_stats.mode = APP_MODE;
#if APP_MODE == APP_MODE_EM1 || APP_MODE == APP_MODE_DEMO
  /* TIMER PWM needs the high-frequency clock in EM1; EM2 stops that clock. */
  sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
#endif
  /* Add project driver initialization here and check each returned status. */
  sl_status_t status = kit_demo_init();

  // start INA228 
  i2c_bus_init();
  ina_rc = ina228_init(INA228_ADDR_DEFAULT, 0.100f, 0.4f);  /* Sunflower PCB: 100 mOhm shunt, 0.4 A max */

  if (status == SL_STATUS_OK) {
    status = sl_sleeptimer_start_periodic_timer_ms(
        &period_timer, APP_PERIOD_MS, period_elapsed, NULL, 0, 0);
  }
#if APP_MODE == APP_MODE_DEMO
  /* This timer changes brightness; the hardware PWM timer supplies the carrier. */
  if (status == SL_STATUS_OK) {
    status = sl_sleeptimer_start_periodic_timer_ms(
        &breath_timer, APP_LED_BREATH_STEP_MS, breath_elapsed, NULL, 0, 0);
  }
#endif
  /* Latch startup failures so the sleep hooks leave the CPU available to debug. */
  app_stats.init_status = status;
  init_failed = status != SL_STATUS_OK;
}

void app_process_action(void)
{
  // macro for irqState: saves the interrupt on/off state
  // 
  CORE_DECLARE_IRQ_STATE;
  bool run_job;

#if APP_MODE == APP_MODE_DEMO
  bool run_breath;
#endif

  /* Read + decrement is a transaction. Volatile alone cannot protect it. */
  CORE_ENTER_ATOMIC();
  run_job = pending_jobs != 0u;
  if (run_job) {
    --pending_jobs;
  }

#if APP_MODE == APP_MODE_DEMO
  run_breath = breath_pending;
  breath_pending = false;
#endif
  CORE_EXIT_ATOMIC();

#if APP_MODE == APP_MODE_DEMO
  if (run_breath) {
    /* One percentage point per update; reverse at 100% and 0%. */
    if (breath_rising) {
      if (++breath_duty == 100u) {
        breath_rising = false;
      }
    } else if (--breath_duty == 0u) {
      breath_rising = true;
    }
    kit_demo_set_led_duty(breath_duty);
  }
#endif

  if (run_job) {
    uint32_t start = sl_sleeptimer_get_tick_count();

    /* Read the INA228 once per second (polled I2C, a few ms total). Skip if init failed. */
    if (ina_rc == I2C_BUS_OK) {
      ina228_read_bus_mV((float *)&ina_bus_mv);
      ina228_read_shunt_uV((float *)&ina_shunt_uv);
      ina228_read_current_mA((float *)&ina_current_ma);
      ina228_read_power_mW((float *)&ina_power_mw);
      ina228_read_die_temp_C((float *)&ina_temp_c);
      ina228_dump_regs();
    }

    /* Debug: how long the job took (sleeptimer ticks, 32768/s) and how many have run. */
    app_stats.last_job_ticks = sl_sleeptimer_get_tick_count() - start;
    ++app_stats.completed_jobs;
  }
}

bool app_is_ok_to_sleep(void)
{
#if APP_MODE == APP_MODE_AWAKE
  /* Reference build: intentionally refuse idle sleep for the energy comparison. */
  return false;
#else
  /* SDK calls this with its sleep-decision critical section held. */
#if APP_MODE == APP_MODE_DEMO
  if (breath_pending) {
    return false;
  }
#endif
  return pending_jobs == 0u && !init_failed;
#endif
}

sl_power_manager_on_isr_exit_t app_sleep_on_isr_exit(void)
{
  /* Wake into the cooperative loop instead of returning directly to sleep. */
  /* A posted event must reach the main loop before another sleep. */
#if APP_MODE == APP_MODE_DEMO
  if (breath_pending) {
    return SL_POWER_MANAGER_WAKEUP;
  }
#endif
  return pending_jobs != 0u || init_failed
      ? SL_POWER_MANAGER_WAKEUP : SL_POWER_MANAGER_IGNORE;
}
