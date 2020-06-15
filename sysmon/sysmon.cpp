#include "pch.h"
#include "FastMutex.h"
#include "AutoLock.h"
#include "ntifs.h "
#include "sysmon.h"
//#include <ntifs.h>

#define DRIVER_PREFIX "Sysmon: "
#define DRIVER_TAG 'eee'

NTSTATUS SysmonCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS SysmonRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS SysmonWrite(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS SysmonControlDevice(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
void SysmonUnload(_In_ PDRIVER_OBJECT DriverObject);
void PushItem(LIST_ENTRY* entry);

void OnProcessNotify(_Inout_ PEPROCESS Process, _In_ HANDLE ProcessId, _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo);
void OnThreadNotify(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create);
void OnImageLoad(
	_In_opt_ PUNICODE_STRING FullImageName,
	_In_ HANDLE ProcessId, // pid into which image is being mapped
	_In_ PIMAGE_INFO ImageInfo);

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0)
{
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

Globals g_Globals;

// DriverEntry
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	NTSTATUS status;
	PDEVICE_OBJECT DeviceObject;
	//PCREATE_PROCESS_NOTIFY_ROUTINE_EX NotifyRoutine;

	UNREFERENCED_PARAMETER(RegistryPath);
	DriverObject->MajorFunction[IRP_MJ_CREATE] = SysmonCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = SysmonCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = SysmonRead;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = SysmonWrite;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SysmonControlDevice;
	DriverObject->DriverUnload = SysmonUnload;

	InitializeListHead(&g_Globals.ItemsHead);
	g_Globals.Mutex.Init();

	status = PsSetCreateThreadNotifyRoutine(OnThreadNotify);
	status &= PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
	status &= PsSetLoadImageNotifyRoutine(OnImageLoad);
	if (!NT_SUCCESS(status)) {
		KdPrint((DRIVER_PREFIX "failed to register process callback (0x%08X)\n", status));
	}

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\Sysmon");
	status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, TRUE, &DeviceObject);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Failed to create device object (0x%08)\n", status));
		return status;
	}
	// set up Direct I/O
	DeviceObject->Flags |= DO_DIRECT_IO;

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Sysmon");
	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Failed to create symbolic link (0x%08X)\n", status));
		IoDeleteDevice(DeviceObject);
		return status;
	}

	return STATUS_SUCCESS;
}


NTSTATUS SysmonCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	return CompleteIrp(Irp, STATUS_SUCCESS);
}


NTSTATUS SysmonRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Read.Length;
	auto buff = (UCHAR*)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buff)
		return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES);

	AutoLock<FastMutex> lock(g_Globals.Mutex);

	int count = 0;

	while (TRUE)
	{
		if (IsListEmpty(&g_Globals.ItemsHead))
			break;

		auto entry = RemoveHeadList(&g_Globals.ItemsHead);
		auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
		auto size = info->Data.Size;
		if (len < size)
		{
			// user's buffer is full, insert item back
			InsertHeadList(&g_Globals.ItemsHead, entry);
			break;
		}
		len -= size;
		count += size;
		//KdPrint(("size : %p", info->Data.Size));
		::memcpy(buff, &info->Data, info->Data.Size);
		buff += size;
		//KdPrint(("address : %p", buff));
		ExFreePool(info);
	}

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = count;
	IoCompleteRequest(Irp, 0);
	return STATUS_SUCCESS;
}


NTSTATUS SysmonWrite(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	return CompleteIrp(Irp, STATUS_SUCCESS, 0);
}


NTSTATUS SysmonControlDevice(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case 1:
	{
		return CompleteIrp(Irp, STATUS_SUCCESS, 0);
		break;
	}
	default:
		return CompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST);
		break;
	}
}


void SysmonUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	KdPrint(("Driver unloaded\n"));
	auto currentPid = PsGetCurrentProcessId();
	KdPrint(("pid %p\n", currentPid));
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
	PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
	PsRemoveLoadImageNotifyRoutine(OnImageLoad);
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Sysmon");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
}


