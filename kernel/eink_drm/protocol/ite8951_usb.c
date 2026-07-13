// SPDX-License-Identifier: GPL-2.0-only
/*
 * ITE8951 USB bulk transport for the YogaBook C930 E-Ink panel.
 *
 * Three-step bulk exchange per command:
 *   USBC control (OUT) -> data (IN) -> USBS status (IN)
 *
 * Uses one persistent bulk IN URB (reused across reads) and async bulk OUT.
 * Response and status may arrive in a single IN transfer on this hardware.
 */

#include <linux/dev_printk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>

#include "../eink_panel.h"
#include "ite8951.h"

#define ITE8951_DRAIN_TIMEOUT_MS	100

static size_t ite8951_in_urb_length(const struct ite8951_usb *link)
{
	return ITE8951_IN_BUFFER_SIZE;
}

static const u8 ite8951_usbc_header[] = {
	0x55, 0x53, 0x42, 0x43, 0x61, 0x89, 0x51, 0x89,
};

static const u8 ite8951_usbs_ok[] = {
	0x55, 0x53, 0x42, 0x53, 0x61, 0x89, 0x51, 0x89,
	0x00, 0x00, 0x00, 0x00, 0x00,
};

#define ITE8951_SCSI_CUSTOM_OPCODE	0xfe
#define ITE8951_CDB_PAYLOAD_SIZE	0x10

struct ite8951_ctrl_packet {
	u8 header[8];
	__le32 response_length;
	u8 direction_in;
	u8 reserved0;
	u8 cdb_length;
	u8 scsi_opcode;
	u8 scsi_lun;
	__be32 address;
	u8 ite_opcode;
	__be16 arg1;
	__be16 arg2;
	__be16 arg3;
	__be16 arg4;
	u8 padding;
} __packed;

struct ite8951_out_wait {
	struct completion done;
	int status;
};

struct ite8951_blit_payload {
	__be32 mem_addr;
	__be32 waveform;
	__be32 dest_x;
	__be32 dest_y;
	__be32 width;
	__be32 height;
} __packed;

struct ite8951_dpy_status {
	__le32 timestamp;
	__le16 engine_busy;
	__le16 reserved;
} __packed;

static void ite8951_in_urb_done(struct urb *urb)
{
	struct ite8951_usb *link = urb->context;

	link->in_status = urb->status;
	link->in_actual = urb->actual_length;
	complete(&link->in_done);
}

static void ite8951_out_urb_done(struct urb *urb)
{
	struct ite8951_out_wait *wait = urb->context;

	wait->status = urb->status;
	complete(&wait->done);
}

int ite8951_usb_setup(struct ite8951_usb *link)
{
	link->bulk_in_buffer = kmalloc(ITE8951_IN_BUFFER_SIZE, GFP_KERNEL);
	if (!link->bulk_in_buffer)
		return -ENOMEM;

	link->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!link->bulk_in_urb) {
		kfree(link->bulk_in_buffer);
		link->bulk_in_buffer = NULL;
		return -ENOMEM;
	}

	init_completion(&link->in_done);
	return 0;
}

void ite8951_usb_teardown(struct ite8951_usb *link)
{
	if (!link)
		return;

	if (link->bulk_in_urb) {
		usb_kill_urb(link->bulk_in_urb);
		usb_free_urb(link->bulk_in_urb);
		link->bulk_in_urb = NULL;
	}

	kfree(link->bulk_in_buffer);
	link->bulk_in_buffer = NULL;
}

static void ite8951_clear_in_halt(struct ite8951_usb *link)
{
	usb_clear_halt(link->usb_dev,
		       usb_rcvbulkpipe(link->usb_dev, link->bulk_in));
}

static void ite8951_clear_out_halt(struct ite8951_usb *link)
{
	usb_clear_halt(link->usb_dev,
		       usb_sndbulkpipe(link->usb_dev, link->bulk_out));
}

static int ite8951_submit_in(struct ite8951_usb *link, size_t length,
			     const char *step)
{
	int ret;

	usb_kill_urb(link->bulk_in_urb);
	reinit_completion(&link->in_done);
	link->in_status = -EIO;
	link->in_actual = 0;

	usb_fill_bulk_urb(link->bulk_in_urb, link->usb_dev,
			  usb_rcvbulkpipe(link->usb_dev, link->bulk_in),
			  link->bulk_in_buffer, length,
			  ite8951_in_urb_done, link);

	ret = usb_submit_urb(link->bulk_in_urb, GFP_KERNEL);
	if (ret)
		dev_err(&link->usb_dev->dev, "%s IN submit failed: %d\n",
			step, ret);

	return ret;
}

static int ite8951_wait_in(struct ite8951_usb *link, unsigned int timeout_ms,
			   const char *step, int *bytes_read)
{
	unsigned long remain;

	remain = wait_for_completion_timeout(&link->in_done,
					     msecs_to_jiffies(timeout_ms));
	if (!remain) {
		usb_kill_urb(link->bulk_in_urb);
		ite8951_clear_in_halt(link);
		dev_dbg(&link->usb_dev->dev, "%s: no data within %u ms\n",
			step, timeout_ms);
		return -ETIMEDOUT;
	}

	if (link->in_status) {
		if (link->in_status == -EOVERFLOW && link->in_actual > 0) {
			dev_warn(&link->usb_dev->dev,
				 "%s IN overflow (%d bytes), using partial data\n",
				 step, link->in_actual);
		} else {
			dev_err(&link->usb_dev->dev, "%s IN error: %d (%d bytes)\n",
				step, link->in_status, link->in_actual);
			return link->in_status;
		}
	}

	if (bytes_read)
		*bytes_read = link->in_actual;

	return 0;
}

