#include <windows.h>
#include <stdio.h>
#include "..\PriorityBooster\PriorityBooster.h"

int main(int argc, const char* argv[])
{
	ThreadData data;
	DWORD returned;
	HANDLE device = CreateFile(L"\\\\.\\PriorityBooster", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	data.ThreadId = atoi(argv[1]);
	data.Priority = atoi(argv[2]);

	BOOL success = DeviceIoControl(device, IOCTL_PRIORITY_BOOSTER_SET_PRIORITY, &data, sizeof(data), nullptr, 0, &returned, nullptr);


	CloseHandle(device);
}