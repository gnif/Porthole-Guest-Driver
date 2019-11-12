/*
Copyright 2019 Geoffrey McRae

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "pch.h"
#include <stdio.h>

#pragma pack(push,1)
typedef struct _MsgSetup
{
	UINT32  version;
	UINT32  mainRingSize;
	CHAR    mainRing[0];
}
MsgSetup, *PMsgSetup;
#pragma pack(pop)

int main()
{
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);

	HDEVINFO                         deviceInfoSet;
	PSP_DEVICE_INTERFACE_DETAIL_DATA infData    = NULL;
	HANDLE                           devHandle  = INVALID_HANDLE_VALUE;
	SP_DEVICE_INTERFACE_DATA		 devInfData = { 0 };
	DWORD                            reqSize    = 0;
	ULONG                            returned;
	HANDLE                           connectEvent, disconnectEvent;

	devInfData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	deviceInfoSet     = SetupDiGetClassDevs(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES | DIGCF_DEVICEINTERFACE);
	SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &GUID_DEVINTERFACE_PORTHOLE, 0, &devInfData);

	SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &devInfData, NULL, 0, &reqSize, NULL);
	if (reqSize == 0)
	{
		printf("Failed to get reqSize, is the driver loaded?\n");
		return -1;
	}

	// open the device
	infData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(reqSize);
	infData->cbSize = sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA);
	SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &devInfData, infData, reqSize, NULL, NULL);
	devHandle = CreateFile(infData->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
	free(infData);
	
	// register events
	PortholeEvents events;
	events.connect    = CreateEvent(NULL, TRUE, FALSE, NULL);
	events.disconnect = CreateEvent(NULL, TRUE, FALSE, NULL);

	DeviceIoControl(devHandle, IOCTL_PORTHOLE_REGISTER_EVENTS, &events,
		sizeof(PortholeEvents), NULL, 0, &returned, NULL);

	// wait for a connection
	printf("waiting for connect event.\n");
	while (WaitForSingleObject(events.connect, INFINITE) != WAIT_OBJECT_0)	{}

#define RING_SIZE (16384)

	// build the setup message
	PMsgSetup msgSetup     = (PMsgSetup)malloc(sizeof(MsgSetup) + RING_SIZE);
	msgSetup->version      = 0x0100;
	msgSetup->mainRingSize = RING_SIZE;

	const char str[] = "Test message from windows";
	for (UINT32 i = 0; i < RING_SIZE; ++i)
		msgSetup->mainRing[i] = str[i % sizeof(str)];

	printf("sending info to host...");

	// send the message
	PortholeMsg msg;
	PortholeMapID id;
	msg.type = 0x1;
	msg.addr = msgSetup;
	msg.size = sizeof(MsgSetup) + RING_SIZE;
	DeviceIoControl(devHandle, IOCTL_PORTHOLE_SEND_MSG, &msg,
		sizeof(PortholeMsg), &id, sizeof(PortholeMapID), &returned, NULL);

	printf("done.\n");

	DeviceIoControl(
		devHandle, IOCTL_PORTHOLE_UNLOCK_BUFFER,
		&id, sizeof(PortholeMapID),
		NULL     , 0,
		&returned,
		NULL);

	// wait for disconect
	printf("wating for disconnect event.\n");
	while (WaitForSingleObject(events.disconnect, INFINITE) != WAIT_OBJECT_0) {}
	
	CloseHandle(devHandle);
	CloseHandle(events.connect);
	CloseHandle(events.disconnect);
	return 0;
}