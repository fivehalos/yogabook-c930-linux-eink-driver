/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EINK_UINPUT_OUT_H
#define EINK_UINPUT_OUT_H

#include "../common/eink_touch.h"

#include <linux/input.h>
#include <stdbool.h>

struct eink_uinput {
	int fd;		/* virtual pointer (REL mouse) — 0x90 path */
	int kbd_fd;	/* virtual keyboard (gesture keys) */
	int mt_fd;	/* ABS_MT touchpad — HID 0x0c path */
	bool btn_left_down;
	int mt_tracking[EINK_TOUCH_MAX_CONTACTS];
};

int uinput_open(struct eink_uinput *out);
int uinput_open_mt(struct eink_uinput *out);
void uinput_close(struct eink_uinput *out);

int uinput_emit_rel(struct eink_uinput *out, int dx, int dy);
int uinput_emit_btn(struct eink_uinput *out, unsigned int btn, int value);
int uinput_emit_key(struct eink_uinput *out, unsigned int key, int value);
int uinput_emit_key_combo(struct eink_uinput *out, unsigned int key);
int uinput_emit_mt_frame(struct eink_uinput *out,
			 const struct eink_touch_frame *frame);
int uinput_selftest_pointer(struct eink_uinput *out);

#endif /* EINK_UINPUT_OUT_H */
