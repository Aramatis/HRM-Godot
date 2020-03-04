// HRM.cpp : This file contains the 'main' function. Program execution begins and ends there.

#include "pch.h"

#include "MiBand3.h"
#include "RemoteCommunication.h"

using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

std::wstring FormatBluetoothAddress(unsigned long long BluetoothAddress)
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

int main(Platform::Array<Platform::String^>^ args)
{
	MiBand3^ MB3 = ref new MiBand3();

	MB3->RC->StartServer();

	std::wcout << "Started service" << "\n";

	Advertisement::BluetoothLEAdvertisementWatcher^ AdvertisementWatcher = ref new Advertisement::BluetoothLEAdvertisementWatcher();
	AdvertisementWatcher->ScanningMode = Advertisement::BluetoothLEScanningMode::Active;
	AdvertisementWatcher->Received += ref new Windows::Foundation::TypedEventHandler<Advertisement::BluetoothLEAdvertisementWatcher^, Advertisement::BluetoothLEAdvertisementReceivedEventArgs^>(
		[AdvertisementWatcher, MB3](Advertisement::BluetoothLEAdvertisementWatcher^ Watcher, Advertisement::BluetoothLEAdvertisementReceivedEventArgs^ EventArgs) {
			unsigned int Index;
			if (EventArgs->Advertisement->ServiceUuids->IndexOf(MB3->UUIDServiceInfo, &Index) /* && EventArgs->BluetoothAddress == 0xcac40a8840d8*/)
			{
				Platform::String^ StrAddress = ref new Platform::String(FormatBluetoothAddress(EventArgs->BluetoothAddress).c_str());
				std::wcout << "Device: " << StrAddress->Data() << std::endl;

				if (MB3->bAuthenticated)
				{
					AdvertisementWatcher->Stop();
				}

				// MB3->Connect(EventArgs->BluetoothAddress);
				
				AdvertisementWatcher->Stop();

				while (!MB3->bAuthenticated) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
			}
		});
	AdvertisementWatcher->Start();

	int a;
	std::cin >> a;

	return 0;
}