void OnThreadNotify(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create)
{
	OBJECT_ATTRIBUTES ObjectAttributes;
	HANDLE handle;
	InitializeObjectAttributes(&ObjectAttributes, nullptr, 0, 0, nullptr);
	CLIENT_ID ClientId = {};
	ClientId.UniqueProcess = ProcessId;
	ClientId.UniqueThread = ThreadId;
	auto status = ZwOpenProcess(&handle, READ_CONTROL, &ObjectAttributes, &ClientId);

	if (!NT_SUCCESS(status))
	{
		KdPrint((DRIVER_PREFIX "Failed to open process\n"));
		return;
	}

	if (!Create)
		return;


	PROCESS_BASIC_INFORMATION  ProcessInformation;
	ULONG ReturnLength;
	UNICODE_STRING ZwQueryInformationProcessName, NtQueryInformationThreadName;

	if(ZwQueryInformationProcess == nullptr)
	{
		RtlInitUnicodeString(&ZwQueryInformationProcessName, L"ZwQueryInformationProcess");
		RtlInitUnicodeString(&NtQueryInformationThreadName, L"ZwQueryInformationThread");

		ZwQueryInformationProcess = (QUERY_INFO_PROCESS)MmGetSystemRoutineAddress(&ZwQueryInformationProcessName);


		NtQueryInformationThread = (QUERY_INFO_THREAD)MmGetSystemRoutineAddress(&NtQueryInformationThreadName);



		if(ZwQueryInformationProcess == nullptr)
		{
			DbgPrint("Cannot resolve ZwQueryInformationProcess\n");
			ZwClose(handle);
			return;
		}

		if (NtQueryInformationThread == nullptr)
		{
			DbgPrint("Cannot resolve ZwQueryThreadInformation\n");
			ZwClose(handle);
			return;
		}
	}
	status = ZwQueryInformationProcess(handle, ProcessBasicInformation, &ProcessInformation, sizeof(ProcessInformation), &ReturnLength);
	
	auto currentPid = PsGetCurrentProcessId();

	if(Create)
		KdPrint(("Inhereted %d, pid %d, current %d, thread %d \n", ProcessInformation.InheritedFromUniqueProcessId, HandleToULong(ProcessId), HandleToUlong(currentPid), HandleToULong(ThreadId)));

	// ProcessInformation.InheritedFromUniqueProcessId => father
	// ProcessId of the thread
	// currentPid process executing right now 

	if (ProcessInformation.InheritedFromUniqueProcessId != HandleToUlong(currentPid) && HandleToULong(ProcessId)  != HandleToUlong(currentPid))
	{
		KdPrint(("Injection of thread %d on %d \n", HandleToULong(ThreadId), HandleToUlong(ProcessId)));
		PVOID ThreadInformation;



		PETHREAD peThread;

		status = PsLookupThreadByThreadId(ThreadId, &peThread);

		if (!NT_SUCCESS(status))
		{
			KdPrint(("failed PsLookupThreadByThreadId (0x%08X)\n", status));
			ZwClose(handle);
			return;
		}

		HANDLE hThreadRef;
		status = ObOpenObjectByPointer(peThread, OBJ_KERNEL_HANDLE, NULL, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &hThreadRef);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("failed ObOpenObjectByPointer (0x%08X)\n", status));
			ZwClose(handle);
			return;
		}

		status = NtQueryInformationThread(hThreadRef, ThreadQuerySetWin32StartAddress, &ThreadInformation, sizeof(PVOID), &ReturnLength);

		if (!NT_SUCCESS(status))
		{
			KdPrint(("failed QueryThreadInformation (0x%08X)\n", status));
			ZwClose(handle);
			return;
		}
		KdPrint(("Start address is : %p", ThreadInformation));

		KAPC_STATE *Apcstate;
		PEPROCESS eProcess;

		Apcstate = (KAPC_STATE*)ExAllocatePoolWithTag(NonPagedPool, sizeof(KAPC_STATE), DRIVER_TAG);
		if (Apcstate == nullptr) 
		{
			KdPrint(("Error allocate apcstate"));
			ZwClose(handle);
			return;
		}

		status = PsLookupProcessByProcessId(ProcessId, &eProcess);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("failed PsLookupProcessByProcessId (0x%08X)\n", status));
			ZwClose(handle);
			return;
		}

		KeStackAttachProcess(eProcess, Apcstate);
	
		int i;
		for(i = 0; i<20; i++)
			KdPrint(("%hhx", *(UCHAR*)((UCHAR*)ThreadInformation+i)));



		ExFreePool(Apcstate);
		ObDereferenceObject(eProcess);
		KeUnstackDetachProcess(Apcstate);

		KdPrint(("gg"));
	}


	ZwClose(handle);




	auto info = (FullItem<ThreadCreateExitInfo>*)ExAllocatePoolWithTag(PagedPool, sizeof(FullItem<ThreadCreateExitInfo>), DRIVER_TAG);
	if (info == nullptr) {
		KdPrint((DRIVER_PREFIX "Failed to allocate memory\n"));
		return;
	}
	info->Data.ProcessId = HandleToULong(ProcessId);
	info->Data.ThreadId = HandleToULong(ThreadId);
	info->Data.Size = sizeof(ThreadCreateExitInfo);
	KeQuerySystemTimePrecise(&info->Data.Time);
	info->Data.Type = Create ? ItemType::ThreadCreate : ItemType::ThreadExit;
	PushItem(&info->Entry);
}


