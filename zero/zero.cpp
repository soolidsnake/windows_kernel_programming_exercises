#include "pch.h"
#include "zero.h"
#define DRIVER_PREFIX "Zero: "

NTSTATUS ZeroCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS ZeroRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS ZeroWrite(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS ZeroControlDevice(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);

long long g_TotalRead, g_TotalWrite;

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0)
{
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}


// DriverEntry
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	NTSTATUS status;
	PDEVICE_OBJECT DeviceObject;

	UNREFERENCED_PARAMETER(RegistryPath);
	DriverObject->MajorFunction[IRP_MJ_CREATE] = ZeroCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = ZeroCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = ZeroRead;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = ZeroWrite;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ZeroControlDevice;

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\Zero");
	status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, TRUE, &DeviceObject);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Failed to create device object (0x%08)\n", status));
		return status;
	}
	// set up Direct I/O
	DeviceObject->Flags |= DO_DIRECT_IO;

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Zero");
	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Failed to create symbolic link (0x%08X)\n", status));
		IoDeleteDevice(DeviceObject);
		return status;
	}


	return STATUS_SUCCESS;
}

NTSTATUS ZeroCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	return CompleteIrp(Irp, STATUS_SUCCESS);
}

NTSTATUS ZeroRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto length = stack->Parameters.Read.Length;
	if (length == 0)
		return CompleteIrp(Irp, STATUS_INVALID_BUFFER_SIZE);

	auto buff = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buff)
		return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES);

	g_TotalRead += length;
	memset(buff, 0, length);
	return CompleteIrp(Irp, STATUS_SUCCESS, length);
}
NTSTATUS ZeroWrite(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto length = stack->Parameters.Write.Length;
	g_TotalWrite += length;

	return CompleteIrp(Irp, STATUS_SUCCESS, length);
}

NTSTATUS ZeroControlDevice(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_ZERO_GET_STATS:
	{
		if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ZeroStats))
			return CompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL);

		auto stats = (ZeroStats*)Irp->AssociatedIrp.SystemBuffer;
		stats->TotalRead = g_TotalRead;
		stats->TotalWritten = g_TotalWrite;
		return CompleteIrp(Irp, STATUS_SUCCESS, sizeof(ZeroStats));
		break;
	}
	default:
		return CompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST);
		break;
	}
}