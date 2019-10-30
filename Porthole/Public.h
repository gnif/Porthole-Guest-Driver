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

#include <initguid.h>

DEFINE_GUID (GUID_DEVINTERFACE_PORTHOLE,
    0x10ccc0ac,0xf4b0,0x4d78,0xba,0x41,0x1e,0xbb,0x38,0x5a,0x52,0x85);
// {10ccc0ac-f4b0-4d78-ba41-1ebb385a5285}

typedef struct _PortholeMsg
{
	UINT32 type;
	PVOID  addr;
	UINT32 size;
}
PortholeMsg, *PPortholeMsg;

typedef struct _PortholeLockMsg
{
	PVOID  addr;
	UINT32 size;
}
PortholeLockMsg, *PPortholeLockMsg;

#define IOCTL_PORTHOLE_SEND_MSG      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PORTHOLE_UNLOCK_BUFFER CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)