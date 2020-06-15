
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
struct ImageLoad : ItemHeader {
	ULONG ProcessId;
	PVOID imageBase;
	WCHAR ImageFileName[MaxImageFileSize];
};

template<typename T>
struct FullItem {
	LIST_ENTRY Entry;
	T Data;
};