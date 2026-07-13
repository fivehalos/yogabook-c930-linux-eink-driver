/* SPDX-License-Identifier: GPL-2.0-only */

#define _POSIX_C_SOURCE 200809L

#include "evdev_grab.h"
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
		"  -t, --selftest      Inject test pointer motion at startup\n"
		"  -n, --dry-run       Parse input but do not create uinput device\n"
		"  -H, --hid PATH      Use this hidraw node (default: auto-detect)\n"
		"  -s, --sensitivity F Pointer scale factor (default: 1.5)\n"
		"  -r, --rotate DEG    Match output transform: 0, 90, 180, 270 (CW)\n"
		"      --warp-max N    Drop single-frame deltas > N display px (default: 40)\n"
		"      --idle-ms N     Pointer rebase after N ms silence (default: 50)\n"
		"      --release-ms N  End gesture / allow chord within N ms (default: 300)\n"
		"  -g, --grab          Also HIDIOCREVOKE on hidraw (usually unnecessary)\n"
		"      --no-evdev-grab  Allow kernel absolute touch (causes cursor jump)\n"
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
		.tap_threshold_px = 28,
		.pointer_sensitivity = 1.5f,
		.rotate_deg = 0,
		.pointer_max_step_px = 40,
		.pointer_idle_ms = 50,
		.release_idle_ms = 300,
		.chord_sep_px = 100,
		.chord_slot_ttl_ms = 100,
	};
	const char *hid_path = NULL;
	bool debug = false;
	bool dry_run = false;
	bool grab_hid = false;
	bool selftest = false;
	bool grab_evdev = true;
	uint8_t buf[256];
	int opt;

	static const struct option long_opts[] = {
		{ "debug", no_argument, NULL, 'd' },
		{ "selftest", no_argument, NULL, 't' },
		{ "dry-run", no_argument, NULL, 'n' },
		{ "hid", required_argument, NULL, 'H' },
		{ "sensitivity", required_argument, NULL, 's' },
		{ "rotate", required_argument, NULL, 'r' },
		{ "warp-max", required_argument, NULL, 'W' },
		{ "idle-ms", required_argument, NULL, 'I' },
		{ "release-ms", required_argument, NULL, 'R' },
		{ "grab", no_argument, NULL, 'g' },
		{ "no-evdev-grab", no_argument, NULL, 'G' },
		{ "list-hid", no_argument, NULL, 'l' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	while ((opt = getopt_long(argc, argv, "dtnH:s:r:hglG", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'd':
			debug = true;
			break;
		case 't':
			selftest = true;
			break;
		case 'n':
			dry_run = true;
			break;
		case 'g':
			grab_hid = true;
			break;
		case 'G':
			grab_evdev = false;
			break;
		case 'H':
			hid_path = optarg;
			break;
		case 's':
			gcfg.pointer_sensitivity = strtof(optarg, NULL);
			break;
		case 'r':
			gcfg.rotate_deg = atoi(optarg);
			if (gcfg.rotate_deg != 0 && gcfg.rotate_deg != 90 &&
			    gcfg.rotate_deg != 180 && gcfg.rotate_deg != 270) {
				fprintf(stderr,
					"--rotate must be 0, 90, 180, or 270\n");
				return 1;
			}
			break;
		case 'W':
			gcfg.pointer_max_step_px = atoi(optarg);
			break;
		case 'I':
			gcfg.pointer_idle_ms = atoi(optarg);
			break;
		case 'R':
			gcfg.release_idle_ms = atoi(optarg);
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

	if (grab_evdev && !dry_run) {
		int gerr = evdev_grab_hidraw_siblings(hid.path);

		if (gerr < 0)
			fprintf(stderr,
				"warning: could not EVIOCGRAB kernel touch nodes (%s)\n"
				"  absolute cursor jumps may still occur\n",
				strerror(-gerr));
	}

	if (!dry_run) {
		int uinput_err = uinput_open(&out);

		if (uinput_err < 0) {
			fprintf(stderr, "failed to create uinput device: %s\n",
				strerror(-uinput_err));
			hid_touch_close(&hid);
			return 1;
		}
		fprintf(stderr, "uinput devices created (pointer + gesture keyboard)\n");
		if (selftest) {
			int st = uinput_selftest_pointer(&out);

			if (st < 0) {
				fprintf(stderr, "selftest failed: %s\n",
					strerror(-st));
				uinput_close(&out);
				hid_touch_close(&hid);
				return 1;
			}
		}
	}

	gesture_state_init(&gesture, &gcfg);
	fprintf(stderr,
		"touchpad mode active — 1 finger move/tap, 2/3 finger "
		"tap=click swipe=keys\n"
		"  taps: 1=left  2=right  3=middle  "
		"(rotate=%d warp-max=%d idle=%d release=%d)\n",
		gcfg.rotate_deg, gcfg.pointer_max_step_px, gcfg.pointer_idle_ms,
		gcfg.release_idle_ms);

	while (!g_stop) {
		struct eink_touch_frame frame;
		int n;
		int i;

		n = hid_touch_read(&hid, buf, sizeof(buf));
		evdev_grab_poll();
		if (n < 0) {
			if (n == -EINTR)
				continue;
			fprintf(stderr, "hid read error: %s\n", strerror(-n));
			break;
		}
		if (n == 0)
			continue;

		if (touch_parse_report(buf, (size_t)n, &frame) < 0 ||
		    frame.contact_count == 0) {
			if (debug) {
				int h;

				fprintf(stderr, "hid %d bytes (no 0x90 touch):", n);
				for (h = 0; h < n && h < 32; h++)
					fprintf(stderr, " %02x", buf[h]);
				if (n > 32)
					fprintf(stderr, " ...");
				fprintf(stderr, "\n");
			}
			continue;
		}

		if (debug) {
			for (i = 0; i < frame.contact_count; i++) {
				const struct eink_touch_contact *c =
					&frame.contacts[i];

				fprintf(stderr,
					"touch[%d] mode=%d pos=%d,%d raw=%d,%d mt=%d\n",
					c->slot, c->mode, c->display_x,
					c->display_y, c->raw_x, c->raw_y,
					evdev_grab_finger_count());
			}
		}

		if (!dry_run) {
			int mt = evdev_grab_finger_count();
			int gret = gesture_process_frame(&gesture, &frame, &out,
							 debug, mt);

			if (gret < 0)
				fprintf(stderr, "uinput emit failed: %s\n",
					strerror(-gret));
		}
	}

	uinput_close(&out);
	evdev_grab_release();
	hid_touch_close(&hid);
	fprintf(stderr, "stopped\n");
	return 0;
}
