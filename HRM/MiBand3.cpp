#include "pch.h"
#include "MiBand3.h"

#include "RemoteCommunication.h"
#include <algorithm>

MiBand3::MiBand3()
{
	RC = ref new RemoteCommunication(this);

	UUIDServiceInfo = BluetoothUuidHelper::FromShortId(0xfee0);
	UUIDServiceAuthentication = BluetoothUuidHelper::FromShortId(0xfee1);
	UUIDServiceAlertNotification = BluetoothUuidHelper::FromShortId(0x1811);
	UUIDServiceInmediateAlert = BluetoothUuidHelper::FromShortId(0x1802);
	UUIDServiceHeartRate = BluetoothUuidHelper::FromShortId(0x180d);

	HeartRateCounter = 0;
	HeartRateLastCounter = 0;

	bAuthenticated = false;
}

void MiBand3::Connect(unsigned long long BluetoothAddress)
{
	InConnect(BluetoothAddress);
}

concurrency::task<void> MiBand3::InConnect(unsigned long long BluetoothAddress)
{
	co_await Initialize(co_await BluetoothLEDevice::FromBluetoothAddressAsync(BluetoothAddress));

	co_await Authentication();

	Authenticated.wait();

	bAuthenticated = true;

	co_await Run();
}

concurrency::task<void> MiBand3::Initialize(BluetoothLEDevice^ InDevice)
{
	Device = InDevice;

	ServiceAuthentication = (co_await Device->GetGattServicesForUuidAsync(UUIDServiceAuthentication))->Services->GetAt(0);
	CharacteristicAuthentication = (co_await ServiceAuthentication->GetCharacteristicsForUuidAsync(GetGuidFromStringBase("0009")))->Characteristics->GetAt(0);
	DescriptorAuthentication = (co_await CharacteristicAuthentication->GetDescriptorsForUuidAsync(BluetoothUuidHelper::FromShortId(0x2902)))->Descriptors->GetAt(0);

	ServiceHeartRate = (co_await Device->GetGattServicesForUuidAsync(UUIDServiceHeartRate))->Services->GetAt(0);
	CharacteristicHeartRateControlPoint = (co_await ServiceHeartRate->GetCharacteristicsForUuidAsync(BluetoothUuidHelper::FromShortId(0x2a39)))->Characteristics->GetAt(0);
	CharacteristicHeartRateMeasurement = (co_await ServiceHeartRate->GetCharacteristicsForUuidAsync(BluetoothUuidHelper::FromShortId(0x2a37)))->Characteristics->GetAt(0);
	DescriptorHeartRateMeasurement = (co_await CharacteristicHeartRateMeasurement->GetDescriptorsForUuidAsync(BluetoothUuidHelper::FromShortId(0x2902)))->Descriptors->GetAt(0);

	ServiceInmediateAlert = (co_await Device->GetGattServicesForUuidAsync(UUIDServiceInmediateAlert))->Services->GetAt(0);
	CharacteristicAlert = (co_await ServiceInmediateAlert->GetCharacteristicsForUuidAsync(BluetoothUuidHelper::FromShortId(0x2a06)))->Characteristics->GetAt(0);

	ServiceAlertNotification = (co_await Device->GetGattServicesForUuidAsync(UUIDServiceAlertNotification))->Services->GetAt(0);
	CharacteristicNewAlert = (co_await ServiceAlertNotification->GetCharacteristicsForUuidAsync(BluetoothUuidHelper::FromShortId(0x2a46)))->Characteristics->GetAt(0);
	CharacteristicAlertNotificationControlPoint = (co_await ServiceAlertNotification->GetCharacteristicsForUuidAsync(BluetoothUuidHelper::FromShortId(0x2a44)))->Characteristics->GetAt(0);
}

concurrency::task<void> MiBand3::Authentication()
{
	co_await EnableAuthenticationNotifications();

	// Request key. If the key stored on the device is different we send our key.
	co_await RequestRandomKey();
}

