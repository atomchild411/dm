// WebLogin on Linux: a GTK window with WebKitGTK's view on
// https://discord.com/login, as WebLogin_mac.mm does with WKWebView.
//
// WebKitGTK (the GTK 3 one, webkit2gtk 4.1 or 4.0) is loaded when first
// needed, so the program runs without it: then Available() is false and the
// login dialog offers the QR code and the token, as before.  GTK runs on the
// UI thread: while a window is open, Busy() asks the main loop to call
// Pump() often (GTK's events are not GLFW's).

#if defined(__linux__)
#include "WebLogin.hpp"
#include "WebLoginPages.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

namespace
{
	// GLib, GTK and WebKitGTK as far as used here: opaque pointers and the
	// functions, looked up in the loaded library (and what it loads)
	typedef void (*GCallback)(void);
	typedef unsigned long (*SignalConnectFn)(void*, const char*, GCallback, void*, void*, int);
	typedef int (*ContextIterationFn)(void*, int);
	typedef void (*GFreeFn)(void*);
	typedef void* (*GObjectNewFn)(unsigned long, const char*, ...);
	typedef void (*GObjectUnrefFn)(void*);
	typedef int (*GtkInitCheckFn)(int*, char***);
	typedef void* (*GtkWindowNewFn)(int);
	typedef void* (*GtkOffscreenNewFn)(void);
	typedef void (*GtkWindowSetTitleFn)(void*, const char*);
	typedef void (*GtkWindowSetSizeFn)(void*, int, int);
	typedef void (*GtkWindowSetPosFn)(void*, int);
	typedef void (*GtkContainerAddFn)(void*, void*);
	typedef void (*GtkWidgetFn)(void*);
	typedef void (*GtkWindowPresentFn)(void*);
	typedef unsigned long (*GetTypeFn)(void);
	typedef void* (*NewFn)(void);
	typedef int (*RegisterHandlerFn)(void*, const char*);
	typedef void* (*UserScriptNewFn)(const char*, int, int, const char* const*, const char* const*);
	typedef void (*AddScriptFn)(void*, void*);
	typedef void (*UnrefFn)(void*);
	typedef void (*LoadUriFn)(void*, const char*);
	typedef void (*LoadHtmlFn)(void*, const char*, const char*);
	typedef const char* (*GetUriFn)(void*);
	typedef void (*AsyncReadyFn)(void*, void*, void*);
	typedef void (*RunJsFn)(void*, const char*, void*, AsyncReadyFn, void*);
	typedef void* (*RunJsFinishFn)(void*, void*, void**);
	typedef void* (*JsResultValueFn)(void*);
	typedef int (*JscIsStringFn)(void*);
	typedef char* (*JscToStringFn)(void*);

	struct Api
	{
		bool tried = false, ok = false;
		SignalConnectFn signalConnect;
		ContextIterationFn iteration;
		GFreeFn gFree;
		GObjectNewFn objectNew;
		GObjectUnrefFn objectUnref;
		GtkInitCheckFn gtkInitCheck;
		GtkWindowNewFn windowNew;
		GtkOffscreenNewFn offscreenNew;
		GtkWindowSetTitleFn setTitle;
		GtkWindowSetSizeFn setDefaultSize;
		GtkWindowSetPosFn setPosition;
		GtkContainerAddFn containerAdd;
		GtkWidgetFn showAll;
		GtkWidgetFn destroy;
		GtkWindowPresentFn present;
		GetTypeFn webViewType;
		NewFn ephemeralContext;
		NewFn contentManagerNew;
		RegisterHandlerFn registerHandler;
		UserScriptNewFn userScriptNew;
		AddScriptFn addScript;
		UnrefFn userScriptUnref;
		LoadUriFn loadUri;
		LoadHtmlFn loadHtml;
		GetUriFn getUri;
		RunJsFn runJs;
		RunJsFinishFn runJsFinish;
		JsResultValueFn jsResultValue;
		UnrefFn jsResultUnref;
		JscIsStringFn jscIsString;
		JscToStringFn jscToString;
	} g;

	template <class T> bool Sym(void* h, T& f, const char* name)
	{
		f = (T) dlsym(h, name);
		return f != nullptr;
	}

