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
#include "queue.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, PortholeQueueInitialize)
#endif

// forwards
static void wait_device(PortholeDeviceRegisters *regs, UINT32 mask, UINT32 value);
static NTSTATUS check_success(const PortholeDeviceRegisters *regs);
static NTSTATUS ioctl_send_msg(const PDEVICE_CONTEXT DeviceContext, const PFILE_OBJECT_CONTEXT fileContext, const size_t OutputBufferLength, const size_t InputBufferLength, const WDFREQUEST Request, size_t * BytesReturned);
static NTSTATUS ioctl_unlock_buffer(const PDEVICE_CONTEXT DeviceContext, const PFILE_OBJECT_CONTEXT fileContext, const size_t OutputBufferLength, const size_t InputBufferLength, const WDFREQUEST Request, size_t * BytesReturned);

void free_mdl(PMDL mdl)
{
	for (PMDL nextMdl; mdl; mdl = nextMdl)
	{
		nextMdl = mdl->Next;
		if (mdl->MdlFlags & MDL_PAGES_LOCKED)
			MmUnlockPages(mdl);
		IoFreeMdl(mdl);
	}
}

NTSTATUS
PortholeQueueInitialize(_In_ WDFDEVICE Device)
{
    WDFQUEUE              queue;
    WDF_IO_QUEUE_CONFIG   queueConfig;

    PAGED_CODE();

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = PortholeEvtIoDeviceControl;
    queueConfig.EvtIoStop          = PortholeEvtIoStop;

    NTSTATUS status = WdfIoQueueCreate(Device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
	if (!NT_SUCCESS(status))
		return status;

	return status;
}

VOID
PortholeEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
)
{
	WDFDEVICE            hDevice       = WdfIoQueueGetDevice(Queue);
	WDFFILEOBJECT        fileObject    = WdfRequestGetFileObject(Request);
	PDEVICE_CONTEXT      deviceContext = DeviceGetContext(hDevice);
	PFILE_OBJECT_CONTEXT fileContext   = FileGetContext(fileObject);
	NTSTATUS             status        = STATUS_INVALID_DEVICE_REQUEST;
	size_t               bytesReturned = 0;

	switch (IoControlCode)
	{
		case IOCTL_PORTHOLE_SEND_MSG:
			status = ioctl_send_msg(deviceContext, fileContext, OutputBufferLength, InputBufferLength, Request, &bytesReturned);
			break;

		case IOCTL_PORTHOLE_UNLOCK_BUFFER:
			status = ioctl_unlock_buffer(deviceContext, fileContext, OutputBufferLength, InputBufferLength, Request, &bytesReturned);
			break;
	}

	// a disconnect invalidates all mappings
	if (status == STATUS_DEVICE_NOT_CONNECTED)
		PortholeDeviceFileCleanup(fileObject);

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

VOID PortholeEvtIoStop(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG      ActionFlags
)
{
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(ActionFlags);
	WdfRequestStopAcknowledge(Request, TRUE);
}

VOID PortholeDeviceFileCreate(WDFDEVICE Device, WDFREQUEST Request, WDFFILEOBJECT FileObject)
{
	UNREFERENCED_PARAMETER(Request);

	PFILE_OBJECT_CONTEXT fileContext = FileGetContext(FileObject);
	RtlZeroMemory(fileContext, sizeof(FILE_OBJECT_CONTEXT));
	fileContext->deviceContext = DeviceGetContext(Device);

	WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID PortholeDeviceFileCleanup(WDFFILEOBJECT FileObject)
{
	PFILE_OBJECT_CONTEXT fileContext = FileGetContext(FileObject);
	const PDEVICE_CONTEXT deviceContext = fileContext->deviceContext;

	for (int i = 0; i < PORTHOLE_MAX_LOCKS; ++i)
		if (fileContext->mdlList[i].size)
		{
			PMDLInfo info = &fileContext->mdlList[i];

			/* tell the device about the unmapping */
			deviceContext->regs->addr.q = info->id;
			deviceContext->regs->cr |= PH_REG_CR_UNMAP;
			wait_device(deviceContext->regs, PH_REG_CR_UNMAP, 0);

			/* we don't check for errors here intentionally */

			free_mdl(info->mdl);
			info->size = 0;
		}
}

static void wait_device(PortholeDeviceRegisters *regs, UINT32 mask, UINT32 value)
{
	LARGE_INTEGER delay = { 1 };
	while ((regs->cr & mask) != value)
		KeDelayExecutionThread(KernelMode, FALSE, &delay);
}

static NTSTATUS check_success(const PortholeDeviceRegisters *regs)
{
	/* this must be checked first, a disconnect invalidates all mappings */
	if (regs->cr & PH_REG_CR_NOCONN)
		return STATUS_DEVICE_NOT_CONNECTED;

	if (regs->cr & PH_REG_CR_TIMEOUT)
		return STATUS_TIMEOUT;

	if (regs->cr & PH_REG_CR_BADADDR)
		return STATUS_INVALID_ADDRESS;

	if (regs->cr & PH_REG_CR_NORES)
		return STATUS_DEVICE_INSUFFICIENT_RESOURCES;

	if (regs->cr & PH_REG_CR_DEVERR)
		return STATUS_INVALID_DEVICE_REQUEST;

	return STATUS_SUCCESS;
}

static NTSTATUS send_segment(PortholeDeviceRegisters *regs, UINT64 addr, UINT32 size)
{
	wait_device(regs, PH_REG_CR_ADD_SEGMENT, 0x0);
	NTSTATUS result = check_success(regs);
	if (!NT_SUCCESS(result))
		return result;

	/* send the segment */
	regs->addr.q = addr;
	regs->size   = size;
	regs->cr     = PH_REG_CR_ADD_SEGMENT;

	return STATUS_SUCCESS;
}

static NTSTATUS ioctl_send_msg(
	const PDEVICE_CONTEXT      DeviceContext,
	const PFILE_OBJECT_CONTEXT FileContext,
	const size_t               OutputBufferLength,
	const size_t               InputBufferLength,
	const WDFREQUEST           Request,
	size_t                   * BytesReturned
)
{
	if (InputBufferLength != sizeof(PortholeMsg))
		return STATUS_INVALID_BUFFER_SIZE;

	if (OutputBufferLength != sizeof(PortholeMapID))
		return STATUS_INVALID_BUFFER_SIZE;

	PPortholeMsg   input;
	PPortholeMapID output;
	if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(PortholeMsg), (PVOID *)&input, NULL)))
		return STATUS_INVALID_USER_BUFFER;

	if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(PortholeMapID), (PVOID *)&output, NULL)))
		return STATUS_INVALID_USER_BUFFER;

	/* ensure the supplied buffer size is valid */
	if (input->size == 0)
		return STATUS_INVALID_USER_BUFFER;

	/* find a free mdl and reserve it */
	PMDLInfo mdlInfo = NULL;
	for (int i = 0; i < PORTHOLE_MAX_LOCKS; ++i)
		if (!FileContext->mdlList[i].size)
		{
			mdlInfo = &FileContext->mdlList[i];
			mdlInfo->addr = input->addr;
			mdlInfo->size = input->size;
			break;
		}

	if (!mdlInfo)
		return STATUS_DEVICE_INSUFFICIENT_RESOURCES;

	/* allocate a MDL for the address provided */
	PMDL mdl = IoAllocateMdl(input->addr, input->size, FALSE, FALSE, NULL);
	if (!mdl)
	{
		mdlInfo->size = 0;
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	mdlInfo->mdl = mdl;

	/* lock the page into ram */
	try
	{
		MmProbeAndLockPages(mdl, UserMode, IoModifyAccess);
	}
	except(STATUS_ACCESS_VIOLATION)
	{
		mdlInfo->size = 0;
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	NTSTATUS result;

	/* tell the device we are about to send a list of segments */
	DeviceContext->regs->cr = PH_REG_CR_START;
	wait_device(DeviceContext->regs, PH_REG_CR_START, 0x0);
	if (!NT_SUCCESS(result = check_success(DeviceContext->regs)))
	{
		free_mdl(mdl);
		mdlInfo->size = 0;
		return result;
	}

	ULONG   segSize   = 0;
	ULONG64 lastPA    = 0;
	UINT32  remaining = input->size;

	for (PMDL curMdl = mdl; curMdl != NULL; curMdl = curMdl->Next)
	{
		const ULONG pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(curMdl), MmGetMdlByteCount(curMdl));
		ULONG pageOffset  = MmGetMdlByteOffset(curMdl);
		PPFN_NUMBER pfn   = MmGetMdlPfnArray(curMdl);
		for (ULONG i = 0; i < pages; ++i, ++pfn)
		{
			const UINT64 curPA    = (*pfn << PAGE_SHIFT) + pageOffset;
			const ULONG  pageSize = min(PAGE_SIZE - pageOffset, remaining);
			remaining -= pageSize;
			pageOffset = 0;

			/* if it's the first entry */
			if (lastPA == 0)
			{
				lastPA   = curPA;
				segSize += pageSize;
				continue;
			}

			/* if the pages are contiguous, merge them */
			if (lastPA + segSize == curPA)
			{
				segSize += pageSize;
				continue;
			}

			if (!NT_SUCCESS(result = send_segment(DeviceContext->regs, lastPA, segSize)))
			{
				free_mdl(mdl);
				mdlInfo->size = 0;
				return result;
			}

			/* move on to the next segment */
			lastPA  = curPA;
			segSize = pageSize;
		}
	}

	/* send the final segment */
	if (!NT_SUCCESS(result = send_segment(DeviceContext->regs, lastPA, segSize)))
	{
		free_mdl(mdl);
		mdlInfo->size = 0;
		return result;
	}

	/* want for the final segment and check it's result */
	wait_device(DeviceContext->regs, PH_REG_CR_ADD_SEGMENT, 0x0);
	if (!NT_SUCCESS(result = check_success(DeviceContext->regs)))
	{
		free_mdl(mdl);
		mdlInfo->size = 0;
		return result;
	}

	/* send the final message */
	DeviceContext->regs->type = input->type;
	DeviceContext->regs->cr   = PH_REG_CR_FINISH;
	wait_device(DeviceContext->regs, PH_REG_CR_FINISH, 0x0);
	result = check_success(DeviceContext->regs);
	if (!NT_SUCCESS(result))
		return result;

	/* the mapping ID will be in the lower address register */
	mdlInfo->id = DeviceContext->regs->addr.l;
	*output = mdlInfo->id;
	*BytesReturned = sizeof(PortholeMapID);
	return STATUS_SUCCESS;
}

