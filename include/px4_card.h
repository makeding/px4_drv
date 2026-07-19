// SPDX-License-Identifier: GPL-2.0-only
/*
 * Private userspace ABI for px4_drv built-in smart-card readers.
 *
 * This is not a CCID interface.  pcsc-lite support is provided by the
 * userspace IFD Handler which translates this private transport to PC/SC.
 */

#ifndef __PX4_CARD_H__
#define __PX4_CARD_H__

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef __u8 px4_card_u8;
typedef __u16 px4_card_u16;
typedef __u32 px4_card_u32;
typedef __u64 px4_card_u64;
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint8_t px4_card_u8;
typedef uint16_t px4_card_u16;
typedef uint32_t px4_card_u32;
typedef uint64_t px4_card_u64;
#endif

#define PX4_CARD_ABI_VERSION	1
#define PX4_CARD_MAX_DATA_SIZE	255

struct px4_card_info {
	px4_card_u32 abi_version;
	px4_card_u16 vendor_id;
	px4_card_u16 product_id;
	px4_card_u16 bus_number;
	px4_card_u16 device_number;
	px4_card_u8 device_id;
	px4_card_u8 reserved[7];
	px4_card_u64 serial_number;
};

struct px4_card_status {
	px4_card_u8 present;
	px4_card_u8 reserved[7];
};

struct px4_card_data {
	px4_card_u32 length;
	px4_card_u8 data[PX4_CARD_MAX_DATA_SIZE];
};

enum px4_card_baudrate {
	PX4_CARD_BAUDRATE_9600 = 0,
	PX4_CARD_BAUDRATE_19200 = 1,
	PX4_CARD_BAUDRATE_38400 = 2,
};

#define PX4_CARD_IOC_MAGIC	0xca
#define PX4_CARD_GET_INFO	_IOR(PX4_CARD_IOC_MAGIC, 0x00, struct px4_card_info)
#define PX4_CARD_GET_STATUS	_IOR(PX4_CARD_IOC_MAGIC, 0x01, struct px4_card_status)
#define PX4_CARD_RESET		_IO(PX4_CARD_IOC_MAGIC, 0x02)
#define PX4_CARD_SET_BAUDRATE	_IOW(PX4_CARD_IOC_MAGIC, 0x03, px4_card_u32)
#define PX4_CARD_GET_RX_READY	_IOR(PX4_CARD_IOC_MAGIC, 0x04, px4_card_u32)
#define PX4_CARD_READ		_IOWR(PX4_CARD_IOC_MAGIC, 0x05, struct px4_card_data)
#define PX4_CARD_WRITE		_IOW(PX4_CARD_IOC_MAGIC, 0x06, struct px4_card_data)

#endif
