/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EINK_UINPUT_OUT_H
#define EINK_UINPUT_OUT_H

#include <linux/input.h>
#include <stdbool.h>

struct eink_uinput {
	int fd;
	bool btn_left_down;
};

int uinput_open(struct eink_uinput *out);
void uinput_close(struct eink_uinput *out);

int uinput_emit_rel(struct eink_uinput *out, int dx, int dy);
int uinput_emit_btn(struct eink_uinput *out, unsigned int btn, int value);
int uinput_emit_key(struct eink_uinput *out, unsigned int key, int value);
int uinput_emit_key_combo(struct eink_uinput *out, unsigned int key);

#endif /* EINK_UINPUT_OUT_H */
