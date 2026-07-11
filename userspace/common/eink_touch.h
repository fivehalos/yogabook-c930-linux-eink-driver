/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * YogaBook E-Ink touch HID (report 0x90) — shared types.
 */

#ifndef EINK_TOUCH_H
#define EINK_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#define EINK_TOUCH_REPORT_ID	0x90
#define EINK_TOUCH_REPORT_SIZE	11
#define EINK_TOUCH_MAX_CONTACTS	10

enum eink_touch_contact_mode {
	EINK_TOUCH_MODE_UNKNOWN = 0,
	EINK_TOUCH_MODE_DOWN,
	EINK_TOUCH_MODE_MOVE,
	EINK_TOUCH_MODE_UP,
};

struct eink_touch_contact {
	int slot;
	enum eink_touch_contact_mode mode;
	int raw_x;
	int raw_y;
	int raw_z;
	bool new_action;
	int display_x;
	int display_y;
};

struct eink_touch_frame {
	int contact_count;
	struct eink_touch_contact contacts[EINK_TOUCH_MAX_CONTACTS];
};

#endif /* EINK_TOUCH_H */
