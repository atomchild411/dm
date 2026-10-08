// WebLogin on Windows: windows with Microsoft's Edge WebView2 (the
// runtime Windows 10 and 11 carry), as WebLogin_mac.mm does with WebKit.
// Its views are InPrivate: no cookies or storage stay behind.  WebView2
// calls back on the thread that made it, the UI thread, through the
// messages GLFW's loop dispatches.

#if defined(_WIN32)
#include "WebLogin.hpp"
#include "WebLoginPages.hpp"

#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>

#include <WebView2.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "posix/MainQueue.hpp"

namespace
{
	std::wstring Wide(const std::string& s)
	{
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
		std::wstring w(n > 0 ? n - 1 : 0, L'\0');
		if (n > 1)
			MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
		return w;
	}

	std::string Narrow(const wchar_t* w)
	{
		if (!w)
			return "";
		int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
		std::string s(n > 0 ? n - 1 : 0, '\0');
		if (n > 1)
			WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
		return s;
	}

	// A WebView2 callback: the COM object around a function, for the handler
	// interface I whose Invoke takes A...
	template <class I, class... A>
	class Handler : public I
	{
	public:
		explicit Handler(std::function<HRESULT(A...)> fn) : m_fn(std::move(fn)) {}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			ULONG n = --m_refs;
			if (n == 0)
				delete this;
			return n;
		}
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
		{
			if (iid == __uuidof(IUnknown) || iid == __uuidof(I)) {
				*out = static_cast<I*>(this);
				AddRef();
				return S_OK;
			}
			*out = nullptr;
			return E_NOINTERFACE;
		}
		HRESULT STDMETHODCALLTYPE Invoke(A... args) override { return m_fn(args...); }
	private:
		std::atomic<ULONG> m_refs{ 1 };
		std::function<HRESULT(A...)> m_fn;
	};

	// Hands a new handler to call(handler), then lets go of our reference.
	template <class I, class... A, class F, class C>
	HRESULT With(F fn, C call)
	{
		I* h = new Handler<I, A...>(std::function<HRESULT(A...)>(fn));
		HRESULT hr = call(h);
		h->Release();
		return hr;
	}

	ICoreWebView2Environment* g_env;

	bool InitCom()
	{
		static bool done = false, ok = false;
		if (!done) {
			done = true;
			HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
		}
		return ok;
	}

	// The browser's own files (not the pages'): %LOCALAPPDATA%\DiscordMessenger\WebView2
	std::wstring UserDataFolder()
	{
		wchar_t buf[MAX_PATH * 2];
		DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH * 2);
		std::wstring dir = n > 0 && n < MAX_PATH * 2 ? buf : L".";
		dir += L"\\DiscordMessenger";
		CreateDirectoryW(dir.c_str(), nullptr);
		dir += L"\\WebView2";
		CreateDirectoryW(dir.c_str(), nullptr);
		return dir;
	}

	// One window with one web view.
	struct Page
	{
		HWND hwnd = nullptr;
		ICoreWebView2Controller* controller = nullptr;
		ICoreWebView2* web = nullptr;
		bool closed = false;
		std::string captchaHtml; // served as https://discord.com/dm-captcha

		std::function<void(Page*)> ready;                       // the view is there
		std::function<void(Page*, const std::string&)> message; // "name:value"
		std::function<void(Page*, const std::string&)> loaded;  // a navigation completed: the URL
		std::function<void(Page*)> userClosed;                  // the user closed the window
	};

	void ClosePage(Page* p)
	{
		if (p->closed)
			return;
		p->closed = true;
		if (p->controller) {
			p->controller->Close();
			p->controller->Release();
			p->controller = nullptr;
		}
		if (p->web) {
			p->web->Release();
			p->web = nullptr;
		}
		if (p->hwnd) {
			SetWindowLongPtrW(p->hwnd, GWLP_USERDATA, 0);
			DestroyWindow(p->hwnd);
			p->hwnd = nullptr;
		}
		// the rest goes once WebView2's callbacks in flight are done
		MainQueue::Post([p] { delete p; });
	}

	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		Page* p = (Page*) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
		switch (msg) {
		case WM_SIZE:
			if (p && p->controller) {
				RECT r;
				GetClientRect(hwnd, &r);
				p->controller->put_Bounds(r);
			}
			return 0;
		case WM_CLOSE:
			if (p && p->userClosed)
				p->userClosed(p);
			else if (p)
				ClosePage(p);
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wp, lp);
	}

	HWND MakeWindow(const wchar_t* title, int w, int h, bool show)
	{
		static bool registered = false;
		HINSTANCE inst = GetModuleHandleW(nullptr);
		if (!registered) {
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof wc;
			wc.lpfnWndProc = WndProc;
			wc.hInstance = inst;
			wc.hCursor = LoadCursorW(nullptr, (LPCWSTR) IDC_ARROW);
			wc.hIcon = LoadIconW(inst, L"GLFW_ICON");
			wc.hbrBackground = CreateSolidBrush(RGB(0x31, 0x33, 0x38));
			wc.lpszClassName = L"DiscordMessengerWebView";
			RegisterClassExW(&wc);
			registered = true;
		}
		RECT r = { 0, 0, w, h };
		AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
		int ww = r.right - r.left, wh = r.bottom - r.top;
		int x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
		HWND hwnd = CreateWindowExW(0, L"DiscordMessengerWebView", title, WS_OVERLAPPEDWINDOW,
			x, y, ww, wh, nullptr, nullptr, inst, nullptr);
		if (hwnd && show) {
			ShowWindow(hwnd, SW_SHOWNORMAL);
			SetForegroundWindow(hwnd);
		}
		return hwnd;
	}

	void Fail(Page* p, const char* what, HRESULT hr)
	{
		fprintf(stderr, "dm: WebView2: %s failed (0x%08lx)\n", what, (unsigned long) hr);
		if (p->userClosed)
			p->userClosed(p); // as if the user gave up: the caller's cancelled()
		else
			ClosePage(p);
	}

	// The view in the page's window; then p->ready.
	void MakeView(Page* p, ICoreWebView2Environment* env)
	{
		auto controllerMade = [p](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
			if (p->closed)
				return S_OK;
			if (FAILED(hr) || !controller) {
				Fail(p, "creating the web view", hr);
				return S_OK;
			}
			p->controller = controller;
			controller->AddRef();
			controller->get_CoreWebView2(&p->web);
			RECT r;
			GetClientRect(p->hwnd, &r);
			controller->put_Bounds(r);

			EventRegistrationToken tok;
			With<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
				[p](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
					LPWSTR text = nullptr;
					if (!p->closed && SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text && p->message)
						p->message(p, Narrow(text));
					CoTaskMemFree(text);
					return S_OK;
				},
				[&](ICoreWebView2WebMessageReceivedEventHandler* h) { return p->web->add_WebMessageReceived(h, &tok); });
			With<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
				[p](ICoreWebView2* web, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
					LPWSTR uri = nullptr;
					if (!p->closed && SUCCEEDED(web->get_Source(&uri)) && p->loaded)
						p->loaded(p, Narrow(uri));
					CoTaskMemFree(uri);
					return S_OK;
				},
				[&](ICoreWebView2NavigationCompletedEventHandler* h) { return p->web->add_NavigationCompleted(h, &tok); });

			// the captcha's page, as discord.com's (hCaptcha checks the host)
			if (!p->captchaHtml.empty()) {
				p->web->AddWebResourceRequestedFilter(L"https://discord.com/dm-captcha*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
				With<ICoreWebView2WebResourceRequestedEventHandler, ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>(
					[p](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
						IStream* body = SHCreateMemStream((const BYTE*) p->captchaHtml.data(), (UINT) p->captchaHtml.size());
						ICoreWebView2WebResourceResponse* response = nullptr;
						if (body && g_env && SUCCEEDED(g_env->CreateWebResourceResponse(body, 200, L"OK",
								L"Content-Type: text/html; charset=utf-8", &response))) {
							args->put_Response(response);
							response->Release();
						}
						if (body)
							body->Release();
						return S_OK;
					},
					[&](ICoreWebView2WebResourceRequestedEventHandler* h) { return p->web->add_WebResourceRequested(h, &tok); });
			}

			if (p->ready)
				p->ready(p);
			return S_OK;
		};

		// InPrivate where the runtime has it: nothing is kept
		ICoreWebView2Environment10* env10 = nullptr;
		ICoreWebView2ControllerOptions* opts = nullptr;
		HRESULT hr;
		if (SUCCEEDED(env->QueryInterface(__uuidof(ICoreWebView2Environment10), (void**) &env10)) &&
			SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(&opts))) {
			opts->put_IsInPrivateModeEnabled(TRUE);
			hr = With<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(controllerMade,
				[&](ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* h) {
					return env10->CreateCoreWebView2ControllerWithOptions(p->hwnd, opts, h);
				});
			opts->Release();
		}
		else {
			hr = With<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(controllerMade,
				[&](ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* h) {
					return env->CreateCoreWebView2Controller(p->hwnd, h);
				});
		}
		if (env10)
			env10->Release();
		if (FAILED(hr))
			Fail(p, "creating the web view", hr);
	}

	// A window with a view: once there, p->ready.
	Page* OpenPage(const wchar_t* title, int w, int h, bool show)
	{
		Page* p = new Page;
		p->hwnd = MakeWindow(title, w, h, show);
		if (!p->hwnd || !InitCom()) {
			delete p;
			return nullptr;
		}
		SetWindowLongPtrW(p->hwnd, GWLP_USERDATA, (LONG_PTR) p);
		if (g_env) {
			MakeView(p, g_env);
			return p;
		}
		HRESULT hr = With<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
			[p](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
				if (p->closed)
					return S_OK;
				if (FAILED(hr) || !env) {
					Fail(p, "starting WebView2", hr);
					return S_OK;
				}
				if (!g_env) {
					g_env = env;
					g_env->AddRef();
				}
				MakeView(p, g_env);
				return S_OK;
			},
			[&](ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* h) {
				return CreateCoreWebView2EnvironmentWithOptions(nullptr, UserDataFolder().c_str(), nullptr, h);
			});
		if (FAILED(hr))
			Fail(p, "starting WebView2", hr);
		return p;
	}

	// The watcher in every document, then the login page.
	void LoadLogin(Page* p)
	{
		std::wstring watcher = Wide(WebLoginPages::TokenWatcher());
		With<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler, HRESULT, LPCWSTR>(
			[p](HRESULT, LPCWSTR) -> HRESULT {
				if (!p->closed)
					p->web->Navigate(L"https://discord.com/login");
				return S_OK;
			},
			[&](ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler* h) {
				return p->web->AddScriptToExecuteOnDocumentCreated(watcher.c_str(), h);
			});
	}

	// The result of ExecuteScript (JSON) as a string, or "".
	std::string JsonString(LPCWSTR json)
	{
		try {
			nlohmann::json j = nlohmann::json::parse(Narrow(json));
			return j.is_string() ? j.get<std::string>() : "";
		}
		catch (...) {
			return "";
		}
	}

	Page* g_login;
	Page* g_captcha;
}

