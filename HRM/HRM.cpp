// HRM.cpp : This file contains the 'main' function. Program execution begins and ends there.

#include "pch.h"

#include "MiBand3.h"
#include "RemoteCommunication.h"

// Main function of the program
int main(Platform::Array<Platform::String^>^ args)
{
	MiBand3^ MB3 = ref new MiBand3();

	std::wcout << "Started service" << std::endl;

	// Wait for user input to end
	int a;
	std::cin >> a;

	return 0;
}