static int ite8951_bulk_out(struct ite8951_usb *link, const void *buffer,
			    size_t length, const char *step)
{
	struct ite8951_out_wait wait;
	struct urb *urb;
	void *dma_buf;
	unsigned long remain;
	int ret;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	dma_buf = usb_alloc_coherent(link->usb_dev, length, GFP_KERNEL,
				     &urb->transfer_dma);
	if (!dma_buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	memcpy(dma_buf, buffer, length);

	init_completion(&wait.done);
	usb_fill_bulk_urb(urb, link->usb_dev,
			  usb_sndbulkpipe(link->usb_dev, link->bulk_out),
			  dma_buf, length, ite8951_out_urb_done, &wait);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		dev_err(&link->usb_dev->dev, "%s OUT submit failed: %d\n",
			step, ret);
		goto out_free;
	}

	remain = wait_for_completion_timeout(&wait.done,
			msecs_to_jiffies(ITE8951_USB_TIMEOUT_MS));
	if (!remain) {
		usb_kill_urb(urb);
		dev_err(&link->usb_dev->dev, "%s OUT timed out\n", step);
		ret = -ETIMEDOUT;
		goto out_free;
	}

	ret = wait.status;
	if (ret)
		dev_err(&link->usb_dev->dev, "%s OUT error: %d\n", step, ret);

out_free:
	usb_free_coherent(link->usb_dev, length, dma_buf, urb->transfer_dma);
	usb_free_urb(urb);
	return ret;
}

static bool ite8951_is_status_packet(const u8 *data, int length)
{
	if (length < ITE8951_STATUS_BYTES)
		return false;

	return memcmp(data, ite8951_usbs_ok, ITE8951_STATUS_BYTES) == 0;
}

static bool ite8951_find_status_packet(const u8 *data, int length, int *offset)
{
	int i;

	for (i = 0; i <= length - ITE8951_STATUS_BYTES; i++) {
		if (ite8951_is_status_packet(data + i, length - i)) {
			if (offset)
				*offset = i;
			return true;
		}
	}

	return false;
}

static int ite8951_read_status_only(struct ite8951_usb *link)
{
	int ret, bytes_read, offset;

	ret = ite8951_submit_in(link, ite8951_in_urb_length(link), "status");
	if (ret)
		return ret;

	ret = ite8951_wait_in(link, ITE8951_USB_TIMEOUT_MS, "status",
			      &bytes_read);
	if (ret)
		return ret;

	if (ite8951_find_status_packet(link->bulk_in_buffer, bytes_read, &offset))
		return 0;

	dev_err(&link->usb_dev->dev,
		"unexpected status packet (%d bytes)\n", bytes_read);
	print_hex_dump(KERN_ERR, "ite8951 status: ", DUMP_PREFIX_OFFSET,
		       16, 1, link->bulk_in_buffer, bytes_read, true);
	return -EPROTO;
}

static void ite8951_build_ctrl_packet(struct ite8951_ctrl_packet *packet,
				      bool expect_response, u32 response_length,
				      u8 ite_opcode, u32 address,
				      u16 arg1, u16 arg2, u16 arg3, u16 arg4)
{
	memcpy(packet->header, ite8951_usbc_header, sizeof(packet->header));
	packet->response_length = cpu_to_le32(response_length);
	packet->direction_in = expect_response ? 0x80 : 0x00;
	packet->cdb_length = ITE8951_CDB_PAYLOAD_SIZE;
	packet->scsi_opcode = ITE8951_SCSI_CUSTOM_OPCODE;
	packet->address = cpu_to_be32(address);
	packet->ite_opcode = ite_opcode;
	packet->arg1 = cpu_to_be16(arg1);
	packet->arg2 = cpu_to_be16(arg2);
	packet->arg3 = cpu_to_be16(arg3);
	packet->arg4 = cpu_to_be16(arg4);
}

static int ite8951_exchange(struct ite8951_usb *link,
			    struct ite8951_ctrl_packet *packet,
			    void *response_buf, size_t response_length)
{
	int ret, bytes_read;

	mutex_lock(&link->io_lock);

	if (response_buf && response_length) {
		ret = ite8951_submit_in(link, ite8951_in_urb_length(link),
					"response");
		if (ret)
			goto unlock;
	}

	ret = ite8951_bulk_out(link, packet, sizeof(*packet), "control");
	if (ret)
		goto unlock;

	if (!response_buf || !response_length) {
		ret = ite8951_read_status_only(link);
		goto unlock;
	}

	ret = ite8951_wait_in(link, ITE8951_USB_TIMEOUT_MS, "response",
			      &bytes_read);
	if (ret)
		goto unlock;

	if (bytes_read < response_length) {
		dev_err(&link->usb_dev->dev,
			"short response: got %d, expected %zu\n",
			bytes_read, response_length);
		ret = -EPROTO;
		goto unlock;
	}

	memcpy(response_buf, link->bulk_in_buffer, response_length);

	if (bytes_read >= response_length + ITE8951_STATUS_BYTES &&
	    ite8951_is_status_packet(link->bulk_in_buffer + response_length,
				     bytes_read - response_length)) {
		ret = 0;
		goto unlock;
	}

	if (ite8951_find_status_packet(link->bulk_in_buffer, bytes_read, NULL)) {
		ret = 0;
		goto unlock;
	}

	ret = ite8951_read_status_only(link);

unlock:
	mutex_unlock(&link->io_lock);
	return ret;
}

static int ite8951_exchange_out_payload(struct ite8951_usb *link,
					struct ite8951_ctrl_packet *packet,
					const void *payload, size_t payload_length,
					const char *step)
{
	int ret;

	mutex_lock(&link->io_lock);

	ret = ite8951_bulk_out(link, packet, sizeof(*packet), step);
	if (ret)
		goto unlock;

	if (payload && payload_length) {
		ret = ite8951_bulk_out(link, payload, payload_length, "payload");
		if (ret)
			goto unlock;
	}

	ret = ite8951_read_status_only(link);

unlock:
	mutex_unlock(&link->io_lock);
	return ret;
}

static void ite8951_store_system_info(struct ite8951_usb *link,
				      const struct ite8951_system_info *info)
{
	link->panel_width = info->panel_width;
	link->panel_height = info->panel_height;

	if (info->image_buf_base)
		link->image_buf_base = info->image_buf_base;
	else if (info->update_buf_base)
		link->image_buf_base = info->update_buf_base;
	else if (!link->image_buf_base)
		link->image_buf_base = ITE8951_DEFAULT_IMAGE_BUF;

	if (info->update_buf_base)
		link->update_buf_base = info->update_buf_base;
}

int ite8951_parse_system_info(const u8 *response, size_t length,
			      struct ite8951_system_info *info)
{
	if (!response || !info || length < 32)
		return -EINVAL;

