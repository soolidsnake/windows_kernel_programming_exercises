#include "pch.h"

enum class ItemType : USHORT {
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad
};

struct ItemHeader {
	ItemType Type;
	USHORT Size;
	LARGE_INTEGER Time;
}; 

struct ProcessExitInfo : ItemHeader {
	ULONG ProcessId;
};

struct ProcessCreateInfo : ItemHeader {
	ULONG ProcessId;
	ULONG PProcessId;
	USHORT CommandLineLength;
	USHORT CommandLineOffset;
};

struct ThreadCreateExitInfo : ItemHeader {
	ULONG ThreadId;
	ULONG ProcessId;
};

const int MaxImageFileSize = 300;
struct ImageLoad: ItemHeader {
	ULONG ProcessId;
	PVOID imageBase;
	WCHAR ImageFileName[MaxImageFileSize];
};

template<typename T>
struct FullItem {
	LIST_ENTRY Entry;
	T Data;
};

struct Globals {
	LIST_ENTRY ItemsHead;
	int ItemCount;
	FastMutex Mutex;
};



typedef NTSTATUS(*QUERY_INFO_PROCESS) (
	__in HANDLE ProcessHandle,
	__in PROCESSINFOCLASS ProcessInformationClass,
	__out_bcount(ProcessInformationLength) PVOID ProcessInformation,
	__in ULONG ProcessInformationLength,
	__out_opt PULONG ReturnLength
	);


QUERY_INFO_PROCESS ZwQueryInformationProcess;

typedef NTSTATUS(*QUERY_INFO_THREAD)(
	IN HANDLE          ThreadHandle,
	IN THREADINFOCLASS ThreadInformationClass,
	OUT PVOID          ThreadInformation,
	IN ULONG           ThreadInformationLength,
	OUT PULONG         ReturnLength
	);

QUERY_INFO_THREAD NtQueryInformationThread;