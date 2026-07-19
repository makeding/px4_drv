#include "linux_card_device.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include "px4_card.h"

namespace px4::pcsc {

namespace {

constexpr std::uint16_t PX4_VENDOR_ID = 0x0511;
constexpr std::uint16_t PX_Q3U4_PRODUCT_ID = 0x084a;

int IoctlResult(int result)
{
	return result < 0 ? -errno : 0;
}

} // namespace

LinuxCardDevice::LinuxCardDevice(std::string device_name)
	: device_name_(std::move(device_name)), fd_(-1)
{
}

LinuxCardDevice::~LinuxCardDevice()
{
	CloseCard();
}

int LinuxCardDevice::OpenCard()
{
	if (fd_ >= 0)
		return -EALREADY;
	return FindAndOpenDevice();
}

void LinuxCardDevice::CloseCard()
{
	if (fd_ < 0)
		return;
	close(fd_);
	fd_ = -1;
}

int LinuxCardDevice::FindAndOpenDevice()
{
	int expected_bus = -1;
	int expected_device = -1;
	const char *usb_path = std::strstr(device_name_.c_str(), "/dev/bus/usb/");
	if (usb_path)
		std::sscanf(usb_path, "/dev/bus/usb/%d/%d", &expected_bus,
			&expected_device);

	if (device_name_.rfind("/dev/px4card", 0) == 0)
		return OpenDevicePath(device_name_, expected_bus, expected_device);

	DIR *directory = opendir("/dev");
	if (!directory)
		return -errno;

	std::vector<std::string> paths;
	while (dirent *entry = readdir(directory)) {
		if (std::strncmp(entry->d_name, "px4card", 7) == 0)
			paths.emplace_back(std::string("/dev/") + entry->d_name);
	}
	closedir(directory);
	std::sort(paths.begin(), paths.end());

	int last_error = -ENODEV;
	for (const auto &path : paths) {
		int ret = OpenDevicePath(path, expected_bus, expected_device);
		if (!ret)
			return 0;
		if (ret != -ENODEV)
			last_error = ret;
	}
	return last_error;
}

int LinuxCardDevice::OpenDevicePath(const std::string &path, int expected_bus,
				    int expected_device)
{
	int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	px4_card_info info{};
	int ret = IoctlResult(ioctl(fd, PX4_CARD_GET_INFO, &info));
	if (!ret && (info.abi_version != PX4_CARD_ABI_VERSION ||
		info.vendor_id != PX4_VENDOR_ID ||
		info.product_id != PX_Q3U4_PRODUCT_ID || info.device_id != 1))
		ret = -ENODEV;
	if (!ret && expected_bus >= 0 && expected_device >= 0 &&
		(info.bus_number != expected_bus || info.device_number != expected_device))
		ret = -ENODEV;

	if (ret) {
		close(fd);
		return ret;
	}

	fd_ = fd;
	return 0;
}

int LinuxCardDevice::DetectCard(bool &detected)
{
	if (fd_ < 0)
		return -ENODEV;
	px4_card_status status{};
	int ret = IoctlResult(ioctl(fd_, PX4_CARD_GET_STATUS, &status));
	if (!ret)
		detected = status.present != 0;
	return ret;
}

int LinuxCardDevice::ResetCard()
{
	return fd_ < 0 ? -ENODEV : IoctlResult(ioctl(fd_, PX4_CARD_RESET));
}

int LinuxCardDevice::SetCardBaudrate(::it930x_uart_baudrate baudrate)
{
	if (fd_ < 0)
		return -ENODEV;
	std::uint32_t value = static_cast<std::uint32_t>(baudrate);
	return IoctlResult(ioctl(fd_, PX4_CARD_SET_BAUDRATE, &value));
}

int LinuxCardDevice::IsCardDataReady(bool &ready)
{
	if (fd_ < 0)
		return -ENODEV;
	std::uint32_t value = 0;
	int ret = IoctlResult(ioctl(fd_, PX4_CARD_GET_RX_READY, &value));
	if (!ret)
		ready = value != 0;
	return ret;
}

int LinuxCardDevice::ReadCardData(std::uint8_t *buf, std::uint8_t &len)
{
	if (fd_ < 0)
		return -ENODEV;
	if (!buf || !len)
		return -EINVAL;

	px4_card_data data{};
	data.length = len;
	int ret = IoctlResult(ioctl(fd_, PX4_CARD_READ, &data));
	if (!ret) {
		std::memcpy(buf, data.data, data.length);
		len = static_cast<std::uint8_t>(data.length);
	}
	return ret;
}

int LinuxCardDevice::WriteCardData(const std::uint8_t *buf, std::uint8_t len)
{
	if (fd_ < 0)
		return -ENODEV;
	if (!buf || !len)
		return -EINVAL;

	px4_card_data data{};
	data.length = len;
	std::memcpy(data.data, buf, len);
	return IoctlResult(ioctl(fd_, PX4_CARD_WRITE, &data));
}

} // namespace px4::pcsc