concurrency::task<void> MiBand3::Run()
{
	std::cout << "Test initialized" << std::endl;

	EnableHeartRateNotifications();

	WriteMessage((uint8*)u8"� o �)>", 9);

	std::string ConnectedString = "Connected";
	auto ConnectedWString = std::wstring(ConnectedString.begin(), ConnectedString.end());

	auto Message = ref new Platform::String(ConnectedWString.c_str());

	InWriteToServer(Message);

	concurrency::call<int> HeartRatePingCallback([this](int) {
		HeartRatePing();
		});

	HeartRatePingTimer = new concurrency::timer<int>(12000, 0, &HeartRatePingCallback, true);

	concurrency::call<int> HeartRateCounteDelayCallback([this](int) {
		std::cout << "Delayed End" << std::endl;
		HeartRateCounterDelayTimer->pause();
		HeartRateCounterTimer->start();
		});

	HeartRateCounterDelayTimer = new concurrency::timer<int>(20000, 0, &HeartRateCounteDelayCallback, true);

	concurrency::call<int> HeartRateCounterCallback([this](int) {
		CheckReset();
		});

	HeartRateCounterTimer = new concurrency::timer<int>(7000, 0, &HeartRateCounterCallback, true);

	std::cout << "Started" << std::endl;

	// Change for a wait or something
	int a;
	std::cin >> a;

	std::cout << "Test finished" << std::endl;

	co_return;
}

concurrency::task<Platform::Array<unsigned char>^> MiBand3::ReadFromCharacteristic(GenericAttributeProfile::GattCharacteristic^ Characteristic)
{
	auto Data = co_await Characteristic->ReadValueAsync();
	if (Data->Status == GenericAttributeProfile::GattCommunicationStatus::Success)
	{
		auto DataReader = Windows::Storage::Streams::DataReader::FromBuffer(Data->Value);
		auto Bytes = ref new Platform::Array<unsigned char>(DataReader->UnconsumedBufferLength);
		DataReader->ReadBytes(Bytes);
		co_return Bytes;
	}
	else
	{
		co_return ref new Platform::Array<unsigned char>(0);
	}
}

concurrency::task<void> MiBand3::WriteToCharacteristic(GenericAttributeProfile::GattCharacteristic^ Characteristic, std::vector<unsigned char> Data)
{
	auto Writer = ref new DataWriter();
	Writer->WriteBytes(ref new Platform::Array<unsigned char>(Data.data(), static_cast<unsigned int>(Data.size())));
	co_await Characteristic->WriteValueAsync(Writer->DetachBuffer());
}

concurrency::task<void> MiBand3::WriteToDescriptor(GenericAttributeProfile::GattDescriptor^ Descriptor, std::vector<unsigned char> Data)
{
	auto Writer = ref new DataWriter();
	Writer->WriteBytes(ref new Platform::Array<unsigned char>(Data.data(), static_cast<unsigned int>(Data.size())));
	co_await DescriptorAuthentication->WriteValueAsync(Writer->DetachBuffer());
}

concurrency::task<void> MiBand3::EnableNotifications(GenericAttributeProfile::GattDescriptor^ Descriptor, GenericAttributeProfile::GattCharacteristic^ Characteristic, concurrency::task<void>(MiBand3::* HandleNotifications)(GenericAttributeProfile::GattCharacteristic^ Sender, GenericAttributeProfile::GattValueChangedEventArgs^ Args))
{
	// Enable notifications
	co_await WriteToDescriptor(Descriptor, { 0x01, 0x00 });

	auto Status = co_await Characteristic->WriteClientCharacteristicConfigurationDescriptorAsync(GenericAttributeProfile::GattClientCharacteristicConfigurationDescriptorValue::Notify);
	if (Status != GenericAttributeProfile::GattCommunicationStatus::Success)
	{
		std::cout << "Enable notifications error." << std::endl;
	}
	// Handle notifications
	Characteristic->ValueChanged += ref new Windows::Foundation::TypedEventHandler<GenericAttributeProfile::GattCharacteristic^, GenericAttributeProfile::GattValueChangedEventArgs^>(
		[this, HandleNotifications](GenericAttributeProfile::GattCharacteristic^ Sender, GenericAttributeProfile::GattValueChangedEventArgs^ Args) {
			(this->*HandleNotifications)(Sender, Args);
		});
}

concurrency::task<void> MiBand3::EnableAuthenticationNotifications()
{
	co_await EnableNotifications(DescriptorAuthentication, CharacteristicAuthentication, &MiBand3::HandleAuthenticationNotifications);
}