	info->signature = le32_to_cpup((__le32 *)(response + 8));
	if (info->signature != ITE8951_GET_SYS_SIGNATURE)
		return -EPROTO;

	info->panel_width = le32_to_cpup((__le32 *)(response + 16));
	info->panel_height = le32_to_cpup((__le32 *)(response + 20));
	info->update_buf_base = le32_to_cpup((__le32 *)(response + 24));
	info->image_buf_base = le32_to_cpup((__le32 *)(response + 28));

	if (info->panel_width != EINK_PANEL_WIDTH ||
	    info->panel_height != EINK_PANEL_HEIGHT) {
		/* GET_SYS fields are logged for bring-up; xfer/blit use fixed
		 * panel geometry like the 2019 reference driver.
		 */
		info->panel_width = EINK_PANEL_WIDTH;
		info->panel_height = EINK_PANEL_HEIGHT;
	}

	return 0;
}

/* Reference eink.c always uses 0x00382f30 for xfer/blit regardless of GET_SYS. */
static u32 ite8951_xfer_image_base(void)
{
	return ITE8951_DEFAULT_IMAGE_BUF;
}

static u32 ite8951_rect_buf_offset(u32 buf_offset, const struct ite8951_rect *region)
{
	return buf_offset - region->x - EINK_PANEL_WIDTH * region->y;
}

static u32 ite8951_blit_mem_addr(u32 buf_offset, const struct ite8951_rect *dest)
{
	return ite8951_xfer_image_base() + ite8951_rect_buf_offset(buf_offset, dest);
}

int ite8951_usb_prepare(struct ite8951_usb *link)
{
	int ret, bytes_read, total_drained = 0;

	ite8951_clear_out_halt(link);
	ite8951_clear_in_halt(link);

	mutex_lock(&link->io_lock);

	for (;;) {
		ret = ite8951_submit_in(link, link->bulk_in_max, "drain");
		if (ret)
			goto unlock;

		ret = ite8951_wait_in(link, ITE8951_DRAIN_TIMEOUT_MS, "drain",
				      &bytes_read);
		if (ret == -ETIMEDOUT || bytes_read == 0) {
			ret = 0;
			break;
		}
		if (ret)
			goto unlock;

		total_drained += bytes_read;
		if (total_drained > ITE8951_IN_BUFFER_SIZE * 4)
			break;
	}

	if (total_drained)
		dev_dbg(&link->usb_dev->dev, "drained %d stale bytes\n",
			total_drained);

unlock:
	mutex_unlock(&link->io_lock);
	return ret;
}

static int ite8951_doorknock(struct ite8951_usb *link)
{
	struct ite8951_ctrl_packet packet;
	struct ite8951_system_info info;
	u8 system_info[ITE8951_GET_SYS_RESPONSE_BYTES];
	int ret;

	ite8951_build_ctrl_packet(&packet, true, ITE8951_GET_SYS_RESPONSE_BYTES,
				  ITE8951_USB_OP_GET_SYS,
				  ITE8951_GET_SYS_SIGNATURE,
				  1, 2, 0, 0);

	ret = ite8951_exchange(link, &packet, system_info, sizeof(system_info));
	if (ret)
		return ret;

	ret = ite8951_parse_system_info(system_info, sizeof(system_info), &info);
	if (!ret) {
		ite8951_store_system_info(link, &info);
		dev_dbg(&link->usb_dev->dev,
			"GET_SYS version=0x%08x reported image=0x%08x (xfer uses 0x%08x)\n",
			le32_to_cpup((__le32 *)(system_info + 12)),
			info.image_buf_base, ite8951_xfer_image_base());
	}

	dev_dbg(&link->usb_dev->dev, "GET_SYS signature: %*ph\n", 4,
		system_info + 8);
	return 0;
}

int ite8951_panel_init_sequence(struct ite8951_usb *link)
{
	int knock, ret;

	ret = ite8951_usb_prepare(link);
	if (ret)
		return ret;

	for (knock = 0; knock < ITE8951_DOORKNOCK_COUNT; knock++) {
		ret = ite8951_doorknock(link);
		if (ret) {
			dev_err(&link->usb_dev->dev,
				"doorknock %d/%d failed: %d\n",
				knock + 1, ITE8951_DOORKNOCK_COUNT, ret);
			return ret;
		}
	}

	dev_info(&link->usb_dev->dev,
		 "panel init complete (%d doorknocks), xfer_base=0x%08x "
		 "(GET_SYS image=0x%08x update=0x%08x)\n",
		 ITE8951_DOORKNOCK_COUNT, ite8951_xfer_image_base(),
		 link->image_buf_base, link->update_buf_base);
	return 0;
}

int ite8951_set_waveform(struct ite8951_usb *link, enum ite8951_waveform mode)
{
	return ite8951_set_waveform_u16(link, mode);
}

int ite8951_set_waveform_u16(struct ite8951_usb *link, u16 mode)
{
	struct ite8951_ctrl_packet packet;
	u8 response;
	int ret;

	if (!link)
		return -EINVAL;

	ite8951_build_ctrl_packet(&packet, true, 1, ITE8951_USB_OP_SET_WAVEFORM,
				  0, mode, 0, 0, 0);

	ret = ite8951_exchange(link, &packet, &response, sizeof(response));
	if (ret)
		dev_err(&link->usb_dev->dev, "SET_WAVEFORM(%u) failed: %d\n",
			mode, ret);

	return ret;
}

int ite8951_query_display_status(struct ite8951_usb *link, u16 *engine_busy)
{
	struct ite8951_ctrl_packet packet;
	u8 response[ITE8951_DPY_STATUS_BYTES];
	struct ite8951_dpy_status *status;
	int ret;

	if (!link || !engine_busy)
		return -EINVAL;

	ite8951_build_ctrl_packet(&packet, true, ITE8951_DPY_STATUS_BYTES,
				  ITE8951_USB_OP_GET_DPY_STATUS,
				  0, 0, 0, 0, 0);

	ret = ite8951_exchange(link, &packet, response, sizeof(response));
	if (ret)
		return ret;

	status = (struct ite8951_dpy_status *)response;
	*engine_busy = le16_to_cpu(status->engine_busy);
	return 0;
}

