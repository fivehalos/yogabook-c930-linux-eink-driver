/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * YogaBook E-Ink touch HID — shared types.
 *
 * Report 0x90: single-contact custom (TOUCH_PEN draw path).
 * Report 0x0c: multi-contact digitizer (GET=3 MT latch / Homebar path).
 */

#ifndef EINK_TOUCH_H
#define EINK_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#define EINK_TOUCH_REPORT_ID	0x90
#define EINK_TOUCH_REPORT_SIZE	11

#define EINK_MT_REPORT_ID	0x0c
/* Linux hidraw after mt-arm may use report 0x03; header matches 0x0c layout. */
#define EINK_MT_REPORT_ID_LINUX	0x03
#define EINK_MT_CONTACT_STRIDE	7
#define EINK_MT_HEADER_SIZE	3

#define EINK_TOUCH_MAX_CONTACTS	10

enum eink_touch_report_kind {
	EINK_TOUCH_KIND_NONE = 0,
	EINK_TOUCH_KIND_SINGLE,	/* HID 0x90 */
	EINK_TOUCH_KIND_MT,	/* HID 0x0c */
};

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
	enum eink_touch_report_kind kind;
	int contact_count;
	struct eink_touch_contact contacts[EINK_TOUCH_MAX_CONTACTS];
};

#endif /* EINK_TOUCH_H */
