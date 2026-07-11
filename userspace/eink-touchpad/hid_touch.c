/* SPDX-License-Identifier: GPL-2.0-only */

#define _DEFAULT_SOURCE

#include "hid_touch.h"

#include "../common/eink_panel.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/hidraw.h>
#include <poll.h>
#include <sys/ioctl.h>

#define EINK_TOUCH_REPORT_ID	0x90
#define EINK_PEN_REPORT_ID	0x91

static int hid_read_unsigned(const char *path, unsigned int *value)
{
	FILE *f;
	char line[32];

	f = fopen(path, "r");
	if (!f)
		return -1;

	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return -1;
	}

	fclose(f);
	*value = (unsigned int)strtoul(line, NULL, 10);
	return 0;
}

static int hid_path_matches_yogabook(const char *hidraw_sysfs_dir)
{
	char sysfs_path[512];
	char line[256];
	FILE *f;
	unsigned int vendor = 0;
	unsigned int product = 0;
	int found = 0;

	snprintf(sysfs_path, sizeof(sysfs_path), "%s/device/uevent",
		 hidraw_sysfs_dir);

	f = fopen(sysfs_path, "r");
	if (!f)
		return 0;

	while (fgets(line, sizeof(line), f)) {
		/* HID_ID=0003:0000048D:00008951 (bus:vendor:product) */
		if (sscanf(line, "HID_ID=%*x:%x:%x", &vendor, &product) == 2) {
			found = 1;
			break;
		}
	}

	fclose(f);

	if (!found)
		return 0;

	return vendor == EINK_USB_VENDOR_ID &&
	       product == EINK_USB_PRODUCT_ID;
}

static int hid_report_has_id(const char *hidraw_sysfs_dir, uint8_t report_id)
{
	char sysfs_path[512];
	uint8_t desc[HID_MAX_DESCRIPTOR_SIZE];
	FILE *f;
	size_t len;
	size_t i;

	snprintf(sysfs_path, sizeof(sysfs_path),
		 "%s/device/report_descriptor", hidraw_sysfs_dir);

	f = fopen(sysfs_path, "rb");
	if (!f)
		return 0;

	len = fread(desc, 1, sizeof(desc), f);
	fclose(f);

	for (i = 0; i + 1 < len; i++) {
		if (desc[i] == 0x85 && desc[i + 1] == report_id)
			return 1;
	}

	return 0;
}

static int hid_fill_candidate(const char *hidraw_name,
			      struct eink_hid_candidate *candidate)
{
	char sysfs_dir[256];
	char hidraw_path[EINK_HID_PATH_MAX];
	unsigned int iface = 99;
	unsigned int protocol = 99;

	snprintf(sysfs_dir, sizeof(sysfs_dir), "/sys/class/hidraw/%s",
		 hidraw_name);
	snprintf(hidraw_path, sizeof(hidraw_path), "/dev/%s", hidraw_name);

	if (!hid_path_matches_yogabook(sysfs_dir))
		return -ENODEV;

	char iface_path[512];
	char proto_path[512];

	snprintf(iface_path, sizeof(iface_path),
		 "%s/device/../bInterfaceNumber", sysfs_dir);
	snprintf(proto_path, sizeof(proto_path),
		 "%s/device/../bInterfaceProtocol", sysfs_dir);
	hid_read_unsigned(iface_path, &iface);
	hid_read_unsigned(proto_path, &protocol);

	memset(candidate, 0, sizeof(*candidate));
	snprintf(candidate->path, sizeof(candidate->path), "%s", hidraw_path);
	candidate->iface = (int)iface;
	candidate->protocol = (int)protocol;
	candidate->has_touch_report =
		hid_report_has_id(sysfs_dir, EINK_TOUCH_REPORT_ID);
	candidate->has_pen_report =
		hid_report_has_id(sysfs_dir, EINK_PEN_REPORT_ID);

	return 0;
}

static int hid_touch_score_candidate(const struct eink_hid_candidate *candidate)
{
	int score = 0;

	if (candidate->protocol == 1)
		return -1;

	if (candidate->has_touch_report)
		score += 100;
	if (candidate->has_pen_report)
		score += 10;

	/* Prefer the dedicated touch interface (USB iface 1 / hidraw0). */
	if (candidate->iface == 1)
		score += 5;

	return score;
}