bool WebLogin::Available()
{
	static int available = -1;
	if (available < 0) {
		LPWSTR version = nullptr;
		available = SUCCEEDED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &version)) && version ? 1 : 0;
		CoTaskMemFree(version);
		if (!available)
			fprintf(stderr, "dm: WebView2 runtime not found: no login on discord.com's page\n");
	}
	return available == 1;
}

void WebLogin::Open(std::function<void(const std::string&)> done, std::function<void()> cancelled)
{
	if (g_login) {
		ShowWindow(g_login->hwnd, SW_SHOWNORMAL);
		SetForegroundWindow(g_login->hwnd);
		return;
	}
	// finish(token): "" if the user gave up
	auto finish = [done, cancelled](Page* p, const std::string& token) {
		if (p != g_login)
			return;
		g_login = nullptr;
		ClosePage(p);
		MainQueue::Post([done, cancelled, token] {
			if (!token.empty()) {
				if (done)
					done(token);
			}
			else if (cancelled)
				cancelled();
		});
	};
	Page* p = OpenPage(L"Log in to Discord", 520, 760, true);
	if (!p) {
		if (cancelled)
			cancelled();
		return;
	}
	g_login = p;
	p->ready = LoadLogin;
	p->message = [finish](Page* p, const std::string& m) {
		const std::string key = "dmToken:";
		if (m.compare(0, key.size(), key) == 0 && m.size() >= key.size() + 30)
			finish(p, m.substr(key.size()));
	};
	// past the login (the app's pages): the stored token, should the
	// requests not have given it already
	p->loaded = [finish](Page* p, const std::string& url) {
		std::string path = url.compare(0, 19, "https://discord.com") == 0 ? url.substr(19) : "";
		if (path.compare(0, 9, "/channels") != 0 && path.compare(0, 4, "/app") != 0)
			return;
		std::wstring script = Wide(WebLoginPages::kStoredToken);
		With<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(
			[p, finish](HRESULT hr, LPCWSTR json) -> HRESULT {
				std::string t = SUCCEEDED(hr) ? JsonString(json) : "";
				if (!p->closed && t.size() >= 30)
					finish(p, t);
				return S_OK;
			},
			[&](ICoreWebView2ExecuteScriptCompletedHandler* h) { return p->web->ExecuteScript(script.c_str(), h); });
	};
	p->userClosed = [finish](Page* p) { finish(p, ""); };
}

