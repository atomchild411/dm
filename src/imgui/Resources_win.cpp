// Windows: what the program finds beside itself, and where it writes its log.
#if defined(_WIN32)

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

// The fonts are in "fonts" beside the program, unless DM_FONT_DIR names
// others.  The program is a windowed one (no console of its own): started
// from a console, it writes its messages there.
void UseProgramResources()
{
	// (unless they go somewhere already: redirected to a file or a pipe)
	HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
	bool redirected = err && err != INVALID_HANDLE_VALUE && GetFileType(err) != FILE_TYPE_UNKNOWN;
	if (!redirected && AttachConsole(ATTACH_PARENT_PROCESS)) {
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
	}

	wchar_t wpath[4096];
	DWORD n = GetModuleFileNameW(nullptr, wpath, sizeof wpath / sizeof wpath[0]);
	if (n == 0 || n >= sizeof wpath / sizeof wpath[0])
		return;
	char path[4096 * 3];
	if (!WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, sizeof path, nullptr, nullptr))
		return;
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of("\\/"));
	std::string fonts = dir + "\\fonts";
	struct stat st;
	if (!getenv("DM_FONT_DIR") && stat(fonts.c_str(), &st) == 0)
		_putenv_s("DM_FONT_DIR", fonts.c_str());
}

#endif