int ite8951_wait_display_ready(struct ite8951_usb *link, unsigned int timeout_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);
	u16 engine_busy;
	int ret;

	if (!link)
		return -EINVAL;

	for (;;) {
		ret = ite8951_query_display_status(link, &engine_busy);
		if (ret)
			return ret;

		if (!engine_busy)
			return 0;

		if (time_after(jiffies, deadline))
			return -ETIMEDOUT;

		msleep(ITE8951_PANEL_READY_POLL_MS);
	}
}

static int ite8951_read_reg_u32(struct ite8951_usb *link, u32 reg, u32 *value)
{
	struct ite8951_ctrl_packet packet;
	u8 response[4];
	int ret;

	ite8951_build_ctrl_packet(&packet, true, sizeof(response),
				  ITE8951_USB_OP_READ_REG, reg,
				  sizeof(response), 0, 0, 0);

	ret = ite8951_exchange(link, &packet, response, sizeof(response));
	if (ret)
		return ret;

	*value = be32_to_cpup((__be32 *)response);
	return 0;
}

static int ite8951_write_reg_u32(struct ite8951_usb *link, u32 reg, u32 value)
{
	struct ite8951_ctrl_packet packet;
	__be32 payload = cpu_to_be32(value);

	ite8951_build_ctrl_packet(&packet, false, sizeof(payload),
				  ITE8951_USB_OP_WRITE_REG, reg,
				  sizeof(payload), 0, 0, 0);

	return ite8951_exchange_out_payload(link, &packet, &payload,
					    sizeof(payload), "write_reg");
}

#define ITE8951_TP_AREA_TOUCH_PEN	0x03
#define ITE8951_TP_AREA_NO_REPORT	0x00
#define ITE8951_TP_AREA_SLOT_COUNT	8

struct ite8951_handwr_region {
	__le16 x;
	__le16 y;
	__le16 w;
	__le16 h;
} __packed;

/* Matches Windows SET_TP_AREA (EinkIteAPI.h, #pragma pack(4)): 4×ulong + index + flag + pad. */
struct ite8951_tp_area {
	__le32 x;
	__le32 y;
	__le32 w;
	__le32 h;
	u8 index;
	u8 flag;
	u8 pad[2];
} __packed;

static_assert(sizeof(struct ite8951_tp_area) == 20);

struct ite8951_dynamic_setting {
	u8 is_set;
	u8 pad1[3];
	u32 enable_hand_in_hw_draw;
	u32 enable_axis_transform_in_pen_mouse;
	u8 uc_bypass_flag;
	u8 uc_pen_mouse_flag;
	u8 pad2[2];
	u32 enable_pressure_in_hw_draw;
	u32 enable_upper_button_draw_in_hw_draw;
} __packed;

static bool touch_userspace_config = true;
module_param(touch_userspace_config, bool, 0644);
MODULE_PARM_DESC(touch_userspace_config,
		 "Route touch to custom HID after draw (0xAC/0xB3/0xAF); boot keyboard HID needs 0xA6 leave-KB (scenario<<24)");

static bool reference_draw_mode;
module_param(reference_draw_mode, bool, 0644);
MODULE_PARM_DESC(reference_draw_mode,
		 "Use 2019 reference enable_draw DWORD sequence for display latch");

static u8 input_scenario = ITE8951_SCENARIO_PEN_MOUSE;
module_param(input_scenario, byte, 0644);
MODULE_PARM_DESC(input_scenario,
		 "Input mode after draw (0=owner-draw, 1=keyboard, 3=pen-mouse leave-KB via addr<<24)");

static bool keyboard_exit_before_draw;
module_param(keyboard_exit_before_draw, bool, 0644);
MODULE_PARM_DESC(keyboard_exit_before_draw,
		 "Reference draw only: run enable_kb-style 0x00040000 transition before scenarios (usually wrong for draw entry)");

static bool keep_firmware_keyboard_input;
module_param(keep_firmware_keyboard_input, bool, 0644);
MODULE_PARM_DESC(keep_firmware_keyboard_input,
		 "Leave firmware keyboard touch zones active (ghostty typing trial)");

static bool draw_scenario_preamble;
module_param(draw_scenario_preamble, bool, 0644);
MODULE_PARM_DESC(draw_scenario_preamble,
		 "Send 0x01010000 before draw opcode (re-arms keyboard; usually leave off)");

static bool scenario_verify_strict;
module_param(scenario_verify_strict, bool, 0644);
MODULE_PARM_DESC(scenario_verify_strict,
		 "Fail draw entry if still in firmware keyboard after leave-KB (GET byte1==1)");

static bool reassert_draw_scenario = true;
module_param(reassert_draw_scenario, bool, 0644);
MODULE_PARM_DESC(reassert_draw_scenario,
		 "Re-send leave-KB if GET still reports keyboard around each frame");

static bool reassert_draw_input = true;
module_param(reassert_draw_input, bool, 0644);
MODULE_PARM_DESC(reassert_draw_input,
		 "Re-send 0xAC/0xB3/0xAF input routing around each compositor frame");

static int ite8951_set_handwriting_region(struct ite8951_usb *link,
					  u16 x, u16 y, u16 w, u16 h)
{
	struct ite8951_ctrl_packet packet;
	struct ite8951_handwr_region payload = {
		.x = cpu_to_le16(x),
		.y = cpu_to_le16(y),
		.w = cpu_to_le16(w),
		.h = cpu_to_le16(h),
	};

	ite8951_build_ctrl_packet(&packet, false, sizeof(payload),
				  ITE8951_USB_OP_SET_HANDWR_REGION, 0,
				  sizeof(payload), 0, 0, 0);

	return ite8951_exchange_out_payload(link, &packet, &payload,
					    sizeof(payload), "handwr_region");
}

static int ite8951_set_tp_area(struct ite8951_usb *link,
			       u32 x, u32 y, u32 w, u32 h,
			       u8 index, u8 flag)
{
	struct ite8951_ctrl_packet packet;
	struct ite8951_tp_area payload = {
		.x = cpu_to_le32(x),
		.y = cpu_to_le32(y),
		.w = cpu_to_le32(w),
		.h = cpu_to_le32(h),
		.index = index,
		.flag = flag,
	};

	ite8951_build_ctrl_packet(&packet, false, sizeof(payload),
				  ITE8951_USB_OP_SET_TP_AREA, 0,
				  sizeof(payload), 0, 0, 0);

	return ite8951_exchange_out_payload(link, &packet, &payload,
					    sizeof(payload), "tp_area");
}

