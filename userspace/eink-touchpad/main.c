/* SPDX-License-Identifier: GPL-2.0-only */

#define _POSIX_C_SOURCE 200809L

#include "gestures.h"
#include "hid_touch.h"
#include "touch_parse.h"
#include "uinput_out.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Full-screen E-Ink touchpad emulator: read YogaBook touch HID (0x90),\n"
		"recognise 1/2/3-finger gestures, emit a virtual pointer + keys via uinput.\n"
		"\n"
		"Options:\n"
		"  -d, --debug         Log touch coords and emitted events\n"
		"  -n, --dry-run       Parse input but do not create uinput device\n"
		"  -H, --hid PATH      Use this hidraw node (default: auto-detect)\n"
		"  -s, --sensitivity F Pointer scale factor (default: 1.5)\n"
		"  -g, --grab          Exclusive HID access (HIDIOCREVOKE; may disconnect)\n"
		"  -l, --list-hid      Show YogaBook hidraw nodes and exit\n"
		"  -h, --help          Show this help\n"
		"\n"
		"Architecture:\n"
		"  hidraw (touch) -> this daemon -> /dev/uinput -> libinput -> compositor\n"
		"\n"
		"Drawing a blank panel is separate: exit firmware keyboard mode and let the\n"
		"DRM driver / compositor own pixels. This tool only handles input.\n"
		"\n"
		"Requires root (or input group + uinput access) for HID grab and uinput.\n",
		prog);
}

int main(int argc, char **argv)
{
	struct eink_hid_touch hid = { .fd = -1 };
	struct eink_uinput out = { .fd = -1 };
	struct eink_gesture_state gesture;
	struct eink_gesture_config gcfg = {
		.swipe_threshold_px = 80,
		.tap_threshold_px = 12,
		.pointer_sensitivity = 1.5f,
	};
	const char *hid_path = NULL;
	bool debug = false;
	bool dry_run = false;
	bool grab_hid = false;
	uint8_t buf[256];
	int opt;

	static const struct option long_opts[] = {
		{ "debug", no_argument, NULL, 'd' },
		{ "dry-run", no_argument, NULL, 'n' },
		{ "hid", required_argument, NULL, 'H' },
		{ "sensitivity", required_argument, NULL, 's' },
		{ "grab", no_argument, NULL, 'g' },
		{ "list-hid", no_argument, NULL, 'l' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	while ((opt = getopt_long(argc, argv, "dnH:s:hgl", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'd':
			debug = true;
			break;
		case 'n':
			dry_run = true;
			break;
		case 'g':
			grab_hid = true;
			break;
		case 'H':
			hid_path = optarg;
			break;
		case 's':
			gcfg.pointer_sensitivity = strtof(optarg, NULL);
			break;
		case 'l':
			return hid_touch_list_candidates() == 0 ? 0 : 1;
		case 'h':
			usage(argv[0]);
			return 0;
		case '?':
			return 1;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	int hid_err;

	hid_err = hid_touch_open(&hid, hid_path, grab_hid);
	if (hid_err < 0) {
		fprintf(stderr, "failed to open touch hidraw");
		if (hid_path)
			fprintf(stderr, " (%s)", hid_path);
		fprintf(stderr, ": %s\n", strerror(-hid_err));
		fprintf(stderr, "Hint: run as root, and ensure firmware keyboard is not\n"
				"holding the touch interface exclusively.\n");
		return 1;
	}

	fprintf(stderr, "touch HID: %s\n", hid.path);

	if (!dry_run) {
		int uinput_err = uinput_open(&out);

		if (uinput_err < 0) {
			fprintf(stderr, "failed to create uinput device: %s\n",
				strerror(-uinput_err));
			hid_touch_close(&hid);
			return 1;
		}
		fprintf(stderr, "uinput device created (YogaBook E-Ink Touchpad)\n");
	}

	gesture_state_init(&gesture, &gcfg);
	fprintf(stderr, "touchpad mode active — 1 finger move, 2/3 finger swipe\n");

	while (!g_stop) {
		struct eink_touch_frame frame;
		int n;
		int i;

		n = hid_touch_read(&hid, buf, sizeof(buf));
		if (n < 0) {
			if (n == -EINTR)
				continue;
			fprintf(stderr, "hid read error: %s\n", strerror(-n));
			break;
		}
		if (n == 0)
			continue;

		if (touch_parse_report(buf, (size_t)n, &frame) < 0) {
			if (debug)
				fprintf(stderr, "ignored non-touch report\n");
			continue;
		}

		if (debug) {
			for (i = 0; i < frame.contact_count; i++) {
				const struct eink_touch_contact *c =
					&frame.contacts[i];

				fprintf(stderr,
					"touch[%d] mode=%d pos=%d,%d raw=%d,%d\n",
					c->slot, c->mode, c->display_x,
					c->display_y, c->raw_x, c->raw_y);
			}
		}

		if (!dry_run)
			gesture_process_frame(&gesture, &frame, &out, debug);
	}

	uinput_close(&out);
	hid_touch_close(&hid);
	fprintf(stderr, "stopped\n");
	return 0;
}
