#pragma once

#include "pch.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <agents.h>
#include <ppltasks.h>
#include <pplawait.h>
#include <ppl.h>
//#include <Windows.h>
#include <collection.h>
////#include <Windows.Foundation.h>
#include <Windows.Devices.Bluetooth.h>
#include <Windows.Devices.Enumeration.h>
#include <Windows.Networking.Sockets.h>

//#include <winrt/Windows.Foundation.h>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

ref class RemoteCommunication;

ref class MiBand3 sealed
{
public:
	MiBand3();

	property RemoteCommunication^ RC;
	void Connect(unsigned long long BluetoothAddress);

	void Vibrate(uint16 Milliseconds);
	void WriteMessage(uint8* Message, uint32 MessageSize);

	void HeartRateStart();
	void HeartRatePing();
	void HeartRateStop();

	void EnableHeartRateNotifications();

	void Vibrate();

	void WriteToServer(Platform::String^ Message);

	property Platform::Guid UUIDServiceInfo;
	property Platform::Guid UUIDServiceAuthentication;
	property Platform::Guid UUIDServiceAlertNotification;
	property Platform::Guid UUIDServiceInmediateAlert;
	property Platform::Guid UUIDServiceHeartRate;

	property bool bAuthenticated;

private:
	concurrency::task<void> InConnect(unsigned long long BluetoothAddress);
	concurrency::task<void> Initialize(/*unsigned long long BluetoothAddress*/ BluetoothLEDevice^ InDevice);
	concurrency::task<void> Authentication();

	concurrency::task<void> Run();

	concurrency::task<Platform::Array<unsigned char>^> ReadFromCharacteristic(GenericAttributeProfile::GattCharacteristic^ Characteristic);
	concurrency::task<void> WriteToCharacteristic(GenericAttributeProfile::GattCharacteristic^ Characteristic, std::vector<unsigned char> Data);
	concurrency::task<void> WriteToDescriptor(GenericAttributeProfile::GattDescriptor^ Descriptor, std::vector<unsigned char> Data);

	concurrency::task<void> EnableNotifications(GenericAttributeProfile::GattDescriptor^ Descriptor, GenericAttributeProfile::GattCharacteristic^ Characteristic, concurrency::task<void>(MiBand3::* HandleNotifications)(GenericAttributeProfile::GattCharacteristic^ Sender, GenericAttributeProfile::GattValueChangedEventArgs^ Args));
	concurrency::task<void> EnableAuthenticationNotifications();
	concurrency::task<void> HandleAuthenticationNotifications(GenericAttributeProfile::GattCharacteristic^ Sender, GenericAttributeProfile::GattValueChangedEventArgs^ Args);
	concurrency::task<void> SendNewKey(std::vector<unsigned char> Key);
	concurrency::task<void> RequestRandomKey();
	concurrency::task<void> SendEncryptedKey(std::vector<unsigned char> Encrypted);

	concurrency::task<void> HandleHeartRateNotifications(GenericAttributeProfile::GattCharacteristic^ Sender, GenericAttributeProfile::GattValueChangedEventArgs^ Args);

	std::string FormatHeartRate(const Platform::Array<unsigned char>^ HeartRate);

	concurrency::task<void> HeartRateDefault();

	void CheckReset();

	uint8 HeartRateCounter;
	uint8 HeartRateLastCounter;

	concurrency::task<void> InWriteToServer(Platform::String^ Message);

	std::vector<unsigned char> Concat(std::vector<unsigned char> Prefix, std::vector<unsigned char> Data);

	std::vector<unsigned char> Encrypt(unsigned char* Data, unsigned char* Key);
	Platform::Guid GetGuidFromString(std::string Guid);
	Platform::Guid GetGuidFromStringBase(std::string SubGuid);

private:
	// Must be generated randomly
	std::vector<unsigned char> AuthKey{ 0x75, 0xa8, 0xd5, 0x03, 0xc8, 0x3f, 0x66, 0x44, 0x18, 0xe3, 0x96, 0x9d, 0x67, 0x17, 0x2e, 0xaa };

	BluetoothLEDevice^ Device;

	GenericAttributeProfile::GattDeviceService^ ServiceAuthentication;
	GenericAttributeProfile::GattCharacteristic^ CharacteristicAuthentication;
	GenericAttributeProfile::GattDescriptor^ DescriptorAuthentication;

	GenericAttributeProfile::GattDeviceService^ ServiceHeartRate;
	GenericAttributeProfile::GattCharacteristic^ CharacteristicHeartRateControlPoint;
	GenericAttributeProfile::GattCharacteristic^ CharacteristicHeartRateMeasurement;
	GenericAttributeProfile::GattDescriptor^ DescriptorHeartRateMeasurement;

	GenericAttributeProfile::GattDeviceService^ ServiceInmediateAlert;
	GenericAttributeProfile::GattCharacteristic^ CharacteristicAlert;

	GenericAttributeProfile::GattDeviceService^ ServiceAlertNotification;
	GenericAttributeProfile::GattCharacteristic^ CharacteristicNewAlert;
	GenericAttributeProfile::GattDescriptor^ DescriptorCharacteristicUserDescription;
	GenericAttributeProfile::GattCharacteristic^ CharacteristicAlertNotificationControlPoint;
	GenericAttributeProfile::GattDescriptor^ DescriptorClientCharacteristicConfiguration;

	concurrency::timer<int>* HeartRatePingTimer;
	concurrency::timer<int>* HeartRateCounterTimer;
	concurrency::timer<int>* HeartRateCounterDelayTimer;
	concurrency::timer<int>* VibratePingTimer;

	concurrency::event Authenticated;
	concurrency::event HeartMeasureReaded;
};