	// WebKitGTK and a display GTK can use, or false
	bool Load()
	{
		if (g.tried)
			return g.ok;
		g.tried = true;
		if (getenv("DM_NO_WEBLOGIN"))
			return false;
		// pages drawn without OpenGL: GTK's own GL setup would otherwise
		// take over the thread's current context (ours), and in VMs without
		// a usable GL for GTK, WebKit aborts.  A login page needs no GPU.
		// (DM_WEBKIT_GL=1 leaves WebKit's GPU drawing on.)
		if (!getenv("DM_WEBKIT_GL")) {
			setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 0);
			setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
		}
		void* h = dlopen("libwebkit2gtk-4.1.so.0", RTLD_NOW | RTLD_LOCAL);
		if (!h)
			h = dlopen("libwebkit2gtk-4.0.so.37", RTLD_NOW | RTLD_LOCAL);
		if (!h)
			return false;
		bool ok =
			Sym(h, g.signalConnect, "g_signal_connect_data") &&
			Sym(h, g.iteration, "g_main_context_iteration") &&
			Sym(h, g.gFree, "g_free") &&
			Sym(h, g.objectNew, "g_object_new") &&
			Sym(h, g.objectUnref, "g_object_unref") &&
			Sym(h, g.gtkInitCheck, "gtk_init_check") &&
			Sym(h, g.windowNew, "gtk_window_new") &&
			Sym(h, g.offscreenNew, "gtk_offscreen_window_new") &&
			Sym(h, g.setTitle, "gtk_window_set_title") &&
			Sym(h, g.setDefaultSize, "gtk_window_set_default_size") &&
			Sym(h, g.setPosition, "gtk_window_set_position") &&
			Sym(h, g.containerAdd, "gtk_container_add") &&
			Sym(h, g.showAll, "gtk_widget_show_all") &&
			Sym(h, g.destroy, "gtk_widget_destroy") &&
			Sym(h, g.present, "gtk_window_present") &&
			Sym(h, g.webViewType, "webkit_web_view_get_type") &&
			Sym(h, g.ephemeralContext, "webkit_web_context_new_ephemeral") &&
			Sym(h, g.contentManagerNew, "webkit_user_content_manager_new") &&
			Sym(h, g.registerHandler, "webkit_user_content_manager_register_script_message_handler") &&
			Sym(h, g.userScriptNew, "webkit_user_script_new") &&
			Sym(h, g.addScript, "webkit_user_content_manager_add_script") &&
			Sym(h, g.userScriptUnref, "webkit_user_script_unref") &&
			Sym(h, g.loadUri, "webkit_web_view_load_uri") &&
			Sym(h, g.loadHtml, "webkit_web_view_load_html") &&
			Sym(h, g.getUri, "webkit_web_view_get_uri") &&
			Sym(h, g.runJs, "webkit_web_view_run_javascript") &&
			Sym(h, g.runJsFinish, "webkit_web_view_run_javascript_finish") &&
			Sym(h, g.jsResultValue, "webkit_javascript_result_get_js_value") &&
			Sym(h, g.jsResultUnref, "webkit_javascript_result_unref") &&
			Sym(h, g.jscIsString, "jsc_value_is_string") &&
			Sym(h, g.jscToString, "jsc_value_to_string");
		// GTK on the display the program already uses (X11 or Wayland)
		g.ok = ok && g.gtkInitCheck(nullptr, nullptr);
		return g.ok;
	}

	unsigned long Connect(void* obj, const char* signal, GCallback cb, void* data)
	{
		return g.signalConnect(obj, signal, cb, data, nullptr, 0);
	}

	// A JSCValue's string, or "" (the value is not ours to free)
	std::string JscString(void* value)
	{
		if (!value || !g.jscIsString(value))
			return "";
		char* s = g.jscToString(value);
		std::string out = s ? s : "";
		g.gFree(s);
		return out;
	}

	// One window with a web view: the login page, or the captcha.
	struct Window
	{
		void* window = nullptr;
		void* view = nullptr;
		bool finished = false;
		bool test = false;
		bool captcha = false;
		std::function<void(const std::string&)> done; // the token or answer; "" when closed
	};

	Window* g_login = nullptr;
	Window* g_captcha = nullptr;

	void Finish(Window* w, const std::string& value)
	{
		if (w->finished)
			return;
		w->finished = true;
		if (w->captcha && value.empty())
			fprintf(stderr, "dm: captcha: window closed unsolved\n");
		if (w == g_login)
			g_login = nullptr;
		if (w == g_captcha)
			g_captcha = nullptr;
		auto done = w->done;
		void* win = w->window;
		w->window = nullptr;
		if (win)
			g.destroy(win); // ("destroy" sees it finished)
		if (done)
			done(value);
	}

	// (the Window stays allocated: a script's answer may still come for it)
	void OnDestroy(void*, void* data)
	{
		Window* w = (Window*) data;
		w->window = nullptr;
		if (!w->finished)
			Finish(w, "");
	}

	void OnToken(void*, void* jsResult, void* data)
	{
		Window* w = (Window*) data;
		if (w->test)
			return;
		std::string t = JscString(g.jsResultValue(jsResult));
		if (!t.empty())
			Finish(w, t);
	}

	void OnCaptcha(void*, void* jsResult, void* data)
	{
		std::string a = JscString(g.jsResultValue(jsResult));
		if (!a.empty())
			Finish((Window*) data, a);
	}

	void OnCaptchaLog(void*, void* jsResult, void* data)
	{
		Window* w = (Window*) data;
		std::string m = JscString(g.jsResultValue(jsResult));
		fprintf(stderr, "dm: captcha: %s\n", m.c_str());
		// the test: shown is as far as it goes without a person
		if (w->test && m.compare(0, 12, "widget shown") == 0)
			Finish(w, "");
	}

	// the stored token, once past the login (should the requests not have
	// given it already); or, in the test, the page's title
	void OnScriptDone(void* view, void* res, void* data)
	{
		Window* w = (Window*) data;
		void* result = g.runJsFinish(view, res, nullptr);
		std::string s = result ? JscString(g.jsResultValue(result)) : "";
		if (result)
			g.jsResultUnref(result);
		if (w->finished)
			return;
		if (w->test) {
			fprintf(stderr, "dm: web login test: %s: %s\n", g.getUri(view) ? g.getUri(view) : "", s.c_str());
			Finish(w, "");
		}
		else if (s.size() >= 30)
			Finish(w, s);
	}

	void OnLoadChanged(void* view, int event, void* data)
	{
		Window* w = (Window*) data;
		const int LOAD_FINISHED = 3;
		if (event != LOAD_FINISHED || w->finished)
			return;
		if (w->test) {
			g.runJs(view, WebLoginPages::kTestProbe, nullptr, OnScriptDone, w);
			return;
		}
		const char* uri = g.getUri(view);
		const char* path = uri ? strstr(uri, "discord.com/") : nullptr;
		if (path && (!strncmp(path + 11, "/channels", 9) || !strncmp(path + 11, "/app", 4)))
			g.runJs(view, WebLoginPages::kStoredToken, nullptr, OnScriptDone, w);
	}

	// A window with a web view whose pages may call the named handlers;
	// script runs at the start of every page, before the page's own.
	Window* Open(const char* title, int width, int height, bool test,
		const char* const* handlers, const GCallback* callbacks, int n, const std::string& script)
	{
		Window* w = new Window;
		w->test = test;
		void* ucm = g.contentManagerNew();
		if (!script.empty()) {
			void* us = g.userScriptNew(script.c_str(), 1 /* top frame */, 0 /* document start */, nullptr, nullptr);
			g.addScript(ucm, us);
			g.userScriptUnref(us);
		}
		for (int i = 0; i < n; i++) {
			g.registerHandler(ucm, handlers[i]);
			std::string signal = std::string("script-message-received::") + handlers[i];
			Connect(ucm, signal.c_str(), callbacks[i], w);
		}
		void* context = g.ephemeralContext(); // nothing stays behind
		w->view = g.objectNew(g.webViewType(), "web-context", context, "user-content-manager", ucm, (char*) nullptr);
		g.objectUnref(context);
		g.objectUnref(ucm);
		w->window = test ? g.offscreenNew() : g.windowNew(0 /* GTK_WINDOW_TOPLEVEL */);
		g.setTitle(w->window, title);
		g.setDefaultSize(w->window, width, height);
		g.setPosition(w->window, 1 /* GTK_WIN_POS_CENTER */);
		g.containerAdd(w->window, w->view);
		Connect(w->window, "destroy", (GCallback) OnDestroy, w);
		Connect(w->view, "load-changed", (GCallback) OnLoadChanged, w);
		g.showAll(w->window);
		return w;
	}

	Window* OpenLogin(bool test)
	{
		static const char* const handlers[] = { "dmToken" };
		static const GCallback callbacks[] = { (GCallback) OnToken };
		// the token watcher, shared with the other systems
		Window* w = Open("Log in to Discord", 520, 760, test, handlers, callbacks, 1, WebLoginPages::TokenWatcher());
		g.loadUri(w->view, "https://discord.com/login");
		return w;
	}
}

