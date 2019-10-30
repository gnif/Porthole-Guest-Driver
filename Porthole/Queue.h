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

EXTERN_C_START

typedef struct _MDLInfo
{
	PVOID  addr;
	UINT32 size;
	PMDL   mdl;
}
MDLInfo, *PMDLInfo;

#define PORTHOLE_MAX_LOCKS 32

typedef struct _FILE_OBJECT_CONTEXT
{
	MDLInfo mdlList[PORTHOLE_MAX_LOCKS];
}
FILE_OBJECT_CONTEXT, *PFILE_OBJECT_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILE_OBJECT_CONTEXT, FileGetContext)

NTSTATUS PortholeQueueInitialize  (WDFDEVICE Device);
VOID     PortholeDeviceFileCreate (WDFDEVICE Device, WDFREQUEST Request, WDFFILEOBJECT FileObject);
VOID     PortholeDeviceFileCleanup(WDFFILEOBJECT FileObject);

//
// Events from the IoQueue object
//
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL PortholeEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_STOP PortholeEvtIoStop;

// Helpers
void free_mdl(PMDL mdl);

EXTERN_C_END