static int hid_touch_pick_candidate(struct eink_hid_candidate *best_out)
{
	DIR *dir;
	struct dirent *ent;
	struct eink_hid_candidate best;
	int best_score = -1;
	int found = 0;

	dir = opendir("/sys/class/hidraw");
	if (!dir)
		return -ENODEV;

	memset(&best, 0, sizeof(best));

	while ((ent = readdir(dir)) != NULL) {
		struct eink_hid_candidate candidate;
		int score;

		if (strncmp(ent->d_name, "hidraw", 6) != 0)
			continue;

		if (hid_fill_candidate(ent->d_name, &candidate) < 0)
			continue;

		score = hid_touch_score_candidate(&candidate);
		if (score < 0)
			continue;

		if (!found || score > best_score) {
			best = candidate;
			best_score = score;
			found = 1;
		}
	}

	closedir(dir);

	if (!found || best_score <= 0)
		return -ENODEV;

	*best_out = best;
	return 0;
}

int hid_touch_list_candidates(void)
{
	DIR *dir;
	struct dirent *ent;
	int any = 0;

	dir = opendir("/sys/class/hidraw");
	if (!dir)
		return -ENODEV;

	printf("YogaBook hidraw candidates (VID %04x PID %04x):\n",
	       EINK_USB_VENDOR_ID, EINK_USB_PRODUCT_ID);

	while ((ent = readdir(dir)) != NULL) {
		struct eink_hid_candidate candidate;
		int score;

		if (strncmp(ent->d_name, "hidraw", 6) != 0)
			continue;

		if (hid_fill_candidate(ent->d_name, &candidate) < 0)
			continue;

		any = 1;
		score = hid_touch_score_candidate(&candidate);

		printf("  %s  iface=%d proto=%d touch0x90=%s pen0x91=%s score=%d%s\n",
		       candidate.path,
		       candidate.iface,
		       candidate.protocol,
		       candidate.has_touch_report ? "yes" : "no",
		       candidate.has_pen_report ? "yes" : "no",
		       score,
		       score < 0 ? " (skipped)" :
		       score >= 100 ? " <-- touch" : "");
	}

	closedir(dir);

	if (!any)
		printf("  (none — is USB %04x:%04x connected?)\n",
		       EINK_USB_VENDOR_ID, EINK_USB_PRODUCT_ID);

	return any ? 0 : -ENODEV;
}

static int hid_touch_find_path(char *path, size_t pathlen)
{
	struct eink_hid_candidate best;

	if (hid_touch_pick_candidate(&best) < 0)
		return -ENODEV;

	snprintf(path, pathlen, "%s", best.path);
	return 0;
}

int hid_touch_open(struct eink_hid_touch *hid, const char *path_override,
		   bool grab)
{
	const char *path = path_override;
	int fd;

	if (!hid)
		return -EINVAL;

	memset(hid, 0, sizeof(*hid));
	hid->fd = -1;
	hid->grab = grab;

	if (!path) {
		if (hid_touch_find_path(hid->path, sizeof(hid->path)) < 0)
			return -ENODEV;
		path = hid->path;
	} else {
		snprintf(hid->path, sizeof(hid->path), "%s", path);
	}

	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	if (grab) {
		if (ioctl(fd, HIDIOCREVOKE, 0) < 0) {
			fprintf(stderr,
				"warning: HIDIOCREVOKE failed on %s: %s\n",
				path, strerror(errno));
		} else {
			fprintf(stderr, "exclusive HID grab enabled on %s\n",
				path);
		}
	}

	hid->fd = fd;
	return 0;
}

void hid_touch_close(struct eink_hid_touch *hid)
{
	if (!hid || hid->fd < 0)
		return;

	close(hid->fd);
	hid->fd = -1;
}

int hid_touch_read(struct eink_hid_touch *hid, uint8_t *buf, size_t buflen)
{
	struct pollfd pfd;
	ssize_t n;
	int ret;

	if (!hid || hid->fd < 0 || !buf)
		return -EINVAL;

	pfd.fd = hid->fd;
	pfd.events = POLLIN;
	pfd.revents = 0;

	ret = poll(&pfd, 1, -1);
	if (ret < 0)
		return -errno;
	if (ret == 0)
		return 0;

	if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
		return -ENODEV;

	n = read(hid->fd, buf, buflen);
	if (n < 0)
		return -errno;

	return (int)n;
}