static void OpenCaptcha(const std::string& sitekey, const std::string& rqdata, bool test,
	std::function<void(const std::string&)> done, std::function<void()> cancelled)
{
	if (g_captcha) {
		ShowWindow(g_captcha->hwnd, SW_SHOWNORMAL);
		SetForegroundWindow(g_captcha->hwnd);
		return;
	}
	auto finish = [done, cancelled](Page* p, const std::string& answer) {
		if (p != g_captcha)
			return;
		g_captcha = nullptr;
		if (answer.empty())
			fprintf(stderr, "dm: captcha: window closed unsolved\n");
		ClosePage(p);
		MainQueue::Post([done, cancelled, answer] {
			if (!answer.empty()) {
				if (done)
					done(answer);
			}
			else if (cancelled)
				cancelled();
		});
	};
	Page* p = OpenPage(L"Discord: are you human?", 420, 640, !test);
	if (!p) {
		if (cancelled)
			cancelled();
		return;
	}
	g_captcha = p;
	p->captchaHtml = WebLoginPages::CaptchaPage(sitekey, rqdata);
	fprintf(stderr, "dm: captcha: window open (%s)\n", rqdata.empty() ? "no rqdata" : "with rqdata");
	p->ready = [](Page* p) { p->web->Navigate(L"https://discord.com/dm-captcha"); };
	p->message = [finish, test](Page* p, const std::string& m) {
		size_t colon = m.find(':');
		std::string name = m.substr(0, colon), value = colon == std::string::npos ? "" : m.substr(colon + 1);
		if (name == "dmCaptchaLog") {
			fprintf(stderr, "dm: captcha: %s\n", value.c_str());
			// the test: shown is as far as it goes without a person
			if (test && value.compare(0, 12, "widget shown") == 0)
				finish(p, "");
		}
		else if (name == "dmCaptcha" && !value.empty())
			finish(p, value);
	};
	p->userClosed = [finish](Page* p) { finish(p, ""); };
}

