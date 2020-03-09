#include "pch.h"
#include "MiBand3.h"

#include "RemoteCommunication.h"
#include <algorithm>

// Class that represents a MiBand 3 object and handles all communication with the
// MiBand 3 peripheral. It doesn't need to have the Bluetooth address when creating
// the object and automatically creates a RemoteCommunication object for remote
// control.

MiBand3::MiBand3()
{
	// Create a new RemoteCommunicaton object
	RC = ref new RemoteCommunication(this);
	// Set variables
	UUIDServiceInfo = BluetoothUuidHelper::FromShortId(0xfee0);
	UUIDServiceAuthentication = BluetoothUuidHelper::FromShortId(0xfee1);
	UUIDServiceAlertNotification = BluetoothUuidHelper::FromShortId(0x1811);
	UUIDServiceInmediateAlert = BluetoothUuidHelper::FromShortId(0x1802);
	UUIDServiceHeartRate = BluetoothUuidHelper::FromShortId(0x180d);
	// Start counters to check if the notifications stop arriving
	HeartRateCounter = 0;
	HeartRateLastCounter = 0;

	bAuthenticated = false;
}

// Connects to a MiBand 3 peripheral in the given bluetooth address. If await is 
// true, it waits for the asyncronous call to return.
void MiBand3::Connect(unsigned long long BluetoothAddress)
{
	InConnect(BluetoothAddress);
}

// Asyncronously connect to the MiBand 3 peripheral
concurrency::task<void> MiBand3::InConnect(unsigned long long BluetoothAddress)
{
	// Initializes the connection with the peripheral
	std::wcout << "Before Init" << std::endl;
	co_await Initialize(co_await BluetoothLEDevice::FromBluetoothAddressAsync(BluetoothAddress));
	// Authenticates the connection
	std::wcout << "Before Auth" << std::endl;
	co_await Authentication();
	Authenticated.wait();
	std::wcout << "After Auth" << std::endl;
	bAuthenticated = true;
	Authenticated.reset();
	Connected.set();
}

// Standard HRM behaviour
concurrency::task<void> MiBand3::RunHRM()
{
	// Waits for authentication ending
	if (!this->bAuthenticated) {
		std::wcout << "Waiting auth" << std::endl;
		Connected.wait();
	}
	Connected.reset();

	std::cout << "Running MiBand3 HRM" << std::endl;

	// Write a notification to check the connection
	WriteMessage((uint8*)u8"� o �)>", 9);

	// Sends a ping to keep alive the Heart Rate Monitoring
	concurrency::call<int> HeartRatePingCallback([this](int) {
		HeartRatePing();
		});

	// Sets a timer to send the ping every 12 seconds
	HeartRatePingTimer = new concurrency::timer<int>(
		12000, 0, &HeartRatePingCallback, true);

	// Creates a timer to delay start on the checkReset timer
	concurrency::call<int> HeartRateCounterDelayCallback([this](int) {
		HeartRateCounterDelayTimer->pause();
		HeartRateCounterTimer->start();
		});
	HeartRateCounterDelayTimer = new concurrency::timer<int>(
		20000, 0, &HeartRateCounterDelayCallback, true);

	// CheckReset timer, checks periodically if the band is sending HRM notifications
	concurrency::call<int> HeartRateCounterCallback([this](int) {
		CheckReset();
		});

	HeartRateCounterTimer = new concurrency::timer<int>(
		7000, 0, &HeartRateCounterCallback, true);

	// Start timers
	HeartRatePingTimer->start();
	HeartRateCounterDelayTimer->start();

	std::cout << "Started stardard HRM behaviour" << std::endl;

	// Wait for event to stop
	MonitoringStop.wait();
	MonitoringStop.reset();

	std::cout << "After wait HRM" << std::endl;

	co_return;
}

