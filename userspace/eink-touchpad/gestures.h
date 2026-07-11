/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EINK_GESTURES_H
#define EINK_GESTURES_H

#include "../common/eink_touch.h"

#include "uinput_out.h"

#include <stdbool.h>

struct eink_gesture_config {
	int swipe_threshold_px;
	int tap_threshold_px;
	float pointer_sensitivity;
};

struct eink_gesture_state {
	struct eink_gesture_config cfg;
	bool pointer_active;
	int pointer_x;
	int pointer_y;
	bool tracking;
	int tracking_fingers;
	int start_x;
	int start_y;
	int last_x;
	int last_y;
};

void gesture_state_init(struct eink_gesture_state *st,
			const struct eink_gesture_config *cfg);

/*
 * Feed one parsed touch frame. Emits uinput pointer motion, button clicks,
 * and multi-finger swipe shortcuts.
 */
int gesture_process_frame(struct eink_gesture_state *st,
			  const struct eink_touch_frame *frame,
			  struct eink_uinput *out,
			  bool debug);

#endif /* EINK_GESTURES_H */
