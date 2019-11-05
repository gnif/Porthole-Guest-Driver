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

#include "driver.h"
#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, PortholeCreateDevice   )
#pragma alloc_text (PAGE, PortholePrepareHardware)
#pragma alloc_text (PAGE, PortholeReleaseHardware)
#endif

NTSTATUS PortholeCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    PDEVICE_CONTEXT       deviceContext;
    WDFDEVICE             device;
    NTSTATUS              status;

    PAGED_CODE();

	WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
	pnpPowerCallbacks.EvtDevicePrepareHardware = PortholePrepareHardware;
	pnpPowerCallbacks.EvtDeviceReleaseHardware = PortholeReleaseHardware;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

	WDF_FILEOBJECT_CONFIG fileConfig;
	WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, PortholeDeviceFileCreate, NULL, PortholeDeviceFileCleanup);
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FILE_OBJECT_CONTEXT);
	WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &attributes);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
		return status;

    deviceContext = DeviceGetContext(device);
	RtlZeroMemory(deviceContext, sizeof(DEVICE_CONTEXT));
	KeInitializeSpinLock(&deviceContext->deviceLock);

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_PORTHOLE, NULL);

	if (!NT_SUCCESS(status))
		return status;

	status = PortholeQueueInitialize(device);
	if (!NT_SUCCESS(status))
		return status;

    return status;
}

NTSTATUS PortholePrepareHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST ResourceRaw, _In_ WDFCMRESLIST ResourceTranslated)
{
	UNREFERENCED_PARAMETER(ResourceRaw);

	PAGED_CODE();
	PDEVICE_CONTEXT deviceContext = DeviceGetContext(Device);
	
	PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
	descriptor = WdfCmResourceListGetDescriptor(ResourceTranslated, 0);
	if (descriptor->u.Memory.Length < sizeof(PortholeDeviceRegisters))
		return STATUS_DEVICE_HARDWARE_ERROR;

	deviceContext->regs = (PPortholeDeviceRegisters)
		MmMapIoSpace(descriptor->u.Memory.Start, sizeof(PortholeDeviceRegisters), MmNonCached);

	return STATUS_SUCCESS;
}

NTSTATUS PortholeReleaseHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST ResourceTranslated)
{
	UNREFERENCED_PARAMETER(ResourceTranslated);

	PAGED_CODE();
	PDEVICE_CONTEXT deviceContext = DeviceGetContext(Device);

	MmUnmapIoSpace(deviceContext->regs, sizeof(PortholeDeviceRegisters));

	return STATUS_SUCCESS;
}