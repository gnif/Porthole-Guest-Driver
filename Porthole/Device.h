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
	volatile ULONG isr;
	volatile ULONG type;

	volatile ULONG size;
	volatile PHYSICAL_ADDRESS addr;

	volatile ULONG reserved1;
	volatile ULONG reserved2;
}
PortholeDeviceRegisters, *PPortholeDeviceRegisters;
#pragma align(pop)

#define PH_REG_CR_IRQ         (1 << 0) // SW=S, SW=C, enable interrupts
#define PH_REG_CR_START       (1 << 1) // SW=S, HW=C, start of a mapping
#define PH_REG_CR_ADD_SEGMENT (1 << 2) // SW=S, HW=C, add a segment to mapping
#define PH_REG_CR_FINISH      (1 << 3) // SW=S, HW=C, end of segments
#define PH_REG_CR_UNMAP       (1 << 4) // SW=S, HW=C, unmap a segment

#define PH_REG_CR_TIMEOUT     (1 << 5) // HW=S, HW=C, timeout occured
#define PH_REG_CR_BADADDR     (1 << 6) // HW=S, HW=C, bad address specified
#define PH_REG_CR_NOCONN      (1 << 7) // HW=S, HW=C, no client connection
#define PH_REG_CR_NORES       (1 << 8) // HW=S, HW=C, no resources left
#define PH_REG_CR_DEVERR      (1 << 9) // HW=S, HW=C, invalid device usage

// All ISRs are set by hardware and cleared by writing to them

/* client connection state changed
 * check PH_REG_CR_NOCONN to determine the current state
 */
#define PH_REG_ISR_CONNECT    (1 << 0)
#define PH_REG_ISR_DISCONNECT (1 << 1)

#define TAG (ULONG)'TROP'

typedef struct _PORTHOLE_EVENT
{
	PVOID      owner;
	LIST_ENTRY listEntry;
	PKEVENT    connect;
	PKEVENT    disconnect;
}
PORTHOLE_EVENT, *PPORTHOLE_EVENT;

typedef struct _DEVICE_CONTEXT
{
	PPortholeDeviceRegisters regs;
	BOOLEAN      connected;
	WDFINTERRUPT interrupt;
	KSPIN_LOCK   deviceLock;

	KSPIN_LOCK eventListLock;
	LIST_ENTRY eventList;
}
DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)
NTSTATUS PortholeCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit);
EXTERN_C_END
