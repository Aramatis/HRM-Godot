#include "pch.h"
#include "RemoteCommunication.h"

#include "MiBand3.h"

#include "intrin.h"
#include <algorithm>
#include <comdef.h>

int CharToInt(char Char)
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

unsigned long long FormatBluetoothAddressInverse(Platform::Array<uint8>^ BluetoothAddress)
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

RemoteCommunication::RemoteCommunication(MiBand3^ InMiBand) : MiBand(InMiBand)
{
	// Create the StreamSocket and establish a connection to the HRM server.
	ClientSocket = ref new StreamSocket();

	bClientConnected = false;
	bServerRunning = false;

	bWaitingClientConnection = false;
}

void RemoteCommunication::StartClient()
{

	if (!bClientConnected && !bWaitingClientConnection)
	{
		if (!ClientSocket)
		{
			ClientSocket = ref new StreamSocket();
		}

		bWaitingClientConnection = true;
		// The server hostname that we will be establishing a connection to. In this example, the server and client are in the same process.
		auto InHostName = ref new Windows::Networking::HostName(RCHostName);

		//while (!bClientConnected && Tries-- > 0)
		//{
		concurrency::create_task(ClientSocket->ConnectAsync(InHostName, ClientPort)).then([this](concurrency::task<void> PreviousTask) {
			try
			{
				PreviousTask.get();
				std::cout << "Client connected" << std::endl;
				bWaitingClientConnection = false;
				bClientConnected = true;
			}
			catch (Platform::Exception ^ Ex)
			{
				bClientConnected = false;
				std::cout << "The client couldn't connect with the server" << std::endl;

				SocketErrorStatus WebErrorStatus = SocketError::GetStatus(Ex->HResult);
				std::cout << (WebErrorStatus.ToString() != L"Unknown" ? WebErrorStatus.ToString() : Ex->Message)->Data() << std::endl;
			}
			});
	}
}

void RemoteCommunication::StopClient()
{
	if (bClientConnected)
	{
		bClientConnected = false;
		delete ClientSocket;
		ClientSocket = nullptr;
	}
}

void RemoteCommunication::StartServer()
{
	ServerSocket = ref new StreamSocketListener();
	ServerSocket->ConnectionReceived += ref new Windows::Foundation::TypedEventHandler<StreamSocketListener^, StreamSocketListenerConnectionReceivedEventArgs^>(this, &RemoteCommunication::OnConnection);

	concurrency::create_task(ServerSocket->BindServiceNameAsync(ServerPort)).then([this](concurrency::task<void> PreviousTask) {
		try
		{
			// Try getting an exception.
			PreviousTask.get();
			std::cout << "Sever started" << std::endl;
		}
		catch (Platform::Exception ^ Ex)
		{
			std::cout << "The server could't start" << std::endl;

			SocketErrorStatus WebErrorStatus = SocketError::GetStatus(Ex->HResult);
			std::cout << (WebErrorStatus.ToString() != L"Unknown" ? WebErrorStatus.ToString() : Ex->Message)->Data() << std::endl;
		}
		});
}

void RemoteCommunication::OnConnection(StreamSocketListener^ Listener, StreamSocketListenerConnectionReceivedEventArgs^ Args)
{
	auto Reader = ref new DataReader(Args->Socket->InputStream);
	Reader->UnicodeEncoding = UnicodeEncoding::Utf8;
	Reader->ByteOrder = ByteOrder::LittleEndian;

	// Start a receive loop.
	ReceiveStringLoop(Reader, Args->Socket);
}

