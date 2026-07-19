#pragma once

#include <cstdint>
#include <string>

#include "card_device.hpp"

namespace px4::pcsc {

class LinuxCardDevice final : public px4::CardDevice {
public:
	explicit LinuxCardDevice(std::string device_name);
	~LinuxCardDevice() override;

	LinuxCardDevice(const LinuxCardDevice &) = delete;
	LinuxCardDevice& operator=(const LinuxCardDevice &) = delete;

	int OpenCard() override;
	void CloseCard() override;
	int DetectCard(bool &detected) override;
	int ResetCard() override;
	int SetCardBaudrate(::it930x_uart_baudrate baudrate) override;
	int IsCardDataReady(bool &ready) override;
	int ReadCardData(std::uint8_t *buf, std::uint8_t &len) override;
	int WriteCardData(const std::uint8_t *buf, std::uint8_t len) override;

private:
	int FindAndOpenDevice();
	int OpenDevicePath(const std::string &path, int expected_bus,
			   int expected_device);

	std::string device_name_;
	int fd_;
};

} // namespace px4::pcsc
