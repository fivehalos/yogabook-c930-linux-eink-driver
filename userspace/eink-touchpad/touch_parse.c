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
 * Linux hidraw after mt-arm may stream report 0x03 with the same header:
 *   [0]=id  [1]=pad  [2]=contact count
 * Each contact (7 B): tip_flags, contact_id, pad, x_le16, y_le16
 */
static int touch_mt_contact_count(uint8_t report_id, const uint8_t *buf)
{
	if (report_id == EINK_MT_REPORT_ID ||
	    report_id == EINK_MT_REPORT_ID_LINUX)
		return buf[2];
	return -1;
}

static int touch_parse_mt_at(const uint8_t *buf, size_t len,
			     struct eink_touch_frame *frame)
{
	int count_i;
	unsigned int count;
	unsigned int i;
	size_t need;
	uint8_t report_id = buf[0];

	if (len < EINK_MT_HEADER_SIZE)
		return -1;

	count_i = touch_mt_contact_count(report_id, buf);
	if (count_i < 0)
		return -1;

	count = (unsigned int)count_i;
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
		if (out->slot < 0 || out->slot >= EINK_TOUCH_MAX_CONTACTS)
			out->slot = (int)frame->contact_count;
		out->new_action = false;
		raw_x = (int)(uint16_t)(c[3] | (c[4] << 8));
		raw_y = (int)(uint16_t)(c[5] | (c[6] << 8));
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

	if (buf[0] == EINK_MT_REPORT_ID || buf[0] == EINK_MT_REPORT_ID_LINUX) {
		size_t offset = 0;
		int ret = -1;

		/*
		 * One read() may bundle several back-to-back MT reports (often
		 * 17 B each for 0x03 / 2 contacts). Use the last complete one.
		 */
		while (offset < len) {
			int count_i;
			size_t need;

			if (buf[offset] != EINK_MT_REPORT_ID &&
			    buf[offset] != EINK_MT_REPORT_ID_LINUX)
				break;

			count_i = touch_mt_contact_count(buf[offset],
							 buf + offset);
			if (count_i < 0)
				break;

			need = EINK_MT_HEADER_SIZE +
			       (size_t)count_i * EINK_MT_CONTACT_STRIDE;
			if (offset + need > len)
				break;

			memset(frame, 0, sizeof(*frame));
			if (touch_parse_mt_at(buf + offset, need, frame) == 0)
				ret = 0;

			offset += need;
		}

		return ret;
	}

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
