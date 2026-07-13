/* SPDX-License-Identifier: GPL-2.0-only */

#define _DEFAULT_SOURCE

#include "uinput_out.h"

#include "../common/eink_panel.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

/*
 * Two virtual devices:
 *   pointer  — plain relative mouse (REL + BTN_LEFT/RIGHT only)
 *   keyboard — gesture key injection only
 *
 * Do not mix keyboard keys or BTN_TOOL_FINGER onto the pointer node:
 * libinput then treats it as a keyboard/touchpad and may drop REL motion.
 */

static int write_event(int fd, __u16 type, __u16 code, __s32 value)
{
	struct input_event ev;
	ssize_t n;

	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;

	n = write(fd, &ev, sizeof(ev));
	if (n < 0)
		return -errno;
	if (n != (ssize_t)sizeof(ev))
		return -EIO;

	return 0;
}

static int write_syn(int fd)
{
	return write_event(fd, EV_SYN, SYN_REPORT, 0);
}

static int open_uinput_fd(void)
{
	int fd;

	/* Blocking I/O — O_NONBLOCK can make writes fail with EAGAIN. */
	fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		fd = open("/dev/input/uinput", O_WRONLY | O_CLOEXEC);

	return fd < 0 ? -errno : fd;
}

static int enable_key(int fd, unsigned int key)
{
	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
		return -errno;
	if (ioctl(fd, UI_SET_KEYBIT, key) < 0)
		return -errno;

	return 0;
}

static int find_event_num(const char *name)
{
	FILE *f;
	char line[512];
	int in_block = 0;
	int event_num = -1;

	f = fopen("/proc/bus/input/devices", "r");
	if (!f)
		return -1;

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "N: Name=", 8) == 0) {
			in_block = (strstr(line, name) != NULL);
			event_num = -1;
		} else if (in_block && strncmp(line, "H: Handlers=", 12) == 0) {
			const char *p = strstr(line, "event");

			if (p && sscanf(p, "event%d", &event_num) == 1) {
				fclose(f);
				return event_num;
			}
		}
	}

	fclose(f);
	return -1;
}

static void log_sysname(int fd, const char *label, const char *pretty_name)
{
	char sysname[64];
	int ret;
	int event_num;

	memset(sysname, 0, sizeof(sysname));
	/* UI_GET_SYSNAME returns strlen+1 on success, not 0. */
	ret = ioctl(fd, UI_GET_SYSNAME(sizeof(sysname)), sysname);
	if (ret >= 0)
		fprintf(stderr, "%s sysfs=%s\n", label, sysname);

	event_num = find_event_num(pretty_name);
	if (event_num >= 0)
		fprintf(stderr, "%s -> /dev/input/event%d\n", label, event_num);
	else
		fprintf(stderr, "%s: event node not found in /proc yet\n", label);
}

/*
 * niri/libinput open event nodes as the seated user. Devices created via
 * sudo often stay root:root 0600 until udev uaccess runs — too late or never
 * for a short-lived test. World-rw is a bring-up hammer; prefer the udev rule
 * in scripts/99-eink-touchpad.rules once confirmed.
 */
static void expose_event_node(const char *pretty_name)
{
	char path[64];
	char cmd[160];
	int event_num;
	int i;

	for (i = 0; i < 20; i++) {
		event_num = find_event_num(pretty_name);
		if (event_num >= 0)
			break;
		usleep(50 * 1000);
	}

	if (event_num < 0) {
		fprintf(stderr, "warn: %s not in /proc yet — compositor may miss it\n",
			pretty_name);
		return;
	}

	snprintf(path, sizeof(path), "/dev/input/event%d", event_num);

	snprintf(cmd, sizeof(cmd),
		 "udevadm trigger --action=add --sysname-match=event%d 2>/dev/null; "
		 "udevadm settle --timeout=2 2>/dev/null",
		 event_num);
	(void)system(cmd);

	if (chmod(path, 0666) == 0)
		fprintf(stderr,
			"exposed %s mode 0666 (bring-up; install 99-eink-touchpad.rules)\n",
			path);
	else
		fprintf(stderr, "warn: chmod %s failed: %s\n", path,
			strerror(errno));
}

