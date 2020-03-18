#pragma once

#include "pch.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <agents.h>
#include <ppltasks.h>
#include <pplawait.h>
#include <ppl.h>
#include <collection.h>
#include <Windows.Devices.Bluetooth.h>
#include <Windows.Devices.Enumeration.h>
#include <Windows.Networking.Sockets.h>

using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

ref class MiBand3;

ref class RemoteCommunication sealed
{
public:
	RemoteCommunication(MiBand3^ InMiBand);

	void StartClient(int tries = 5);
	void StopClient();
	void StartServer(int tries = 5);

	property Windows::Networking::Sockets::StreamSocket^ ClientSocket;
	property Windows::Networking::Sockets::StreamSocketListener^ ServerSocket;

	property bool bClientConnected;
	property bool bServerRunning;

private:
	MiBand3^ MiBand;

	Platform::String^ RCHostName = L"localhost";
	Platform::String^ ClientPort = L"1242";
	Platform::String^ ServerPort = L"1243";

	bool bWaitingClientConnection;

	void OnConnection(StreamSocketListener^ Listener, 
		StreamSocketListenerConnectionReceivedEventArgs^ Args);

	/**
	 * Instructions structure
	 * byte Id: instruction to execute
	 * T Args: instruction dependant arguments
	 ***
	 * Vibrate x seconds
	 * 0
	 * uint16 x
	 ***
	 * Custom message
	 * 1
	 * uint32 message size
	 * message
	 ***
	 * Heart Rate
	 * 2
	 * 0 off / 1 on
	 ***
	 * Default vibration
	 * 3
	 */
	void ReceiveStringLoop(DataReader^ Reader, StreamSocket^ Socket);
};