void RemoteCommunication::ReceiveStringLoop(DataReader^ Reader, StreamSocket^ Socket)
{
	// Fist read the instruction id to execute.
	concurrency::create_task(Reader->LoadAsync(sizeof(byte))).then([this, Reader, Socket](unsigned int Size) {
		if (Size < sizeof(byte))
		{
			// The underlying socket was closed before we were able to read the whole data.
			concurrency::cancel_current_task();
		}

		byte Id = Reader->ReadByte();

		if (Id == 0)
		{
			return concurrency::create_task(Reader->LoadAsync(sizeof(bool))).then([this, Reader](unsigned int Size) {
				if (Size < sizeof(bool))
				{
					// The underlying socket was closed before we were able to read the whole data.
					concurrency::cancel_current_task();
				}

				bool bStart = Reader->ReadBoolean();

				if (bStart)
				{
					StartClient();
				}
				else
				{
					StopClient();
				}
				});
		}
		else if (Id == 1)
		{
			return concurrency::create_task(Reader->LoadAsync(sizeof(uint32))).then([this, Reader](unsigned int Size) {
				if (Size < sizeof(uint32))
				{
					// The underlying socket was closed before we were able to read the whole data.
					concurrency::cancel_current_task();
				}

				//uint32 MessageSize = _byteswap_ulong(Reader->ReadUInt32());
				uint32 MessageSize = Reader->ReadUInt32();

				return concurrency::create_task(Reader->LoadAsync(MessageSize)).then([this, Reader, MessageSize](unsigned int Size) {
					if (Size < MessageSize)
					{
						// The underlying socket was closed before we were able to read the whole data.
						concurrency::cancel_current_task();
					}

					Platform::Array<uint8>^ Message = ref new Platform::Array<uint8>(Size);

					Reader->ReadBytes(Message);

					auto Address = FormatBluetoothAddressInverse(Message);

					MiBand->Connect(Address);
					});
				});
		}
		else if (MiBand->bAuthenticated)
		{
			if (Id == 2)
			{
				return concurrency::create_task(Reader->LoadAsync(sizeof(uint32))).then([this, Reader](unsigned int Size) {
					if (Size < sizeof(uint32))
					{
						// The underlying socket was closed before we were able to read the whole data.
						concurrency::cancel_current_task();
					}

					//uint32 MessageSize = _byteswap_ulong(Reader->ReadUInt32());
					uint32 MessageSize = Reader->ReadUInt32();

					return concurrency::create_task(Reader->LoadAsync(MessageSize)).then([this, Reader, MessageSize](unsigned int Size) {
						if (Size < MessageSize)
						{
							// The underlying socket was closed before we were able to read the whole data.
							concurrency::cancel_current_task();
						}

						Platform::Array<uint8>^ Message = ref new Platform::Array<uint8>(Size);

						Reader->ReadBytes(Message);

						MiBand->WriteMessage(Message->Data, Size);
						});
					});
			}
			else if (Id == 3)
			{
				return concurrency::create_task(Reader->LoadAsync(sizeof(bool))).then([this, Reader](unsigned int Size) {
					if (Size < sizeof(bool))
					{
						// The underlying socket was closed before we were able to read the whole data.
						concurrency::cancel_current_task();
					}

					bool bStart = Reader->ReadBoolean();

					if (bStart)
					{
						MiBand->HeartRateStart();
					}
					else
					{
						MiBand->HeartRateStop();
						StopClient();
					}
					});
			}
			else if (Id == 4)
			{
				return concurrency::create_task(Reader->LoadAsync(sizeof(uint16))).then([this, Reader](unsigned int Size) {
					if (Size < sizeof(uint16))
					{
						// The underlying socket was closed before we were able to read the whole data.
						concurrency::cancel_current_task();
					}

					uint16 Seconds = Reader->ReadUInt16();

					MiBand->Vibrate(Seconds);
					});
			}
			else if (Id == 5)
			{
				MiBand->Vibrate();
			}
		}

		return concurrency::create_task([] {});
		})
		.then([this, Reader, Socket](concurrency::task<void> PreviousTask) {
			try
			{
				PreviousTask.get();

				// Loop
				ReceiveStringLoop(Reader, Socket);
			}
			catch (Platform::Exception ^ Ex)
			{
				std::cout << "Read stream failed with error: " << Ex->Message->Data() << std::endl;
				// Explicitly close the socket.
				delete Socket;
			}
			catch (concurrency::task_canceled&)
			{
				// Do not print anything here - this will usually happen because user closed the client socket.

				// Explicitly close the socket.
				delete Socket;
			}
			});
}