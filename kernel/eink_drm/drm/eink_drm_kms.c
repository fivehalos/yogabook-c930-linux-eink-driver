// SPDX-License-Identifier: GPL-2.0-only
/*
 * DRM/KMS layer for the YogaBook C930 E-Ink panel.
 *
 * Follows the in-kernel GUD USB display driver layout and init order.
 */

#include <linux/dma-direction.h>
#include <linux/dma-resv.h>
#include <linux/iosys-map.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "../eink_panel.h"
#include "../protocol/ite8951.h"
#include "eink_drm_drv.h"

#define EINK_DRM_DRIVER_NAME	"eink_drm"
#define EINK_DRM_CHUNK_ROWS		32

static bool drm_enable = true;
module_param(drm_enable, bool, 0644);
MODULE_PARM_DESC(drm_enable,
		 "Register a DRM connector (0 = USB init only, for recovery)");

/*
 * Bisect DRM bring-up on hardware (default 5 = full path):
 *  1 = mode config only
 *  2 = + simple pipe
 *  3 = + connector
 *  4 = + mode config reset
 *  5 = + drm_dev_register
 */
static int drm_stage = 5;
module_param(drm_stage, int, 0644);
MODULE_PARM_DESC(drm_stage, "DRM init stage for hardware bisect (1-5)");

static bool panel_test_pattern = true;
module_param(panel_test_pattern, bool, 0644);
MODULE_PARM_DESC(panel_test_pattern,
		 "Upload grey ramp test pattern instead of compositor pixels (bring-up)");

static bool exit_keyboard_on_load;
module_param(exit_keyboard_on_load, bool, 0644);
MODULE_PARM_DESC(exit_keyboard_on_load,
		 "Exit firmware keyboard mode at load (raw touch HID for userspace)");

static unsigned int min_blit_interval_ms = 2000;
module_param(min_blit_interval_ms, uint, 0644);
MODULE_PARM_DESC(min_blit_interval_ms,
		 "Minimum ms between full-frame blits (E-Ink refresh budget)");

struct eink_connector {
	struct drm_connector base;
};

static const u32 eink_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const u64 eink_plane_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static void eink_drm_step(struct device *dev, int stage, const char *msg)
{
	dev_alert(dev, "DRM stage %d/%d: %s\n", stage, drm_stage, msg);
}

static u8 eink_drm_grey_to_panel_byte(u8 grey)
{
	grey &= 0x0f;
	return (grey << 4) | 0x0f;
}

static u8 eink_drm_xrgb_to_panel_byte(u32 pixel)
{
	u8 red = (pixel >> 16) & 0xff;
	u8 green = (pixel >> 8) & 0xff;
	u8 blue = pixel & 0xff;
	u32 luminance = (red * 30 + green * 59 + blue * 11) / 100;

	return eink_drm_grey_to_panel_byte(luminance >> 4);
}

static u32 eink_drm_read_xrgb_pixel(const struct drm_framebuffer *fb,
				    const struct iosys_map *map,
				    u32 x, u32 y)
{
	size_t offset = fb->offsets[0] + y * fb->pitches[0] +
			x * fb->format->cpp[0];

	if (!map->is_iomem && map->vaddr)
		return *(const u32 *)(map->vaddr + offset);

	return iosys_map_rd(map, offset, u32);
}

static int eink_drm_fb_wait_render(struct drm_framebuffer *fb)
{
	struct drm_gem_object *obj = drm_gem_fb_get_obj(fb, 0);
	long ret;

	if (!obj || !obj->resv)
		return 0;

	ret = dma_resv_wait_timeout(obj->resv, DMA_RESV_USAGE_READ,
				    true, msecs_to_jiffies(1000));
	if (ret < 0)
		return ret;

	return 0;
}

static int eink_drm_push_rect(struct eink_drm_device *pdev,
			      const struct drm_framebuffer *fb,
			      const struct iosys_map *map,
			      const struct ite8951_rect *dest)
{
	struct ite8951_usb *link = &pdev->usb_link;
	u32 row, panel_stride = EINK_PANEL_WIDTH;
	u32 buf_offset = dest->y * panel_stride + dest->x;
	u8 *chunk;
	size_t chunk_size = dest->width * dest->height;
	int ret;

	chunk = kvmalloc(chunk_size, GFP_KERNEL);
	if (!chunk)
		return -ENOMEM;

	for (row = 0; row < dest->height; row++) {
		u32 src_y = dest->y + row;
		u32 col;
		u8 *dst = chunk + row * dest->width;

		for (col = 0; col < dest->width; col++) {
			u32 src_x = dest->x + col;
			u32 pixel;

			if (src_x >= fb->width || src_y >= fb->height) {
				dst[col] = eink_drm_grey_to_panel_byte(0);
				continue;
			}

			pixel = eink_drm_read_xrgb_pixel(fb, map, src_x, src_y);
			dst[col] = eink_drm_xrgb_to_panel_byte(pixel);
		}
	}

	ret = ite8951_load_pixels(link, buf_offset, dest, chunk, chunk_size);
	kvfree(chunk);
	return ret;
}