// Gets the descriptors for the different services and characteristics of a MiBand 3
// peripheral.
concurrency::task<void> MiBand3::Initialize(BluetoothLEDevice^ InDevice)
{
	Device = InDevice;
	// Authentication
	ServiceAuthentication =
		(co_await Device->GetGattServicesForUuidAsync(UUIDServiceAuthentication))
		->Services->GetAt(0);
	CharacteristicAuthentication =
		(co_await ServiceAuthentication->
			GetCharacteristicsForUuidAsync(GetGuidFromStringBase("0009")))
		->Characteristics->GetAt(0);
	DescriptorAuthentication =
		(co_await CharacteristicAuthentication->
			GetDescriptorsForUuidAsync(BluetoothUuidHelper::FromShortId(0x2902)))
		->Descriptors->GetAt(0);
	// Heart Rate Monitoring
	ServiceHeartRate =
		(co_await Device->GetGattServicesForUuidAsync(UUIDServiceHeartRate))
		->Services->GetAt(0);
	CharacteristicHeartRateControlPoint =
		(co_await ServiceHeartRate->
			GetCharacteristicsForUuidAsync(BluetoothUuidHelper::FromShortId(0x2a39)))
		->Characteristics->GetAt(0);
	CharacteristicHeartRateMeasurement =
		(co_await ServiceHeartRate->
			GetCharacteristicsForUuidAsync(
				BluetoothUuidHelper::FromShortId(0x2a37)))
		->Characteristics->GetAt(0);
	DescriptorHeartRateMeasurement =
		(co_await CharacteristicHeartRateMeasurement->GetDescriptorsForUuidAsync(
			BluetoothUuidHelper::FromShortId(0x2902)))->Descriptors->GetAt(0);
	// Messages
	ServiceInmediateAlert =
		(co_await Device->GetGattServicesForUuidAsync(UUIDServiceInmediateAlert))
		->Services->GetAt(0);
	CharacteristicAlert =
		(co_await ServiceInmediateAlert->GetCharacteristicsForUuidAsync(
			BluetoothUuidHelper::FromShortId(0x2a06)))->Characteristics->GetAt(0);

	ServiceAlertNotification =
		(co_await Device->GetGattServicesForUuidAsync(UUIDServiceAlertNotification))
		->Services->GetAt(0);
	CharacteristicNewAlert =
		(co_await ServiceAlertNotification->GetCharacteristicsForUuidAsync(
			BluetoothUuidHelper::FromShortId(0x2a46)))->Characteristics->GetAt(0);
	CharacteristicAlertNotificationControlPoint =
		(co_await ServiceAlertNotification->GetCharacteristicsForUuidAsync(
			BluetoothUuidHelper::FromShortId(0x2a44)))->Characteristics->GetAt(0);
}

// Authenticates with the MiBand 3.
concurrency::task<void> MiBand3::Authentication()
{
	// Enables the notifications about authentification and handles their responses
	co_await EnableAuthenticationNotifications();

	// Request key. If the key stored on the device is different we send our key
	co_await RequestRandomKey();
}

// Reads the value from a given characteristic of the MiBand 3 periferal.
concurrency::task<Platform::Array<unsigned char>^> MiBand3::ReadFromCharacteristic(
	GenericAttributeProfile::GattCharacteristic^ Characteristic)
{
	// Attemps a reading
	auto Data = co_await Characteristic->ReadValueAsync();
	// If the reading succeeds, decode the reading and returns it as a byte array
	if (Data->Status == GenericAttributeProfile::GattCommunicationStatus::Success)
	{
		auto DataReader = Windows::Storage::Streams::DataReader::FromBuffer(
			Data->Value);
		auto Bytes = ref new Platform::Array<unsigned char>(
			DataReader->UnconsumedBufferLength);
		DataReader->ReadBytes(Bytes);
		co_return Bytes;
	}
	// If the reading fails, return an empty array
	else
	{
		co_return ref new Platform::Array<unsigned char>(0);
	}
}

// Writes to a given characteristic
concurrency::task<void> MiBand3::WriteToCharacteristic(
	GenericAttributeProfile::GattCharacteristic^ Characteristic,
	std::vector<unsigned char> Data)
{
	// Create new writer and load the data
	auto Writer = ref new DataWriter();
	Writer->WriteBytes(ref new Platform::Array<unsigned char>(
		Data.data(), static_cast<unsigned int>(Data.size())));
	// Write the data asyncronously
	co_await Characteristic->WriteValueAsync(Writer->DetachBuffer());
}