concurrency::task<void> MiBand3::HandleAuthenticationNotifications(GenericAttributeProfile::GattCharacteristic^ Sender, GenericAttributeProfile::GattValueChangedEventArgs^ Args)
{
	auto Lenght = Args->CharacteristicValue->Length;

	if (Lenght > 2)
	{
		auto Bytes = ref new Platform::Array<unsigned char>(Lenght);
		Windows::Storage::Streams::DataReader::FromBuffer(Args->CharacteristicValue)->ReadBytes(Bytes);

		if (Bytes[0] == 0x10 && Bytes[1] == 0x01 && Bytes[2] == 0x01) // Key received
		{
			co_await RequestRandomKey(); // Request random authentication key
		}
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x02 && Bytes[2] == 0x01) // Random authentication key received
		{
			auto Encrypted = Encrypt(Bytes->Data + 3, AuthKey.data());
			co_await SendEncryptedKey(Encrypted); // Send encrypted random authentication key
		}
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x03 && Bytes[2] == 0x01) // Authentication completed
		{
			std::cout << "Success - Authentication completed." << std::endl;
			Authenticated.set();
		}
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x01 && Bytes[2] == 0x04) // Key not received
		{
			std::cout << "Failed - Key not received." << std::endl;
		}
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x02 && Bytes[2] == 0x04) // Encrypted random authentication key not received
		{
			std::cout << "Failed - Encrypted random authentication key not received." << std::endl;
		}
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x03 && Bytes[2] == 0x04) // Key encryption failed
		{
			std::cout << "Key encryption failed, sending new one." << std::endl;
			co_await SendNewKey(AuthKey);
		}
	}
}

concurrency::task<void> MiBand3::SendNewKey(std::vector<unsigned char> Key)
{
	auto Data = Concat({ 0x01, 0x00 }, Key);
	co_await WriteToCharacteristic(CharacteristicAuthentication, Data);
}

concurrency::task<void> MiBand3::RequestRandomKey()
{
	co_await WriteToCharacteristic(CharacteristicAuthentication, { 0x02, 0x00, 0x02 });
}

concurrency::task<void> MiBand3::SendEncryptedKey(std::vector<unsigned char> Encrypted)
{
	auto Data = Concat({ 0x03, 0x00 }, Encrypted);
	co_await WriteToCharacteristic(CharacteristicAuthentication, Data);
}

void MiBand3::EnableHeartRateNotifications()
{
	EnableNotifications(DescriptorHeartRateMeasurement, CharacteristicHeartRateMeasurement, &MiBand3::HandleHeartRateNotifications);
}

concurrency::task<void> MiBand3::HandleHeartRateNotifications(GenericAttributeProfile::GattCharacteristic^ Sender, GenericAttributeProfile::GattValueChangedEventArgs^ Args)
{
	auto HeartRate = ref new Platform::Array<unsigned char>(Args->CharacteristicValue->Length);
	Windows::Storage::Streams::DataReader::FromBuffer(Args->CharacteristicValue)->ReadBytes(HeartRate);

	auto HeartRateString = FormatHeartRate(HeartRate);
	auto HeartRateWString = std::wstring(HeartRateString.begin(), HeartRateString.end());

	std::cout << "Heart Rate: " << HeartRateString << std::endl;

	auto Message = ref new Platform::String(HeartRateWString.c_str());

	InWriteToServer(Message);

	++HeartRateCounter;

	if (!HeartRatePingTimer)
	{
		HeartMeasureReaded.set();
	}
	co_return;
}

std::string MiBand3::FormatHeartRate(const Platform::Array<unsigned char>^ HeartRate)
{
	unsigned short HearRateValue = (static_cast<unsigned short>(HeartRate[0]) << 8) | (0x00ff & HeartRate[1]);
	std::stringstream BatteryBuffer;
	BatteryBuffer << HearRateValue;
	return BatteryBuffer.str();
}

concurrency::task<void> MiBand3::HeartRateDefault()
{
	co_await WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x01, 0x00 }); // Disable continuous
	co_await WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x02, 0x00 }); // Disable one-shot
	co_await WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x02, 0x01 }); // Enable one-shot

	HeartMeasureReaded.wait();
}

void MiBand3::CheckReset()
{
	std::cout << "Check Reset" << std::endl;
	if (HeartRateCounter == HeartRateLastCounter)
	{
		std::cout << "Check Reset Start" << std::endl;

		HeartRateStop();
		concurrency::wait(2000);
		HeartRateStart();
	}
	else
	{
		HeartRateLastCounter = HeartRateCounter;
	}
}

void MiBand3::HeartRateStart()
{
	RC->StartClient();

	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x02, 0x00 }); // Disable one-shot
	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x01, 0x00 }); // Disable continuous
	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x01, 0x01 }); // Enable continuous

	if (HeartRatePingTimer)
	{
		HeartRatePingTimer->start();
	}
	if (HeartRateCounterDelayTimer)
	{
		HeartRateCounterDelayTimer->start();
	}
}

