/*
Copyright (c) 2009-2012, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*
   written by Roman Dementiev

   WARNING: This driver code is only for testing purposes, not for production use
*/

#include "msr.h"
#include "ntdef.h"


/*!     \file msrmain.cpp
        \brief Test Windows 7 Model Specific Driver implementation
*/

#define NT_DEVICE_NAME L"\\Driver\\RDMSR"
#define DOS_DEVICE_NAME L"\\DosDevices\\RDMSR"


DRIVER_INITIALIZE DriverEntry;


__drv_dispatchType(IRP_MJ_CREATE)
__drv_dispatchType(IRP_MJ_CLOSE)
DRIVER_DISPATCH dummyFunction;


__drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH deviceControl;


DRIVER_UNLOAD MSRUnload;


#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,DriverEntry)
#pragma alloc_text(PAGE,MSRUnload)
#pragma alloc_text(PAGE,dummyFunction)
#pragma alloc_text(PAGE,deviceControl)
#endif

NTSTATUS
DriverEntry(
    __in PDRIVER_OBJECT DriverObject,
    __in PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    UNICODE_STRING UnicodeString;
    UNICODE_STRING dosDeviceName, RegName;
    UNICODE_STRING RegValueName;
    PDEVICE_OBJECT MSRSystemDeviceObject = NULL;
    OBJECT_ATTRIBUTES RegKeyAttributes;
    HANDLE RegKey;
    UCHAR buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 1024];
    ULONG resultLength;
    PWCHAR domainStr;
    ULONG domainStrL;
    int i = 0;

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlInitUnicodeString(&UnicodeString, NT_DEVICE_NAME);
    RtlInitUnicodeString(&dosDeviceName, DOS_DEVICE_NAME);

    status = IoCreateDevice(DriverObject,
                            0,
                            &UnicodeString,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &MSRSystemDeviceObject
                            );

    if (!NT_SUCCESS(status))
        return status;


    DriverObject->DriverUnload = MSRUnload;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = dummyFunction;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = dummyFunction;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = deviceControl;


    IoCreateSymbolicLink(&dosDeviceName, &UnicodeString);


    return status;
}


NTSTATUS dummyFunction(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;


    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


VOID MSRUnload(PDRIVER_OBJECT DriverObject)
{
    PDEVICE_OBJECT deviceObject = DriverObject->DeviceObject;
    UNICODE_STRING nameString;

    PAGED_CODE();

    RtlInitUnicodeString(&nameString, DOS_DEVICE_NAME);

    IoDeleteSymbolicLink(&nameString);

    if (deviceObject != NULL)
    {
        IoDeleteDevice(deviceObject);
    }
}


NTSTATUS deviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStackLocation = NULL;
    struct MSR_Request * input = NULL;
    ULONG64 * output = NULL;
    KAFFINITY old_affinity, new_affinity;

    PAGED_CODE();

    IrpStackLocation = IoGetCurrentIrpStackLocation(Irp);

    if (IrpStackLocation)
    {
        if (IrpStackLocation->Parameters.DeviceIoControl.InputBufferLength >=
            sizeof(struct MSR_Request)
            && IrpStackLocation->Parameters.DeviceIoControl.OutputBufferLength >=
            sizeof(ULONG64))
        {
            input = (struct MSR_Request *)Irp->AssociatedIrp.SystemBuffer;
            output = (ULONG64 *)input;

            switch (IrpStackLocation->Parameters.DeviceIoControl.IoControlCode)
            {
            case IO_CTL_MSR_WRITE:
                new_affinity = 1ULL << (input->core_id);
                KeSetSystemAffinityThread(new_affinity);
                __writemsr(input->msr_address, input->write_value);
                KeRevertToUserAffinityThread();
                Irp->IoStatus.Information = 0;                         // result size
                break;
            case IO_CTL_MSR_READ:
                new_affinity = 1ULL << (input->core_id);
                KeSetSystemAffinityThread(new_affinity);
                *output = __readmsr(input->msr_address);
                KeRevertToUserAffinityThread();
                Irp->IoStatus.Information = sizeof(ULONG64);                         // result size
                break;
            default:
                status = STATUS_INVALID_DEVICE_REQUEST;
            }
        }
        else
            status = STATUS_INVALID_DEVICE_REQUEST;
    }
    else
        status = STATUS_INVALID_DEVICE_REQUEST;


    Irp->IoStatus.Status = status;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}
