#include <windows.h>
#include <stdio.h>

int Error(const char* msg) {
	printf("%s: error=%d\n", msg, ::GetLastError());
	return 1;
}

int main() 
{
	BYTE buffer[64];

	HANDLE hDevice = ::CreateFile(L"\\\\.\\Zero", GENERIC_READ | GENERIC_WRITE,
		0, nullptr, OPEN_EXISTING, 0, nullptr);

	if (hDevice == INVALID_HANDLE_VALUE) 
		return Error("failed to open device");
	
	// store some non-zero data
	for (int i = 0; i < sizeof(buffer); ++i)
		buffer[i] = i + 1;
	DWORD bytes;
	BOOL ok = ::ReadFile(hDevice, buffer, sizeof(buffer), &bytes, nullptr);
	if (!ok)
		return Error("failed to read");
	if (bytes != sizeof(buffer))
		printf("Wrong number of bytes\n");
	// check if buffer data sum is zero
	long total = 0;
	for (auto n : buffer)
		total += n;
	if (total != 0)
		printf("Wrong data\n");
	// test write
	BYTE buffer2[1024]; // contains junk
	ok = ::WriteFile(hDevice, buffer2, sizeof(buffer2), &bytes, nullptr);
	if (!ok)
		return Error("failed to write");
	if (bytes != sizeof(buffer2))
		printf("Wrong byte count\n");
	::CloseHandle(hDevice);
}