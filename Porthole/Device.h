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
	volatile UINT32 status;

	volatile UINT32 msg_cr;
	volatile UINT32 msg_type;
	volatile UINT32 msg_addr_l;
	volatile UINT32 msg_addr_h;
	volatile UINT32 msg_addr_size;

	volatile UINT32 reserved1;
	volatile UINT32 reserved2;
}
PortholeDeviceRegisters, *PPortholeDeviceRegisters;

#define PORTHOLE_REG_MSG_CR_RESET       (1 << 1)
#define PORTHOLE_REG_MSG_CR_ADD_SEGMENT (1 << 2)
#define PORTHOLE_REG_MSG_CR_FINISH      (1 << 3)
#define PORTHOLE_REG_MSG_CR_TIMEOUT     (1 << 4)
#define PORTHOLE_REG_MSG_CR_BADADDR     (1 << 5)
#define PORTHOLE_REG_MSG_CR_NOCONN      (1 << 6)

typedef struct _DEVICE_CONTEXT
{
	PPortholeDeviceRegisters regs;
}
DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)
NTSTATUS PortholeCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit);

EXTERN_C_END
