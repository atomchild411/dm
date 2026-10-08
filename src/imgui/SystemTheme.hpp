#pragma once

#include <functional>

struct GLFWwindow;

// What the system says about dark and light, and the frames of the app's
// windows (title bars) to match the theme the app shows.
namespace SystemTheme
{
	// Whether the system wants dark: answer(dark) on the UI thread once it
	// knows (at once on macOS and Windows; on Linux the desktop's settings
	// portal is asked on a thread).  No answer where the system does not say.
	void Query(std::function<void(bool dark)> answer);

	// Whether Query is cheap enough to ask every few seconds (macOS, Windows:
	// a setting read; Linux runs a program, so only at start and on focus).
	bool CheapToPoll();

	// The frames: dark or light; followSystem when the app follows the
	// system (macOS then lets the system decide for its windows).
	void FrameWindows(GLFWwindow* main, bool dark, bool followSystem);

	// The same for a window made later (Windows: the login windows' HWND).
	void FrameNativeWindow(void* nativeWindow);
}
