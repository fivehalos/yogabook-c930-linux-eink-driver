/* SPDX-License-Identifier: GPL-2.0-only */

#include "touch_parse.h"

#include "touch_coord.h"

#include <string.h>

static enum eink_touch_contact_mode touch_mode_from_byte(uint8_t mode)
{
	switch (mode) {
	case 0x01:
		return EINK_TOUCH_MODE_DOWN;
	case 0x02:
		return EINK_TOUCH_MODE_UP;
	default:
		return EINK_TOUCH_MODE_MOVE;
	}
}

static int touch_parse_one(const uint8_t *report,
			   struct eink_touch_contact *contact)
{
	int raw_x;
	int raw_y;
	int raw_z;

	if (report[0] != EINK_TOUCH_REPORT_ID)
		return -1;

	contact->slot = report[2];
	contact->mode = touch_mode_from_byte(report[3]);
	contact->new_action = report[4] == 0;

	raw_x = report[5] | (report[6] << 8);
	raw_y = report[7] | (report[8] << 8);
	raw_z = report[9] | (report[10] << 8);

	contact->raw_x = raw_x;
	contact->raw_y = raw_y;
	contact->raw_z = raw_z;
	contact->display_x = touch_coord_display_x(raw_x, raw_y);
	contact->display_y = touch_coord_display_y(raw_x, raw_y);

	return 0;
}

/*
 * One read() may contain several back-to-back 11-byte reports (one finger each).
 */
int touch_parse_report(const uint8_t *buf, size_t len,
		       struct eink_touch_frame *frame)
{
	size_t offset = 0;

	memset(frame, 0, sizeof(*frame));

	if (!buf || !frame)
		return -1;

	while (offset + EINK_TOUCH_REPORT_SIZE <= len) {
		const uint8_t *report = buf + offset;

		if (report[0] != EINK_TOUCH_REPORT_ID)
			break;

		if (frame->contact_count >= EINK_TOUCH_MAX_CONTACTS)
			break;

		if (touch_parse_one(report,
				    &frame->contacts[frame->contact_count]) < 0)
			return -1;

		frame->contact_count++;
		offset += EINK_TOUCH_REPORT_SIZE;
	}

	return 0;
}
