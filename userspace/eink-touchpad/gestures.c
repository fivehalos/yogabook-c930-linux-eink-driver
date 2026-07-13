/* SPDX-License-Identifier: GPL-2.0-only */

#define _POSIX_C_SOURCE 200809L

#include "gestures.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum eink_swipe_dir {
	EINK_SWIPE_NONE = 0,
	EINK_SWIPE_LEFT,
	EINK_SWIPE_RIGHT,
	EINK_SWIPE_UP,
	EINK_SWIPE_DOWN,
};

static int64_t timespec_ms_delta(const struct timespec *later,
				 const struct timespec *earlier)
{
	return (int64_t)(later->tv_sec - earlier->tv_sec) * 1000 +
	       (later->tv_nsec - earlier->tv_nsec) / 1000000;
}

static void mark_active_now(struct eink_gesture_state *st)
{
	clock_gettime(CLOCK_MONOTONIC, &st->last_active_time);
	st->have_active_time = true;
}

static int dist2(int x0, int y0, int x1, int y1)
{
	int dx = x0 - x1;
	int dy = y0 - y1;

	return dx * dx + dy * dy;
}

static void slots_expire(struct eink_gesture_state *st, const struct timespec *now)
{
	int ttl = st->cfg.chord_slot_ttl_ms;
	int i;

	if (ttl <= 0)
		ttl = 90;

	for (i = 0; i < EINK_TOUCH_MAX_CONTACTS; i++) {
		if (!st->slots[i].active)
			continue;
		if (timespec_ms_delta(now, &st->slots[i].last_seen) > ttl)
			st->slots[i].active = false;
	}
}

static int slots_active_count(const struct eink_gesture_state *st)
{
	int n = 0;
	int i;

	for (i = 0; i < EINK_TOUCH_MAX_CONTACTS; i++) {
		if (st->slots[i].active)
			n++;
	}
	return n;
}

