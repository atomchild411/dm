// The window with Xlib, GLX 1.2 and OpenGL 1.1 (IRIX, whose GLX is older
// than GLFW needs): see Platform.hpp.  Dear ImGui's platform back end for it
// is this file's own; its renderer is Dear ImGui's OpenGL 2 one (fixed
// function: OpenGL 1.1 calls only).
#if defined(DM_IMGUI_X11)

#include "Platform.hpp"

#include <algorithm>
#include <cerrno>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include "imgui.h"
#include "imgui_impl_opengl2.h"

namespace
{
	Display* g_dpy;
	::Window g_win;
	GLXContext g_ctx;
	Colormap g_cmap;
	Atom g_wmDelete, g_wmProtocols, g_clipboard, g_utf8, g_targets, g_selProp;
	int g_wake[2] = { -1, -1 };
	int g_w, g_h;
	bool g_close, g_focused = true, g_input;
	double g_t0, g_last;
	Cursor g_cursors[ImGuiMouseCursor_COUNT];
	Cursor g_blank;
	int g_cursor = -2;
	std::string g_clipOwn, g_clipGot;
	int g_maxTex = 1024;

	double Now()
	{
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		return tv.tv_sec + tv.tv_usec / 1e6;
	}

	// ---- text: X gives Latin-1, ImGui takes UTF-8 ----------------------

	std::string Latin1ToUtf8(const char* s, size_t n)
	{
		std::string out;
		for (size_t i = 0; i < n; i++) {
			unsigned char c = (unsigned char) s[i];
			if (c < 0x80)
				out += char(c);
			else {
				out += char(0xc0 | (c >> 6));
				out += char(0x80 | (c & 0x3f));
			}
		}
		return out;
	}