static int create_pointer(int fd)
{
	struct uinput_setup setup;
	const unsigned int buttons[] = {
		BTN_LEFT, BTN_RIGHT, BTN_MIDDLE,
	};
	size_t i;

	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
		return -errno;
	if (ioctl(fd, UI_SET_EVBIT, EV_REL) < 0)
		return -errno;
	if (ioctl(fd, UI_SET_RELBIT, REL_X) < 0)
		return -errno;
	if (ioctl(fd, UI_SET_RELBIT, REL_Y) < 0)
		return -errno;
	if (ioctl(fd, UI_SET_RELBIT, REL_WHEEL) < 0)
		return -errno;

	for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (enable_key(fd, buttons[i]) < 0)
			return -errno;
	}

	if (ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER) < 0)
		return -errno;

	memset(&setup, 0, sizeof(setup));
	snprintf(setup.name, sizeof(setup.name), "YogaBook E-Ink Pointer");
	/*
	 * Do not reuse the panel's 048d:8951 — libinput groups by USB id and
	 * may merge this virtual mouse into the real composite device group.
	 */
	setup.id.bustype = BUS_USB;
	setup.id.vendor = 0x1234;
	setup.id.product = 0xe101;
	setup.id.version = 1;

	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0)
		return -errno;
	if (ioctl(fd, UI_DEV_CREATE) < 0)
		return -errno;

	return 0;
}

static int create_keyboard(int fd)
{
	struct uinput_setup setup;
	const unsigned int keys[] = {
		KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
		KEY_PAGEUP, KEY_PAGEDOWN, KEY_HOME, KEY_END,
		KEY_BACK, KEY_FORWARD,
	};
	size_t i;

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (enable_key(fd, keys[i]) < 0)
			return -errno;
	}

	memset(&setup, 0, sizeof(setup));
	snprintf(setup.name, sizeof(setup.name), "YogaBook E-Ink Gestures");
	setup.id.bustype = BUS_USB;
	setup.id.vendor = 0x1234;
	setup.id.product = 0xe102;
	setup.id.version = 1;

	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0)
		return -errno;
	if (ioctl(fd, UI_DEV_CREATE) < 0)
		return -errno;

	return 0;
}

static int set_abs(int fd, unsigned int code, int min, int max, int flat)
{
	struct uinput_abs_setup abs;

	if (ioctl(fd, UI_SET_ABSBIT, code) < 0)
		return -errno;

	memset(&abs, 0, sizeof(abs));
	abs.code = code;
	abs.absinfo.minimum = min;
	abs.absinfo.maximum = max;
	abs.absinfo.fuzz = 0;
	abs.absinfo.flat = flat;
	abs.absinfo.resolution = 0;

	if (ioctl(fd, UI_ABS_SETUP, &abs) < 0)
		return -errno;

	return 0;
}

static int create_mt_touchpad(int fd)
{
	struct uinput_setup setup;
	int ret;

	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
		return -errno;
	if (ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0)
		return -errno;
	if (enable_key(fd, BTN_LEFT) < 0)
		return -errno;
	if (enable_key(fd, BTN_TOOL_FINGER) < 0)
		return -errno;
	if (enable_key(fd, BTN_TOUCH) < 0)
		return -errno;
	if (enable_key(fd, BTN_TOOL_DOUBLETAP) < 0)
		return -errno;
	if (enable_key(fd, BTN_TOOL_TRIPLETAP) < 0)
		return -errno;

	if (ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER) < 0)
		return -errno;
	if (ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_BUTTONPAD) < 0)
		return -errno;

	ret = set_abs(fd, ABS_X, 0, EINK_PANEL_WIDTH - 1, 0);
	if (ret < 0)
		return ret;
	ret = set_abs(fd, ABS_Y, 0, EINK_PANEL_HEIGHT - 1, 0);
	if (ret < 0)
		return ret;
	ret = set_abs(fd, ABS_MT_SLOT, 0, EINK_TOUCH_MAX_CONTACTS - 1, 0);
	if (ret < 0)
		return ret;
	ret = set_abs(fd, ABS_MT_TRACKING_ID, 0, 65535, 0);
	if (ret < 0)
		return ret;
	ret = set_abs(fd, ABS_MT_POSITION_X, 0, EINK_PANEL_WIDTH - 1, 0);
	if (ret < 0)
		return ret;
	ret = set_abs(fd, ABS_MT_POSITION_Y, 0, EINK_PANEL_HEIGHT - 1, 0);
	if (ret < 0)
		return ret;

	memset(&setup, 0, sizeof(setup));
	snprintf(setup.name, sizeof(setup.name), "YogaBook E-Ink Touchpad");
	setup.id.bustype = BUS_USB;
	setup.id.vendor = 0x1234;
	setup.id.product = 0xe103;
	setup.id.version = 1;

	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0)
		return -errno;
	if (ioctl(fd, UI_DEV_CREATE) < 0)
		return -errno;

	return 0;
}