static void slots_centroid(const struct eink_gesture_state *st,
			   int *cx, int *cy)
{
	int sum_x = 0;
	int sum_y = 0;
	int count = 0;
	int i;

	for (i = 0; i < EINK_TOUCH_MAX_CONTACTS; i++) {
		if (!st->slots[i].active)
			continue;
		sum_x += st->slots[i].display_x;
		sum_y += st->slots[i].display_y;
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

/*
 * HID 0x90 always reports contact id=1. Multi-touch is interleaved samples
 * at different positions — cluster into soft-slots by distance.
 */
static void slots_observe_point(struct eink_gesture_state *st, int x, int y,
				const struct timespec *now)
{
	int sep = st->cfg.chord_sep_px;
	int best = -1;
	int best_d2 = 0;
	int i;
	int free_i = -1;

	if (sep <= 0)
		sep = 100;

	slots_expire(st, now);

	for (i = 0; i < EINK_TOUCH_MAX_CONTACTS; i++) {
		int d2;

		if (!st->slots[i].active) {
			if (free_i < 0)
				free_i = i;
			continue;
		}
		d2 = dist2(x, y, st->slots[i].display_x, st->slots[i].display_y);
		if (best < 0 || d2 < best_d2) {
			best = i;
			best_d2 = d2;
		}
	}

	if (best >= 0 && best_d2 <= sep * sep) {
		st->slots[best].display_x = x;
		st->slots[best].display_y = y;
		st->slots[best].last_seen = *now;
		return;
	}

	if (free_i < 0)
		free_i = 0;

	st->slots[free_i].active = true;
	st->slots[free_i].display_x = x;
	st->slots[free_i].display_y = y;
	st->slots[free_i].last_seen = *now;
}

static void slots_apply_frame(struct eink_gesture_state *st,
			      const struct eink_touch_frame *frame,
			      const struct timespec *now)
{
	int i;

	for (i = 0; i < frame->contact_count; i++) {
		const struct eink_touch_contact *c = &frame->contacts[i];

		if (c->mode == EINK_TOUCH_MODE_UP)
			continue;

		slots_observe_point(st, c->display_x, c->display_y, now);
	}
}

static void slots_clear(struct eink_gesture_state *st)
{
	memset(st->slots, 0, sizeof(st->slots));
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

static void rotate_delta(int rotate_deg, int *dx, int *dy)
{
	int x = *dx;
	int y = *dy;

	switch (rotate_deg) {
	case 90:
		*dx = -y;
		*dy = x;
		break;
	case 180:
		*dx = -x;
		*dy = -y;
		break;
	case 270:
		*dx = y;
		*dy = -x;
		break;
	case 0:
	default:
		break;
	}
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

static int gesture_emit_click(struct eink_uinput *out, unsigned int btn)
{
	const char *name = "left";

	if (btn == BTN_RIGHT)
		name = "right";
	else if (btn == BTN_MIDDLE)
		name = "middle";

	fprintf(stderr, "pointer: %s click\n", name);

	if (uinput_emit_btn(out, btn, 1) < 0)
		return -1;
	return uinput_emit_btn(out, btn, 0);
}

static int gesture_emit_pointer(struct eink_gesture_state *st,
				struct eink_uinput *out, int x, int y,
				bool debug)
{
	int dx_raw;
	int dy_raw;
	int dx;
	int dy;

	if (!st->pointer_active) {
		st->pointer_active = true;
		st->pointer_x = x;
		st->pointer_y = y;
		return 0;
	}

	dx_raw = x - st->pointer_x;
	dy_raw = y - st->pointer_y;
	st->pointer_x = x;
	st->pointer_y = y;

	if (dx_raw == 0 && dy_raw == 0)
		return 0;

	if (st->cfg.pointer_max_step_px > 0 &&
	    (abs(dx_raw) > st->cfg.pointer_max_step_px ||
	     abs(dy_raw) > st->cfg.pointer_max_step_px)) {
		fprintf(stderr,
			"pointer: drop warp %d,%d (max %d)\n",
			dx_raw, dy_raw, st->cfg.pointer_max_step_px);
		return 0;
	}

	dx = (int)(dx_raw * st->cfg.pointer_sensitivity);
	dy = (int)(dy_raw * st->cfg.pointer_sensitivity);

	if (dx == 0 && dy == 0)
		return 0;

	rotate_delta(st->cfg.rotate_deg, &dx, &dy);

	if (debug)
		fprintf(stderr, "pointer: rel %d,%d (rotate=%d)\n", dx, dy,
			st->cfg.rotate_deg);

	return uinput_emit_rel(out, dx, dy);
}

static int gesture_finish(struct eink_gesture_state *st,
			  struct eink_uinput *out, bool debug)
{
	int dx;
	int dy;
	int adx;
	int ady;
	int fingers;
	enum eink_swipe_dir dir;

	if (!st->tracking)
		return 0;

	dx = st->last_x - st->start_x;
	dy = st->last_y - st->start_y;
	rotate_delta(st->cfg.rotate_deg, &dx, &dy);
	dir = swipe_classify(dx, dy, st->cfg.swipe_threshold_px);
	adx = dx < 0 ? -dx : dx;
	ady = dy < 0 ? -dy : dy;
	fingers = st->max_fingers > 0 ? st->max_fingers : st->tracking_fingers;

	st->tracking = false;
	st->tracking_fingers = 0;
	st->max_fingers = 0;
	st->pointer_active = false;
	slots_clear(st);

	if (fingers >= 2 && dir != EINK_SWIPE_NONE)
		return gesture_emit_swipe(out, fingers, dir, debug);

	if (adx <= st->cfg.tap_threshold_px &&
	    ady <= st->cfg.tap_threshold_px) {
		fprintf(stderr, "pointer: tap fingers=%d move=%d,%d\n",
			fingers, adx, ady);
		if (fingers >= 3)
			return gesture_emit_click(out, BTN_MIDDLE);
		if (fingers == 2)
			return gesture_emit_click(out, BTN_RIGHT);
		if (fingers == 1)
			return gesture_emit_click(out, BTN_LEFT);
	}

	return 0;
}

static void maybe_idle_update(struct eink_gesture_state *st,
			      struct eink_uinput *out, bool debug)
{
	struct timespec now;
	int64_t gap_ms;
	int release_ms;

	if (!st->have_active_time)
		return;

	clock_gettime(CLOCK_MONOTONIC, &now);
	gap_ms = timespec_ms_delta(&now, &st->last_active_time);

	if (st->cfg.pointer_idle_ms > 0 && gap_ms > st->cfg.pointer_idle_ms)
		st->pointer_active = false;

	release_ms = st->cfg.release_idle_ms;
	if (release_ms <= 0)
		release_ms = 300;
	if (release_ms <= st->cfg.pointer_idle_ms)
		release_ms = st->cfg.pointer_idle_ms + 100;

	if (gap_ms > release_ms) {
		fprintf(stderr, "pointer: release idle (%lld ms) maxf=%d\n",
			(long long)gap_ms, st->max_fingers);
		(void)gesture_finish(st, out, debug);
	}
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
			  bool debug,
			  int mt_fingers)
{
	struct timespec now;
	int active;
	int cx;
	int cy;
	int i;
	int fingers;

	maybe_idle_update(st, out, debug);

	if (frame->contact_count <= 0)
		return 0;

	clock_gettime(CLOCK_MONOTONIC, &now);
	slots_apply_frame(st, frame, &now);
	active = slots_active_count(st);
	fingers = active;
	if (mt_fingers > fingers)
		fingers = mt_fingers;
	mark_active_now(st);

	if (debug) {
		fprintf(stderr, "slots active=%d mt=%d max=%d [", active,
			mt_fingers, st->max_fingers);
		for (i = 0; i < EINK_TOUCH_MAX_CONTACTS; i++) {
			if (!st->slots[i].active)
				continue;
			fprintf(stderr, " %d:%d,%d", i, st->slots[i].display_x,
				st->slots[i].display_y);
		}
		fprintf(stderr, " ]\n");
	}

	if (active == 0 && mt_fingers <= 0)
		return gesture_finish(st, out, debug);

	slots_centroid(st, &cx, &cy);

	if (!st->tracking) {
		st->tracking = true;
		st->tracking_fingers = fingers;
		st->max_fingers = fingers;
		st->start_x = cx;
		st->start_y = cy;
		st->last_x = cx;
		st->last_y = cy;
		st->pointer_active = false;
	} else {
		if (fingers > st->max_fingers)
			st->max_fingers = fingers;
		if (fingers != st->tracking_fingers) {
			st->tracking_fingers = fingers;
			st->start_x = cx;
			st->start_y = cy;
			st->pointer_active = false;
		}
	}

	st->last_x = cx;
	st->last_y = cy;

	if (fingers >= 2)
		return 0;

	return gesture_emit_pointer(st, out, cx, cy, debug);
}