bool WebLogin::Available()
{
	return Load();
}

bool WebLogin::Busy()
{
	return g_login || g_captcha;
}

void WebLogin::Pump()
{
	if (!g.ok)
		return;
	for (int i = 0; i < 100 && g.iteration(nullptr, 0); i++) {}
}

void WebLogin::Open(std::function<void(const std::string&)> done, std::function<void()> cancelled)
{
	if (!Load()) {
		if (cancelled)
			cancelled();
		return;
	}
	if (g_login) {
		if (g_login->window)
			g.present(g_login->window);
		return;
	}
	g_login = OpenLogin(false);
	g_login->done = [done, cancelled](const std::string& token) {
		if (!token.empty()) {
			if (done)
				done(token);
		}
		else if (cancelled)
			cancelled();
	};
}

void WebLogin::ShowCaptcha(const std::string& sitekey, const std::string& rqdata,
	std::function<void(const std::string&)> done, std::function<void()> cancelled)
{
	if (!Load()) {
		if (cancelled)
			cancelled();
		return;
	}
	if (g_captcha) {
		if (g_captcha->window)
			g.present(g_captcha->window);
		return;
	}
	static const char* const handlers[] = { "dmCaptcha", "dmCaptchaLog" };
	static const GCallback callbacks[] = { (GCallback) OnCaptcha, (GCallback) OnCaptchaLog };
	g_captcha = ::Open("Discord: are you human?", 420, 640, false, handlers, callbacks, 2, "");
	g_captcha->captcha = true;
	g_captcha->done = [done, cancelled](const std::string& answer) {
		if (!answer.empty()) {
			if (done)
				done(answer);
		}
		else if (cancelled)
			cancelled();
	};
	// hCaptcha's widget, as Discord's page shows it (the page is Discord's
	// for the widget: the site key is Discord's)
	g.loadHtml(g_captcha->view, WebLoginPages::CaptchaPage(sitekey, rqdata).c_str(), "https://discord.com/");
	fprintf(stderr, "dm: captcha: window open (%s)\n", rqdata.empty() ? "no rqdata" : "with rqdata");
}

void WebLogin::SelfTest(std::function<void()> finished)
{
	if (!Load()) {
		fprintf(stderr, "dm: web login test: WebKitGTK is not available\n");
		if (finished)
			finished();
		return;
	}
	// DM_TEST_WEBLOGIN=captcha: the captcha's page with hCaptcha's test key,
	// off screen, until the widget is shown
	const char* what = getenv("DM_TEST_WEBLOGIN");
	if (what && !strcmp(what, "captcha")) {
		static const char* const handlers[] = { "dmCaptcha", "dmCaptchaLog" };
		static const GCallback callbacks[] = { (GCallback) OnCaptcha, (GCallback) OnCaptchaLog };
		g_captcha = ::Open("Discord: are you human?", 420, 640, true, handlers, callbacks, 2, "");
		g_captcha->captcha = true;
		g_captcha->done = [finished](const std::string&) { if (finished) finished(); };
		g.loadHtml(g_captcha->view, WebLoginPages::CaptchaPage("10000000-ffff-ffff-ffff-000000000001", "").c_str(),
			"https://discord.com/");
		return;
	}
	g_login = OpenLogin(true);
	g_login->done = [finished](const std::string&) { if (finished) finished(); };
}
#endif