	// UTF-8 as Latin-1, '?' for what Latin-1 has not.
	std::string Utf8ToLatin1(const std::string& s)
	{
		std::string out;
		for (size_t i = 0; i < s.size(); ) {
			unsigned char c = (unsigned char) s[i];
			unsigned cp;
			size_t len;
			if (c < 0x80) { cp = c; len = 1; }
			else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; len = 2; }
			else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; len = 3; }
			else { cp = c & 0x07; len = 4; }
			for (size_t k = 1; k < len && i + k < s.size(); k++)
				cp = (cp << 6) | ((unsigned char) s[i + k] & 0x3f);
			out += cp < 0x100 ? char(cp) : '?';
			i += len;
		}
		return out;
	}

	// ---- keys ---------------------------------------------------------------

	ImGuiKey KeyFor(KeySym ks)
	{
		if (ks >= XK_a && ks <= XK_z) return (ImGuiKey) (ImGuiKey_A + (ks - XK_a));
		if (ks >= XK_A && ks <= XK_Z) return (ImGuiKey) (ImGuiKey_A + (ks - XK_A));
		if (ks >= XK_0 && ks <= XK_9) return (ImGuiKey) (ImGuiKey_0 + (ks - XK_0));
		if (ks >= XK_F1 && ks <= XK_F12) return (ImGuiKey) (ImGuiKey_F1 + (ks - XK_F1));
		switch (ks)
		{
			case XK_Tab: case XK_ISO_Left_Tab: return ImGuiKey_Tab;
			case XK_Left: case XK_KP_Left: return ImGuiKey_LeftArrow;
			case XK_Right: case XK_KP_Right: return ImGuiKey_RightArrow;
			case XK_Up: case XK_KP_Up: return ImGuiKey_UpArrow;
			case XK_Down: case XK_KP_Down: return ImGuiKey_DownArrow;
			case XK_Prior: case XK_KP_Prior: return ImGuiKey_PageUp;
			case XK_Next: case XK_KP_Next: return ImGuiKey_PageDown;
			case XK_Home: case XK_KP_Home: return ImGuiKey_Home;
			case XK_End: case XK_KP_End: return ImGuiKey_End;
			case XK_Insert: case XK_KP_Insert: return ImGuiKey_Insert;
			case XK_Delete: case XK_KP_Delete: return ImGuiKey_Delete;
			case XK_BackSpace: return ImGuiKey_Backspace;
			case XK_space: return ImGuiKey_Space;
			case XK_Return: return ImGuiKey_Enter;
			case XK_KP_Enter: return ImGuiKey_KeypadEnter;
			case XK_Escape: return ImGuiKey_Escape;
			case XK_apostrophe: return ImGuiKey_Apostrophe;
			case XK_comma: return ImGuiKey_Comma;
			case XK_minus: return ImGuiKey_Minus;
			case XK_period: return ImGuiKey_Period;
			case XK_slash: return ImGuiKey_Slash;
			case XK_semicolon: return ImGuiKey_Semicolon;
			case XK_equal: return ImGuiKey_Equal;
			case XK_bracketleft: return ImGuiKey_LeftBracket;
			case XK_backslash: return ImGuiKey_Backslash;
			case XK_bracketright: return ImGuiKey_RightBracket;
			case XK_grave: return ImGuiKey_GraveAccent;
			case XK_Shift_L: return ImGuiKey_LeftShift;
			case XK_Shift_R: return ImGuiKey_RightShift;
			case XK_Control_L: return ImGuiKey_LeftCtrl;
			case XK_Control_R: return ImGuiKey_RightCtrl;
			case XK_Alt_L: case XK_Meta_L: return ImGuiKey_LeftAlt;
			case XK_Alt_R: case XK_Meta_R: return ImGuiKey_RightAlt;
			default: return ImGuiKey_None;
		}
	}

	void Modifiers(unsigned state)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiMod_Ctrl, (state & ControlMask) != 0);
		io.AddKeyEvent(ImGuiMod_Shift, (state & ShiftMask) != 0);
		io.AddKeyEvent(ImGuiMod_Alt, (state & Mod1Mask) != 0);
	}

	// ---- the clipboard (X selections) -----------------------------------------

	void OwnClipboard(const std::string& text)
	{
		g_clipOwn = text;
		XSetSelectionOwner(g_dpy, g_clipboard, g_win, CurrentTime);
		XSetSelectionOwner(g_dpy, XA_PRIMARY, g_win, CurrentTime);
	}

	void AnswerSelection(XSelectionRequestEvent& rq)
	{
		XSelectionEvent ev;
		memset(&ev, 0, sizeof ev);
		ev.type = SelectionNotify;
		ev.display = rq.display;
		ev.requestor = rq.requestor;
		ev.selection = rq.selection;
		ev.target = rq.target;
		ev.time = rq.time;
		ev.property = None;
		Atom prop = rq.property != None ? rq.property : rq.target;
		if (rq.target == g_targets) {
			Atom t[3] = { g_targets, g_utf8, XA_STRING };
			XChangeProperty(g_dpy, rq.requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char*) t, 3);
			ev.property = prop;
		}
		else if (rq.target == g_utf8) {
			XChangeProperty(g_dpy, rq.requestor, prop, g_utf8, 8, PropModeReplace,
				(const unsigned char*) g_clipOwn.data(), (int) g_clipOwn.size());
			ev.property = prop;
		}
		else if (rq.target == XA_STRING) {
			std::string l1 = Utf8ToLatin1(g_clipOwn);
			XChangeProperty(g_dpy, rq.requestor, prop, XA_STRING, 8, PropModeReplace,
				(const unsigned char*) l1.data(), (int) l1.size());
			ev.property = prop;
		}
		XSendEvent(g_dpy, rq.requestor, False, 0, (XEvent*) &ev);
	}

	// What another program has on a selection, as target (UTF8_STRING or
	// STRING); waits a second at most.
	bool ReadSelection(Atom selection, Atom target, std::string& out)
	{
		XConvertSelection(g_dpy, selection, target, g_selProp, g_win, CurrentTime);
		XFlush(g_dpy);
		double until = Now() + 1.0;
		XEvent ev;
		while (Now() < until) {
			if (XCheckTypedWindowEvent(g_dpy, g_win, SelectionNotify, &ev)) {
				if (ev.xselection.property == None)
					return false;
				Atom type;
				int format;
				unsigned long n, after;
				unsigned char* data = nullptr;
				if (XGetWindowProperty(g_dpy, g_win, g_selProp, 0, 1 << 20, True, AnyPropertyType,
						&type, &format, &n, &after, &data) != Success || !data)
					return false;
				out = type == XA_STRING ? Latin1ToUtf8((const char*) data, n) : std::string((const char*) data, n);
				XFree(data);
				return true;
			}
			usleep(5000);
		}
		return false;
	}

	const char* GetClipboard(ImGuiContext*)
	{
		if (XGetSelectionOwner(g_dpy, g_clipboard) == g_win)
			return g_clipOwn.c_str();
		g_clipGot.clear();
		for (Atom sel : { g_clipboard, (Atom) XA_PRIMARY })
			if (ReadSelection(sel, g_utf8, g_clipGot) || ReadSelection(sel, XA_STRING, g_clipGot))
				break;
		return g_clipGot.c_str();
	}

	void SetClipboard(ImGuiContext*, const char* text)
	{
		OwnClipboard(text ? text : "");
	}

	// ---- events ---------------------------------------------------------------

	void Handle(XEvent& ev)
	{
		ImGuiIO& io = ImGui::GetIO();
		switch (ev.type)
		{
			case ConfigureNotify:
				g_w = ev.xconfigure.width;
				g_h = ev.xconfigure.height;
				break;
			case ClientMessage:
				if (ev.xclient.message_type == g_wmProtocols && (Atom) ev.xclient.data.l[0] == g_wmDelete)
					g_close = true;
				break;
			case FocusIn:
			case FocusOut:
				g_focused = ev.type == FocusIn;
				if (g_input)
					io.AddFocusEvent(g_focused);
				break;
			case SelectionRequest:
				AnswerSelection(ev.xselectionrequest);
				break;
			case SelectionClear:
				break;
		}
		if (!g_input)
			return;
		switch (ev.type)
		{
			case MotionNotify:
				io.AddMousePosEvent((float) ev.xmotion.x, (float) ev.xmotion.y);
				break;
			case LeaveNotify:
				io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
				break;
			case EnterNotify:
				io.AddMousePosEvent((float) ev.xcrossing.x, (float) ev.xcrossing.y);
				break;
			case ButtonPress:
			case ButtonRelease: {
				bool down = ev.type == ButtonPress;
				Modifiers(ev.xbutton.state);
				io.AddMousePosEvent((float) ev.xbutton.x, (float) ev.xbutton.y);
				switch (ev.xbutton.button) {
					case Button1: io.AddMouseButtonEvent(0, down); break;
					case Button2: io.AddMouseButtonEvent(2, down); break;
					case Button3: io.AddMouseButtonEvent(1, down); break;
					case Button4: if (down) io.AddMouseWheelEvent(0, 1); break;
					case Button5: if (down) io.AddMouseWheelEvent(0, -1); break;
					case 6: if (down) io.AddMouseWheelEvent(1, 0); break;
					case 7: if (down) io.AddMouseWheelEvent(-1, 0); break;
				}
				break;
			}
			case KeyPress:
			case KeyRelease: {
				char buf[32];
				KeySym ks = 0;
				int n = XLookupString(&ev.xkey, buf, sizeof buf, &ks, nullptr);
				bool down = ev.type == KeyPress;
				Modifiers(ev.xkey.state);
				ImGuiKey key = KeyFor(ks);
				if (key != ImGuiKey_None)
					io.AddKeyEvent(key, down);
				// the text it types (not with Ctrl: those are commands)
				if (down && n > 0 && !(ev.xkey.state & ControlMask)) {
					std::string text;
					for (int i = 0; i < n; i++) {
						unsigned char c = (unsigned char) buf[i];
						if (c >= 0x20 && c != 0x7f)
							text += char(c);
					}
					if (!text.empty())
						io.AddInputCharactersUTF8(Latin1ToUtf8(text.data(), text.size()).c_str());
				}
				break;
			}
		}
	}

	void UpdateCursor()
	{
		ImGuiIO& io = ImGui::GetIO();
		if (!g_input || (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange))
			return;
		int c = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
		if (c == g_cursor)
			return;
		g_cursor = c;
		Cursor cur = c == ImGuiMouseCursor_None || c < 0 || c >= ImGuiMouseCursor_COUNT ? g_blank : g_cursors[c];
		XDefineCursor(g_dpy, g_win, cur ? cur : g_cursors[ImGuiMouseCursor_Arrow]);
	}
}