static int eink_drm_push_frame(struct eink_drm_device *pdev,
			       struct drm_framebuffer *fb,
			       const struct iosys_map *map)
{
	struct ite8951_rect chunk;
	u32 row;
	static bool logged_sample;

	if (!logged_sample && iosys_map_is_set(map)) {
		u32 p0 = eink_drm_read_xrgb_pixel(fb, map, 0, 0);
		u32 pm = eink_drm_read_xrgb_pixel(fb, map, fb->width / 2,
						  fb->height / 2);

		dev_info(pdev->drm.dev,
			 "fb sample pixels: offset=%u pitch=%u center=0x%08x origin=0x%08x\n",
			 fb->offsets[0], fb->pitches[0], pm, p0);
		logged_sample = true;
	}

	chunk.x = 0;
	chunk.width = fb->width;

	for (row = 0; row < fb->height; row += EINK_DRM_CHUNK_ROWS) {
		chunk.y = row;
		chunk.height = min_t(u32, EINK_DRM_CHUNK_ROWS, fb->height - row);

		if (chunk.height * chunk.width > ITE8951_MAX_XFER_BYTES)
			return -E2BIG;

		if (eink_drm_push_rect(pdev, fb, map, &chunk))
			return -EIO;
	}

	return 0;
}

#define EINK_PATCH_W			200
#define EINK_PATCH_H			200

static int eink_drm_push_center_patch(struct eink_drm_device *pdev,
				      struct ite8951_rect *blit_dest)
{
	struct ite8951_usb *link = &pdev->usb_link;
	struct ite8951_rect load = {
		.x = 0,
		.y = 0,
		.width = EINK_PATCH_W,
		.height = EINK_PATCH_H,
	};
	u8 *pixels;
	u32 x, y, row;
	int ret;

	x = (EINK_PANEL_WIDTH - EINK_PATCH_W) / 2;
	y = (EINK_PANEL_HEIGHT - EINK_PATCH_H) / 2;

	blit_dest->x = x;
	blit_dest->y = y;
	blit_dest->width = EINK_PATCH_W;
	blit_dest->height = EINK_PATCH_H;

	pixels = kvmalloc(EINK_PATCH_W * EINK_PATCH_H, GFP_KERNEL);
	if (!pixels)
		return -ENOMEM;

	for (row = 0; row < EINK_PATCH_H; row++) {
		u8 grey = min_t(u8, 15, (row * 16) / EINK_PATCH_H);
		u8 panel_byte = eink_drm_grey_to_panel_byte(grey);

		memset(pixels + row * EINK_PATCH_W, panel_byte, EINK_PATCH_W);
	}

	dev_info(pdev->drm.dev,
		 "center patch %ux%u at (%u,%u) xfer_base=0x%08x (off=0)\n",
		 EINK_PATCH_W, EINK_PATCH_H, x, y, ITE8951_DEFAULT_IMAGE_BUF);

	ret = ite8951_load_pixels(link, 0, &load, pixels,
				  EINK_PATCH_W * EINK_PATCH_H);
	kvfree(pixels);
	return ret;
}

static int eink_drm_prepare_panel(struct eink_drm_device *pdev)
{
	int ret;

	if (pdev->usb_link.draw_mode_active)
		return 0;

	ret = ite8951_enter_draw_mode(&pdev->usb_link);
	if (ret)
		dev_err(pdev->drm.dev, "enter draw mode failed: %d\n", ret);

	return ret;
}

static void eink_drm_panel_recover(struct eink_drm_device *pdev)
{
	/*
	 * Drain stale USB data after a failed upload/blit. Do not clear
	 * draw_mode_active — that forced ite8951_enter_draw_mode() on every
	 * subsequent compositor commit, re-running draw-mode entry.
	 */
	ite8951_usb_prepare(&pdev->usb_link);
}

static void eink_pipe_update(struct drm_simple_display_pipe *pipe,
			     struct drm_plane_state *old_plane_state)
{
	struct eink_drm_device *pdev =
		container_of(pipe, struct eink_drm_device, pipe);
	struct drm_device *drm = &pdev->drm;
	struct drm_plane_state *plane_state = pipe->plane.state;
	struct drm_framebuffer *fb = plane_state->fb;
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(NULL);
	struct iosys_map map_data = IOSYS_MAP_INIT_VADDR(NULL);
	struct ite8951_rect dest;
	u32 blit_waveform;
	static bool patch_test_done;
	unsigned long now;
	int ret, idx;

