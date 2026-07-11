/* SPDX-License-Identifier: GPL-2.0-only */

#include "touch_coord.h"

#include "../common/eink_panel.h"

#include <stddef.h>

/*
 * Touch reports native 7680x4320 with axes swapped vs the panel. Match the
 * reference USBHIDAPI.cpp mapping, then clamp to the visible panel.
 */
int touch_coord_display_x(int raw_x, int raw_y)
{
	int x;
	int y_scaled;

	(void)raw_x;

	y_scaled = raw_y * EINK_PANEL_WIDTH / EINK_TOUCH_NATIVE_HEIGHT;
	x = EINK_PANEL_WIDTH - y_scaled;

	if (x < 0)
		x = 0;
	if (x >= EINK_PANEL_WIDTH)
		x = EINK_PANEL_WIDTH - 1;

	return x;
}

int touch_coord_display_y(int raw_x, int raw_y)
{
	int y;

	(void)raw_y;

	y = raw_x * EINK_PANEL_HEIGHT / EINK_TOUCH_NATIVE_WIDTH;

	if (y < 0)
		y = 0;
	if (y >= EINK_PANEL_HEIGHT)
		y = EINK_PANEL_HEIGHT - 1;

	return y;
}
