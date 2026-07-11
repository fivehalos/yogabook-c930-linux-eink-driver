/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared panel constants for YogaBook C930 E-Ink userspace tools.
 * Keep in sync with kernel/eink_drm/eink_panel.h.
 */

#ifndef EINK_USERSPACE_PANEL_H
#define EINK_USERSPACE_PANEL_H

#define EINK_PANEL_WIDTH	1920
#define EINK_PANEL_HEIGHT	1080
#define EINK_GREY_LEVELS	16

#define EINK_USB_VENDOR_ID	0x048d
#define EINK_USB_PRODUCT_ID	0x8951

/*
 * Touch digitizer reports coordinates in a larger native range; scale to the
 * 1920x1080 panel after the axis swap documented in touch_coord.c.
 */
#define EINK_TOUCH_NATIVE_WIDTH		7680
#define EINK_TOUCH_NATIVE_HEIGHT	4320

#endif /* EINK_USERSPACE_PANEL_H */
