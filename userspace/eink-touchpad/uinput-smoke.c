/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Minimal uinput smoke test — inject REL forever so evtest can attach.
 *
 *   sudo ./uinput-smoke
 *   # other terminal: use the event node this prints
 *   sudo evtest /dev/input/eventN
 */

#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

static const char *device_name = "YogaBook uinput smoke";

static int emit(int fd, __u16 type, __u16 code, __s32 value)
{
	struct input_event ev;
	ssize_t n;

	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;

	n = write(fd, &ev, sizeof(ev));
	if (n < 0) {
		fprintf(stderr, "write type=%u code=%u: %s\n", type, code,
			strerror(errno));
		return -1;
	}
	if (n != (ssize_t)sizeof(ev)) {
		fprintf(stderr, "short write: %zd\n", n);
		return -1;
	}
	return 0;
}

/* Parse /proc/bus/input/devices for our name; return event number or -1. */
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

static void print_device_block(const char *name)
{
	FILE *f;
	char line[512];
	int in_block = 0;

	f = fopen("/proc/bus/input/devices", "r");
	if (!f) {
		perror("open /proc/bus/input/devices");
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "N: Name=", 8) == 0)
			in_block = (strstr(line, name) != NULL);

		if (in_block)
			fputs(line, stderr);

		if (in_block && line[0] == '\n')
			break;
	}

	fclose(f);
}

int main(void)
{
	struct uinput_setup setup;
	char sysname[64];
	int fd;
	int event_num;
	int version = 0;
	int i = 0;

	fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		fd = open("/dev/input/uinput", O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		perror("open uinput");
		return 1;
	}

	if (ioctl(fd, UI_GET_VERSION, &version) == 0)
		fprintf(stderr, "uinput API version %d\n", version);
	else
		fprintf(stderr, "UI_GET_VERSION failed: %s\n", strerror(errno));

	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
		perror("UI_SET_EVBIT EV_KEY");
		return 1;
	}
	if (ioctl(fd, UI_SET_KEYBIT, BTN_LEFT) < 0) {
		perror("UI_SET_KEYBIT");
		return 1;
	}
	if (ioctl(fd, UI_SET_EVBIT, EV_REL) < 0) {
		perror("UI_SET_EVBIT EV_REL");
		return 1;
	}
	if (ioctl(fd, UI_SET_RELBIT, REL_X) < 0 ||
	    ioctl(fd, UI_SET_RELBIT, REL_Y) < 0) {
		perror("UI_SET_RELBIT");
		return 1;
	}
	if (ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER) < 0) {
		perror("UI_SET_PROPBIT");
		return 1;
	}

	memset(&setup, 0, sizeof(setup));
	snprintf(setup.name, sizeof(setup.name), "%s", device_name);
	setup.id.bustype = BUS_USB;
	setup.id.vendor = 0x1234;
	setup.id.product = 0x5678;
	setup.id.version = 1;

	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0) {
		perror("UI_DEV_SETUP");
		return 1;
	}
	if (ioctl(fd, UI_DEV_CREATE) < 0) {
		perror("UI_DEV_CREATE");
		return 1;
	}

	usleep(400 * 1000);

	memset(sysname, 0, sizeof(sysname));
	/* UI_GET_SYSNAME returns strlen+1 on success, not 0. */
	if (ioctl(fd, UI_GET_SYSNAME(sizeof(sysname)), sysname) >= 0)
		fprintf(stderr, "UI_GET_SYSNAME -> %s\n", sysname);
	else
		fprintf(stderr, "UI_GET_SYSNAME failed: %s (falling back to /proc)\n",
			strerror(errno));

	fprintf(stderr, "--- /proc/bus/input/devices block ---\n");
	print_device_block(device_name);
	fprintf(stderr, "------------------------------------\n");

	event_num = find_event_num(device_name);
	if (event_num >= 0) {
		fprintf(stderr,
			"\n*** run this in another terminal ***\n"
			"  sudo evtest /dev/input/event%d\n"
			"  sudo libinput debug-events --device /dev/input/event%d\n"
			"************************************\n\n",
			event_num, event_num);
	} else {
		fprintf(stderr,
			"WARNING: device name not found in /proc/bus/input/devices\n"
			"UI_DEV_CREATE may have failed silently — events go nowhere.\n");
	}

	fprintf(stderr, "injecting REL_X=1 every 100ms — Ctrl+C to stop\n");

	for (;;) {
		if (emit(fd, EV_REL, REL_X, 1) < 0)
			return 1;
		if (emit(fd, EV_SYN, SYN_REPORT, 0) < 0)
			return 1;
		if ((i++ % 10) == 0)
			fprintf(stderr, "injected %d events\n", i);
		usleep(100 * 1000);
	}
}
