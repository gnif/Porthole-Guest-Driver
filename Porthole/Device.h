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

#include "public.h"

EXTERN_C_START

#pragma align(push, 4)
typedef struct PortholeDeviceRegisters
{
	volatile ULONG cr;
	volatile ULONG type;

	volatile PHYSICAL_ADDRESS addr;
	volatile ULONG size;

	volatile ULONG reserved1;
	volatile ULONG reserved2;
	volatile ULONG reserved3;
}
PortholeDeviceRegisters, *PPortholeDeviceRegisters;
#pragma align(pop)

#define PH_REG_CR_START       (1 << 1) // SW=S, HW=C, start of a mapping
#define PH_REG_CR_ADD_SEGMENT (1 << 2) // SW=S, HW=C, add a segment to mapping
#define PH_REG_CR_FINISH      (1 << 3) // SW=S, HW=C, end of segments
#define PH_REG_CR_UNMAP       (1 << 4) // SW=S, HW=C, unmap a segment

#define PH_REG_CR_TIMEOUT     (1 << 5) // HW=S, HW=C, timeout occured
#define PH_REG_CR_BADADDR     (1 << 6) // HW=S, HW=C, bad address specified
#define PH_REG_CR_NOCONN      (1 << 7) // HW=S, HW=C, no client connection
#define PH_REG_CR_NORES       (1 << 8) // HW=S, HW=C, no resources left
#define PH_REG_CR_DEVERR      (1 << 9) // HW=S, HW=C, invalid device usage

typedef struct _DEVICE_CONTEXT
{
	PPortholeDeviceRegisters regs;
	KSPIN_LOCK deviceLock;
}
DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)
NTSTATUS PortholeCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit);

EXTERN_C_END
