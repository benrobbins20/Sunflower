/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
#include "sl_bt_api.h"
#include "sl_main_init.h"
#include "app_assert.h"
#include "app.h"
#include "i2c_bus.h"
#include "uart_console.h"
#include <stdio.h>
#include "SEGGER_RTT.h"
#include <stdarg.h>
#include <stdint.h>

#define INA228_ADDR       0x40    // breakout default (A1=A0=GND), confirmed by scan
#define INA228_REG_MFG_ID 0x3E

// rtt_printf: printf-style output straight to RTT channel 0.
// SEGGER_RTT_printf lives in SEGGER_RTT_printf.c, which the segger_rtt component doesn't build,
// so format with vsnprintf (newlib) and push the string with SEGGER_RTT_WriteString (SEGGER_RTT.c).
static void rtt_printf(const char *fmt, ...)
{
	// start commander (like gdb server)
	// & "C:\Program Files\SEGGER\JLink_V*\JLink.exe" -device EFR32MG24B220F1536IM48 -if SWD -speed 4000 -autoconnect 1
	char buf[128];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	// RAM address where this line lands in the RTT up-buffer (channel 0):
	// buffer start + current write offset. _SEGGER_RTT is the control block J-Link scans for.
	uintptr_t addr = (uintptr_t)_SEGGER_RTT.aUp[0].pBuffer + _SEGGER_RTT.aUp[0].WrOff;
	char tag[16];
	snprintf(tag, sizeof(tag), "[0x%08lX] ", (unsigned long)addr);

	SEGGER_RTT_WriteString(0, tag);   // prefix, then the message
	SEGGER_RTT_WriteString(0, buf);
}

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;

// Application Init.
void app_init(void)
{
	uart_console_init();
	uart_printf("Sunflower boot (UART)\n");
	i2c_bus_init();
	

	uint8_t mfg_id[2] = {0};    // MANUFACTURER_ID is 16-bit: expect {0x54, 0x49} "TI"
	volatile int rc = i2c_bus_read_reg(INA228_ADDR, INA228_REG_MFG_ID, mfg_id, 2);
	(void)rc;
	volatile uint8_t found[16] = {0};
	for (uint8_t a=0x40;a<=0x45;a++) {
		// is anyone home
		found[a - 0x40] = i2c_bus_write(a, NULL, 0) == I2C_BUS_OK;		
	}

	 uint8_t id40[2] = {0};
	 volatile int rc40 = i2c_bus_read_reg(0x40, INA228_REG_MFG_ID, id40, 2);
	(void)rc40;

	// rtt or printf f the manufacturing stuff
	printf("INA228 Manufacturer ID: 0x%02X%02X\n", mfg_id[0], mfg_id[1]);
	for (uint8_t a=0x40;a<=0x45;a++) {
		printf("I2C address 0x%02X found: %s\n", a, found[a - 0x40] ? "yes" : "no");
	}
	printf("I2C address 0x40 Manufacturer ID: 0x%02X%02X\n", id40[0], id40[1]);

	rtt_printf("INA228 Manufacturer ID: 0x%02X%02X\n", mfg_id[0], mfg_id[1]);
	uart_printf("INA228 Manufacturer ID: 0x%02X%02X\n", id40[0], id40[1]);
	for (uint8_t a=0x40;a<=0x45;a++) {
		rtt_printf("I2C address 0x%02X found: %s\n", a, found[a - 0x40] ? "yes" : "no");
	}
	rtt_printf("I2C address 0x40 Manufacturer ID: 0x%02X%02X\n", id40[0], id40[1]);
}



// Application Process Action.
void app_process_action(void)
{
	if (app_is_process_required()) {
		
	}
}

/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the default weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
	sl_status_t sc;

	switch (SL_BT_MSG_ID(evt->header)) {
		// -------------------------------
		// This event indicates the device has started and the radio is ready.
		// Do not call any stack command before receiving this boot event!
		case sl_bt_evt_system_boot_id:
			// Create an advertising set.
			sc = sl_bt_advertiser_create_set(&advertising_set_handle);
			app_assert_status(sc);

			// Generate data for advertising
			sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
																								 sl_bt_advertiser_general_discoverable);
			app_assert_status(sc);

			// Set advertising interval to 100ms.
			sc = sl_bt_advertiser_set_timing(
				advertising_set_handle,
				160, // min. adv. interval (milliseconds * 1.6)
				160, // max. adv. interval (milliseconds * 1.6)
				0,   // adv. duration
				0);  // max. num. adv. events
			app_assert_status(sc);
			// Start advertising and enable connections.
			sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
																				 sl_bt_legacy_advertiser_connectable);
			app_assert_status(sc);
			break;

		// -------------------------------
		// This event indicates that a new connection was opened.
		case sl_bt_evt_connection_opened_id:
			break;

		// -------------------------------
		// This event indicates that a connection was closed.
		case sl_bt_evt_connection_closed_id:
			// Generate data for advertising
			sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
																								 sl_bt_advertiser_general_discoverable);
			app_assert_status(sc);

			// Restart advertising after client has disconnected.
			sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
																				 sl_bt_legacy_advertiser_connectable);
			app_assert_status(sc);
			break;

		///////////////////////////////////////////////////////////////////////////
		// Add additional event handlers here as your application requires!      //
		///////////////////////////////////////////////////////////////////////////

		// -------------------------------
		// Default event handler.
		default:
			break;
	}
}
