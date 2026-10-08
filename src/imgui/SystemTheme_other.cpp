// SystemTheme on Linux and the like: the desktop's settings portal
// (org.freedesktop.appearance color-scheme: 1 dark, 2 light, 0 no
// preference), asked through gdbus on a thread.  The frames are the window
// manager's.
#if !defined(__APPLE__) && !defined(_WIN32)

#include "SystemTheme.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "posix/MainQueue.hpp"

namespace
{
	std::atomic<bool> g_asking(false);

	// The portal's color-scheme, or -1 if it did not say.
	int AskPortal()
	{
		const char* const methods[] = {
			"ReadOne", // portals from 2023 on
			"Read",    // the older ones (the value in one more variant)
		};
		for (const char* m : methods) {
			std::string cmd = std::string("gdbus call --session --timeout 2 --dest org.freedesktop.portal.Desktop "
				"--object-path /org/freedesktop/portal/desktop --method org.freedesktop.portal.Settings.") + m +
				" org.freedesktop.appearance color-scheme 2>/dev/null";
			FILE* p = popen(cmd.c_str(), "r");
			if (!p)
				return -1;
			char buf[256] = "";
			size_t n = fread(buf, 1, sizeof buf - 1, p);
			buf[n] = 0;
			pclose(p);
			const char* u = strstr(buf, "uint32 ");
			if (u)
				return u[7] - '0';
		}
		return -1;
	}
}

void SystemTheme::Query(std::function<void(bool dark)> answer)
{
	if (g_asking.exchange(true))
		return; // already asking
	std::thread([answer] {
		int scheme = AskPortal();
		g_asking = false;
		if ((scheme == 1 || scheme == 2) && answer)
			MainQueue::Post([answer, scheme] { answer(scheme == 1); });
	}).detach();
}

bool SystemTheme::CheapToPoll()
{
	return false;
}

void SystemTheme::FrameWindows(GLFWwindow*, bool, bool)
{
}

void SystemTheme::FrameNativeWindow(void*)
{
}

#endif