// Writes to a given descriptor
concurrency::task<void> MiBand3::WriteToDescriptor(
	GenericAttributeProfile::GattDescriptor^ Descriptor,
	std::vector<unsigned char> Data)
{
	// Create new writer and load the data
	auto Writer = ref new DataWriter();
	Writer->WriteBytes(ref new Platform::Array<unsigned char>(
		Data.data(), static_cast<unsigned int>(Data.size())));
	// Write the data asyncronously
	co_await DescriptorAuthentication->WriteValueAsync(Writer->DetachBuffer());
}

// Enables the notifications from a given descriptor and characteristic, and sets a
// task to handle them on arrival.
concurrency::task<void> MiBand3::EnableNotifications(
	GenericAttributeProfile::GattDescriptor^ Descriptor,
	GenericAttributeProfile::GattCharacteristic^ Characteristic,
	concurrency::task<void>(MiBand3::* HandleNotifications)
	(GenericAttributeProfile::GattCharacteristic^ Sender,
		GenericAttributeProfile::GattValueChangedEventArgs^ Args))
{
	// Enable notifications
	co_await WriteToDescriptor(Descriptor, { 0x01, 0x00 });

	// Set the characteristic on Notify
	auto Status = co_await Characteristic->
		WriteClientCharacteristicConfigurationDescriptorAsync(
			GenericAttributeProfile::
			GattClientCharacteristicConfigurationDescriptorValue::Notify);
	// Logs an error
	if (Status != GenericAttributeProfile::GattCommunicationStatus::Success)
	{
		std::cout << "Enable notifications error." << std::endl;
	}
	// Sets the given task to handle the notifications, along with any other set 
	// before
	Characteristic->ValueChanged += ref new Windows::Foundation::
		TypedEventHandler<GenericAttributeProfile::GattCharacteristic^,
		GenericAttributeProfile::GattValueChangedEventArgs^>(
			[this, HandleNotifications](
				GenericAttributeProfile::GattCharacteristic^ Sender,
				GenericAttributeProfile::GattValueChangedEventArgs^ Args) {
					(this->*HandleNotifications)(Sender, Args);
			});
}

// Enable the notification from authentications and sets their handler
concurrency::task<void> MiBand3::EnableAuthenticationNotifications()
{
	co_await EnableNotifications(
		DescriptorAuthentication, CharacteristicAuthentication,
		&MiBand3::HandleAuthenticationNotifications);
}

// Handles the arrival of the notifications for authentication and their responses, 
// to link the device with the pc.
concurrency::task<void> MiBand3::HandleAuthenticationNotifications(
	GenericAttributeProfile::GattCharacteristic^ Sender,
	GenericAttributeProfile::GattValueChangedEventArgs^ Args)
{

	auto Lenght = Args->CharacteristicValue->Length;

	if (Lenght > 2)
	{
		auto Bytes = ref new Platform::Array<unsigned char>(Lenght);
		Windows::Storage::Streams::DataReader::FromBuffer(
			Args->CharacteristicValue)->ReadBytes(Bytes);
		// If the key is received
		if (Bytes[0] == 0x10 && Bytes[1] == 0x01 && Bytes[2] == 0x01)
		{
			// Request random authentication key
			co_await RequestRandomKey();
		}
		// If the random key is received
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x02 && Bytes[2] == 0x01)
		{
			// Send encrypted random authentication key
			auto Encrypted = Encrypt(Bytes->Data + 3, AuthKey.data());
			co_await SendEncryptedKey(Encrypted);
		}
		// If the authentication is completed
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x03 && Bytes[2] == 0x01)
		{
			// Set internal status as authenticated
			std::cout << "Success - Authentication completed." << std::endl;
			Authenticated.set();
		}
		// If the key isn't received
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x01 && Bytes[2] == 0x04)
		{
			std::cout << "Failed - Key not received." << std::endl;
		}
		// If the encrypted random authentication key isn't received
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x02 && Bytes[2] == 0x04)
		{
			std::cout << "Failed - Encrypted random authentication key not received."
				<< std::endl;
		}
		// If the key encryption fails
		else if (Bytes[0] == 0x10 && Bytes[1] == 0x03 && Bytes[2] == 0x04)
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
	co_await WriteToCharacteristic(CharacteristicAuthentication,
		{ 0x02, 0x00, 0x02 });
}

concurrency::task<void> MiBand3::SendEncryptedKey(
	std::vector<unsigned char> Encrypted)
{
	auto Data = Concat({ 0x03, 0x00 }, Encrypted);
	co_await WriteToCharacteristic(CharacteristicAuthentication, Data);
}