int uinput_open(struct eink_uinput *out)
{
	int ptr_fd;
	int kbd_fd;
	int ret;
	int i;

	if (!out)
		return -EINVAL;

	memset(out, 0, sizeof(*out));
	out->fd = -1;
	out->kbd_fd = -1;
	out->mt_fd = -1;
	for (i = 0; i < EINK_TOUCH_MAX_CONTACTS; i++)
		out->mt_tracking[i] = -1;

	ptr_fd = open_uinput_fd();
	if (ptr_fd < 0)
		return ptr_fd;

	ret = create_pointer(ptr_fd);
	if (ret < 0) {
		close(ptr_fd);
		return ret;
	}

	kbd_fd = open_uinput_fd();
	if (kbd_fd < 0) {
		ioctl(ptr_fd, UI_DEV_DESTROY);
		close(ptr_fd);
		return kbd_fd;
	}

	ret = create_keyboard(kbd_fd);
	if (ret < 0) {
		ioctl(ptr_fd, UI_DEV_DESTROY);
		close(ptr_fd);
		close(kbd_fd);
		return ret;
	}

	/* Let udev tag the nodes before the first inject. */
	usleep(200 * 1000);

	log_sysname(ptr_fd, "pointer", "YogaBook E-Ink Pointer");
	log_sysname(kbd_fd, "gestures", "YogaBook E-Ink Gestures");
	expose_event_node("YogaBook E-Ink Pointer");
	expose_event_node("YogaBook E-Ink Gestures");

	out->fd = ptr_fd;
	out->kbd_fd = kbd_fd;
	return 0;
}

int uinput_open_mt(struct eink_uinput *out)
{
	int mt_fd;
	int ret;
	int i;

	if (!out)
		return -EINVAL;

	if (out->mt_fd >= 0)
		return 0;

	if (out->fd < 0 && out->kbd_fd < 0) {
		memset(out, 0, sizeof(*out));
		out->fd = -1;
		out->kbd_fd = -1;
		out->mt_fd = -1;
		for (i = 0; i < EINK_TOUCH_MAX_CONTACTS; i++)
			out->mt_tracking[i] = -1;
	}

	mt_fd = open_uinput_fd();
	if (mt_fd < 0)
		return mt_fd;

	ret = create_mt_touchpad(mt_fd);
	if (ret < 0) {
		close(mt_fd);
		return ret;
	}

	usleep(200 * 1000);
	log_sysname(mt_fd, "touchpad", "YogaBook E-Ink Touchpad");
	expose_event_node("YogaBook E-Ink Touchpad");
	out->mt_fd = mt_fd;
	return 0;
}

void uinput_close(struct eink_uinput *out)
{
	if (!out)
		return;

	if (out->mt_fd >= 0) {
		ioctl(out->mt_fd, UI_DEV_DESTROY);
		close(out->mt_fd);
		out->mt_fd = -1;
	}

	if (out->kbd_fd >= 0) {
		ioctl(out->kbd_fd, UI_DEV_DESTROY);
		close(out->kbd_fd);
		out->kbd_fd = -1;
	}

	if (out->fd >= 0) {
		ioctl(out->fd, UI_DEV_DESTROY);
		close(out->fd);
		out->fd = -1;
	}
}

