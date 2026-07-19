extern "C" {
#include <ifdhandler.h>
}

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "linux_card_device.hpp"
#include "smart_card.hpp"

namespace {

struct Reader final {
	explicit Reader(const char *device_name)
		: device(std::make_shared<px4::pcsc::LinuxCardDevice>(
			device_name ? device_name : "")), card(device)
	{
	}

	int EnsureOpen()
	{
		if (open)
			return 0;
		int ret = card.Open();
		if (!ret)
			open = true;
		return ret;
	}

	void Close()
	{
		if (!open)
			return;
		card.Close();
		open = false;
		atr.clear();
	}

	std::mutex lock;
	std::shared_ptr<px4::pcsc::LinuxCardDevice> device;
	px4::SmartCard card;
	bool open = false;
	std::vector<std::uint8_t> atr;
};

std::mutex readers_lock;
std::unordered_map<DWORD, std::shared_ptr<Reader>> readers;

std::shared_ptr<Reader> GetReader(DWORD lun)
{
	std::lock_guard<std::mutex> lock(readers_lock);
	auto iterator = readers.find(lun);
	return iterator == readers.end() ? nullptr : iterator->second;
}

RESPONSECODE MapError(int error)
{
	switch (error) {
	case 0:
		return IFD_SUCCESS;
	case -ENODEV:
	case -ENOENT:
	case -ENXIO:
		return IFD_NO_SUCH_DEVICE;
	case px4::SMART_CARD_NO_MEDIUM:
#ifdef ENOMEDIUM
	case -ENOMEDIUM:
#endif
		return IFD_ICC_NOT_PRESENT;
	case -ETIMEDOUT:
		return IFD_RESPONSE_TIMEOUT;
	case -ENOBUFS:
	case -EMSGSIZE:
		return IFD_ERROR_INSUFFICIENT_BUFFER;
	case -EPROTONOSUPPORT:
		return IFD_PROTOCOL_NOT_SUPPORTED;
	default:
		return IFD_COMMUNICATION_ERROR;
	}
}

RESPONSECODE CopyValue(PDWORD length, PUCHAR value, const void *source,
		       std::size_t source_length)
{
	if (!length)
		return IFD_COMMUNICATION_ERROR;
	if (*length < source_length || (source_length && !value)) {
		*length = source_length;
		return IFD_ERROR_INSUFFICIENT_BUFFER;
	}
	if (source_length)
		std::memcpy(value, source, source_length);
	*length = source_length;
	return IFD_SUCCESS;
}

} // namespace

extern "C" RESPONSECODE IFDHCreateChannelByName(DWORD Lun, LPSTR DeviceName)
{
	if ((Lun & 0xffffU) != 0)
		return IFD_NO_SUCH_DEVICE;

	auto reader = std::make_shared<Reader>(DeviceName);
	{
		std::lock_guard<std::mutex> lock(reader->lock);
		int ret = reader->EnsureOpen();
		if (ret)
			return MapError(ret);
	}

	std::lock_guard<std::mutex> lock(readers_lock);
	if (readers.find(Lun) != readers.end())
		return IFD_COMMUNICATION_ERROR;
	readers.emplace(Lun, std::move(reader));
	return IFD_SUCCESS;
}

extern "C" RESPONSECODE IFDHCreateChannel(DWORD Lun, DWORD Channel)
{
	(void)Lun;
	(void)Channel;
	return IFD_NOT_SUPPORTED;
}

extern "C" RESPONSECODE IFDHCloseChannel(DWORD Lun)
{
	std::shared_ptr<Reader> reader;
	{
		std::lock_guard<std::mutex> lock(readers_lock);
		auto iterator = readers.find(Lun);
		if (iterator == readers.end())
			return IFD_NO_SUCH_DEVICE;
		reader = iterator->second;
		readers.erase(iterator);
	}

	std::lock_guard<std::mutex> lock(reader->lock);
	reader->Close();
	return IFD_SUCCESS;
}

extern "C" RESPONSECODE IFDHGetCapabilities(DWORD Lun, DWORD Tag,
					     PDWORD Length, PUCHAR Value)
{
	auto reader = GetReader(Lun);
	if (!reader)
		return IFD_NO_SUCH_DEVICE;
	std::lock_guard<std::mutex> lock(reader->lock);

	switch (Tag) {
	case TAG_IFD_ATR:
#ifdef SCARD_ATTR_ATR_STRING
	case SCARD_ATTR_ATR_STRING:
#endif
	{
		int ret = reader->EnsureOpen();
		if (ret)
			return MapError(ret);
		bool present = false;
		bool initialized = false;
		ret = reader->card.GetStatus(present, initialized, reader->atr);
		if (ret)
			return MapError(ret);
		if (!present || !initialized)
			reader->atr.clear();
		return CopyValue(Length, Value, reader->atr.data(), reader->atr.size());
	}

	case TAG_IFD_SIMULTANEOUS_ACCESS:
	{
		const UCHAR count = 16;
		return CopyValue(Length, Value, &count, sizeof(count));
	}

	case TAG_IFD_THREAD_SAFE:
	case TAG_IFD_SLOT_THREAD_SAFE:
	case TAG_IFD_SLOTS_NUMBER:
	{
		const UCHAR enabled = 1;
		return CopyValue(Length, Value, &enabled, sizeof(enabled));
	}

	default:
		return IFD_ERROR_TAG;
	}
}