bool Platform::Open(int w, int h, const char* title, bool hidden, std::string& err)
{
	(void) hidden; // (no off-screen target in OpenGL 1.1: the window shows)
	g_dpy = XOpenDisplay(nullptr);
	if (!g_dpy) {
		err = "cannot open the X display (is DISPLAY set?)";
		return false;
	}
	int screen = DefaultScreen(g_dpy);
	::Window root = RootWindow(g_dpy, screen);

	// RGBA, double buffered: 8 bits a colour if there is such a visual
	int good[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None };
	int any[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1, None };
	XVisualInfo* vi = glXChooseVisual(g_dpy, screen, good);
	if (!vi)
		vi = glXChooseVisual(g_dpy, screen, any);
	if (!vi) {
		err = "the X server has no double-buffered RGBA OpenGL visual";
		return false;
	}
	g_ctx = glXCreateContext(g_dpy, vi, nullptr, True);
	if (!g_ctx) {
		err = "cannot make an OpenGL context";
		return false;
	}
	g_cmap = XCreateColormap(g_dpy, root, vi->visual, AllocNone);
	XSetWindowAttributes swa;
	memset(&swa, 0, sizeof swa);
	swa.colormap = g_cmap;
	swa.border_pixel = 0;
	swa.background_pixmap = None;
	swa.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask |
		FocusChangeMask;
	g_win = XCreateWindow(g_dpy, root, 0, 0, w, h, 0, vi->depth, InputOutput, vi->visual,
		CWColormap | CWBorderPixel | CWEventMask | CWBackPixmap, &swa);
	XFree(vi);
	g_w = w;
	g_h = h;

	XStoreName(g_dpy, g_win, title);
	XSetIconName(g_dpy, g_win, title);
	XClassHint* ch = XAllocClassHint();
	ch->res_name = (char*) "discord-messenger";
	ch->res_class = (char*) "DiscordMessenger";
	XSetClassHint(g_dpy, g_win, ch);
	XFree(ch);
	XSizeHints* sh = XAllocSizeHints();
	sh->flags = PMinSize;
	sh->min_width = 640;
	sh->min_height = 400;
	XSetWMNormalHints(g_dpy, g_win, sh);
	XFree(sh);
	g_wmProtocols = XInternAtom(g_dpy, "WM_PROTOCOLS", False);
	g_wmDelete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(g_dpy, g_win, &g_wmDelete, 1);
	g_clipboard = XInternAtom(g_dpy, "CLIPBOARD", False);
	g_utf8 = XInternAtom(g_dpy, "UTF8_STRING", False);
	g_targets = XInternAtom(g_dpy, "TARGETS", False);
	g_selProp = XInternAtom(g_dpy, "DM_SELECTION", False);

	XMapWindow(g_dpy, g_win);
	for (;;) {
		XEvent ev;
		XWindowEvent(g_dpy, g_win, StructureNotifyMask, &ev);
		if (ev.type == MapNotify)
			break;
		if (ev.type == ConfigureNotify) {
			g_w = ev.xconfigure.width;
			g_h = ev.xconfigure.height;
		}
	}
	if (!glXMakeCurrent(g_dpy, g_win, g_ctx)) {
		err = "cannot make the OpenGL context current";
		return false;
	}
	GLint maxTex = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
	// (texture memory is small on these boards: 1 MB on High IMPACT)
	g_maxTex = maxTex > 0 ? std::min((int) maxTex, 1024) : 1024;

	if (pipe(g_wake) == 0) {
		fcntl(g_wake[0], F_SETFL, fcntl(g_wake[0], F_GETFL) | O_NONBLOCK);
		fcntl(g_wake[1], F_SETFL, fcntl(g_wake[1], F_GETFL) | O_NONBLOCK);
	}
	g_t0 = g_last = Now();
	return true;
}

