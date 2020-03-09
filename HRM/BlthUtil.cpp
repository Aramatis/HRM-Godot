#include "pch.h"
#include "BlthUtil.h"

using namespace BluetoothUtilities;

// Simple char to int representation converter
int BluetoothUtilities::CharToInt(char Char)
{
	if (Char >= '0' && Char <= '9')
	{
		return Char - '0';
	}
	if (Char >= 'a' && Char <= 'f')
	{
		return Char - 'a' + 10;
	}
	return -1;
}

// Formats a given long long representing a bluetooth address to its string 
// representation.
std::wstring BluetoothUtilities::FormatBluetoothAddress(
	unsigned long long BluetoothAddress)
{
	std::wostringstream Address;
	Address << std::hex << std::setfill(L'0')
		<< std::setw(2) << ((BluetoothAddress >> (5 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (4 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (3 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (2 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (1 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (0 * 8)) & 0xff);
	return Address.str();
}

// Formats a given string representing a bluetooth address to its long long 
// representation
unsigned long long BluetoothUtilities::FormatBluetoothAddressInverse(
	Platform::Array<uint8>^ BluetoothAddress)
{
	uint64 Multiplier = 1;
	unsigned long long Address = 0;
	for (int i = BluetoothAddress->Length - 1; i >= 0; --i)
	{
		uint8 Number = BluetoothAddress[i];

		if (Number != ':')
		{
			Address += CharToInt(Number) * Multiplier;
			Multiplier *= 16;
		}
	}
	return Address;
}

// Scans for MiBand 3 peripherals to connect to and sends them to the client of the
// given MiBand3 object.
void BluetoothUtilities::scan(MiBand3^ band)
{
	std::wcout << "BLE Scanner started" << std::endl;

	// Creates a new Bluetooth advertiser and sets it into scanning mode
	Advertisement::BluetoothLEAdvertisementWatcher^ AdWatcher = ref new Advertisement::BluetoothLEAdvertisementWatcher();
	AdWatcher->ScanningMode =
		Advertisement::BluetoothLEScanningMode::Active;
	// Set the scanner to send every found device to the client
	AdWatcher->Received += ref new Windows::Foundation::TypedEventHandler
		<Advertisement::BluetoothLEAdvertisementWatcher^,
		Advertisement::BluetoothLEAdvertisementReceivedEventArgs^>(
			[AdWatcher, band](
				Advertisement::BluetoothLEAdvertisementWatcher^ Watcher,
				Advertisement::BluetoothLEAdvertisementReceivedEventArgs^ EventArgs)
			{
				unsigned int Index;
				if (EventArgs->Advertisement->ServiceUuids->
					IndexOf(band->UUIDServiceInfo, &Index))
				{
					Platform::String^ StrAddress =
						ref new Platform::String(
							FormatBluetoothAddress(
								EventArgs->BluetoothAddress).c_str());
					std::wcout << "Device: " << StrAddress->Data() <<
						" found." << std::endl;
					band->WriteToServer(StrAddress, true);
				}
			});

	// Start the scanner and the timer to stop it
	AdWatcher->Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(20000));
	AdWatcher->Stop();
}