	if (!fb || !drm->registered)
		return;

	if (!mutex_trylock(&pdev->update_lock))
		return;

	if (!drm_dev_enter(drm, &idx))
		goto unlock;

	ret = eink_drm_prepare_panel(pdev);
	if (ret)
		goto exit_drm;

	ret = ite8951_assert_draw_scenario(&pdev->usb_link);
	if (ret)
		dev_warn(drm->dev, "draw scenario reassert failed: %d\n", ret);

	if (panel_test_pattern) {
		if (patch_test_done)
			goto exit_drm;

		ret = eink_drm_push_center_patch(pdev, &dest);
		if (ret) {
			dev_err(drm->dev, "center patch upload failed: %d\n", ret);
			eink_drm_panel_recover(pdev);
			goto exit_drm;
		}

		blit_waveform = ITE8951_WAVEFORM_CURRENT;
		ret = ite8951_blit(&pdev->usb_link, 0, &dest, blit_waveform);
		if (ret) {
			dev_err(drm->dev, "center patch blit failed: %d\n", ret);
			eink_drm_panel_recover(pdev);
		} else {
			patch_test_done = true;
			dev_info(drm->dev,
				 "center patch blit done (waveform %u) — wait ~30s\n",
				 blit_waveform);
		}
		goto exit_drm;
	}

	ret = eink_drm_fb_wait_render(fb);
	if (ret) {
		dev_err(drm->dev, "fb fence wait failed: %d\n", ret);
		goto exit_drm;
	}

	now = jiffies;
	if (min_blit_interval_ms && pdev->panel_init_refresh_done &&
	    time_before(now,
			pdev->last_blit_jiffies +
			msecs_to_jiffies(min_blit_interval_ms)))
		goto exit_drm;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret) {
		dev_err(drm->dev, "fb cpu access failed: %d\n", ret);
		goto exit_drm;
	}

	ret = drm_gem_fb_vmap(fb, &map, &map_data);
	if (ret) {
		dev_err(drm->dev, "fb vmap failed: %d\n", ret);
		goto end_cpu;
	}

	ret = eink_drm_push_frame(pdev, fb, &map);
	drm_gem_fb_vunmap(fb, &map);
	if (ret) {
		dev_err(drm->dev, "frame upload failed: %d\n", ret);
		eink_drm_panel_recover(pdev);
		goto end_cpu;
	}

	dest.x = 0;
	dest.y = 0;
	dest.width = fb->width;
	dest.height = fb->height;

	blit_waveform = ITE8951_WAVEFORM_CURRENT;

	ret = ite8951_blit(&pdev->usb_link, 0, &dest, blit_waveform);
	if (ret) {
		dev_err(drm->dev, "full-frame blit failed: %d\n", ret);
		eink_drm_panel_recover(pdev);
	} else {
		pdev->panel_init_refresh_done = true;
		pdev->last_blit_jiffies = jiffies;
		ret = ite8951_assert_draw_scenario(&pdev->usb_link);
		if (ret)
			dev_warn(drm->dev, "post-blit draw assert failed: %d\n", ret);
		else
			ite8951_reapply_draw_input(&pdev->usb_link);
		dev_info(drm->dev, "frame blit %ux%u complete (waveform %u)\n",
			 fb->width, fb->height, blit_waveform);
	}

end_cpu:
	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
exit_drm:
	drm_dev_exit(idx);
unlock:
	mutex_unlock(&pdev->update_lock);
}

static const struct drm_simple_display_pipe_funcs eink_pipe_funcs = {
	.update = eink_pipe_update,
};

static int eink_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (!mode)
		return 0;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	mode->clock = 148500;
	mode->hdisplay = EINK_PANEL_WIDTH;
	mode->hsync_start = EINK_PANEL_WIDTH + 88;
	mode->hsync_end = EINK_PANEL_WIDTH + 88 + 44;
	mode->htotal = EINK_PANEL_WIDTH + 88 + 44 + 148;
	mode->vdisplay = EINK_PANEL_HEIGHT;
	mode->vsync_start = EINK_PANEL_HEIGHT + 4;
	mode->vsync_end = EINK_PANEL_HEIGHT + 4 + 5;
	mode->vtotal = EINK_PANEL_HEIGHT + 4 + 5 + 36;
	mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_connector_helper_funcs eink_connector_helper_funcs = {
	.get_modes = eink_connector_get_modes,
};

static void eink_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
	kfree(container_of(connector, struct eink_connector, base));
}

static const struct drm_connector_funcs eink_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.destroy = eink_connector_destroy,
};

static const struct drm_mode_config_funcs eink_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_FOPS(eink_drm_fops);