extern "C" RESPONSECODE IFDHSetCapabilities(DWORD Lun, DWORD Tag,
					     DWORD Length, PUCHAR Value)
{
	(void)Lun;
	(void)Tag;
	(void)Length;
	(void)Value;
	return IFD_ERROR_VALUE_READ_ONLY;
}

extern "C" RESPONSECODE IFDHSetProtocolParameters(DWORD Lun, DWORD Protocol,
	UCHAR Flags, UCHAR PTS1, UCHAR PTS2, UCHAR PTS3)
{
	(void)Flags;
	(void)PTS1;
	(void)PTS2;
	(void)PTS3;
	if (!GetReader(Lun))
		return IFD_NO_SUCH_DEVICE;
	return Protocol == SCARD_PROTOCOL_T1 ? IFD_SUCCESS :
		IFD_PROTOCOL_NOT_SUPPORTED;
}

extern "C" RESPONSECODE IFDHPowerICC(DWORD Lun, DWORD Action, PUCHAR Atr,
				     PDWORD AtrLength)
{
	auto reader = GetReader(Lun);
	if (!reader)
		return IFD_NO_SUCH_DEVICE;
	if (!AtrLength)
		return IFD_COMMUNICATION_ERROR;
	std::lock_guard<std::mutex> lock(reader->lock);

	if (Action == IFD_POWER_DOWN) {
		reader->Close();
		*AtrLength = 0;
		return IFD_SUCCESS;
	}
	if (Action != IFD_POWER_UP && Action != IFD_RESET)
		return IFD_NOT_SUPPORTED;

	int ret = reader->EnsureOpen();
	if (ret)
		return MapError(ret);
	ret = reader->card.Reset(reader->atr);
	if (ret) {
		*AtrLength = 0;
		RESPONSECODE mapped = MapError(ret);
		return mapped == IFD_COMMUNICATION_ERROR ? IFD_ERROR_POWER_ACTION : mapped;
	}
	return CopyValue(AtrLength, Atr, reader->atr.data(), reader->atr.size());
}

extern "C" RESPONSECODE IFDHTransmitToICC(DWORD Lun,
	SCARD_IO_HEADER SendPci, PUCHAR TxBuffer, DWORD TxLength,
	PUCHAR RxBuffer, PDWORD RxLength, PSCARD_IO_HEADER RecvPci)
{
	auto reader = GetReader(Lun);
	if (!reader)
		return IFD_NO_SUCH_DEVICE;
	if (SendPci.Protocol != SCARD_PROTOCOL_T1)
		return IFD_PROTOCOL_NOT_SUPPORTED;
	if (!TxBuffer || !TxLength || !RxBuffer || !RxLength)
		return IFD_COMMUNICATION_ERROR;
	std::lock_guard<std::mutex> lock(reader->lock);

	int ret = reader->EnsureOpen();
	if (ret)
		return MapError(ret);
	std::size_t receive_length = *RxLength;
	ret = reader->card.Transmit(TxBuffer, TxLength, RxBuffer, receive_length);
	*RxLength = receive_length;
	if (ret)
		return MapError(ret);
	if (RecvPci) {
		RecvPci->Protocol = SCARD_PROTOCOL_T1;
		RecvPci->Length = sizeof(*RecvPci);
	}
	return IFD_SUCCESS;
}

extern "C" RESPONSECODE IFDHControl(DWORD Lun, DWORD dwControlCode,
	PUCHAR TxBuffer, DWORD TxLength, PUCHAR RxBuffer, DWORD RxLength,
	LPDWORD pdwBytesReturned)
{
	(void)Lun;
	(void)dwControlCode;
	(void)TxBuffer;
	(void)TxLength;
	(void)RxBuffer;
	(void)RxLength;
	if (pdwBytesReturned)
		*pdwBytesReturned = 0;
	return IFD_NOT_SUPPORTED;
}

extern "C" RESPONSECODE IFDHICCPresence(DWORD Lun)
{
	auto reader = GetReader(Lun);
	if (!reader)
		return IFD_NO_SUCH_DEVICE;
	std::lock_guard<std::mutex> lock(reader->lock);

	int ret = reader->EnsureOpen();
	if (ret)
		return MapError(ret);
	bool present = false;
	bool initialized = false;
	ret = reader->card.GetStatus(present, initialized, reader->atr);
	if (ret)
		return MapError(ret);
	return present ? IFD_ICC_PRESENT : IFD_ICC_NOT_PRESENT;
}
