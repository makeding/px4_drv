// SPDX-License-Identifier: GPL-2.0-only
/* Linux rc-core integration for the PX-Q3U4 infrared receiver. */

#include "print_format.h"
#include "px4_ir.h"

#include <linux/kernel.h>
#include <linux/kconfig.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "px4_device.h"
#include "px4_usb.h"

#if IS_REACHABLE(CONFIG_RC_CORE)

#include <media/rc-core.h>
#include <media/rc-map.h>

/* Linux の IT930x 共通ドライバで実績のあるポーリング周期。 */
#define PX4_IR_POLL_INTERVAL_MS	500

static void px4_ir_decode(const u8 code[4], enum rc_proto *protocol,
			  u32 *scancode)
{
	if ((code[2] + code[3]) == 0xff) {
		if ((code[0] + code[1]) == 0xff) {
			*protocol = RC_PROTO_NEC;
			*scancode = RC_SCANCODE_NEC(code[0], code[2]);
		} else {
			*protocol = RC_PROTO_NECX;
			*scancode = RC_SCANCODE_NECX(code[0] << 8 | code[1],
						     code[2]);
		}
	} else {
		*protocol = RC_PROTO_NEC32;
		*scancode = RC_SCANCODE_NEC32((u32)code[0] << 24 |
						 (u32)code[1] << 16 |
						 (u32)code[2] << 8 |
						 code[3]);
	}
}

static void px4_ir_poll(struct work_struct *work)
{
	struct px4_ir_device *ir = container_of(to_delayed_work(work),
						 struct px4_ir_device, poll_work);
	struct px4_device *px4 = ir->parent;
	u8 code[4];
	int ret;

	if (!atomic_read(&ir->polling) || !atomic_read(&px4->available))
		return;

	ret = it930x_ir_get(&px4->it930x, code);
	if (!ret) {
		enum rc_proto protocol;
		u32 scancode;

		px4_ir_decode(code, &protocol, &scancode);
		rc_keydown(ir->rcdev, protocol, scancode, 0);
	} else if (ret != -EAGAIN && ret != -ENODEV) {
		dev_warn_ratelimited(px4->dev,
			"px4_ir_poll: it930x_ir_get() failed. (ret: %d)\n",
			ret);
	}

	if (atomic_read(&ir->polling) && atomic_read(&px4->available))
		schedule_delayed_work(&ir->poll_work,
			msecs_to_jiffies(PX4_IR_POLL_INTERVAL_MS));
}

static int px4_ir_open(struct rc_dev *rcdev)
{
	struct px4_ir_device *ir = rcdev->priv;

	if (!atomic_read(&ir->parent->available))
		return -ENODEV;

	atomic_set(&ir->polling, 1);
	schedule_delayed_work(&ir->poll_work, 0);
	return 0;
}

static void px4_ir_close(struct rc_dev *rcdev)
{
	struct px4_ir_device *ir = rcdev->priv;

	atomic_set(&ir->polling, 0);
	cancel_delayed_work_sync(&ir->poll_work);
}

int px4_ir_device_register(struct px4_ir_device *ir,
			   struct px4_device *px4)
{
	struct usb_device *usb_dev;
	struct rc_dev *rcdev;
	u16 product_id;
	int ret;

	if (!ir || !px4)
		return -EINVAL;

	memset(ir, 0, sizeof(*ir));
	ir->parent = px4;
	atomic_set(&ir->polling, 0);
	INIT_DELAYED_WORK(&ir->poll_work, px4_ir_poll);

	usb_dev = px4->it930x.bus.usb.dev;
	product_id = le16_to_cpu(usb_dev->descriptor.idProduct);

	/* Q3U4 の単一の筐体機能を2つの同一 USB デバイスへ重複登録しない。 */
	if (product_id != USB_PID_PX_Q3U4 || px4->serial.dev_id != 1)
		return 0;

	rcdev = rc_allocate_device(RC_DRIVER_SCANCODE);
	if (!rcdev)
		return -ENOMEM;

	snprintf(ir->phys, sizeof(ir->phys), "%s/ir0", dev_name(px4->dev));
	rcdev->device_name = "PLEX PX-Q3U4 Remote Controller";
	rcdev->input_phys = ir->phys;
	rcdev->input_id.bustype = BUS_USB;
	rcdev->input_id.vendor = le16_to_cpu(usb_dev->descriptor.idVendor);
	rcdev->input_id.product = product_id;
	rcdev->input_id.version = le16_to_cpu(usb_dev->descriptor.bcdDevice);
	rcdev->driver_name = KBUILD_MODNAME;
	rcdev->map_name = RC_MAP_EMPTY;
	rcdev->allowed_protocols = RC_PROTO_BIT_NEC |
				   RC_PROTO_BIT_NECX |
				   RC_PROTO_BIT_NEC32;
	rcdev->priv = ir;
	rcdev->open = px4_ir_open;
	rcdev->close = px4_ir_close;
	rcdev->dev.parent = px4->dev;

	ret = rc_register_device(rcdev);
	if (ret) {
		rc_free_device(rcdev);
		return ret;
	}

	ir->rcdev = rcdev;
	ir->registered = true;
	dev_info(px4->dev, "PX-Q3U4 infrared receiver registered\n");
	return 0;
}

void px4_ir_device_unregister(struct px4_ir_device *ir)
{
	if (!ir || !ir->registered)
		return;

	ir->registered = false;
	atomic_set(&ir->polling, 0);
	cancel_delayed_work_sync(&ir->poll_work);
	rc_unregister_device(ir->rcdev);
	ir->rcdev = NULL;
}

#else

int px4_ir_device_register(struct px4_ir_device *ir,
			   struct px4_device *px4)
{
	if (!ir || !px4)
		return -EINVAL;

	memset(ir, 0, sizeof(*ir));
	return 0;
}

void px4_ir_device_unregister(struct px4_ir_device *ir)
{
	(void)ir;
}

#endif