static struct drm_driver eink_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &eink_drm_fops,
	.name = EINK_DRM_DRIVER_NAME,
	.desc = "Lenovo YogaBook C930 E-Ink",
	.major = 1,
	.minor = 0,
	DRM_GEM_SHMEM_DRIVER_OPS,
};

struct eink_drm_device *eink_drm_device_create(struct usb_interface *intf)
{
	struct eink_drm_device *pdev;

	pdev = devm_drm_dev_alloc(&intf->dev, &eink_drm_driver,
				  struct eink_drm_device, drm);
	if (IS_ERR(pdev))
		return pdev;

	pdev->intf = intf;
	pdev->drm.dev_private = pdev;
	mutex_init(&pdev->update_lock);

	return pdev;
}

static int eink_connector_create(struct eink_drm_device *pdev)
{
	struct drm_device *drm = &pdev->drm;
	struct eink_connector *econn;
	struct drm_connector *connector;
	int ret;

	econn = kzalloc(sizeof(*econn), GFP_KERNEL);
	if (!econn)
		return -ENOMEM;

	connector = &econn->base;
	drm_connector_helper_add(connector, &eink_connector_helper_funcs);

	ret = drm_connector_init(drm, connector, &eink_connector_funcs,
				 DRM_MODE_CONNECTOR_USB);
	if (ret) {
		kfree(econn);
		return ret;
	}

	connector->status = connector_status_connected;
	connector->display_info.width_mm = 295;
	connector->display_info.height_mm = 165;

	ret = drm_connector_attach_encoder(connector, &pdev->pipe.encoder);
	if (ret) {
		drm_connector_cleanup(connector);
		kfree(econn);
		return ret;
	}

	pdev->connector = econn;
	return 0;
}

int eink_drm_kms_register(struct eink_drm_device *pdev)
{
	struct drm_device *drm = &pdev->drm;
	struct device *dev = &pdev->intf->dev;
	struct device *dma_dev;
	int ret;

	if (!drm_enable)
		return 0;

	if (drm_stage < 1 || drm_stage > 5)
		return -EINVAL;

	/* GUD sets this before drmm_mode_config_init(). */
	drm->mode_config.funcs = &eink_mode_config_funcs;

	eink_drm_step(dev, 1, "mode config init");

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = EINK_PANEL_WIDTH;
	drm->mode_config.max_width = EINK_PANEL_WIDTH;
	drm->mode_config.min_height = EINK_PANEL_HEIGHT;
	drm->mode_config.max_height = EINK_PANEL_HEIGHT;
	drm->mode_config.preferred_depth = 32;

	if (drm_stage < 2)
		goto done_early;

	eink_drm_step(dev, 2, "simple pipe init");

	ret = drm_simple_display_pipe_init(drm, &pdev->pipe, &eink_pipe_funcs,
					   eink_plane_formats,
					   ARRAY_SIZE(eink_plane_formats),
					   eink_plane_modifiers, NULL);
	if (ret)
		return ret;

	if (drm_stage < 3)
		goto done_early;

	eink_drm_step(dev, 3, "connector init + attach");

	ret = eink_connector_create(pdev);
	if (ret)
		return ret;

	if (drm_stage < 4)
		goto done_early;

	eink_drm_step(dev, 4, "mode config reset");

	drm_mode_config_reset(drm);

	if (drm_stage < 5)
		goto done_early;

	dma_dev = usb_intf_get_dma_device(pdev->intf);
	if (dma_dev) {
		drm_dev_set_dma_dev(drm, dma_dev);
		put_device(dma_dev);
	} else {
		dev_warn(dev, "buffer sharing not supported\n");
	}

	eink_drm_step(dev, 5, "drm_dev_register");

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	drm_kms_helper_poll_init(drm);

	dev_alert(dev, "DRM connector %s registered (%dx%d)\n",
		  pdev->connector->base.name,
		  EINK_PANEL_WIDTH, EINK_PANEL_HEIGHT);

	if (exit_keyboard_on_load) {
		ret = ite8951_enter_draw_mode(&pdev->usb_link);
		if (ret)
			dev_warn(dev, "exit keyboard on load failed: %d\n", ret);
		else
			dev_info(dev,
				 "firmware keyboard exited — raw touch HID available\n");
	}

	return 0;

done_early:
	dev_alert(dev, "DRM init complete through stage %d (cap=%d)\n",
		  drm_stage, drm_stage);
	return 0;
}

void eink_drm_kms_unregister(struct eink_drm_device *pdev)
{
	struct drm_device *drm;

	if (!pdev || !drm_enable)
		return;

	drm = &pdev->drm;
	if (!drm->registered)
		return;

	drm_kms_helper_poll_fini(drm);
	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}
