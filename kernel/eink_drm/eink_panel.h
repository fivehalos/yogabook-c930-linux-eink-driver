/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Panel constants for the Lenovo YogaBook C930 (YB-J912) E-Ink keyboard.
 */

#ifndef EINK_PANEL_H
#define EINK_PANEL_H

#define EINK_PANEL_WIDTH	1920
#define EINK_PANEL_HEIGHT	1080
#define EINK_GREY_LEVELS	16

#define EINK_USB_VENDOR_ID	0x048d
#define EINK_USB_PRODUCT_ID	0x8951

/* USB interface numbers on 048d:8951 (see hid_touch discovery). */
#define EINK_USB_IFACE_VENDOR_BULK	0
#define EINK_USB_IFACE_TOUCH_HID	1
#define EINK_USB_IFACE_BOOT_KEYBOARD	2

/* Touch digitizer native range (see itetcon.h TP area table). */
#define EINK_TOUCH_NATIVE_WIDTH		7680
#define EINK_TOUCH_NATIVE_HEIGHT	4320

#endif /* EINK_PANEL_H */
