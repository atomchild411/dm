#pragma once

#include <string>

struct ImDrawData;

// The program's window, its OpenGL context and Dear ImGui's platform and
// renderer back ends on it:
// - GLFW and OpenGL 3 (Linux, macOS, Windows): Platform_glfw.cpp;
// - Xlib, GLX 1.2 and OpenGL 1.1 (IRIX, whose GLX is too old for GLFW):
//   Platform_x11.cpp, with Dear ImGui's OpenGL 2 renderer (fixed function).
namespace Platform
{
	// The window (hidden: drawn off screen, for DM_SNAPSHOT, where the
	// platform can do that), current for OpenGL.
	bool Open(int w, int h, const char* title, bool hidden, std::string& err);
	// ImGui's back ends (after ImGui's context and fonts); input: whether
	// they take the user's input.
	void InitImGui(bool input);
	void Close();

	void PollEvents();
	// Waits for an event, a Wake() or timeout seconds; false when nothing
	// came.
	bool WaitEvents(double timeout);
	// Ends a wait, from any thread.
	void Wake();
	bool ShouldClose();
	bool Focused();
	// The screen's size, or 0 by 0.
	void ScreenSize(int& w, int& h);
	double Time();

	void NewFrame();
	void FramebufferSize(int& w, int& h);
	// DM_SNAPSHOT: frames go to a target of that size, read back with
	// glReadPixels (an OpenGL 3 framebuffer; on X11 the window itself).
	void MakeSnapshotTarget(int w, int h);
	void BindSnapshotTarget();
	void Render(ImDrawData* data);
	void Swap();

	// Textures must be powers of two in size, at most this big (OpenGL 1.1).
	bool PowerOfTwoTextures();
	int MaxTextureSize();

	// The native window, for SystemTheme (a GLFWwindow*; null on X11).
	void* Native();
}
