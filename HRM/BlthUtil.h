#pragma once

#include "pch.h"
#include "MiBand3.h"
#include "RemoteCommunication.h"


using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

namespace BluetoothUtilities
{
	int CharToInt(char Char);
	std::wstring FormatBluetoothAddress(unsigned long long BluetoothAddress);
	unsigned long long FormatBluetoothAddressInverse(
		Platform::Array<uint8>^ BluetoothAddress);
	void scan(MiBand3^ band, int secs);
	Platform::Guid GetGuidFromStringBase(std::string SubGuid);
	Platform::Guid GetGuidFromString(std::string Guid);
}
