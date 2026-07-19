// SPDX-License-Identifier: GPL-2.0-only
/*
 * Private character device for built-in smart-card readers.
 *
 * The hardware is not USB CCID.  This device only exposes the IT930x card
 * transport to the px4_drv pcsc-lite IFD Handler.  Card protocol state stays
 * in userspace so it can be tested without loading the kernel module.
 */

#include "print_format.h"
#include "px4_card.h"

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#include "px4_device.h"
#include "px4_usb.h"
#include "../include/px4_card.h"

static atomic_t px4_card_index = ATOMIC_INIT(0);

static int px4_card_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct px4_card_device *card = container_of(miscdev,
						    struct px4_card_device,
						    miscdev);
	struct px4_device *px4 = card->parent;
	int ret;

	if (!kref_get_unless_zero(&px4->kref))
		return -ENODEV;

	if (atomic_cmpxchg(&card->open, 0, 1)) {
		ret = -EBUSY;
		goto fail_ref;
	}

	ret = px4_device_card_open(px4);
	if (ret)
		goto fail_open;

	file->private_data = card;
	return 0;

fail_open:
	atomic_set(&card->open, 0);
fail_ref:
	kref_put(&px4->kref, px4_device_release);
	return ret;
}

static int px4_card_release(struct inode *inode, struct file *file)
{
	struct px4_card_device *card = file->private_data;
	struct px4_device *px4 = card->parent;

	px4_device_card_close(px4);
	atomic_set(&card->open, 0);
	kref_put(&px4->kref, px4_device_release);
	return 0;
}

static long px4_card_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct px4_card_device *card = file->private_data;
	struct px4_device *px4 = card->parent;
	void __user *user = (void __user *)arg;
	int ret = 0;

	switch (cmd) {
	case PX4_CARD_GET_INFO:
	{
		struct usb_device *usb_dev = px4->it930x.bus.usb.dev;
		struct px4_card_info info = {
			.abi_version = PX4_CARD_ABI_VERSION,
			.vendor_id = le16_to_cpu(usb_dev->descriptor.idVendor),
			.product_id = le16_to_cpu(usb_dev->descriptor.idProduct),
			.bus_number = usb_dev->bus->busnum,
			.device_number = usb_dev->devnum,
			.device_id = px4->serial.dev_id,
			.serial_number = px4->serial.serial_number,
		};

		if (copy_to_user(user, &info, sizeof(info)))
			ret = -EFAULT;
		break;
	}

	case PX4_CARD_GET_STATUS:
	{
		struct px4_card_status status = {};
		bool present = false;

		ret = px4_device_card_detect(px4, &present);
		if (ret)
			break;
		status.present = present;
		if (copy_to_user(user, &status, sizeof(status)))
			ret = -EFAULT;
		break;
	}

	case PX4_CARD_RESET:
		ret = px4_device_card_reset(px4);
		break;

	case PX4_CARD_SET_BAUDRATE:
	{
		__u32 baudrate;

		if (copy_from_user(&baudrate, user, sizeof(baudrate))) {
			ret = -EFAULT;
			break;
		}
		if (baudrate > PX4_CARD_BAUDRATE_38400) {
			ret = -EINVAL;
			break;
		}
		ret = px4_device_card_set_baudrate(px4,
			(enum it930x_uart_baudrate)baudrate);
		break;
	}

	case PX4_CARD_GET_RX_READY:
	{
		__u32 ready = 0;
		bool value = false;

		ret = px4_device_card_is_data_ready(px4, &value);
		if (ret)
			break;
		ready = value;
		if (copy_to_user(user, &ready, sizeof(ready)))
			ret = -EFAULT;
		break;
	}

	case PX4_CARD_READ:
	{
		struct px4_card_data data;
		u8 length;

		if (copy_from_user(&data, user, sizeof(data))) {
			ret = -EFAULT;
			break;
		}
		if (!data.length || data.length > PX4_CARD_MAX_DATA_SIZE) {
			ret = -EINVAL;
			break;
		}
		length = data.length;
		ret = px4_device_card_read(px4, data.data, &length);
		if (ret)
			break;
		data.length = length;
		if (copy_to_user(user, &data, sizeof(data)))
			ret = -EFAULT;
		break;
	}

	case PX4_CARD_WRITE:
	{
		struct px4_card_data data;

		if (copy_from_user(&data, user, sizeof(data))) {
			ret = -EFAULT;
			break;
		}
		if (!data.length || data.length > PX4_CARD_MAX_DATA_SIZE) {
			ret = -EINVAL;
			break;
		}
		ret = px4_device_card_write(px4, data.data, data.length);
		break;
	}

	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static const struct file_operations px4_card_fops = {
	.owner = THIS_MODULE,
	.open = px4_card_open,
	.release = px4_card_release,
	.unlocked_ioctl = px4_card_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = px4_card_ioctl,
#endif
	.llseek = noop_llseek,
};

int px4_card_device_register(struct px4_card_device *card,
			     struct px4_device *px4)
{
	struct usb_device *usb_dev = px4->it930x.bus.usb.dev;
	u16 product_id = le16_to_cpu(usb_dev->descriptor.idProduct);
	int ret;

	if (!card || !px4)
		return -EINVAL;

	memset(card, 0, sizeof(*card));
	card->parent = px4;
	atomic_set(&card->open, 0);

	/* Q3U4 は2つの同一 USB デバイスを持つが、カードスロットは主側だけにある。 */
	if (product_id != USB_PID_PX_Q3U4 || px4->serial.dev_id != 1)
		return 0;

	card->miscdev.minor = MISC_DYNAMIC_MINOR;
	card->miscdev.name = kasprintf(GFP_KERNEL, "px4card%d",
		atomic_inc_return(&px4_card_index) - 1);
	if (!card->miscdev.name)
		return -ENOMEM;
	card->miscdev.fops = &px4_card_fops;
	card->miscdev.parent = px4->dev;

	ret = misc_register(&card->miscdev);
	if (ret) {
		kfree(card->miscdev.name);
		card->miscdev.name = NULL;
		return ret;
	}

	card->registered = true;
	dev_info(px4->dev, "/dev/%s: PX-Q3U4 built-in smart-card reader\n",
		 card->miscdev.name);
	return 0;
}

void px4_card_device_unregister(struct px4_card_device *card)
{
	if (!card || !card->registered)
		return;

	card->registered = false;
	misc_deregister(&card->miscdev);
	kfree(card->miscdev.name);
	card->miscdev.name = NULL;
}
