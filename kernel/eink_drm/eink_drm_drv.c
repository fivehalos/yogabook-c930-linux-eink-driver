// SPDX-License-Identifier: GPL-2.0-only
/*
 * Lenovo YogaBook C930 E-Ink — DRM/USB driver (early bring-up).
 */

#include <linux/module.h>
#include <linux/usb.h>

#include "drm/eink_drm_drv.h"
#include "eink_panel.h"
#include "protocol/ite8951.h"

#define DRIVER_NAME	"eink_drm"

static const struct usb_device_id eink_drm_usb_ids[] = {
	{ USB_DEVICE(EINK_USB_VENDOR_ID, EINK_USB_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE(usb, eink_drm_usb_ids);

static int eink_drm_power_up(struct usb_interface *intf,
			     struct eink_drm_device *pdev)
{
	int ret;

	usb_disable_autosuspend(interface_to_usbdev(intf));
	pdev->autosuspend_disabled = true;

	ret = usb_autopm_get_interface(intf);
	if (ret < 0) {
		dev_err(&intf->dev, "USB resume failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void eink_drm_power_down(struct usb_interface *intf,
				 struct eink_drm_device *pdev)
{
	if (!pdev)
		return;

	usb_autopm_put_interface(intf);

	if (pdev->autosuspend_disabled) {
		usb_enable_autosuspend(interface_to_usbdev(intf));
		pdev->autosuspend_disabled = false;
	}
}

static int eink_drm_probe_vendor_interface(struct usb_interface *intf)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct eink_drm_device *pdev;
	struct usb_endpoint_descriptor *bulk_in, *bulk_out;
	int ret;

	pdev = eink_drm_device_create(intf);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	usb_set_intfdata(intf, pdev);

	/* USB reconnect reuses the devm pdev — drop stale link/DRM state. */
	if (pdev->usb_link.usb_dev) {
		eink_drm_kms_unregister(pdev);
		ite8951_usb_teardown(&pdev->usb_link);
		mutex_destroy(&pdev->usb_link.io_lock);
	}
	memset(&pdev->usb_link, 0, sizeof(pdev->usb_link));
	pdev->panel_init_refresh_done = false;

	ret = usb_find_common_endpoints(intf->cur_altsetting,
					&bulk_in, &bulk_out, NULL, NULL);
	if (ret) {
		dev_err(&intf->dev, "missing bulk endpoints: %d\n", ret);
		goto err_clear_intfdata;
	}

	pdev->usb_link.usb_dev = usb_dev;
	pdev->usb_link.bulk_in = bulk_in->bEndpointAddress;
	pdev->usb_link.bulk_out = bulk_out->bEndpointAddress;
	pdev->usb_link.bulk_in_max = usb_endpoint_maxp(bulk_in);
	mutex_init(&pdev->usb_link.io_lock);

	ret = eink_drm_power_up(intf, pdev);
	if (ret)
		goto err_clear_intfdata;

	ret = ite8951_usb_setup(&pdev->usb_link);
	if (ret) {
		dev_err(&intf->dev, "USB transport setup failed: %d\n", ret);
		goto err_power_down;
	}

	dev_info(&intf->dev, "YogaBook E-Ink panel USB link ready\n");

	ret = ite8951_panel_init_sequence(&pdev->usb_link);
	if (ret) {
		dev_err(&intf->dev, "panel init failed: %d\n", ret);
		goto err_teardown;
	}

	ret = eink_drm_kms_register(pdev);
	if (ret) {
		dev_err(&intf->dev, "DRM registration failed: %d\n", ret);
		eink_drm_kms_unregister(pdev);
		goto err_teardown;
	}

	return 0;

err_teardown:
	ite8951_usb_teardown(&pdev->usb_link);
err_power_down:
	eink_drm_power_down(intf, pdev);
err_clear_intfdata:
	usb_set_intfdata(intf, NULL);
	return ret;
}

static void eink_drm_disconnect(struct usb_interface *intf)
{
	struct eink_drm_device *pdev = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (!pdev)
		return;

	eink_drm_kms_unregister(pdev);
	ite8951_usb_teardown(&pdev->usb_link);
	mutex_destroy(&pdev->usb_link.io_lock);
	memset(&pdev->usb_link, 0, sizeof(pdev->usb_link));
	eink_drm_power_down(intf, pdev);
	dev_info(&intf->dev, "YogaBook E-Ink panel disconnected\n");
}

static int eink_drm_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	if (intf->cur_altsetting->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC)
		return -ENODEV;

	return eink_drm_probe_vendor_interface(intf);
}

static struct usb_driver eink_drm_usb_driver = {
	.name			= DRIVER_NAME,
	.probe			= eink_drm_probe,
	.disconnect		= eink_drm_disconnect,
	.id_table		= eink_drm_usb_ids,
	.supports_autosuspend	= 1,
};

module_usb_driver(eink_drm_usb_driver);

MODULE_AUTHOR("YogaBook C930 Linux contributors");
MODULE_DESCRIPTION("DRM driver for Lenovo YogaBook C930 E-Ink panel (bring-up)");
MODULE_LICENSE("GPL");