void OnProcessNotify(_Inout_ PEPROCESS Process, _In_ HANDLE ProcessId, _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	UNREFERENCED_PARAMETER(Process);
	if (CreateInfo) //process creation
	{

		USHORT allocSize = sizeof(FullItem<ProcessCreateInfo>) + CreateInfo->CommandLine->Length;
		USHORT commandLineSize = 0;
		if (CreateInfo->CommandLine) {
			commandLineSize = CreateInfo->CommandLine->Length;
			allocSize += commandLineSize;
		}
		auto info = (FullItem<ProcessCreateInfo>*)ExAllocatePoolWithTag(PagedPool,
			allocSize, DRIVER_TAG);
		if (info == nullptr)
		{
			KdPrint(("Failed to allocate memory for FullItem"));
			return;
		}

		auto& item = info->Data;
		if (commandLineSize > 0) 
		{
			::memcpy((UCHAR*)&item + sizeof(item), CreateInfo->CommandLine->Buffer, commandLineSize);
			item.CommandLineLength = commandLineSize / sizeof(WCHAR);	// length in WCHARs
			item.CommandLineOffset = sizeof(item);
		}
		else 
		{
			item.CommandLineLength = 0;
		}
		
		info->Data.ProcessId = HandleToULong(ProcessId);
		KeQuerySystemTimePrecise(&info->Data.Time);
		info->Data.Size = sizeof(ProcessCreateInfo) + commandLineSize;
		info->Data.Type = ItemType::ProcessCreate;
		info->Data.ProcessId = HandleToULong(ProcessId);
		info->Data.PProcessId = HandleToULong(CreateInfo->ParentProcessId);
		PushItem(&info->Entry);
	}
	else //process exit
	{
		auto info = (FullItem<ProcessExitInfo>*)ExAllocatePoolWithTag(PagedPool,
			sizeof(FullItem<ProcessExitInfo>), DRIVER_TAG);
		if (info == nullptr)
		{
			KdPrint(("Failed to allocate memory for FullItem"));
			return;
		}
		info->Data.ProcessId = HandleToULong(ProcessId);
		KeQuerySystemTimePrecise(&info->Data.Time);
		info->Data.Size = sizeof(ProcessExitInfo);
		info->Data.Type = ItemType::ProcessExit;
		PushItem(&info->Entry);
	}
}


void OnImageLoad(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo)
{
	//UNREFERENCED_PARAMETER(FullImageName);
	//UNREFERENCED_PARAMETER(ProcessId);
	if (ImageInfo->SystemModeImage)
		return;

	auto info = (FullItem<ImageLoad>*)ExAllocatePoolWithTag(PagedPool, sizeof(FullItem<ImageLoad>), DRIVER_TAG);
	if (info == nullptr)
		return;
	info->Data.ProcessId = HandleToULong(ProcessId);
	info->Data.Size = sizeof(ImageLoad);
	info->Data.imageBase = ImageInfo->ImageBase;
	info->Data.Type = ItemType::ImageLoad;
	KeQuerySystemTimePrecise(&info->Data.Time);
	if (FullImageName)
	{
		::memcpy(info->Data.ImageFileName, FullImageName->Buffer, min(MaxImageFileSize*sizeof(WCHAR), FullImageName->Length));
	}

	PushItem(&info->Entry);
}


void PushItem(LIST_ENTRY* entry)
{
	AutoLock<FastMutex> lock(g_Globals.Mutex);
	if (g_Globals.ItemCount > 1024)
	{
		// too many items, remove oldest one
		auto head = RemoveHeadList(&g_Globals.ItemsHead);
		g_Globals.ItemCount--;
		auto item = CONTAINING_RECORD(head, FullItem<ItemHeader>, Entry);
		ExFreePool(item);
	}
	InsertTailList(&g_Globals.ItemsHead, entry);
	g_Globals.ItemCount++;
}