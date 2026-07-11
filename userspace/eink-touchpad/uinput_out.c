/* SPDX-License-Identifier: GPL-2.0-only */

#define _DEFAULT_SOURCE

#include "uinput_out.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <linux/uinput.h>
#include <sys/ioctl.h>

static int uinput_emit_sync(int fd)
{
	struct input_event ev = {
		.type = EV_SYN,
		.code = SYN_REPORT,
		.value = 0,
	};

	if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
		return -errno;

	return 0;
}

static int uinput_emit(int fd, int type, int code, int value)
{
	struct input_event ev = {
		.type = type,
		.code = code,
		.value = value,
	};

	if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
		return -errno;

	return uinput_emit_sync(fd);
}

static int uinput_enable_key(int fd, unsigned int key)
{
	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
		return -errno;
	if (ioctl(fd, UI_SET_KEYBIT, key) < 0)
		return -errno;

	return 0;
}

int uinput_open(struct eink_uinput *out)
{
	struct uinput_setup setup = { .name = "YogaBook E-Ink Touchpad" };
	const unsigned int keys[] = {
		KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
		KEY_PAGEUP, KEY_PAGEDOWN, KEY_HOME, KEY_END,
		KEY_BACK, KEY_FORWARD,
		BTN_LEFT, BTN_RIGHT,
	};
	int fd;
	size_t i;

	if (!out)
		return -EINVAL;

	memset(out, 0, sizeof(*out));
	out->fd = -1;

	fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		fd = open("/dev/input/uinput", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	if (ioctl(fd, UI_SET_EVBIT, EV_REL) < 0)
		goto fail;
	if (ioctl(fd, UI_SET_RELBIT, REL_X) < 0)
		goto fail;
	if (ioctl(fd, UI_SET_RELBIT, REL_Y) < 0)
		goto fail;

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (uinput_enable_key(fd, keys[i]) < 0)
			goto fail;
	}

	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0)
		goto fail;
	if (ioctl(fd, UI_DEV_CREATE) < 0)
		goto fail;

	out->fd = fd;
	return 0;

fail:
	close(fd);
	return -errno;
}

void uinput_close(struct eink_uinput *out)
{
	if (!out || out->fd < 0)
		return;

	ioctl(out->fd, UI_DEV_DESTROY);
	close(out->fd);
	out->fd = -1;
}

int uinput_emit_rel(struct eink_uinput *out, int dx, int dy)
{
	if (!out || out->fd < 0)
		return -EINVAL;

	if (dx != 0 && uinput_emit(out->fd, EV_REL, REL_X, dx) < 0)
		return -errno;
	if (dy != 0 && uinput_emit(out->fd, EV_REL, REL_Y, dy) < 0)
		return -errno;

	return 0;
}

int uinput_emit_btn(struct eink_uinput *out, unsigned int btn, int value)
{
	if (!out || out->fd < 0)
		return -EINVAL;

	return uinput_emit(out->fd, EV_KEY, btn, value);
}

int uinput_emit_key(struct eink_uinput *out, unsigned int key, int value)
{
	if (!out || out->fd < 0)
		return -EINVAL;

	return uinput_emit(out->fd, EV_KEY, key, value);
}

int uinput_emit_key_combo(struct eink_uinput *out, unsigned int key)
{
	if (uinput_emit_key(out, key, 1) < 0)
		return -errno;
	if (uinput_emit_key(out, key, 0) < 0)
		return -errno;

	return 0;
}
