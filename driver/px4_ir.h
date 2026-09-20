// SPDX-License-Identifier: GPL-2.0-only
/* Linux rc-core integration for the PX-Q3U4 infrared receiver. */

#ifndef __PX4_IR_H__
#define __PX4_IR_H__

#include <linux/atomic.h>
#include <linux/workqueue.h>

struct px4_device;
struct rc_dev;

struct px4_ir_device {
	struct px4_device *parent;
	struct rc_dev *rcdev;
	struct delayed_work poll_work;
	atomic_t polling;
	char phys[64];
	bool registered;
};

int px4_ir_device_register(struct px4_ir_device *ir,
			   struct px4_device *px4);
void px4_ir_device_unregister(struct px4_ir_device *ir);

#endif