static int ite8951_set_dynamic_bool_values(struct ite8951_usb *link,
					 const struct ite8951_dynamic_setting *values)
{
	struct ite8951_ctrl_packet packet;

	ite8951_build_ctrl_packet(&packet, false, sizeof(*values),
				  ITE8951_USB_OP_DYNAMICSETTING, 0,
				  sizeof(*values), 0, 0, 0);

	return ite8951_exchange_out_payload(link, &packet, values,
					    sizeof(*values), "dynamic_setting");
}

static int ite8951_reset_tp_slot(struct ite8951_usb *link, u8 index)
{
	return ite8951_set_tp_area(link, 0, 0, 0, 0, index,
				   ITE8951_TP_AREA_NO_REPORT);
}

static int ite8951_clear_all_tp_areas(struct ite8951_usb *link)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ITE8951_TP_AREA_SLOT_COUNT; i++) {
		ret = ite8951_reset_tp_slot(link, i);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Firmware keyboard layout (itetcon.h): touch-only zones in native coords.
 * Clear each slot explicitly — boot keyboard routing stays active until these
 * are NO_REPORT and owner-draw routing is configured.
 */
static int ite8951_clear_keyboard_tp_zones(struct ite8951_usb *link)
{
	static const struct {
		u32 x, y, w, h;
		u8 index;
	} kb_zones[] = {
		{ 0, 0, 7680, 300, 0 },
		{ 0, 300, 7680, 3296, 1 },
		{ 3308, 3840, 404, 160, 2 },
		{ 3208, 4000, 604, 160, 3 },
		{ 3108, 4160, 804, 160, 4 },
	};
	unsigned int i;
	int ret;

	ret = ite8951_reset_tp_slot(link, 0);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(kb_zones); i++) {
		ret = ite8951_set_tp_area(link, kb_zones[i].x, kb_zones[i].y,
					  kb_zones[i].w, kb_zones[i].h,
					  kb_zones[i].index,
					  ITE8951_TP_AREA_NO_REPORT);
		if (ret)
			return ret;
	}

	return ite8951_clear_all_tp_areas(link);
}

/*
 * Owner-draw input path: disable HW ink and route touch to custom HID (0x90)
 * instead of firmware keyboard zones. Matches Windows PreNextButton restore +
 * ReaderBaseFrame OnNotifyResetTpArea.
 */
static int ite8951_detach_keyboard_input(struct ite8951_usb *link)
{
	struct ite8951_dynamic_setting bools = {
		.is_set = 1,
		.enable_hand_in_hw_draw = 0,
		.enable_axis_transform_in_pen_mouse = 0,
		.uc_bypass_flag = 3,
		.uc_pen_mouse_flag = input_scenario == ITE8951_SCENARIO_PEN_MOUSE ? 3 : 0,
		.enable_pressure_in_hw_draw = 0,
		.enable_upper_button_draw_in_hw_draw = 0,
	};
	int ret;

	ret = ite8951_set_handwriting_region(link, 0, 0, 0, 0);
	if (ret) {
		dev_warn(&link->usb_dev->dev,
			 "disable HW handwriting region failed: %d\n", ret);
		return ret;
	}

	ret = ite8951_set_dynamic_bool_values(link, &bools);
	if (ret) {
		dev_warn(&link->usb_dev->dev,
			 "draw input routing (0xB3) failed: %d\n", ret);
		return ret;
	}

	ret = ite8951_clear_keyboard_tp_zones(link);
	if (ret) {
		dev_warn(&link->usb_dev->dev, "keyboard tp zone clear failed: %d\n", ret);
		return ret;
	}

	/* Windows: EiSetTpArea flag=0 then TOUCH_PEN on full panel (1920×1080). */
	ret = ite8951_set_tp_area(link, 0, 0, EINK_PANEL_WIDTH, EINK_PANEL_HEIGHT,
				  0, ITE8951_TP_AREA_NO_REPORT);
	if (ret) {
		dev_warn(&link->usb_dev->dev,
			 "tp area priming (panel) failed: %d\n", ret);
		return ret;
	}

	ret = ite8951_set_tp_area(link, 0, 0, EINK_PANEL_WIDTH, EINK_PANEL_HEIGHT,
				  0, ITE8951_TP_AREA_TOUCH_PEN);
	if (ret) {
		dev_warn(&link->usb_dev->dev,
			 "tp area touch+pen (panel) failed: %d\n", ret);
		return ret;
	}

	ret = ite8951_set_tp_area(link, 0, 0,
				  EINK_TOUCH_NATIVE_WIDTH, EINK_TOUCH_NATIVE_HEIGHT,
				  0, ITE8951_TP_AREA_NO_REPORT);
	if (ret) {
		dev_warn(&link->usb_dev->dev,
			 "tp area priming (native) failed: %d\n", ret);
		return ret;
	}

	ret = ite8951_set_tp_area(link, 0, 0,
				  EINK_TOUCH_NATIVE_WIDTH, EINK_TOUCH_NATIVE_HEIGHT,
				  0, ITE8951_TP_AREA_TOUCH_PEN);
	if (ret) {
		dev_warn(&link->usb_dev->dev,
			 "tp area touch+pen (native) failed: %d\n", ret);
		return ret;
	}

	dev_dbg(&link->usb_dev->dev,
		"touch routing configured (tp slots, custom HID)\n");

	return 0;
}

int ite8951_reapply_draw_input(struct ite8951_usb *link)
{
	if (!link || !link->draw_mode_active || !reassert_draw_input)
		return 0;
	if (keep_firmware_keyboard_input || !touch_userspace_config)
		return 0;

	return ite8951_detach_keyboard_input(link);
}

