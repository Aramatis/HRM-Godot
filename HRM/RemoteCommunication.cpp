#include "pch.h"
#include "RemoteCommunication.h"
#include "MiBand3.h"
#include "intrin.h"
#include <algorithm>
#include <comdef.h>
#include "BlthUtil.h"
#include <iostream>


using namespace BluetoothUtilities;

// Class that handles the remote communication between this HRM module and
// external servers and connectors. Requires a MiBand3 reference, but no extra
// methods invoked after initialization. It's automtically created when creating
// a MiBand3 object.
RemoteCommunication::RemoteCommunication(MiBand3^ InMiBand) : MiBand(InMiBand)
{
	// Create a StreamSocket to establish a connection to the external HRM
	// server.
	ClientSocket = ref new StreamSocket();
	// Initialize variables
	bClientConnected = false;
	bServerRunning = false;
	bWaitingClientConnection = false;
	// Start server to receive incoming messages
	StartServer();
}

// Establishes a connection to the external HRM server. It NEEDS the external
// server to be already listening on the connection port.
void RemoteCommunication::StartClient(int tries)
{
	// When there's no external connection
	if (!bClientConnected && !bWaitingClientConnection)
	{
		if (!ClientSocket)
		{
			ClientSocket = ref new StreamSocket();
		}
		bWaitingClientConnection = true;

		// Hostname of the external HRM server.
		auto InHostName = ref new Windows::Networking::HostName(RCHostName);
		// Attempt to connect to the server through the ClientPort port.
		concurrency::create_task(ClientSocket->ConnectAsync(InHostName,
			ClientPort))
			.then([this, tries](concurrency::task<void> PreviousTask) {
			try
			{
				PreviousTask.get();
				std::wcout << "Client connected" << std::endl;
				bWaitingClientConnection = false;
				bClientConnected = true;
			}
			catch (Platform::Exception ^ Ex)
			{
				bClientConnected = false;
				std::cout << "The client couldn't connect with the server"
					<< std::endl;
				std::wcout << Ex->ToString()->Begin() << std::endl;

				SocketErrorStatus WebErrorStatus =
					SocketError::GetStatus(Ex->HResult);
				std::cout << (WebErrorStatus.ToString()
					!= L"Unknown" ? WebErrorStatus.ToString() :
					Ex->Message)->Data() << std::endl;

				// Retry connection, if so indicated
				if (tries > 0) {
					std::cout << "Retrying connection" << std::endl;
					StartClient(tries - 1);
				}
			}
				});
	}
}

// Stops an established connection to an external HRM server. If there's no
// active connection it's just ignored.
void RemoteCommunication::StopClient()
{
	if (bClientConnected)
	{
		bClientConnected = false;
		// Due to C++ magic, this automatically closes the socket
		delete ClientSocket;
		ClientSocket = nullptr;
	}
}


// Starts a server to receive connections from an external connector. It's
// called automatically on this object's creation.
void RemoteCommunication::StartServer(int tries)
{
	// Create new listener for a socket
	ServerSocket = ref new StreamSocketListener();
	// Bind the receiving of a message to the OnConnection function
	ServerSocket->ConnectionReceived += ref new Windows::Foundation::
		TypedEventHandler<StreamSocketListener^,
		StreamSocketListenerConnectionReceivedEventArgs^>
		(this, &RemoteCommunication::OnConnection);
	// Create asyncronous task to establish the server on the given port
	concurrency::create_task(ServerSocket->BindServiceNameAsync(ServerPort))
		.then([this, tries](concurrency::task<void> PreviousTask) {
		try
		{
			// Try getting an exception.
			PreviousTask.get();
			std::cout << "Server started" << std::endl;
		}
		catch (Platform::Exception ^ Ex)
		{
			std::cout << "The server couldn't start" << std::endl;

			SocketErrorStatus WebErrorStatus =
				SocketError::GetStatus(Ex->HResult);
			std::cout << (WebErrorStatus.ToString() != L"Unknown" ?
				WebErrorStatus.ToString() : Ex->Message)->Data() << std::endl;

			// Retry server startup, if so indicated
			if (tries > 0) {
				std::cout << "Retrying server startup" << std::endl;
				StartServer(tries - 1);
			}
		}
			});
}

// Handles a new connection to the mounted server.
void RemoteCommunication::OnConnection(StreamSocketListener^ Listener,
	StreamSocketListenerConnectionReceivedEventArgs^ Args)
{
	// Create and initialize a DataReader
	auto Reader = ref new DataReader(Args->Socket->InputStream);
	Reader->UnicodeEncoding = UnicodeEncoding::Utf8;
	Reader->ByteOrder = ByteOrder::LittleEndian;

	// Start a receive loop, reading all messages arriving to the DataReader.
	ReceiveStringLoop(Reader, Args->Socket);
}

