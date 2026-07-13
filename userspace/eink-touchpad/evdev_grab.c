/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Silence 048d:8951 kernel pointer nodes so only our relative uinput
 * pointer moves the cursor. Prefer EVIOCGRAB (events still flow to us so
 * we can count ABS_MT fingers — HID 0x90 is single-contact). Optionally
 * sysfs-inhibit REL-only mice that have no MT slots.
 */

#define _DEFAULT_SOURCE

#include "evdev_grab.h"

#include "../common/eink_panel.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stddef.h>

#define EINK_EVDEV_GRAB_MAX 16
#define EINK_INHIBIT_MAX 16
#define EINK_MT_SLOTS 16

struct eink_mt_state {
	int cur_slot;
	int tracking_id[EINK_MT_SLOTS];
};

struct eink_evdev_grab {
	int fds[EINK_EVDEV_GRAB_MAX];
	struct eink_mt_state mt[EINK_EVDEV_GRAB_MAX];
	int count;
	char inhibit_paths[EINK_INHIBIT_MAX][256];
	int inhibit_count;
};

static struct eink_evdev_grab g_grab;

static void mt_reset_one(struct eink_mt_state *mt)
{
	int i;

	mt->cur_slot = 0;
	for (i = 0; i < EINK_MT_SLOTS; i++)
		mt->tracking_id[i] = -1;
}

static void grab_reset(void)
{
	int i;

	for (i = 0; i < EINK_EVDEV_GRAB_MAX; i++) {
		g_grab.fds[i] = -1;
		mt_reset_one(&g_grab.mt[i]);
	}
	g_grab.count = 0;
	g_grab.inhibit_count = 0;
}

static int hex_caps_nonzero(const char *line, size_t prefix_len)
{
	const char *q;

	for (q = line + prefix_len; *q && *q != '\n'; q++) {
		if ((*q >= '1' && *q <= '9') ||
		    (*q >= 'a' && *q <= 'f') ||
		    (*q >= 'A' && *q <= 'F'))
			return 1;
	}
	return 0;
}

static int inhibit_sysfs_input(const char *sysfs_rel, int inhibit)
{
	char path[512];
	int fd;
	ssize_t n;
	const char *val = inhibit ? "1\n" : "0\n";

	if (!sysfs_rel || g_grab.inhibit_count >= EINK_INHIBIT_MAX)
		return -ENOSPC;

	/* Sysfs= lines are like /devices/pci.../input/input24 */
	snprintf(path, sizeof(path), "/sys%s/inhibited", sysfs_rel);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	n = write(fd, val, 2);
	close(fd);
	if (n < 0)
		return -errno;

	if (inhibit) {
		snprintf(g_grab.inhibit_paths[g_grab.inhibit_count],
			 sizeof(g_grab.inhibit_paths[0]), "%s", path);
		g_grab.inhibit_count++;
		fprintf(stderr, "inhibit: %s\n", path);
	}
	return 0;
}

static int grab_event_num(int event_num, const char *name)
{
	char path[64];
	int fd;

	if (event_num < 0 || g_grab.count >= EINK_EVDEV_GRAB_MAX)
		return -ENOSPC;

	snprintf(path, sizeof(path), "/dev/input/event%d", event_num);
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0)
		return -errno;

	if (ioctl(fd, EVIOCGRAB, 1) < 0) {
		int err = -errno;

		close(fd);
		fprintf(stderr, "evdev grab failed: %s (%s)\n", path,
			strerror(-err));
		return err;
	}

	g_grab.fds[g_grab.count] = fd;
	mt_reset_one(&g_grab.mt[g_grab.count]);
	g_grab.count++;
	fprintf(stderr, "evdev grab: %s (%s)\n", path,
		name && name[0] ? name : "absolute");
	return 0;
}

static int should_silence(int has_abs, int has_rel)
{
	return has_abs || has_rel;
}

