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

EVT_WDF_INTERRUPT_ISR     PortholeInterruptISR;
EVT_WDF_INTERRUPT_DPC     PortholeInterruptDPC;
EVT_WDF_INTERRUPT_ENABLE  PortholeInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE PortholeInterruptDisable;

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

	KeInitializeSpinLock(&deviceContext->deviceLock   );
	KeInitializeSpinLock(&deviceContext->eventListLock);
	InitializeListHead(&deviceContext->eventList);

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
	PDEVICE_CONTEXT deviceContext = DeviceGetContext(Device);
	
	const ULONG resCount = WdfCmResourceListGetCount(ResourceTranslated);
	for (ULONG i = 0; i < resCount; ++i)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
		descriptor = WdfCmResourceListGetDescriptor(ResourceTranslated, i);
		if (!descriptor)
			return STATUS_DEVICE_CONFIGURATION_ERROR;

		if (!deviceContext->regs && descriptor->Type == CmResourceTypeMemory)
		{
			if (descriptor->u.Memory.Length != sizeof(PortholeDeviceRegisters))
				continue;

			deviceContext->regs = (PPortholeDeviceRegisters)
				MmMapIoSpace(descriptor->u.Memory.Start, sizeof(PortholeDeviceRegisters), MmNonCached);
			break;
		}
	}

	if (!deviceContext->regs)
		return STATUS_DEVICE_HARDWARE_ERROR;

	NTSTATUS status = STATUS_DEVICE_HARDWARE_ERROR;
	for (ULONG i = 0; i < resCount; ++i)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
		descriptor = WdfCmResourceListGetDescriptor(ResourceTranslated, i);
		if (!descriptor)
		{
			status = STATUS_DEVICE_CONFIGURATION_ERROR;
			break;
		}

		if (descriptor->Type == CmResourceTypeInterrupt && !(descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
		{
			WDF_INTERRUPT_CONFIG irqConfig;
			WDF_INTERRUPT_CONFIG_INIT(&irqConfig, PortholeInterruptISR, PortholeInterruptDPC);
			irqConfig.InterruptTranslated = descriptor;
			irqConfig.InterruptRaw        = WdfCmResourceListGetDescriptor(ResourceRaw, i);
			irqConfig.EvtInterruptEnable  = PortholeInterruptEnable;
			irqConfig.EvtInterruptDisable = PortholeInterruptDisable;
			status = WdfInterruptCreate(Device, &irqConfig, WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->interrupt);
			if (!NT_SUCCESS(status))
				break;
			
			deviceContext->connected = (deviceContext->regs->cr & PH_REG_CR_NOCONN) == 0x0 ? TRUE : FALSE;
			return STATUS_SUCCESS;
		}
	}

	MmUnmapIoSpace(deviceContext->regs, sizeof(PortholeDeviceRegisters));
	deviceContext->regs = NULL;
	return status;
}

NTSTATUS PortholeReleaseHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST ResourceTranslated)
{
	UNREFERENCED_PARAMETER(ResourceTranslated);

	PAGED_CODE();
	PDEVICE_CONTEXT deviceContext = DeviceGetContext(Device);

	// disable interrupts
	deviceContext->regs->cr &= (~PH_REG_CR_IRQ);

	// dereference and free the event list
	KIRQL oldIRQL;
	KeAcquireSpinLock(&deviceContext->eventListLock, &oldIRQL);
	while (!IsListEmpty(&deviceContext->eventList))
	{
		PLIST_ENTRY     entry  = RemoveTailList(&deviceContext->eventList);
		PPORTHOLE_EVENT record = CONTAINING_RECORD(entry, PORTHOLE_EVENT, listEntry);

		if (record->connect)
			ObDereferenceObject(record->connect);

		if (record->disconnect)
			ObDereferenceObject(record->disconnect);

		ExFreePoolWithTag(record, TAG);
	}
	KeReleaseSpinLock(&deviceContext->eventListLock, oldIRQL);

	// unmap the io space
	if (deviceContext->regs)
		MmUnmapIoSpace(deviceContext->regs, sizeof(PortholeDeviceRegisters));

	return STATUS_SUCCESS;
}

NTSTATUS PortholeInterruptEnable(WDFINTERRUPT Interrupt, WDFDEVICE Device)
{
	UNREFERENCED_PARAMETER(Interrupt);

	PDEVICE_CONTEXT deviceContext = DeviceGetContext(Device);
	deviceContext->regs->cr |= PH_REG_CR_IRQ;
	return STATUS_SUCCESS;
}

NTSTATUS PortholeInterruptDisable(WDFINTERRUPT Interrupt, WDFDEVICE Device)
{
	UNREFERENCED_PARAMETER(Interrupt);

	PDEVICE_CONTEXT deviceContext = DeviceGetContext(Device);
	deviceContext->regs->cr &= (~PH_REG_CR_IRQ);
	return STATUS_SUCCESS;
}

BOOLEAN PortholeInterruptISR(WDFINTERRUPT Interrupt, ULONG MessageID)
{
	UNREFERENCED_PARAMETER(MessageID);

	WDFDEVICE       device        = WdfInterruptGetDevice(Interrupt);
	PDEVICE_CONTEXT deviceContext = DeviceGetContext(device);

	if (deviceContext->regs->isr)
		WdfInterruptQueueDpcForIsr(Interrupt);

	return TRUE;
}

void PortholeInterruptDPC(WDFINTERRUPT Interrupt, WDFOBJECT AssociatedObject)
{
	UNREFERENCED_PARAMETER(AssociatedObject);

	WDFDEVICE       device        = WdfInterruptGetDevice(Interrupt);
	PDEVICE_CONTEXT deviceContext = DeviceGetContext(device);

	LONG isr = InterlockedExchange(&(LONG)deviceContext->regs->isr, 0xFFFFFFFF);
	if (!isr)
		return;

	deviceContext->connected = (deviceContext->regs->cr & PH_REG_CR_NOCONN) == 0x0 ? TRUE : FALSE;

	KeAcquireSpinLockAtDpcLevel(&deviceContext->eventListLock);
	for (PLIST_ENTRY entry = deviceContext->eventList.Flink; entry != &deviceContext->eventList; entry = entry->Flink)
	{
		PPORTHOLE_EVENT record = CONTAINING_RECORD(entry, PORTHOLE_EVENT, listEntry);

		// always flag disconnections first if both have happend in the same ISR
		if ((isr & PH_REG_ISR_DISCONNECT) && record->disconnect)
			KeSetEvent(record->disconnect, 0, FALSE);

		if ((isr & PH_REG_ISR_CONNECT) && record->connect)
			KeSetEvent(record->connect, 0, FALSE);
	}
	KeReleaseSpinLockFromDpcLevel(&deviceContext->eventListLock);
}