// Server message handling loop. A properly formatted message indicates its ID
// on the first byte and on the remaining ones gives the payload in accordance
// to its ID.
void RemoteCommunication::ReceiveStringLoop(DataReader^ Reader,
	StreamSocket^ Socket)
{
	// Read the first byte to retrieve the instruction ID
	concurrency::create_task(Reader->LoadAsync(sizeof(byte))).then(
		[this, Reader, Socket](unsigned int Size) {
			// If the size loaded was smaller than the size of a byte the socket
			// was closed before reading the whole data.
			if (Size < sizeof(byte))
			{
				concurrency::cancel_current_task();
			}

			// Save the instruction ID
			byte Id = Reader->ReadByte();

			std::wcout << "Received instruction, ID = " << Id << std::endl;

			// ID = 0 is an instruction to start (true) or stop (false) the
			// client. 
			if (Id == 0)
			{
				return concurrency::create_task(Reader->LoadAsync(sizeof(bool)))
					.then([this, Reader](unsigned int Size) {
					// If the size loaded was smaller than the size of a bool
					// the socket was closed before reading the whole data.
					if (Size < sizeof(bool))
					{
						concurrency::cancel_current_task();
					}
					// Read the instruction
					bool bStart = Reader->ReadBoolean();
					// Start or stop the client
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
			// ID = 1 is an instruction to scan for peripherals for 20 seconds
			// and send the addresses of the ones found. It carries no payload.
			if (Id == 1)
			{
				// Start the scanner with the current MiBand3
				scan(MiBand);
			}
			// ID = 2 is an instruction to connect to a MiBand3 in the given
			// address.
			else if (Id == 2)
			{
				return concurrency::create_task(
					Reader->LoadAsync(sizeof(uint32)))
					.then([this, Reader](unsigned int Size) {
					// If the size loaded was smaller than the size of a uint32
					// the socket was closed before reading the whole size data.
					if (Size < sizeof(uint32))
					{
						concurrency::cancel_current_task();
					}

					// Read the size (in bytes) of the address
					uint32 MessageSize = Reader->ReadUInt32();

					return concurrency::create_task(
						Reader->LoadAsync(MessageSize))
						.then([this, Reader, MessageSize](unsigned int Size) {
						// If the size loaded was smaller than the size
						// indicated the socket was closed before reading the
						// whole message data.
						if (Size < MessageSize)
						{
							concurrency::cancel_current_task();
						}

						// Allocate space, read the address and format it
						Platform::Array<uint8>^ Message =
							ref new Platform::Array<uint8>(Size);
						Reader->ReadBytes(Message);
						auto Address = FormatBluetoothAddressInverse(Message);

						// Connect to the given address
						MiBand->Connect(Address);
							});
						});
			}
			// All the following IDs require a MiBand3 connected and
			// authenticated.
			else if (MiBand->bAuthenticated)
			{
				// ID = 3 is an instruction to write a message to the connected
				// MiBand3.
				if (Id == 3)
				{
					return concurrency::create_task(
						Reader->LoadAsync(sizeof(uint32)))
						.then([this, Reader](unsigned int Size) {
						// If the size loaded was smaller than the size of a
						// uint32 the socket was closed before reading the whole
						// size data.
						if (Size < sizeof(uint32))
						{
							concurrency::cancel_current_task();
						}

						uint32 MessageSize = Reader->ReadUInt32();

						return concurrency::create_task(
							Reader->LoadAsync(MessageSize))
							.then([this, Reader, MessageSize](unsigned int Size)
								{
									// If the size loaded was smaller than the
									// size indicated the socket was closed
									// before reading the whole message data.
									if (Size < MessageSize)
									{
										concurrency::cancel_current_task();
									}

									// Allocate space and read the message
									Platform::Array<uint8>^ Message =
										ref new Platform::Array<uint8>(Size);
									Reader->ReadBytes(Message);

									// Write message to the MiBand3
									MiBand->WriteMessage(Message->Data, Size);
								});
							});
				}
				// ID = 4 is an instruction to start (true) or stop (false) the
				// Heart Rate Monitoring.
				else if (Id == 4)
				{
					return concurrency::create_task(
						Reader->LoadAsync(sizeof(bool)))
						.then([this, Reader](unsigned int Size) {
						// If the size loaded was smaller than the size of a
						// bool the socket was closed before reading the whole
						// data.
						if (Size < sizeof(bool))
						{
							concurrency::cancel_current_task();
						}

						// Read the instruction
						bool bStart = Reader->ReadBoolean();
						// Start or stop the HRM service
						if (bStart)
						{
							MiBand->HeartRateStart();
						}
						else
						{
							MiBand->HeartRateStop();
						}
							});
				}
				// ID = 5 is an instruction to vibrate the MiBand3 for the given
				// amoumt of milliseconds.
				else if (Id == 5)
				{
					return concurrency::create_task(Reader->
						LoadAsync(sizeof(uint16)))
						.then([this, Reader](unsigned int Size) {
						// If the size loaded was smaller than the size of a
						// uint16 the socket was closed before reading the whole
						// data.
						if (Size < sizeof(uint16))
						{
							concurrency::cancel_current_task();
						}

						// Read the messsage
						uint16 Seconds = Reader->ReadUInt16();
						// Vibrate the MiBand3
						MiBand->Vibrate(Seconds);
							});
				}

				// ID = 6 is an instruction to vibrate the MiBand3 for the
				// standard amoumt of milliseconds. It isn't followed by any
				// payload.
				else if (Id == 6)
				{
					// Vibrate the MiBand3
					MiBand->Vibrate();
				}
			}
			return concurrency::create_task([] {});
		})
		// Restart the loop to receive messages.
			.then([this, Reader, Socket](concurrency::task<void> PreviousTask) {
			try
			{
				PreviousTask.get();

				// Recursive invocation
				ReceiveStringLoop(Reader, Socket);
			}
			catch (Platform::Exception ^ Ex)
			{
				std::cout << "Read stream failed with error: "
					<< Ex->Message->Data() << std::endl;
				// Explicitly close the socket.
				delete Socket;
			}
			catch (concurrency::task_canceled&)
			{
				// Do not print anything here - this will usually happen because
				// user closed the client socket.

				// Explicitly close the socket.
				delete Socket;
			}
				});
}