int evdev_grab_hidraw_siblings(const char *hidraw_path)
{
	FILE *f;
	char line[512];
	char name[128];
	char sysfs[256];
	int in_yogabook = 0;
	int has_abs = 0;
	int has_rel = 0;
	int event_num = -1;

	(void)hidraw_path;
	grab_reset();
	name[0] = '\0';
	sysfs[0] = '\0';

	f = fopen("/proc/bus/input/devices", "r");
	if (!f)
		return -errno;

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "I:", 2) == 0) {
			unsigned int vendor = 0;
			unsigned int product = 0;

			if (in_yogabook && should_silence(has_abs, has_rel) &&
			    event_num >= 0) {
				/*
				 * ABS/MT: clear any prior inhibit so we can
				 * read finger count; EVIOCGRAB hides from seat.
				 * REL-only mice: inhibit (no MT to drain).
				 */
				if (sysfs[0]) {
					if (has_rel && !has_abs)
						(void)inhibit_sysfs_input(sysfs, 1);
					else
						(void)inhibit_sysfs_input(sysfs, 0);
				}
				(void)grab_event_num(event_num, name);
			}

			in_yogabook = 0;
			has_abs = 0;
			has_rel = 0;
			event_num = -1;
			name[0] = '\0';
			sysfs[0] = '\0';

			if (sscanf(line, "I: Bus=%*x Vendor=%x Product=%x",
				   &vendor, &product) == 2 &&
			    vendor == EINK_USB_VENDOR_ID &&
			    product == EINK_USB_PRODUCT_ID)
				in_yogabook = 1;
			continue;
		}

		if (!in_yogabook)
			continue;

		if (strncmp(line, "N: Name=", 8) == 0) {
			char *start = strchr(line, '"');
			char *end;

			name[0] = '\0';
			if (start) {
				start++;
				end = strchr(start, '"');
				if (end) {
					size_t len = (size_t)(end - start);

					if (len >= sizeof(name))
						len = sizeof(name) - 1;
					memcpy(name, start, len);
					name[len] = '\0';
				}
			}
			continue;
		}

		if (strncmp(line, "S: Sysfs=", 9) == 0) {
			snprintf(sysfs, sizeof(sysfs), "%s", line + 9);
			sysfs[strcspn(sysfs, "\n")] = '\0';
			continue;
		}

		if (strncmp(line, "H: Handlers=", 12) == 0) {
			const char *p = strstr(line, "event");

			event_num = -1;
			while (p) {
				int n = -1;

				if (sscanf(p, "event%d", &n) == 1)
					event_num = n;
				p = strstr(p + 5, "event");
			}
			continue;
		}

		if (strncmp(line, "B: ABS=", 7) == 0)
			has_abs = hex_caps_nonzero(line, 7);
		else if (strncmp(line, "B: REL=", 7) == 0)
			has_rel = hex_caps_nonzero(line, 7);
	}

	if (in_yogabook && should_silence(has_abs, has_rel) &&
	    event_num >= 0) {
		if (sysfs[0]) {
			if (has_rel && !has_abs)
				(void)inhibit_sysfs_input(sysfs, 1);
			else
				(void)inhibit_sysfs_input(sysfs, 0);
		}
		(void)grab_event_num(event_num, name);
	}

	fclose(f);

	if (g_grab.count == 0 && g_grab.inhibit_count == 0)
		return -ENOENT;

	fprintf(stderr, "evdev silence: grab=%d inhibit=%d\n",
		g_grab.count, g_grab.inhibit_count);
	return 0;
}

static void mt_handle_event(struct eink_mt_state *mt,
			    const struct input_event *ev)
{
	int slot;

	if (ev->type != EV_ABS)
		return;

	if (ev->code == ABS_MT_SLOT) {
		if (ev->value >= 0 && ev->value < EINK_MT_SLOTS)
			mt->cur_slot = ev->value;
		return;
	}

	if (ev->code != ABS_MT_TRACKING_ID)
		return;

	slot = mt->cur_slot;
	if (slot < 0 || slot >= EINK_MT_SLOTS)
		slot = 0;
	mt->tracking_id[slot] = ev->value;
}

void evdev_grab_poll(void)
{
	int i;

	for (i = 0; i < g_grab.count; i++) {
		struct input_event ev;
		ssize_t n;

		if (g_grab.fds[i] < 0)
			continue;

		for (;;) {
			n = read(g_grab.fds[i], &ev, sizeof(ev));
			if (n < 0) {
				if (errno == EAGAIN || errno == EINTR)
					break;
				break;
			}
			if (n != (ssize_t)sizeof(ev))
				break;
			mt_handle_event(&g_grab.mt[i], &ev);
		}
	}
}

int evdev_grab_finger_count(void)
{
	int best = 0;
	int i;
	int s;

	for (i = 0; i < g_grab.count; i++) {
		int n = 0;

		for (s = 0; s < EINK_MT_SLOTS; s++) {
			if (g_grab.mt[i].tracking_id[s] >= 0)
				n++;
		}
		if (n > best)
			best = n;
	}

	return best;
}

void evdev_grab_release(void)
{
	int i;

	for (i = 0; i < g_grab.count; i++) {
		if (g_grab.fds[i] < 0)
			continue;
		ioctl(g_grab.fds[i], EVIOCGRAB, 0);
		close(g_grab.fds[i]);
		g_grab.fds[i] = -1;
		mt_reset_one(&g_grab.mt[i]);
	}
	g_grab.count = 0;

	for (i = 0; i < g_grab.inhibit_count; i++) {
		int fd = open(g_grab.inhibit_paths[i], O_WRONLY | O_CLOEXEC);

		if (fd >= 0) {
			(void)write(fd, "0\n", 2);
			close(fd);
		}
	}
	g_grab.inhibit_count = 0;
}
