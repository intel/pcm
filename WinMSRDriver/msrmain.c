/*
Copyright (c) 2009-2018, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*

   WARNING: This driver code is only for testing purposes, not for production use
*/

#include "msr.h"
#include "ntdef.h"
#include <wdm.h>


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
    UNICODE_STRING dosDeviceName;
    PDEVICE_OBJECT MSRSystemDeviceObject = NULL;

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
    struct MSR_Request * input_msr_req = NULL;
    struct PCICFG_Request * input_pcicfg_req = NULL;
    ULONG64 * output = NULL;
    GROUP_AFFINITY old_affinity, new_affinity;
    ULONG inputSize = 0;
    PCI_SLOT_NUMBER slot;
    unsigned size = 0;
    PROCESSOR_NUMBER ProcNumber;

    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    IrpStackLocation = IoGetCurrentIrpStackLocation(Irp);

    if (IrpStackLocation)
    {
        inputSize = IrpStackLocation->Parameters.DeviceIoControl.InputBufferLength;

        if (IrpStackLocation->Parameters.DeviceIoControl.OutputBufferLength >=
            sizeof(ULONG64))
        {
            input_msr_req = (struct MSR_Request *)Irp->AssociatedIrp.SystemBuffer;
            input_pcicfg_req = (struct PCICFG_Request *)Irp->AssociatedIrp.SystemBuffer;
            output = (ULONG64 *)Irp->AssociatedIrp.SystemBuffer;

            memset(&ProcNumber, 0, sizeof(PROCESSOR_NUMBER));

            switch (IrpStackLocation->Parameters.DeviceIoControl.IoControlCode)
            {
            case IO_CTL_MSR_WRITE:
                if (inputSize < sizeof(struct MSR_Request))
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }
                memset(&new_affinity, 0, sizeof(GROUP_AFFINITY));
                memset(&old_affinity, 0, sizeof(GROUP_AFFINITY));
                KeGetProcessorNumberFromIndex(input_msr_req->core_id, &ProcNumber);
                new_affinity.Group = ProcNumber.Group;
                new_affinity.Mask = 1ULL << (ProcNumber.Number);
                KeSetSystemGroupAffinityThread(&new_affinity, &old_affinity);
                __writemsr(input_msr_req->msr_address, input_msr_req->write_value);
                KeRevertToUserGroupAffinityThread(&old_affinity);
                Irp->IoStatus.Information = 0;                         // result size
                break;
            case IO_CTL_MSR_READ:
                if (inputSize < sizeof(struct MSR_Request))
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }
                memset(&new_affinity, 0, sizeof(GROUP_AFFINITY));
                memset(&old_affinity, 0, sizeof(GROUP_AFFINITY));
                KeGetProcessorNumberFromIndex(input_msr_req->core_id, &ProcNumber);
                new_affinity.Group = ProcNumber.Group;
                new_affinity.Mask = 1ULL << (ProcNumber.Number);
                KeSetSystemGroupAffinityThread(&new_affinity, &old_affinity);
                *output = __readmsr(input_msr_req->msr_address);
                KeRevertToUserGroupAffinityThread(&old_affinity);
                Irp->IoStatus.Information = sizeof(ULONG64);                         // result size
                break;
            case IO_CTL_PCICFG_WRITE:
                if (inputSize < sizeof(struct PCICFG_Request) || (input_pcicfg_req->bytes != 4 && input_pcicfg_req->bytes != 8))
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }
                slot.u.AsULONG = 0;
                slot.u.bits.DeviceNumber = input_pcicfg_req->dev;
                slot.u.bits.FunctionNumber = input_pcicfg_req->func;
                size = HalSetBusDataByOffset(PCIConfiguration, input_pcicfg_req->bus, slot.u.AsULONG,
                                             &(input_pcicfg_req->write_value), input_pcicfg_req->reg, input_pcicfg_req->bytes);
                if (size != input_pcicfg_req->bytes)
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }
                Irp->IoStatus.Information = 0;                                         // result size
                break;
            case IO_CTL_PCICFG_READ:
                if (inputSize < sizeof(struct PCICFG_Request) || (input_pcicfg_req->bytes != 4 && input_pcicfg_req->bytes != 8))
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }
                slot.u.AsULONG = 0;
                slot.u.bits.DeviceNumber = input_pcicfg_req->dev;
                slot.u.bits.FunctionNumber = input_pcicfg_req->func;
                size = HalGetBusDataByOffset(PCIConfiguration, input_pcicfg_req->bus, slot.u.AsULONG,
                                             output, input_pcicfg_req->reg, input_pcicfg_req->bytes);
                if (size != input_pcicfg_req->bytes)
                {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }
                Irp->IoStatus.Information = sizeof(ULONG64);                                         // result size
                break;

            default:
                status = STATUS_INVALID_DEVICE_REQUEST;
            }
        }
        else
            status = STATUS_INVALID_PARAMETER;
    }
    else
        status = STATUS_INVALID_DEVICE_REQUEST;


    Irp->IoStatus.Status = status;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}