static int ite8951_scenario_cmd(struct ite8951_usb *link, u32 address, u16 arg1,
				u8 *scenario_out)
{
	struct ite8951_ctrl_packet packet;
	u8 response[4];
	int ret;

	ite8951_build_ctrl_packet(&packet, true, sizeof(response),
				  ITE8951_USB_OP_SCENARIO, address,
				  arg1, 0, 0, 0);

	ret = ite8951_exchange(link, &packet, response, sizeof(response));
	if (ret) {
		dev_err(&link->usb_dev->dev,
			"SCENARIO(addr=0x%x arg1=0x%x) failed: %d\n",
			address, arg1, ret);
		return ret;
	}

	if (scenario_out)
		*scenario_out = response[1];

	dev_dbg(&link->usb_dev->dev,
		"SCENARIO(addr=0x%x arg1=0x%x) rsp %*ph current=%u\n",
		address, arg1, (int)sizeof(response), response, response[1]);
	return 0;
}

static int ite8951_get_tcon_scenario(struct ite8951_usb *link, u8 *scenario)
{
	return ite8951_scenario_cmd(link, 0, 0, scenario);
}

/*
 * After leave-KB (address 0x03000000), firmware GET reports 0, not 3.
 * Non-keyboard requests succeed when GET is no longer keyboard.
 */
static bool ite8951_scenario_satisfied(u8 want, u8 reported)
{
	if (want == ITE8951_SCENARIO_KEYBOARD)
		return reported == ITE8951_SCENARIO_KEYBOARD;

	return reported != ITE8951_SCENARIO_KEYBOARD;
}

/* SET address for leave-KB / mode change (want==0 cannot use <<24 — that is GET). */
static u32 ite8951_scenario_set_address(u8 want)
{
	if (want == ITE8951_SCENARIO_NORMAL)
		return ITE8951_SCENARIO_LEAVE_KB_ADDR;

	return ITE8951_SCENARIO_ADDR(want);
}

/* 2019 reference firmware opcodes in the address field (not coordinator bytes). */
static int ite8951_firmware_scenario_opcode(struct ite8951_usb *link, u32 opcode)
{
	u8 reported = 0xff;
	int ret;

	ret = ite8951_scenario_cmd(link, opcode, 0, &reported);
	if (ret)
		return ret;

	dev_dbg(&link->usb_dev->dev,
		"firmware SCENARIO opcode 0x%08x (rsp byte1=%u)\n",
		opcode, reported);
	return 0;
}

static int ite8951_log_tcon_scenario(struct ite8951_usb *link, const char *when)
{
	u8 reported = 0xff;
	int ret;

	ret = ite8951_get_tcon_scenario(link, &reported);
	if (ret)
		return ret;

	dev_info(&link->usb_dev->dev, "TCON scenario %u at %s\n", reported, when);
	return reported;
}

/*
 * Windows CDB-inline 0xB3 (no OUT payload) from C-penmouse.pcap — optional
 * before leave-KB; may fail harmlessly.
 */
static int ite8951_dynamic_cdb_pen_mouse(struct ite8951_usb *link)
{
	struct ite8951_ctrl_packet packet;

	ite8951_build_ctrl_packet(&packet, false, 0,
				  ITE8951_USB_OP_DYNAMICSETTING, 0,
				  0x0100, 0x0103, 0x0301, 0);

	return ite8951_exchange(link, &packet, NULL, 0);
}

/* Prefer address = scenario << 24; return 0 if GET left firmware keyboard. */
static int ite8951_try_set_coordinator_scenario(struct ite8951_usb *link, u8 want)
{
	u8 reported = 0xff;
	u32 set_addr;
	int ret;

	set_addr = ite8951_scenario_set_address(want);

	/*
	 * Encoding A — Windows-validated: scenario in address high byte.
	 * Pen-mouse leave-KB = 0x03000000; GET then reports 0 (not 3).
	 */
	ret = ite8951_scenario_cmd(link, set_addr, 0, NULL);
	if (ret)
		return ret;
	ret = ite8951_get_tcon_scenario(link, &reported);
	if (!ret && ite8951_scenario_satisfied(want, reported)) {
		dev_info(&link->usb_dev->dev,
			 "scenario want=%u latched via addr=0x%08x (GET=%u)\n",
			 want, set_addr, reported);
		return 0;
	}
	dev_dbg(&link->usb_dev->dev,
		"addr=0x%08x left GET=%u (want=%u), trying fallbacks\n",
		set_addr, reported, want);

	/* Encoding B — legacy low DWORD (pre-Windows capture guess). */
	if (want != ITE8951_SCENARIO_NORMAL || set_addr != want) {
		ret = ite8951_scenario_cmd(link, want, 0, NULL);
		if (ret)
			return ret;
		ret = ite8951_get_tcon_scenario(link, &reported);
		if (!ret && ite8951_scenario_satisfied(want, reported)) {
			dev_info(&link->usb_dev->dev,
				 "scenario want=%u latched via low DWORD (GET=%u)\n",
				 want, reported);
			return 0;
		}
	}

	/* Encoding C — TconSetScenario-style BYTE in arg1. */
	if (want != ITE8951_SCENARIO_NORMAL) {
		ret = ite8951_scenario_cmd(link, 0, want, NULL);
		if (ret)
			return ret;
		ret = ite8951_get_tcon_scenario(link, &reported);
		if (!ret && ite8951_scenario_satisfied(want, reported)) {
			dev_info(&link->usb_dev->dev,
				 "scenario want=%u latched via arg1 (GET=%u)\n",
				 want, reported);
			return 0;
		}
		dev_dbg(&link->usb_dev->dev,
			"arg1=%u left GET=%u\n", want, reported);
	}

	/* Encoding D — 2019 firmware opcodes (owner-draw path). */
	if (want == ITE8951_SCENARIO_NORMAL) {
		if (draw_scenario_preamble) {
			ret = ite8951_firmware_scenario_opcode(link,
							       ITE8951_SCENARIO_REF_KB_ARM);
			if (ret)
				return ret;
		}
		ret = ite8951_firmware_scenario_opcode(link,
						       ITE8951_SCENARIO_NORMAL);
		if (ret)
			return ret;
		ret = ite8951_get_tcon_scenario(link, &reported);
		if (!ret && ite8951_scenario_satisfied(want, reported)) {
			dev_info(&link->usb_dev->dev,
				 "scenario 0 latched via 2019 opcode pair (GET=%u)\n",
				 reported);
			return 0;
		}
	}

	return ite8951_scenario_satisfied(want, reported) ? 0 : -EAGAIN;
}

