// The POSIX calls the Windows build stands in for (dm_win.h and the headers
// beside it).
#include <windows.h>

#include <string>

#include "dirent.h"
#include "unistd.h"
#include "sys/time.h"

namespace
{
	std::wstring Wide(const char* s)
	{
		int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
		std::wstring w(n > 0 ? n - 1 : 0, L'\0');
		if (n > 1)
			MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
		return w;
	}
}

extern "C" int dm_win_rename(const char* from, const char* to)
{
	return MoveFileExW(Wide(from).c_str(), Wide(to).c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}

extern "C" unsigned int sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}

extern "C" int usleep(unsigned int usec)
{
	Sleep((usec + 999) / 1000);
	return 0;
}

extern "C" int gettimeofday(struct timeval* tv, void*)
{
	FILETIME ft;
	GetSystemTimePreciseAsFileTime(&ft);
	// 100 ns units since 1601 -> microseconds since 1970
	unsigned long long t = ((unsigned long long) ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	t = t / 10 - 11644473600000000ULL;
	tv->tv_sec = (long) (t / 1000000);
	tv->tv_usec = (long) (t % 1000000);
	return 0;
}

struct DmDir
{
	HANDLE find;
	WIN32_FIND_DATAW data;
	bool first;
	struct dirent entry;
};

extern "C" DIR* opendir(const char* path)
{
	DIR* d = new DmDir();
	std::wstring pattern = Wide(path) + L"\\*";
	d->find = FindFirstFileW(pattern.c_str(), &d->data);
	if (d->find == INVALID_HANDLE_VALUE) {
		delete d;
		return nullptr;
	}
	d->first = true;
	return d;
}

extern "C" struct dirent* readdir(DIR* d)
{
	if (!d->first && !FindNextFileW(d->find, &d->data))
		return nullptr;
	d->first = false;
	int n = WideCharToMultiByte(CP_UTF8, 0, d->data.cFileName, -1, d->entry.d_name,
		sizeof d->entry.d_name, nullptr, nullptr);
	if (n <= 0)
		d->entry.d_name[0] = 0;
	return &d->entry;
}

extern "C" int closedir(DIR* d)
{
	if (!d)
		return -1;
	FindClose(d->find);
	delete d;
	return 0;
}
