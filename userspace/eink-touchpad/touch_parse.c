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

/*
 * Windows E-hid-0x85: tip 0x05 = contact, tip 0x04 = lift (bit0 = tip switch).
 */
static enum eink_touch_contact_mode mt_mode_from_tip(uint8_t tip)
{
	if (tip & 0x01)
		return EINK_TOUCH_MODE_MOVE;
	return EINK_TOUCH_MODE_UP;
}

static int touch_parse_one_90(const uint8_t *report,
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
 * HID 0x0c multitouch (descriptor + Windows E-hid-0x85):
 *   [0]=0x0c  [1]=const pad  [2]=contact count
 *   each contact (7 B): tip_flags, contact_id, pad,
 *                       x_le16 (0..4319), y_le16 (0..7679)
 * Tip bit0 = tip switch; axes already match native swap (X≤4319, Y≤7679).
 */
static int touch_parse_0c(const uint8_t *buf, size_t len,
			  struct eink_touch_frame *frame)
{
	unsigned int count;
	unsigned int i;
	size_t need;

	if (len < EINK_MT_HEADER_SIZE || buf[0] != EINK_MT_REPORT_ID)
		return -1;

	count = buf[2];
	if (count == 0)
		return 0;
	if (count > EINK_TOUCH_MAX_CONTACTS)
		count = EINK_TOUCH_MAX_CONTACTS;

	need = EINK_MT_HEADER_SIZE + (size_t)count * EINK_MT_CONTACT_STRIDE;
	if (len < need)
		return -1;

	frame->kind = EINK_TOUCH_KIND_MT;

	for (i = 0; i < count; i++) {
		const uint8_t *c =
			buf + EINK_MT_HEADER_SIZE + i * EINK_MT_CONTACT_STRIDE;
		struct eink_touch_contact *out = &frame->contacts[frame->contact_count];
		int raw_x;
		int raw_y;

		out->mode = mt_mode_from_tip(c[0]);
		out->slot = c[1];
		out->new_action = false;
		/* c[2] is HID constant pad */
		raw_x = c[3] | (c[4] << 8);
		raw_y = c[5] | (c[6] << 8);
		/*
		 * Descriptor X max=4319 (height axis), Y max=7679 (width).
		 * touch_coord expects legacy 0x90 raw (width×height) with
		 * display_x from raw_y — same swap: pass (raw_y, raw_x) as
		 * (width-ish, height-ish) so display mapping stays correct.
		 */
		out->raw_x = raw_y;
		out->raw_y = raw_x;
		out->raw_z = 0;
		out->display_x = touch_coord_display_x(out->raw_x, out->raw_y);
		out->display_y = touch_coord_display_y(out->raw_x, out->raw_y);
		frame->contact_count++;
	}

	return 0;
}

/*
 * One read() may contain several back-to-back 11-byte 0x90 reports, or one
 * multi-contact 0x0c report.
 */
int touch_parse_report(const uint8_t *buf, size_t len,
		       struct eink_touch_frame *frame)
{
	size_t offset = 0;

	memset(frame, 0, sizeof(*frame));

	if (!buf || !frame || len == 0)
		return -1;

	if (buf[0] == EINK_MT_REPORT_ID)
		return touch_parse_0c(buf, len, frame);

	frame->kind = EINK_TOUCH_KIND_SINGLE;

	while (offset + EINK_TOUCH_REPORT_SIZE <= len) {
		const uint8_t *report = buf + offset;

		if (report[0] != EINK_TOUCH_REPORT_ID)
			break;

		if (frame->contact_count >= EINK_TOUCH_MAX_CONTACTS)
			break;

		if (touch_parse_one_90(report,
				       &frame->contacts[frame->contact_count]) < 0)
			return -1;

		frame->contact_count++;
		offset += EINK_TOUCH_REPORT_SIZE;
	}

	return 0;
}