void Platform::InitImGui(bool input)
{
	g_input = input;
	ImGuiIO& io = ImGui::GetIO();
	io.BackendPlatformName = "dm_x11";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
	ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
	pio.Platform_GetClipboardTextFn = GetClipboard;
	pio.Platform_SetClipboardTextFn = SetClipboard;
	io.Fonts->TexMaxWidth = g_maxTex;
	io.Fonts->TexMaxHeight = g_maxTex;

	const unsigned shapes[ImGuiMouseCursor_COUNT] = {
		XC_left_ptr,            // Arrow
		XC_xterm,               // TextInput
		XC_fleur,               // ResizeAll
		XC_sb_v_double_arrow,   // ResizeNS
		XC_sb_h_double_arrow,   // ResizeEW
		XC_bottom_left_corner,  // ResizeNESW
		XC_bottom_right_corner, // ResizeNWSE
		XC_hand2,               // Hand
		XC_watch,               // Wait
		XC_watch,               // Progress
		XC_X_cursor,            // NotAllowed
	};
	for (int i = 0; i < ImGuiMouseCursor_COUNT; i++)
		g_cursors[i] = XCreateFontCursor(g_dpy, shapes[i]);
	static char zero[8] = { 0 };
	Pixmap p = XCreateBitmapFromData(g_dpy, g_win, zero, 8, 8);
	XColor black;
	memset(&black, 0, sizeof black);
	g_blank = XCreatePixmapCursor(g_dpy, p, p, &black, &black, 0, 0);
	XFreePixmap(g_dpy, p);

	ImGui_ImplOpenGL2_Init();
}

