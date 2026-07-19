// SPDX-License-Identifier: GPL-2.0-only
/* Private smart-card character device definitions. */

#ifndef __PX4_CARD_DEVICE_H__
#define __PX4_CARD_DEVICE_H__

#include <linux/atomic.h>
#include <linux/miscdevice.h>

struct px4_device;

struct px4_card_device {
	struct miscdevice miscdev;
	struct px4_device *parent;
	atomic_t open;
	bool registered;
};

int px4_card_device_register(struct px4_card_device *card,
			     struct px4_device *px4);
void px4_card_device_unregister(struct px4_card_device *card);

#endif
