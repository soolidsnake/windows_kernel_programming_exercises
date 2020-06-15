#include <Windows.h>

enum class ItemType : USHORT {
	None,
	ProcessCreate,
	ProcessExit
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
	LPSTR pcmd;
};

template<typename T>
struct FullItem {
	LIST_ENTRY Entry;
	T Data;
};