int uinput_emit_mt_frame(struct eink_uinput *out,
			 const struct eink_touch_frame *frame)
{
	bool seen[EINK_TOUCH_MAX_CONTACTS];
	int active = 0;
	int i;
	int ret;
	int fd;

	if (!out || !frame || out->mt_fd < 0)
		return -EINVAL;

	fd = out->mt_fd;
	memset(seen, 0, sizeof(seen));

	for (i = 0; i < frame->contact_count; i++) {
		const struct eink_touch_contact *c = &frame->contacts[i];
		int slot = c->slot;
		bool down = c->mode != EINK_TOUCH_MODE_UP;

		if (slot < 0 || slot >= EINK_TOUCH_MAX_CONTACTS)
			slot = i % EINK_TOUCH_MAX_CONTACTS;

		seen[slot] = true;

		ret = write_event(fd, EV_ABS, ABS_MT_SLOT, slot);
		if (ret < 0)
			return ret;

		if (down) {
			if (out->mt_tracking[slot] < 0) {
				out->mt_tracking[slot] = slot + 1;
				ret = write_event(fd, EV_ABS, ABS_MT_TRACKING_ID,
						  out->mt_tracking[slot]);
				if (ret < 0)
					return ret;
			}
			ret = write_event(fd, EV_ABS, ABS_MT_POSITION_X,
					  c->display_x);
			if (ret < 0)
				return ret;
			ret = write_event(fd, EV_ABS, ABS_MT_POSITION_Y,
					  c->display_y);
			if (ret < 0)
				return ret;
			active++;
		} else if (out->mt_tracking[slot] >= 0) {
			ret = write_event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
			if (ret < 0)
				return ret;
			out->mt_tracking[slot] = -1;
		}
	}

	/* Clear slots that disappeared without an UP report. */
	for (i = 0; i < EINK_TOUCH_MAX_CONTACTS; i++) {
		if (seen[i] || out->mt_tracking[i] < 0)
			continue;
		ret = write_event(fd, EV_ABS, ABS_MT_SLOT, i);
		if (ret < 0)
			return ret;
		ret = write_event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
		if (ret < 0)
			return ret;
		out->mt_tracking[i] = -1;
	}

	ret = write_event(fd, EV_KEY, BTN_TOUCH, active > 0 ? 1 : 0);
	if (ret < 0)
		return ret;
	ret = write_event(fd, EV_KEY, BTN_TOOL_FINGER, active == 1 ? 1 : 0);
	if (ret < 0)
		return ret;
	ret = write_event(fd, EV_KEY, BTN_TOOL_DOUBLETAP, active == 2 ? 1 : 0);
	if (ret < 0)
		return ret;
	ret = write_event(fd, EV_KEY, BTN_TOOL_TRIPLETAP, active >= 3 ? 1 : 0);
	if (ret < 0)
		return ret;

	if (active > 0) {
		/* Single-touch legacy axes follow first live contact. */
		for (i = 0; i < frame->contact_count; i++) {
			const struct eink_touch_contact *c = &frame->contacts[i];

			if (c->mode == EINK_TOUCH_MODE_UP)
				continue;
			ret = write_event(fd, EV_ABS, ABS_X, c->display_x);
			if (ret < 0)
				return ret;
			ret = write_event(fd, EV_ABS, ABS_Y, c->display_y);
			if (ret < 0)
				return ret;
			break;
		}
	}

	return write_syn(fd);
}

int uinput_emit_rel(struct eink_uinput *out, int dx, int dy)
{
	int ret;

	if (!out || out->fd < 0)
		return -EINVAL;

	if (dx == 0 && dy == 0)
		return 0;

	if (dx != 0) {
		ret = write_event(out->fd, EV_REL, REL_X, dx);
		if (ret < 0)
			return ret;
	}
	if (dy != 0) {
		ret = write_event(out->fd, EV_REL, REL_Y, dy);
		if (ret < 0)
			return ret;
	}

	return write_syn(out->fd);
}

int uinput_emit_btn(struct eink_uinput *out, unsigned int btn, int value)
{
	int ret;

	if (!out || out->fd < 0)
		return -EINVAL;

	ret = write_event(out->fd, EV_KEY, btn, value);
	if (ret < 0)
		return ret;

	return write_syn(out->fd);
}

int uinput_emit_key(struct eink_uinput *out, unsigned int key, int value)
{
	int ret;
	int fd;

	if (!out)
		return -EINVAL;

	fd = out->kbd_fd >= 0 ? out->kbd_fd : out->fd;
	if (fd < 0)
		return -EINVAL;

	ret = write_event(fd, EV_KEY, key, value);
	if (ret < 0)
		return ret;

	return write_syn(fd);
}

int uinput_emit_key_combo(struct eink_uinput *out, unsigned int key)
{
	int ret;

	ret = uinput_emit_key(out, key, 1);
	if (ret < 0)
		return ret;

	return uinput_emit_key(out, key, 0);
}

int uinput_selftest_pointer(struct eink_uinput *out)
{
	int i;
	int ret;
	int event_num;

	if (!out || out->fd < 0)
		return -EINVAL;

	event_num = find_event_num("YogaBook E-Ink Pointer");
	fprintf(stderr,
		"uinput selftest: 60x REL_X=20 (~3s) — watch the on-screen cursor\n");
	if (event_num >= 0)
		fprintf(stderr,
			"  (optional) sudo evtest /dev/input/event%d\n",
			event_num);

	for (i = 0; i < 60; i++) {
		ret = uinput_emit_rel(out, 20, 0);
		if (ret < 0) {
			fprintf(stderr, "uinput selftest write failed: %s\n",
				strerror(-ret));
			return ret;
		}
		usleep(50 * 1000);
	}

	fprintf(stderr, "uinput selftest: done — did the cursor move?\n");
	return 0;
}
