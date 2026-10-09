// The window with GLFW and OpenGL 3 (Linux, macOS, Windows): see Platform.hpp.
#if !defined(DM_IMGUI_X11)

#include "Platform.hpp"

#include <cstdio>

// OpenGL 3 declarations (framebuffers): the core profile header on macOS,
// the extension prototypes elsewhere
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_GLCOREARB
#elif !defined(_WIN32)
#define GL_GLEXT_PROTOTYPES
#define GLFW_INCLUDE_GLEXT
#endif
#include <GLFW/glfw3.h>

#if defined(_WIN32)
// Windows' opengl32 exports OpenGL 1.1: the framebuffer calls (DM_SNAPSHOT)
// are looked up once there is a context
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
static void (__stdcall* glGenFramebuffers)(GLsizei, GLuint*);
static void (__stdcall* glBindFramebuffer)(GLenum, GLuint);
static void (__stdcall* glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);

static void LoadFramebufferCalls()
{
	glGenFramebuffers = (decltype(glGenFramebuffers)) glfwGetProcAddress("glGenFramebuffers");
	glBindFramebuffer = (decltype(glBindFramebuffer)) glfwGetProcAddress("glBindFramebuffer");
	glFramebufferTexture2D = (decltype(glFramebufferTexture2D)) glfwGetProcAddress("glFramebufferTexture2D");
}
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace
{
	GLFWwindow* g_window;
	bool g_hidden;
	GLuint g_snapFbo, g_snapTex;
}

bool Platform::Open(int w, int h, const char* title, bool hidden, std::string& err)
{
	if (!glfwInit()) {
		err = "GLFW could not start (is there a display?)";
		return false;
	}
#if defined(__APPLE__)
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
	g_hidden = hidden;
	if (hidden)
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	g_window = glfwCreateWindow(w, h, title, nullptr, nullptr);
	if (!g_window) {
		err = "no OpenGL 3 window";
		return false;
	}
	glfwMakeContextCurrent(g_window);
	glfwSwapInterval(hidden ? 0 : 1);
	return true;
}

void Platform::InitImGui(bool input)
{
	ImGui_ImplGlfw_InitForOpenGL(g_window, input);
#if defined(__APPLE__)
	ImGui_ImplOpenGL3_Init("#version 150");
#else
	ImGui_ImplOpenGL3_Init("#version 130");
#endif
}

void Platform::Close()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(g_window);
	glfwTerminate();
}

void Platform::PollEvents() { glfwPollEvents(); }
bool Platform::WaitEvents(double timeout)
{
	// (GLFW does not say why it returned: one that came before the time
	// was up had a reason)
	double t0 = glfwGetTime();
	glfwWaitEventsTimeout(timeout);
	return glfwGetTime() - t0 < timeout - 0.002;
}
void Platform::Wake() { glfwPostEmptyEvent(); }
bool Platform::ShouldClose() { return glfwWindowShouldClose(g_window) != 0; }
bool Platform::Focused() { return g_window && glfwGetWindowAttrib(g_window, GLFW_FOCUSED) != 0; }
double Platform::Time() { return glfwGetTime(); }
void* Platform::Native() { return g_window; }
bool Platform::PowerOfTwoTextures() { return false; }
int Platform::MaxTextureSize() { return 8192; }

void Platform::ScreenSize(int& w, int& h)
{
	w = h = 0;
	if (const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor())) {
		w = mode->width;
		h = mode->height;
	}
}

void Platform::NewFrame()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
}

void Platform::FramebufferSize(int& w, int& h)
{
	glfwGetFramebufferSize(g_window, &w, &h);
}

void Platform::MakeSnapshotTarget(int w, int h)
{
#if defined(_WIN32)
	LoadFramebufferCalls();
#endif
	glGenTextures(1, &g_snapTex);
	glBindTexture(GL_TEXTURE_2D, g_snapTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glGenFramebuffers(1, &g_snapFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, g_snapFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_snapTex, 0);
}

void Platform::BindSnapshotTarget()
{
	glBindFramebuffer(GL_FRAMEBUFFER, g_snapFbo);
}

void Platform::Render(ImDrawData* data)
{
	ImGui_ImplOpenGL3_RenderDrawData(data);
}

void Platform::MakeCurrent()
{
	if (g_window)
		glfwMakeContextCurrent(g_window);
}

void Platform::Swap()
{
	glfwSwapBuffers(g_window);
}

#endif
