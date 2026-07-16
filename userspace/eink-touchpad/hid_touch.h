/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EINK_HID_TOUCH_H
#define EINK_HID_TOUCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EINK_HID_PATH_MAX 256

struct eink_hid_touch {
	int fd;
	char path[EINK_HID_PATH_MAX];
	bool grab;
};

struct eink_hid_candidate {
	char path[EINK_HID_PATH_MAX];
	int iface;
	int protocol;
	int has_touch_report;
	int has_pen_report;
	int has_mt_report;
};

int hid_touch_open(struct eink_hid_touch *hid, const char *path_override,
		   bool grab);
void hid_touch_close(struct eink_hid_touch *hid);

/*
 * Blocking read of the next HID input report. Returns bytes read, 0 on EOF,
 * negative errno on error.
 */
int hid_touch_read(struct eink_hid_touch *hid, uint8_t *buf, size_t buflen);

/* Print all YogaBook hidraw nodes and how they were classified. */
int hid_touch_list_candidates(void);

/* True if hidraw advertises report 0x0c or Linux 0x03 MT in its descriptor. */
int hid_touch_path_is_mt(const char *hidraw_path);

#endif /* EINK_HID_TOUCH_H */
