#include <Windows.h>
#include <stdio.h>
#include "main.h"
#include <string>

int Error(const char* msg) {
	printf("%s: error=%d\n", msg, ::GetLastError());
	return 1;
}

int main()
{
	BYTE buffer[1 << 16];

	HANDLE hDevice = ::CreateFile(L"\\\\.\\sysmon", GENERIC_READ | GENERIC_WRITE,
		0, nullptr, OPEN_EXISTING, 0, nullptr);

	if (hDevice == INVALID_HANDLE_VALUE)
		return Error("failed to open device");

	DWORD bytes;
	BOOL ok = ::ReadFile(hDevice, buffer, sizeof(buffer), &bytes, nullptr);
	if (!ok)
		return Error("failed to read");

	printf("Number of bytes returned %d\n ", bytes);
	
	auto pid = GetCurrentProcess();
	printf("current pid == %d\n\n", GetProcessId(pid));


	int len = 0;
	while (TRUE)
	{
		auto header = (ItemHeader*)(buffer + len);
		

		if (len >= bytes)
			break;

		switch(header->Type)
		{
			case ItemType::ProcessExit:
			{
				printf("\nprocess exit:\n");
				auto info = (ProcessExitInfo*)(buffer + len);
				printf("PID: %d \n", info->ProcessId);
				len += info->Size;
				printf("\n\n");
				break;
			}
			case ItemType::ProcessCreate:
			{
				printf("\nprocess create:\n");
				auto info = (ProcessCreateInfo*)(buffer + len);
				printf("PID: %d \n", info->ProcessId);
				printf("PPID: %d \n", info->PProcessId);
				printf("test %d\n", info->CommandLineLength);
				auto cmd = (WCHAR*)malloc((info->CommandLineLength)*sizeof(WCHAR));

				if (cmd == nullptr)
					return 0;

				::memcpy(cmd, (UCHAR*)info + sizeof(ProcessCreateInfo), (info->CommandLineLength)*sizeof(WCHAR));
				printf("%ws\n", cmd);
				len += info->Size;
				printf("\n\n");
				break;
			}
			case ItemType::ThreadCreate:
			{
				auto info = (ThreadCreateExitInfo*)(buffer + len);
				printf("\nthread create:\n");
				printf("Thread %d of PID: %d\n", info->ThreadId, info->ProcessId);
				len += info->Size;
				break;
			}
			case ItemType::ThreadExit:
			{
				auto info = (ThreadCreateExitInfo*)(buffer + len);
				printf("\nthread exit:\n");
				printf("Thread %d of PID: %d\n", info->ThreadId, info->ProcessId);
				len += info->Size;
				break;
			}
			case ItemType::ImageLoad:
			{
				auto info = (ImageLoad*)(buffer + len);
				printf("\nImage Load:\n");
				printf("PID %d\n", info->ProcessId);
				printf("image path: %ws\n", info->ImageFileName);
				len += info->Size;
				break;
			}
			default:
				break;
		}
		Sleep(500);
	}
	
	::CloseHandle(hDevice);
}