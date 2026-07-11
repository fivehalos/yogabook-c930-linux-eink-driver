/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EINK_TOUCH_PARSE_H
#define EINK_TOUCH_PARSE_H

#include "../common/eink_touch.h"

#include <stddef.h>
#include <stdint.h>

int touch_parse_report(const uint8_t *buf, size_t len,
		       struct eink_touch_frame *frame);

#endif /* EINK_TOUCH_PARSE_H */