void WebLogin::ShowCaptcha(const std::string& sitekey, const std::string& rqdata,
	std::function<void(const std::string&)> done, std::function<void()> cancelled)
{
	OpenCaptcha(sitekey, rqdata, false, done, cancelled);
}

void WebLogin::SelfTest(std::function<void()> finished)
{
	// DM_TEST_WEBLOGIN=captcha: the captcha's page with hCaptcha's test key,
	// hidden, until the widget is shown
	const char* what = getenv("DM_TEST_WEBLOGIN");
	if (what && !strcmp(what, "captcha")) {
		OpenCaptcha("10000000-ffff-ffff-ffff-000000000001", "", true, nullptr, finished);
		return;
	}
	Page* p = OpenPage(L"Log in to Discord", 520, 760, false);
	if (!p) {
		if (finished)
			finished();
		return;
	}
	p->ready = LoadLogin;
	p->loaded = [finished](Page* p, const std::string& url) {
		std::wstring probe = Wide(WebLoginPages::kTestProbe);
		With<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(
			[p, url, finished](HRESULT hr, LPCWSTR json) -> HRESULT {
				fprintf(stderr, "dm: web login test: %s: %s\n", url.c_str(),
					SUCCEEDED(hr) ? JsonString(json).c_str() : "the script failed");
				ClosePage(p);
				MainQueue::Post([finished] { if (finished) finished(); });
				return S_OK;
			},
			[&](ICoreWebView2ExecuteScriptCompletedHandler* h) { return p->web->ExecuteScript(probe.c_str(), h); });
	};
	p->userClosed = [finished](Page* p) {
		ClosePage(p);
		if (finished)
			finished();
	};
}
#endif