static NTSTATUS ioctl_unlock_buffer(
	const PDEVICE_CONTEXT      DeviceContext,
	const PFILE_OBJECT_CONTEXT FileContext,
	const size_t               OutputBufferLength,
	const size_t               InputBufferLength,
	const WDFREQUEST           Request,
	size_t                   * BytesReturned
)
{
	UNREFERENCED_PARAMETER(DeviceContext);
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(BytesReturned);

	PPortholeMapID input;

	if (InputBufferLength != sizeof(PortholeMapID))
		return STATUS_INVALID_BUFFER_SIZE;

	if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(PortholeMapID), (PVOID *)&input, NULL)))
		return STATUS_INVALID_USER_BUFFER;

	for (int i = 0; i < PORTHOLE_MAX_LOCKS; ++i)
		if (FileContext->mdlList[i].size > 0 && FileContext->mdlList[i].id == *input)
		{
			PMDLInfo info = &FileContext->mdlList[i];

			DeviceContext->regs->addr.q = info->id;
			DeviceContext->regs->cr |= PH_REG_CR_UNMAP;
			wait_device(DeviceContext->regs, PH_REG_CR_UNMAP, 0);
			NTSTATUS result = check_success(DeviceContext->regs);
			if (!NT_SUCCESS(result))
				return result;

			free_mdl(info->mdl);
			info->size = 0;
			return STATUS_SUCCESS;
		}

	return STATUS_INVALID_ADDRESS;
}