void Platform::Close()
{
	ImGui_ImplOpenGL2_Shutdown();
	ImGui::DestroyContext();
	glXMakeCurrent(g_dpy, None, nullptr);
	glXDestroyContext(g_dpy, g_ctx);
	XDestroyWindow(g_dpy, g_win);
	XFreeColormap(g_dpy, g_cmap);
	XCloseDisplay(g_dpy);
}

void Platform::PollEvents()
{
	while (XPending(g_dpy)) {
		XEvent ev;
		XNextEvent(g_dpy, &ev);
		Handle(ev);
	}
}

bool Platform::WaitEvents(double timeout)
{
	XFlush(g_dpy);
	bool came = XPending(g_dpy) > 0;
	if (!came) {
		int fd = ConnectionNumber(g_dpy);
		fd_set rd;
		FD_ZERO(&rd);
		FD_SET(fd, &rd);
		if (g_wake[0] >= 0)
			FD_SET(g_wake[0], &rd);
		struct timeval tv;
		tv.tv_sec = (long) timeout;
		tv.tv_usec = (long) ((timeout - (double) tv.tv_sec) * 1e6);
		came = select((fd > g_wake[0] ? fd : g_wake[0]) + 1, &rd, nullptr, nullptr, &tv) > 0;
		if (g_wake[0] >= 0 && FD_ISSET(g_wake[0], &rd)) {
			char b[64];
			while (read(g_wake[0], b, sizeof b) > 0) {}
		}
	}
	PollEvents();
	return came;
}

void Platform::Wake()
{
	if (g_wake[1] >= 0) {
		ssize_t r = write(g_wake[1], "x", 1);
		(void) r;
	}
}

bool Platform::ShouldClose() { return g_close; }
bool Platform::Focused() { return g_dpy && g_focused; }
double Platform::Time() { return Now() - g_t0; }
void* Platform::Native() { return nullptr; }
bool Platform::PowerOfTwoTextures() { return true; }
int Platform::MaxTextureSize() { return g_maxTex; }

void Platform::ScreenSize(int& w, int& h)
{
	w = DisplayWidth(g_dpy, DefaultScreen(g_dpy));
	h = DisplayHeight(g_dpy, DefaultScreen(g_dpy));
}

void Platform::NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float) g_w, (float) g_h);
	io.DisplayFramebufferScale = ImVec2(1, 1);
	double now = Now();
	io.DeltaTime = now > g_last ? (float) (now - g_last) : 1.0f / 60;
	g_last = now;
	ImGui_ImplOpenGL2_NewFrame();
	UpdateCursor();
}

void Platform::FramebufferSize(int& w, int& h)
{
	w = g_w;
	h = g_h;
}

// No framebuffer objects in OpenGL 1.1: the snapshot is the window's own
// back buffer, read before the swap.
void Platform::MakeSnapshotTarget(int w, int h)
{
	if (w != g_w || h != g_h)
		XResizeWindow(g_dpy, g_win, w, h);
}

void Platform::BindSnapshotTarget()
{
}

void Platform::Render(ImDrawData* data)
{
	ImGui_ImplOpenGL2_RenderDrawData(data);
}

void Platform::Swap()
{
	glXSwapBuffers(g_dpy, g_win);
}

#endif
