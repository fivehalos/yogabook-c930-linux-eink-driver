/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ITE8951 USB protocol definitions for the YogaBook C930 E-Ink panel.
 */

#ifndef ITE8951_H
#define ITE8951_H

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/types.h>

struct urb;
struct usb_device;

#define ITE8951_USB_OP_GET_SYS		0x80
#define ITE8951_USB_OP_READ_REG		0x83
#define ITE8951_USB_OP_WRITE_REG	0x84
#define ITE8951_USB_OP_DPY_AREA		0x94
#define ITE8951_USB_OP_SCENARIO		0xA6
#define ITE8951_USB_OP_LD_IMG_AREA2	0xA8
#define ITE8951_USB_OP_SET_WAVEFORM	0xA9
#define ITE8951_USB_OP_SET_HANDWR_REGION	0xAC
#define ITE8951_USB_OP_SET_TP_AREA	0xAF
#define ITE8951_USB_OP_DYNAMICSETTING	0xB3
#define ITE8951_USB_OP_GET_DPY_STATUS	0xB1

#define ITE8951_GET_SYS_SIGNATURE	0x38393531u
#define ITE8951_GET_SYS_RESPONSE_BYTES	112
#define ITE8951_DOORKNOCK_COUNT		4
#define ITE8951_USB_TIMEOUT_MS		5000
#define ITE8951_STATUS_BYTES		13
#define ITE8951_IN_BUFFER_SIZE		4096
#define ITE8951_MAX_XFER_BYTES		61440
#define ITE8951_DEFAULT_IMAGE_BUF	0x00382f30u
#define ITE8951_REG_PANEL_MODE		0x18001224u
#define ITE8951_REG_DISPLAY_CFG		0x18001138u
#define ITE8951_REG_PANEL_MODE_READY	0x80000000u
#define ITE8951_DPY_STATUS_BYTES	136
#define ITE8951_WAVEFORM_CURRENT	0xffu
#define ITE8951_PANEL_READY_POLL_MS	50

/*
 * Coordinator scenario IDs (itetcon.h / EinkIteAPI).
 * USB 0xA6 SET puts the ID in the address high byte: address = scenario << 24
 * (validated on Windows — see docs/PROTOCOL_WINDOWS.md). GET (address=0) returns
 * live scenario in response byte 1. After leave-KB (0x03000000), GET reports 0,
 * not 3 — success is "not keyboard", not "GET == requested ID".
 */
#define ITE8951_SCENARIO_NORMAL		0u	/* TCON_SCENARIO_NORMAL, GIHW_OWNER_DRAW */
#define ITE8951_SCENARIO_KEYBOARD	1u	/* TCON_SCENARIO_KBD, GI_SCENARIO_KEYBOARD */
#define ITE8951_SCENARIO_PEN_MOUSE	3u	/* GI_SCENARIO_PEN_MOUSE */
#define ITE8951_SCENARIO_ADDR(s)	((u32)(s) << 24)
#define ITE8951_SCENARIO_LEAVE_KB_ADDR	ITE8951_SCENARIO_ADDR(ITE8951_SCENARIO_PEN_MOUSE)

/* 2019 reference internal transition DWORDs (usercmd_enable_kb / enable_draw). */
#define ITE8951_SCENARIO_REF_KB_EXIT	0x00040000u
#define ITE8951_SCENARIO_REF_KB_ARM	0x01010000u

enum ite8951_waveform {
	ITE8951_WAVEFORM_INIT = 0,
	ITE8951_WAVEFORM_DU2 = 1,
	ITE8951_WAVEFORM_GC16 = 2,
	ITE8951_WAVEFORM_GL16 = 3,
};

/**
 * struct ite8951_rect - axis-aligned region in panel coordinates
 */
struct ite8951_rect {
	u32 x;
	u32 y;
	u32 width;
	u32 height;
};

/**
 * struct ite8951_system_info - fields parsed from GET_SYS response
 */
struct ite8951_system_info {
	u32 signature;
	u32 panel_width;
	u32 panel_height;
	u32 update_buf_base;
	u32 image_buf_base;
};

/**
 * struct ite8951_usb - USB bulk endpoints for the ITE8951 vendor interface
 * @bulk_in_urb: Reused for every bulk IN (panel expects one reader at a time)
 * @bulk_in_buffer: DMA buffer for bulk IN transfers
 */
struct ite8951_usb {
	struct usb_device *usb_dev;
	u8 bulk_in;
	u8 bulk_out;
	int bulk_in_max;
	struct mutex io_lock;
	struct urb *bulk_in_urb;
	u8 *bulk_in_buffer;
	struct completion in_done;
	int in_status;
	int in_actual;
	u32 image_buf_base;
	u32 update_buf_base;
	u32 panel_width;
	u32 panel_height;
	bool draw_mode_active;
};

int ite8951_usb_setup(struct ite8951_usb *link);
void ite8951_usb_teardown(struct ite8951_usb *link);

int ite8951_usb_prepare(struct ite8951_usb *link);
int ite8951_panel_init_sequence(struct ite8951_usb *link);

int ite8951_parse_system_info(const u8 *response, size_t length,
			      struct ite8951_system_info *info);
int ite8951_set_waveform(struct ite8951_usb *link, enum ite8951_waveform mode);
int ite8951_set_waveform_u16(struct ite8951_usb *link, u16 mode);
int ite8951_query_display_status(struct ite8951_usb *link, u16 *engine_busy);
int ite8951_wait_display_ready(struct ite8951_usb *link, unsigned int timeout_ms);
int ite8951_enter_draw_mode(struct ite8951_usb *link);
int ite8951_assert_draw_scenario(struct ite8951_usb *link);
int ite8951_reapply_draw_input(struct ite8951_usb *link);
int ite8951_load_pixels(struct ite8951_usb *link, u32 buf_offset,
			const struct ite8951_rect *region,
			const u8 *pixels, size_t length);
int ite8951_blit(struct ite8951_usb *link, u32 buf_offset,
		 const struct ite8951_rect *dest, u32 waveform);

#endif /* ITE8951_H */