void MiBand3::EnableHeartRateNotifications()
{
	EnableNotifications(DescriptorHeartRateMeasurement,
		CharacteristicHeartRateMeasurement, &MiBand3::HandleHeartRateNotifications);
}

concurrency::task<void> MiBand3::HandleHeartRateNotifications(
	GenericAttributeProfile::GattCharacteristic^ Sender,
	GenericAttributeProfile::GattValueChangedEventArgs^ Args)
{
	auto HeartRate = ref new Platform::Array<unsigned char>(
		Args->CharacteristicValue->Length);
	Windows::Storage::Streams::DataReader::FromBuffer(
		Args->CharacteristicValue)->ReadBytes(HeartRate);

	auto HeartRateString = FormatHeartRate(HeartRate);

	std::cout << "Heart Rate: " << HeartRateString << std::endl;

	auto HeartRateWString = std::wstring(HeartRateString.begin(),
		HeartRateString.end());

	auto Message = ref new Platform::String(HeartRateWString.c_str());

	InWriteToServer(Message, true);

	++HeartRateCounter;

	if (!HeartRatePingTimer)
	{
		HeartMeasureReaded.set();
	}
	co_return;
}

std::string MiBand3::FormatHeartRate(const Platform::Array<unsigned char>^ HeartRate)
{
	unsigned short HearRateValue = (static_cast<unsigned short>(HeartRate[0]) << 8)
		| (0x00ff & HeartRate[1]);
	std::stringstream BatteryBuffer;
	BatteryBuffer << HearRateValue;
	return BatteryBuffer.str();
}

concurrency::task<void> MiBand3::HeartRateDefault()
{
	// Disable continuous
	co_await WriteToCharacteristic(CharacteristicHeartRateControlPoint,
		{ 0x15, 0x01, 0x00 });
	// Disable one-shot
	co_await WriteToCharacteristic(CharacteristicHeartRateControlPoint,
		{ 0x15, 0x02, 0x00 });
	// Enable one-shot
	co_await WriteToCharacteristic(CharacteristicHeartRateControlPoint,
		{ 0x15, 0x02, 0x01 });

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
	// Enable notifications
	EnableHeartRateNotifications();

	// Disable one-shot
	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x02, 0x00 });
	// Disable continuous
	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x01, 0x00 });
	// Enable continuous
	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x01, 0x01 });

	if (HeartRatePingTimer)
	{
		HeartRatePingTimer->start();
	}
	if (HeartRateCounterDelayTimer)
	{
		HeartRateCounterDelayTimer->start();
	}
	// Runs monitoring
	RunHRM();

}

void MiBand3::HeartRatePing()
{
	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x16 });
}

void MiBand3::HeartRateStop()
{
	// Disable continuous
	WriteToCharacteristic(CharacteristicHeartRateControlPoint, { 0x15, 0x01, 0x00 });

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

	MonitoringStop.set();
}

void MiBand3::Vibrate()
{
	WriteToCharacteristic(CharacteristicAlert, { 0x03 });
}

void MiBand3::Vibrate(uint16 Milliseconds)
{
	WriteToCharacteristic(CharacteristicAlert,
		{ 0xff, (unsigned char)(Milliseconds & 0xff),
		(unsigned char)((Milliseconds >> 8) & 0xff), 0x00, 0x00, 0x01 });
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

void MiBand3::WriteToServer(Platform::String^ Message, bool pad)
{
	InWriteToServer(Message, pad);
}

concurrency::task<void> MiBand3::InWriteToServer(
	Platform::String^ Message, bool pad)
{
	// Ignore if there's no client
	if (RC->bClientConnected)
	{
		// Send a request to the HRM server.
		auto Writer = ref new Windows::Storage::Streams::DataWriter(
			RC->ClientSocket->OutputStream);

		Writer->WriteString(Message);

		if (pad) {
			Writer->WriteByte('\0');
		}

		co_await Writer->StoreAsync();

		co_await Writer->FlushAsync();

		Writer->DetachStream();
	}
	co_return;
}

std::vector<unsigned char> MiBand3::Concat(
	std::vector<unsigned char> Prefix, std::vector<unsigned char> Data)
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
