/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared driver state for the YogaBook C930 E-Ink DRM/USB driver.
 */

#ifndef EINK_DRM_DRV_H
#define EINK_DRM_DRV_H

#include <linux/mutex.h>
#include <linux/usb.h>
#include <drm/drm_device.h>
#include <drm/drm_simple_kms_helper.h>

#include "../protocol/ite8951.h"

struct eink_connector;

/**
 * struct eink_drm_device - per-panel USB + DRM state (matches GUD layout)
 * @drm: DRM device embedded for devm_drm_dev_alloc()
 * @pipe: Simple display pipe immediately after @drm (plane + CRTC + encoder)
 * @usb_link: ITE8951 bulk transport
 * @intf: Claimed vendor bulk USB interface
 * @connector: Heap-allocated connector (GUD does not embed this in the main struct)
 * @autosuspend_disabled: USB autosuspend was disabled for reliable bulk I/O
 */
struct eink_drm_device {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct ite8951_usb usb_link;
	struct usb_interface *intf;
	struct eink_connector *connector;
	struct mutex update_lock;
	unsigned long last_blit_jiffies;
	bool panel_init_refresh_done;
	bool autosuspend_disabled;
};

struct eink_drm_device *eink_drm_device_create(struct usb_interface *intf);
int eink_drm_kms_register(struct eink_drm_device *pdev);
void eink_drm_kms_unregister(struct eink_drm_device *pdev);

#endif /* EINK_DRM_DRV_H */
