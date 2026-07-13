/* SPDX-License-Identifier: GPL-2.0-only */

#define _DEFAULT_SOURCE

#include "uinput_out.h"

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

int uinput_open(struct eink_uinput *out)
{
	int ptr_fd;
	int kbd_fd;
	int ret;

	if (!out)
		return -EINVAL;

	memset(out, 0, sizeof(*out));
	out->fd = -1;
	out->kbd_fd = -1;

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

void uinput_close(struct eink_uinput *out)
{
	if (!out)
		return;

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