void MiBand3::HeartRatePing()
{
	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x16 }); 
}

void MiBand3::HeartRateStop()
{
	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x01, 0x00 }); // Disable continuous

	if (HeartRateCounterTimer)
	{
		HeartRateCounterTimer->pause();
	}
	if (HeartRateCounterDelayTimer)
	{
		HeartRateCounterDelayTimer->pause();
	}
	if (HeartRatePingTimer)
	{
		HeartRatePingTimer->pause();
	}
}

void MiBand3::Vibrate()
{
	WriteToCharacteristic(CharacteristicAlert, { 0x03 });
}

void MiBand3::Vibrate(uint16 Milliseconds)
{
	WriteToCharacteristic(CharacteristicAlert, { 0xff, (unsigned char)(Milliseconds & 0xff), (unsigned char)((Milliseconds >> 8) & 0xff), 0x00, 0x00, 0x01 });
}

void MiBand3::WriteMessage(uint8* Message, uint32 MessageSize)
{
	std::vector<unsigned char> Data{ 0x01, 0x01 };
	for (uint32 i = 0; i < MessageSize; ++i)
	{
		Data.push_back(Message[i]);
	}

	WriteToCharacteristic(CharacteristicNewAlert, Data);
}

void MiBand3::WriteToServer(Platform::String^ Message)
{
	InWriteToServer(Message);
}

concurrency::task<void> MiBand3::InWriteToServer(Platform::String^ Message)
{
	if (RC->bClientConnected)
	{
		// Send a request to the HRM server.
		auto Writer = ref new Windows::Storage::Streams::DataWriter(RC->ClientSocket->OutputStream);

		Writer->WriteString(Message);

		co_await Writer->StoreAsync();

		co_await Writer->FlushAsync();

		Writer->DetachStream();
	}
	co_return;
}

std::vector<unsigned char> MiBand3::Concat(std::vector<unsigned char> Prefix, std::vector<unsigned char> Data)
{
	std::vector<unsigned char> ConcatVector;
	ConcatVector.reserve(Prefix.size() + Data.size()); // preallocate memory
	ConcatVector.insert(ConcatVector.end(), Prefix.begin(), Prefix.end());
	ConcatVector.insert(ConcatVector.end(), Data.begin(), Data.end());
	return ConcatVector;
}

// Encrypt using aes ecb 128 no padding
std::vector<unsigned char> MiBand3::Encrypt(unsigned char* Data, unsigned char* Key)
{
	int OutLenght;
	unsigned char Encrypted[16];
	EVP_CIPHER_CTX* Context = EVP_CIPHER_CTX_new();
	EVP_EncryptInit_ex(Context, EVP_aes_128_ecb(), NULL, Key, NULL);
	EVP_CIPHER_CTX_set_padding(Context, 0);
	EVP_EncryptUpdate(Context, Encrypted, &OutLenght, Data, 16);
	EVP_EncryptFinal_ex(Context, Encrypted + OutLenght, &OutLenght);
	EVP_CIPHER_CTX_free(Context);
	return std::vector<unsigned char>(Encrypted, Encrypted + 16);
}

// Example 00000009-0000-3512-2118-0009af100700
Platform::Guid MiBand3::GetGuidFromString(std::string Guid)
{
	unsigned int a;
	unsigned short b;
	unsigned short c;
	unsigned short d;
	unsigned short e;
	unsigned short f;
	unsigned short g;
	unsigned short h;
	unsigned short i;
	unsigned short j;
	unsigned short k;

	std::stringstream ss;
	ss << std::hex;
	ss << Guid.substr(0, 8);
	ss >> a;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(9, 4);
	ss >> b;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(14, 4);
	ss >> c;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(19, 2);
	ss >> d;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(21, 2);
	ss >> e;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(24, 2);
	ss >> f;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(26, 2);
	ss >> g;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(28, 2);
	ss >> h;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(30, 2);
	ss >> i;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(32, 2);
	ss >> j;
	ss.clear();
	ss.str(std::string());
	ss << Guid.substr(34, 2);
	ss >> k;
	return Platform::Guid(a, b, c, (unsigned char)d, (unsigned char)e, (unsigned char)f, (unsigned char)g, (unsigned char)h, (unsigned char)i, (unsigned char)j, (unsigned char)k);
}

Platform::Guid MiBand3::GetGuidFromStringBase(std::string SubGuid)
{
	return GetGuidFromString("0000" + SubGuid + "-0000-3512-2118-0009af100700");
}