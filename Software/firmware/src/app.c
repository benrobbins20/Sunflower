/* SPDX-License-Identifier: MIT */
#include "app.h"
#include "app_config.h"
#include "workload.h"
#include "em_core.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include <stdio.h>
#include "i2c_bus.h"
#include "ina237.h"
#include "servo.h"
#include "adc.h"
#include "board_pins.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "sl_bt_api.h"
#include "app_assert.h"
#include "gatt_db.h"   /* generated from config/btconf/gatt_configuration.btconf */

/* Timers post work; the main loop owns processing. Keep handles alive globally. */
static sl_sleeptimer_timer_handle_t period_timer;
/* volatile makes ISR updates visible; atomic sections protect compound changes. */
static volatile uint32_t pending_jobs;
static volatile bool init_failed;
volatile app_stats_t app_stats;

/* 1 = print the INA237 readings every second over RTT. 0 = still read them into
 * the ina_* variables below (watch them in the debugger/Commander), but don't print. */
#define APP_LOG_INA 0

static int ina_rc;
static volatile float ina_bus_mv, ina_shunt_uv, ina_current_ma, ina_power_mw, ina_temp_c;

/* Servo ramp: the 20 ms timer only sets a flag; the main loop calls servo_ramp_step(). */
#define SERVO_RAMP_MS        20u
#define SERVO_TEST_PERIOD_S  5u    /* test sweep: flip the target between 0 and 180 deg */
static sl_sleeptimer_timer_handle_t servo_timer;
static volatile bool servo_pending;
static uint32_t servo_test_seconds;
static uint8_t servo_test_target = 90;
/* The test sweep runs until a phone connects; then Bluetooth owns the targets. */
static bool servo_test_enabled = true;

/* Bluetooth advertising set, allocated by the stack at boot. */
static uint8_t advertising_set_handle = 0xff;

static void servo_elapsed(sl_sleeptimer_timer_handle_t *timer, void *data)
{
	(void)timer;
	(void)data;
	/* Interrupt context: only post the work. Missed ticks coalesce into one step. */
	servo_pending = true;
}

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
	sl_status_t status = SL_STATUS_OK;

	// start INA237 
	i2c_bus_init();

	// IADC: FSRCO clock, VDD reference, single conversions
	adc_init();

	/* Debug: read the ID registers directly, before the driver checks them. */
	uint8_t raw[2] = {0, 0};
	int id_rc = i2c_bus_read_reg(INA237_ADDR_DEFAULT, 0x3E, raw, 2);   /* MFG_ID */
	printf("INA237 MFG_ID rc=%d raw=0x%02X%02X (expect 0x5449)\r\n", id_rc, raw[0], raw[1]);
	raw[0] = raw[1] = 0;
	id_rc = i2c_bus_read_reg(INA237_ADDR_DEFAULT, 0x3F, raw, 2);       /* DEVICE_ID */
	printf("INA237 DEV_ID rc=%d raw=0x%02X%02X (expect 0x238x)\r\n", id_rc, raw[0], raw[1]);

	ina_rc = ina237_init(INA237_ADDR_DEFAULT, 0.100f, 0.4f);  /* Sunflower PCB: 100 mOhm shunt, 0.4 A max */
	/* 0 = OK; -1 NACK (wrong address / unpowered), -2 bus error, -3 timeout, -10 not an INA237. */
	printf("INA237 init: %d\r\n", ina_rc);

	/* TPS22918 load switch (LD_SW_ON, PB01) powers the servo 5 V rail: turn it on first.
	 * TODO: move into a power/load-switch driver (on/off around moves to save energy). */
	CMU_ClockEnable(cmuClock_GPIO, true);
	GPIO_PinModeSet(LOAD_SW_EN_PORT, LOAD_SW_EN_PIN, gpioModePushPull, 1);
	printf("Load switch on (PB01)\r\n");

	/* Servos: TIMER1 PWM on PB03/PB04, both start centred at 90 deg. */
	servo_init();
	printf("Servo init: 50 Hz on PB03/PB04, center 90 deg\r\n");

	if (status == SL_STATUS_OK) {
		status = sl_sleeptimer_start_periodic_timer_ms(
				&period_timer, APP_PERIOD_MS, period_elapsed, NULL, 0, 0);
	}
	if (status == SL_STATUS_OK) {
		status = sl_sleeptimer_start_periodic_timer_ms(
				&servo_timer, SERVO_RAMP_MS, servo_elapsed, NULL, 0, 0);
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
	bool run_servo;

#if APP_MODE == APP_MODE_DEMO
	bool run_breath;
#endif

	/* Read + decrement is a transaction. Volatile alone cannot protect it. */
	CORE_ENTER_ATOMIC();
	run_job = pending_jobs != 0u;
	if (run_job) {
		--pending_jobs;
	}
	run_servo = servo_pending;
	servo_pending = false;

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

	if (run_servo) {
		/* Move each servo 1 deg toward its target (50 deg/s). */
		servo_ramp_step();
	}

	if (run_job) {
		uint32_t start = sl_sleeptimer_get_tick_count();

		/* Test sweep (until Bluetooth sets targets): every SERVO_TEST_PERIOD_S seconds
		 * flip both targets between 0 and 180 deg. The ramp takes ~3.6 s to get there. */
		if (servo_test_enabled && ++servo_test_seconds >= SERVO_TEST_PERIOD_S) {
			servo_test_seconds = 0;
			servo_test_target = (servo_test_target == 180u) ? 0u : 180u;
			servo_set_target_angle(SERVO_1, servo_test_target);
			servo_set_target_angle(SERVO_2, servo_test_target);
			printf("Servo target: %u deg\r\n", (unsigned)servo_test_target);
		}

		/* Read the INA237 once per second (polled I2C, a few ms total). Skip if init failed. */
		if (ina_rc == I2C_BUS_OK) {
			ina237_read_bus_mV((float *)&ina_bus_mv);
			ina237_read_shunt_uV((float *)&ina_shunt_uv);
			ina237_read_current_mA((float *)&ina_current_ma);
			ina237_read_power_mW((float *)&ina_power_mw);
			ina237_read_die_temp_C((float *)&ina_temp_c);
			ina237_dump_regs();
#if APP_LOG_INA
			/* newlib-nano printf has no %f, so print scaled integers: uA and uW keep small values visible. */
			printf("INA237: %ld mV  %ld uA  %ld uW  %ld C\r\n",
						 (long)ina_bus_mv,
						 (long)(ina_current_ma * 1000.0f),
						 (long)(ina_power_mw * 1000.0f),
						 (long)ina_temp_c);
#endif
		}

		// ADC test: photodiode 1 and battery sense pin, in mV
		uint32_t pd1_mv, vbat_mv;
		if (adc_read_mV(PHOTO_1_PORT, PHOTO_1_PIN, &pd1_mv) == ADC_OK
				&& adc_read_mV(VBAT_SENSE_PORT, VBAT_SENSE_PIN, &vbat_mv) == ADC_OK) {
			printf("PD1: %lu mV, VBAT: %lu mV\r\n", (unsigned long)pd1_mv, (unsigned long)vbat_mv);
		}
		else {
			printf("ADC read failed\r\n");
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
	return pending_jobs == 0u && !servo_pending && !init_failed;
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
	return pending_jobs != 0u || servo_pending || init_failed
			? SL_POWER_MANAGER_WAKEUP : SL_POWER_MANAGER_IGNORE;
}

/* Start (or restart) connectable advertising, 100 ms interval. */
static void advertising_start(void)
{
	sl_status_t sc;
	/* Advertising data: flags + the device name from the GATT database ("Sunflower"). */
	sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
																						 sl_bt_advertiser_general_discoverable);
	app_assert_status(sc);
	sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
																		 sl_bt_legacy_advertiser_connectable);
	app_assert_status(sc);
}

