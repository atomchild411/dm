// SystemTheme on Windows: the "apps use light theme" setting, and dark or
// light title bars (DWM).
#if defined(_WIN32)

#include "SystemTheme.hpp"

#include <windows.h>
#include <dwmapi.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace
{
	bool g_dark = true;
}

void SystemTheme::Query(std::function<void(bool dark)> answer)
{
	DWORD light = 0, size = sizeof light;
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS && answer)
		answer(light == 0);
}

bool SystemTheme::CheapToPoll()
{
	return true;
}

void SystemTheme::FrameNativeWindow(void* nativeWindow)
{
	HWND hwnd = (HWND) nativeWindow;
	if (!hwnd)
		return;
	BOOL on = g_dark ? TRUE : FALSE;
	DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &on, sizeof on);
	// the frame is drawn again only when told
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SystemTheme::FrameWindows(GLFWwindow* main, bool dark, bool)
{
	g_dark = dark;
	if (main)
		FrameNativeWindow(glfwGetWin32Window(main));
}

#endif
