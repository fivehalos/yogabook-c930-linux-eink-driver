/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EINK_GESTURES_H
#define EINK_GESTURES_H

#include "../common/eink_touch.h"

#include "uinput_out.h"

#include <stdbool.h>
#include <time.h>

struct eink_gesture_config {
	int swipe_threshold_px;
	int tap_threshold_px;
	float pointer_sensitivity;
	/* Clockwise degrees to match compositor output transform: 0/90/180/270. */
	int rotate_deg;
	/* Drop single-frame display deltas larger than this (pre-sensitivity). */
	int pointer_max_step_px;
	/* Rebase pointer motion after this many ms without reports (warp guard). */
	int pointer_idle_ms;
	/*
	 * Treat contact as lifted after this many ms without reports (firmware
	 * often omits UP). Must be > pointer_idle_ms.
	 */
	int release_idle_ms;
	/*
	 * HID 0x90 is single-contact: 2/3 fingers are interleaved positions.
	 * A new report farther than this from every live soft-slot is a new finger.
	 */
	int chord_sep_px;
	/* Soft-slot dies if not updated within this many ms. */
	int chord_slot_ttl_ms;
};

struct eink_slot_state {
	bool active;
	int display_x;
	int display_y;
	struct timespec last_seen;
};

struct eink_gesture_state {
	struct eink_gesture_config cfg;
	bool pointer_active;
	int pointer_x;
	int pointer_y;
	bool tracking;
	int tracking_fingers;
	int max_fingers;
	int start_x;
	int start_y;
	int last_x;
	int last_y;
	bool have_active_time;
	struct timespec last_active_time;
	struct eink_slot_state slots[EINK_TOUCH_MAX_CONTACTS];
};

void gesture_state_init(struct eink_gesture_state *st,
			const struct eink_gesture_config *cfg);

/*
 * Feed one parsed touch frame. Emits uinput pointer motion, button clicks,
 * and multi-finger swipe shortcuts.
 * mt_fingers: optional kernel ABS_MT count (often 0 while 0x90 draw mode).
 */
int gesture_process_frame(struct eink_gesture_state *st,
			  const struct eink_touch_frame *frame,
			  struct eink_uinput *out,
			  bool debug,
			  int mt_fingers);

#endif /* EINK_GESTURES_H */