static int ite8951_set_coordinator_scenario(struct ite8951_usb *link, u8 scenario)
{
	int ret;

	ret = ite8951_try_set_coordinator_scenario(link, scenario);
	return ret == -EAGAIN ? 0 : ret;
}

/*
 * Leave firmware keyboard for compositor + touchpad. GET (addr=0) returns the
 * live scenario in response byte 1; after leave-KB that is typically 0.
 */
static int ite8951_request_input_scenario(struct ite8951_usb *link)
{
	u8 reported = 0xff;
	u8 want = input_scenario;
	int ret;

	if (want != ITE8951_SCENARIO_KEYBOARD) {
		ret = ite8951_dynamic_cdb_pen_mouse(link);
		if (ret)
			dev_warn(&link->usb_dev->dev,
				 "input transition: 0xB3 CDB failed: %d\n", ret);
	}

	ret = ite8951_set_waveform_u16(link, 0x200);
	if (ret)
		dev_warn(&link->usb_dev->dev,
			 "input transition: waveform 0x200 failed: %d\n", ret);

	dev_info(&link->usb_dev->dev,
		 "requesting coordinator input scenario %u (SET addr=0x%08x)\n",
		 want, ite8951_scenario_set_address(want));

	ret = ite8951_set_coordinator_scenario(link, want);
	if (ret)
		return ret;

	reported = ite8951_log_tcon_scenario(link, "after coordinator set");
	if (reported < 0)
		return reported;

	if (ite8951_scenario_satisfied(want, reported)) {
		if (want != ITE8951_SCENARIO_KEYBOARD &&
		    reported != want)
			dev_info(&link->usb_dev->dev,
				 "left firmware keyboard (GET=%u, requested %u)\n",
				 reported, want);
		return 0;
	}

	dev_warn(&link->usb_dev->dev,
		 "firmware still reports keyboard scenario %u (wanted %u) — continuing with display_cfg + touch routing\n",
		 reported, want);
	if (scenario_verify_strict)
		return -EIO;

	return 0;
}

static int ite8951_exit_keyboard_mode(struct ite8951_usb *link)
{
	u32 value;
	unsigned int poll;
	int ret;

	ret = ite8951_set_waveform_u16(link, 0x200);
	if (ret)
		return ret;

	ret = ite8951_firmware_scenario_opcode(link, ITE8951_SCENARIO_REF_KB_EXIT);
	if (ret)
		return ret;

	for (poll = 0; poll < 20; poll++) {
		ret = ite8951_read_reg_u32(link, ITE8951_REG_PANEL_MODE, &value);
		if (ret)
			return ret;

		if (value == ITE8951_REG_PANEL_MODE_READY)
			break;

		msleep(ITE8951_PANEL_READY_POLL_MS);
	}

	ret = ite8951_firmware_scenario_opcode(link, ITE8951_SCENARIO_REF_KB_EXIT);
	if (ret)
		return ret;

	dev_dbg(&link->usb_dev->dev, "keyboard mode exited (panel mode 0x%08x)\n",
		value);
	dev_info(&link->usb_dev->dev, "firmware keyboard scenario exit complete\n");
	return 0;
}

static int ite8951_latch_display_draw(struct ite8951_usb *link,
				      u32 *panel_mode_out, u32 *display_cfg_out)
{
	u32 panel_mode, display_cfg;
	int ret;

	ret = ite8951_read_reg_u32(link, ITE8951_REG_PANEL_MODE, &panel_mode);
	if (ret) {
		dev_err(&link->usb_dev->dev,
			"draw mode: read panel mode failed: %d\n", ret);
		return ret;
	}

	ret = ite8951_read_reg_u32(link, ITE8951_REG_DISPLAY_CFG, &display_cfg);
	if (ret) {
		dev_err(&link->usb_dev->dev,
			"draw mode: read display cfg failed: %d\n", ret);
		return ret;
	}

	ret = ite8951_write_reg_u32(link, ITE8951_REG_DISPLAY_CFG, 0x002e0000);
	if (ret) {
		dev_err(&link->usb_dev->dev,
			"draw mode: write display cfg failed: %d\n", ret);
		return ret;
	}

	if (panel_mode_out)
		*panel_mode_out = panel_mode;
	if (display_cfg_out)
		*display_cfg_out = display_cfg;

	return 0;
}

static int ite8951_apply_draw_input(struct ite8951_usb *link)
{
	int ret;

	if (keep_firmware_keyboard_input) {
		dev_info(&link->usb_dev->dev,
			 "firmware keyboard input left active (keep_firmware_keyboard_input=1)\n");
		return 0;
	}

	if (!touch_userspace_config) {
		dev_warn(&link->usb_dev->dev,
			 "touch_userspace_config=0 — touch routing not reconfigured\n");
		return 0;
	}

	ret = ite8951_detach_keyboard_input(link);
	if (ret) {
		dev_warn(&link->usb_dev->dev,
			 "touch routing setup incomplete: %d\n", ret);
		return ret;
	}

	dev_info(&link->usb_dev->dev,
		 "touch routed to custom HID (0xAC/0xB3/0xAF)\n");
	return 0;
}

static int ite8951_enter_draw_mode_reference(struct ite8951_usb *link)
{
	u32 panel_mode, display_cfg;
	int ret;

	dev_info(&link->usb_dev->dev,
		 "entering draw mode (2019 reference enable_draw sequence)\n");

	if (keyboard_exit_before_draw) {
		ret = ite8951_exit_keyboard_mode(link);
		if (ret) {
			dev_err(&link->usb_dev->dev,
				"draw mode: keyboard exit failed: %d\n", ret);
			return ret;
		}
	}

	if (draw_scenario_preamble) {
		ret = ite8951_firmware_scenario_opcode(link,
						       ITE8951_SCENARIO_REF_KB_ARM);
		if (ret) {
			dev_err(&link->usb_dev->dev,
				"draw mode: SCENARIO 0x%08x failed: %d\n",
				ITE8951_SCENARIO_REF_KB_ARM, ret);
			return ret;
		}
	}

	ret = ite8951_set_coordinator_scenario(link, input_scenario);
	if (ret) {
		dev_err(&link->usb_dev->dev,
			"draw mode: coordinator scenario %u failed: %d\n",
			input_scenario, ret);
		return ret;
	}

	ite8951_log_tcon_scenario(link, "reference path");

	ret = ite8951_latch_display_draw(link, &panel_mode, &display_cfg);
	if (ret)
		return ret;

	dev_info(&link->usb_dev->dev,
		 "display draw latched (panel mode 0x%08x, display_cfg was 0x%08x)\n",
		 panel_mode, display_cfg);
	return 0;
}

