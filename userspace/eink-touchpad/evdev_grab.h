/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EINK_EVDEV_GRAB_H
#define EINK_EVDEV_GRAB_H

/*
 * Inhibit + EVIOCGRAB 048d:8951 ABS digitizers and REL mice so the
 * compositor only sees our relative uinput pointer (firmware Mouse +
 * absolute touch together cause cursor teleports on finger-down).
 *
 * Grabbed ABS nodes are also drained for ABS_MT finger counts: HID 0x90
 * only reports a single contact id, so 2/3-finger taps need kernel MT.
 */
int evdev_grab_hidraw_siblings(const char *hidraw_path);
void evdev_grab_release(void);

/* Non-blocking: drain grabbed fds and update MT finger tracking. */
void evdev_grab_poll(void);

/* Current total fingers down across ITE ABS_MT devices (0 if unknown). */
int evdev_grab_finger_count(void);

#endif /* EINK_EVDEV_GRAB_H */