/* Bluetooth stack events. Overrides the SDK's weak default.
 * NOT an interrupt: the stack calls this from sl_system_process_action() in the main loop. */
void sl_bt_on_event(sl_bt_msg_t *evt)
{
	sl_status_t sc;

	switch (SL_BT_MSG_ID(evt->header)) {
		/* Radio is ready. Do not call any stack command before this event. */
		case sl_bt_evt_system_boot_id: {
			bd_addr address;
			uint8_t address_type;
			sc = sl_bt_system_get_identity_address(&address, &address_type);
			app_assert_status(sc);
			/* Stored little-endian: print backwards so it matches what a phone shows. */
			printf("BLE boot, address %02X:%02X:%02X:%02X:%02X:%02X\r\n",
						 address.addr[5], address.addr[4], address.addr[3],
						 address.addr[2], address.addr[1], address.addr[0]);

			sc = sl_bt_advertiser_create_set(&advertising_set_handle);
			app_assert_status(sc);
			/* Interval in 0.625 ms units: 160 = 100 ms. Duration/events 0 = forever. */
			sc = sl_bt_advertiser_set_timing(advertising_set_handle, 160, 160, 0, 0);
			app_assert_status(sc);
			advertising_start();
			printf("BLE advertising as \"Sunflower\"\r\n");
			break;
		}

		case sl_bt_evt_connection_opened_id:
			/* A phone owns the servos now: stop the test sweep. */
			servo_test_enabled = false;
			printf("BLE connected (test sweep off)\r\n");
			break;

		case sl_bt_evt_connection_closed_id:
			printf("BLE disconnected, reason 0x%04X\r\n",
						 (unsigned)evt->data.evt_connection_closed.reason);
			/* Advertising stops on connect; restart it so the phone can reconnect. */
			advertising_start();
			break;

		/* A phone wrote a characteristic. Servo A/B: 1 byte, 0-180 degrees. */
		case sl_bt_evt_gatt_server_attribute_value_id: {
			uint16_t att = evt->data.evt_gatt_server_attribute_value.attribute;
			uint8_t len = evt->data.evt_gatt_server_attribute_value.value.len;
			uint8_t angle = evt->data.evt_gatt_server_attribute_value.value.data[0];

			if (len != 1 || angle > 180u) {
				printf("BLE write ignored: len %u, value %u\r\n", (unsigned)len, (unsigned)angle);
				break;
			}
			/* Only set the target: the 20 ms ramp moves the servo there. */
			if (att == gattdb_servo_a) {
				servo_set_target_angle(SERVO_1, angle);
				printf("BLE Servo A target: %u deg\r\n", (unsigned)angle);
			} else if (att == gattdb_servo_b) {
				servo_set_target_angle(SERVO_2, angle);
				printf("BLE Servo B target: %u deg\r\n", (unsigned)angle);
			}
			break;
		}

		default:
			break;
	}
}