static int ite8951_enter_draw_mode_coordinator(struct ite8951_usb *link)
{
	u32 panel_mode, display_cfg;
	u8 reported = 0xff;
	int ret;

	dev_info(&link->usb_dev->dev,
		 "entering draw mode (input_scenario=%u)\n", input_scenario);

	ret = ite8951_request_input_scenario(link);
	if (ret)
		return ret;

	ret = ite8951_latch_display_draw(link, &panel_mode, &display_cfg);
	if (ret)
		return ret;

	reported = ite8951_log_tcon_scenario(link, "after display_cfg");
	if (reported >= 0 &&
	    !ite8951_scenario_satisfied(input_scenario, reported)) {
		dev_warn(&link->usb_dev->dev,
			 "still keyboard (GET=%u) after display_cfg, retrying leave-KB (want=%u)\n",
			 reported, input_scenario);
		ite8951_try_set_coordinator_scenario(link, input_scenario);
		ite8951_log_tcon_scenario(link, "after post-display retry");
	}

	dev_info(&link->usb_dev->dev,
		 "draw latched (panel mode 0x%08x, display_cfg was 0x%08x)\n",
		 panel_mode, display_cfg);
	return 0;
}

int ite8951_enter_draw_mode(struct ite8951_usb *link)
{
	int ret;

	if (!link)
		return -EINVAL;

	if (link->draw_mode_active)
		return 0;

	ret = ite8951_usb_prepare(link);
	if (ret)
		return ret;

	ret = ite8951_wait_display_ready(link, ITE8951_USB_TIMEOUT_MS);
	if (ret) {
		dev_err(&link->usb_dev->dev,
			"draw mode: display not ready: %d\n", ret);
		return ret;
	}

	if (reference_draw_mode)
		ret = ite8951_enter_draw_mode_reference(link);
	else
		ret = ite8951_enter_draw_mode_coordinator(link);
	if (ret)
		return ret;

	ret = ite8951_apply_draw_input(link);
	if (ret)
		dev_warn(&link->usb_dev->dev,
			 "draw mode active but touch routing incomplete: %d\n", ret);

	link->draw_mode_active = true;
	return 0;
}

int ite8951_assert_draw_scenario(struct ite8951_usb *link)
{
	u8 reported = 0xff;
	int ret;

	if (!link || !link->draw_mode_active || !reassert_draw_scenario)
		return 0;

	ret = ite8951_get_tcon_scenario(link, &reported);
	if (ret)
		return ret;

	if (!ite8951_scenario_satisfied(input_scenario, reported)) {
		dev_dbg(&link->usb_dev->dev,
			"firmware still keyboard (GET=%u), reasserting leave-KB want=%u\n",
			reported, input_scenario);
		ret = ite8951_set_coordinator_scenario(link, input_scenario);
		if (ret)
			return ret;
	}

	ret = ite8951_write_reg_u32(link, ITE8951_REG_DISPLAY_CFG, 0x002e0000);
	if (ret)
		return ret;

	return ite8951_reapply_draw_input(link);
}

int ite8951_load_pixels(struct ite8951_usb *link, u32 buf_offset,
			const struct ite8951_rect *region,
			const u8 *pixels, size_t length)
{
	struct ite8951_ctrl_packet packet;
	size_t expected;
	u32 load_addr;

	if (!link || !region || !pixels)
		return -EINVAL;

	expected = region->width * region->height;
	if (!region->width || !region->height || length < expected)
		return -EINVAL;

	if (expected > ITE8951_MAX_XFER_BYTES) {
		dev_err(&link->usb_dev->dev,
			"load_pixels %ux%u exceeds %u byte limit\n",
			region->width, region->height, ITE8951_MAX_XFER_BYTES);
		return -E2BIG;
	}

	load_addr = ite8951_xfer_image_base() + buf_offset;

	ite8951_build_ctrl_packet(&packet, false, expected,
				  ITE8951_USB_OP_LD_IMG_AREA2,
				  load_addr, 0, 0,
				  region->width, region->height);

	return ite8951_exchange_out_payload(link, &packet, pixels, expected,
					    "load_pixels");
}

int ite8951_blit(struct ite8951_usb *link, u32 buf_offset,
		 const struct ite8951_rect *dest, u32 waveform)
{
	struct ite8951_ctrl_packet packet;
	struct ite8951_blit_payload payload;
	int ret;

	if (!link || !dest)
		return -EINVAL;

	if (!dest->width || !dest->height)
		return -EINVAL;

	ret = ite8951_wait_display_ready(link, ITE8951_USB_TIMEOUT_MS);
	if (ret)
		return ret;

	payload.mem_addr = cpu_to_be32(ite8951_blit_mem_addr(buf_offset, dest));
	payload.waveform = cpu_to_be32(waveform);

	dev_info(&link->usb_dev->dev,
		 "blit mem_addr=0x%08x wf=%u at (%u,%u) %ux%u\n",
		 ite8951_blit_mem_addr(buf_offset, dest), waveform,
		 dest->x, dest->y, dest->width, dest->height);
	payload.dest_x = cpu_to_be32(dest->x);
	payload.dest_y = cpu_to_be32(dest->y);
	payload.width = cpu_to_be32(dest->width);
	payload.height = cpu_to_be32(dest->height);

	ite8951_build_ctrl_packet(&packet, false, sizeof(payload),
				  ITE8951_USB_OP_DPY_AREA,
				  0, 0, 0, 0, 0);

	ret = ite8951_exchange_out_payload(link, &packet, &payload,
					   sizeof(payload), "blit");
	if (ret)
		return ret;

	return ite8951_wait_display_ready(link, ITE8951_USB_TIMEOUT_MS);
}
