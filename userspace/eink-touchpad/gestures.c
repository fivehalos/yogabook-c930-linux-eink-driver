/* SPDX-License-Identifier: GPL-2.0-only */

#include "gestures.h"

#include <stdio.h>
#include <string.h>

enum eink_swipe_dir {
	EINK_SWIPE_NONE = 0,
	EINK_SWIPE_LEFT,
	EINK_SWIPE_RIGHT,
	EINK_SWIPE_UP,
	EINK_SWIPE_DOWN,
};

static int frame_active_contacts(const struct eink_touch_frame *frame)
{
	int active = 0;
	int i;

	for (i = 0; i < frame->contact_count; i++) {
		if (frame->contacts[i].mode != EINK_TOUCH_MODE_UP)
			active++;
	}

	return active;
}

static void frame_centroid(const struct eink_touch_frame *frame,
			   int *cx, int *cy)
{
	int sum_x = 0;
	int sum_y = 0;
	int count = 0;
	int i;

	for (i = 0; i < frame->contact_count; i++) {
		if (frame->contacts[i].mode == EINK_TOUCH_MODE_UP)
			continue;

		sum_x += frame->contacts[i].display_x;
		sum_y += frame->contacts[i].display_y;
		count++;
	}

	if (count == 0) {
		*cx = 0;
		*cy = 0;
		return;
	}

	*cx = sum_x / count;
	*cy = sum_y / count;
}

static enum eink_swipe_dir swipe_classify(int dx, int dy, int threshold)
{
	int adx = dx < 0 ? -dx : dx;
	int ady = dy < 0 ? -dy : dy;

	if (adx < threshold && ady < threshold)
		return EINK_SWIPE_NONE;

	if (adx > ady) {
		if (dx < 0)
			return EINK_SWIPE_LEFT;
		return EINK_SWIPE_RIGHT;
	}

	if (dy < 0)
		return EINK_SWIPE_UP;
	return EINK_SWIPE_DOWN;
}

static int gesture_emit_swipe(struct eink_uinput *out, int fingers,
			      enum eink_swipe_dir dir, bool debug)
{
	unsigned int key = 0;

	if (fingers == 2) {
		switch (dir) {
		case EINK_SWIPE_LEFT:
			key = KEY_LEFT;
			break;
		case EINK_SWIPE_RIGHT:
			key = KEY_RIGHT;
			break;
		case EINK_SWIPE_UP:
			key = KEY_PAGEUP;
			break;
		case EINK_SWIPE_DOWN:
			key = KEY_PAGEDOWN;
			break;
		default:
			return 0;
		}
	} else if (fingers >= 3) {
		switch (dir) {
		case EINK_SWIPE_LEFT:
			key = KEY_HOME;
			break;
		case EINK_SWIPE_RIGHT:
			key = KEY_END;
			break;
		case EINK_SWIPE_UP:
			key = KEY_BACK;
			break;
		case EINK_SWIPE_DOWN:
			key = KEY_FORWARD;
			break;
		default:
			return 0;
		}
	} else {
		return 0;
	}

	if (debug)
		fprintf(stderr, "gesture: %d-finger swipe dir=%d -> key %u\n",
			fingers, dir, key);

	return uinput_emit_key_combo(out, key);
}

static int gesture_emit_pointer(struct eink_gesture_state *st,
				struct eink_uinput *out, int x, int y,
				bool debug)
{
	int dx;
	int dy;

	if (!st->pointer_active) {
		st->pointer_active = true;
		st->pointer_x = x;
		st->pointer_y = y;
		return 0;
	}

	dx = (int)((x - st->pointer_x) * st->cfg.pointer_sensitivity);
	dy = (int)((y - st->pointer_y) * st->cfg.pointer_sensitivity);

	st->pointer_x = x;
	st->pointer_y = y;

	if (dx == 0 && dy == 0)
		return 0;

	if (debug)
		fprintf(stderr, "pointer: rel %d,%d\n", dx, dy);

	return uinput_emit_rel(out, dx, dy);
}

static int gesture_emit_tap(struct eink_uinput *out, bool debug)
{
	if (debug)
		fprintf(stderr, "pointer: tap click\n");

	if (uinput_emit_btn(out, BTN_LEFT, 1) < 0)
		return -1;
	return uinput_emit_btn(out, BTN_LEFT, 0);
}

void gesture_state_init(struct eink_gesture_state *st,
			const struct eink_gesture_config *cfg)
{
	memset(st, 0, sizeof(*st));
	st->cfg = *cfg;
}

int gesture_process_frame(struct eink_gesture_state *st,
			  const struct eink_touch_frame *frame,
			  struct eink_uinput *out,
			  bool debug)
{
	int active;
	int cx;
	int cy;
	int dx;
	int dy;
	enum eink_swipe_dir dir;

	active = frame_active_contacts(frame);

	if (active == 0) {
		if (st->tracking) {
			dx = st->last_x - st->start_x;
			dy = st->last_y - st->start_y;
			dir = swipe_classify(dx, dy, st->cfg.swipe_threshold_px);

			if (st->tracking_fingers >= 2 && dir != EINK_SWIPE_NONE) {
				gesture_emit_swipe(out, st->tracking_fingers,
						   dir, debug);
			} else if (st->tracking_fingers == 1) {
				int adx = dx < 0 ? -dx : dx;
				int ady = dy < 0 ? -dy : dy;

				if (adx < st->cfg.tap_threshold_px &&
				    ady < st->cfg.tap_threshold_px)
					gesture_emit_tap(out, debug);
			}

			st->tracking = false;
			st->tracking_fingers = 0;
		}

		st->pointer_active = false;
		return 0;
	}

	frame_centroid(frame, &cx, &cy);

	if (!st->tracking) {
		st->tracking = true;
		st->tracking_fingers = active;
		st->start_x = cx;
		st->start_y = cy;
		st->last_x = cx;
		st->last_y = cy;
		st->pointer_active = false;
	}

	st->last_x = cx;
	st->last_y = cy;

	if (active >= 2)
		return 0;

	return gesture_emit_pointer(st, out, cx, cy, debug);
}
