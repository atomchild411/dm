// Discord Messenger with Dear ImGui, on GLFW and OpenGL 3 (Linux, macOS, Windows).
//
//   dm-imgui [--demo]
//
// DM_SNAPSHOT=file.png draws the window off screen (hidden, ignoring the
// mouse and keyboard), saves it as a PNG after DM_SNAPSHOT_AFTER seconds
// (default 15) and quits: for tests and screenshots, without a window
// popping up in front of anyone.
//
// The window is drawn afresh every frame; frames are made when something
// happens (input, a network reply, a timer), and the loop sleeps between.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <list>
#include <vector>
#include <sys/stat.h>

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

// Resources_win.cpp
void UseProgramResources();
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// stb's PNG writer (DM_SNAPSHOT); its one sprintf is the SDK's to call deprecated
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <stb/stb_image_write.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "DiscordInstance.hpp"
#include "config/LocalSettings.hpp"
#include "network/WebsocketClient.hpp"
#include "state/MessageCache.hpp"
#include "posix/Frontend_Posix.hpp"
#include "posix/MainQueue.hpp"
#include "posix/NetworkerThread.hpp"
#include "utils/Util.hpp"
#include "shared/ClientConfig.hpp"
#include "shared/Fonts.hpp"
#include "shared/ImageCache.hpp"
#include "shared/QrLogin.hpp"
#include "shared/Sound.hpp"
#include "shared/Timers.hpp"
#include "shared/Typing.hpp"
#include "App.hpp"
#include "SystemTheme.hpp"
#include "WebLogin.hpp"
#include "Gfx.hpp"

static DiscordInstance* g_pDiscordInstance;
static GLFWwindow* g_window;
static bool g_bQuit;

DiscordInstance* GetDiscordInstance()
{
	return g_pDiscordInstance;
}

// ---- timers on the frame loop ----------------------------------------------

namespace
{
	typedef std::chrono::steady_clock Clock;
	struct Timer { Clock::time_point due; std::function<void()> fn; bool cancelled; };
	std::list<Timer> g_timers;

	void RunDueTimers()
	{
		Clock::time_point now = Clock::now();
		for (auto it = g_timers.begin(); it != g_timers.end(); ) {
			if (it->cancelled) {
				it = g_timers.erase(it);
				continue;
			}
			if (it->due <= now) {
				auto fn = it->fn;
				it = g_timers.erase(it);
				fn();
				continue;
			}
			++it;
		}
	}

	// Seconds until the next timer (at most max).
	double TimeToNextTimer(double max)
	{
		Clock::time_point now = Clock::now();
		double t = max;
		for (auto& tm : g_timers)
			if (!tm.cancelled)
				t = std::min(t, std::chrono::duration<double>(tm.due - now).count());
		return std::max(0.0, t);
	}
}

// ---- the frontend ----------------------------------------------------------

class Frontend_ImGui : public Frontend_Posix
{
public:
	void OnConnecting() override { App::SetStatus("Connecting to Discord..."); }
	void OnConnected() override {
		App::SetStatus("");
		App::MarkDirty(App::LISTS);
		MainQueue::Post([] { App::RestoreLastChannel(); });
	}
	void OnSessionClosed(int errorCode) override {
		if (errorCode == CloseCode::TOO_MANY_LOGINS)
			App::SetStatus("Discord keeps ending the connection, so Discord Messenger stopped reconnecting.  Settings > Reconnect to try again.");
		else
			App::SetStatus("Disconnected (" + std::to_string(errorCode) + ").  Settings > Reconnect to try again.");
	}
	void OnLoggedOut() override {
		App::ShowLogin("Discord did not accept the token.  Log in again.");
	}
	void OnAddMessage(Snowflake channelID, const Message& msg) override {
		Frontend_Posix::OnAddMessage(channelID, msg);
		App::MarkDirty(App::LIST_GUILDS | App::MESSAGES);
		Typing::Stopped(channelID, msg.m_author_snowflake);
		Channel* pChan = GetDiscordInstance()->GetChannelGlobally(channelID);
		if (pChan && pChan->IsDM() && GetDiscordInstance()->ResortChannels(pChan->m_parentGuild))
			App::MarkDirty(App::LIST_CHANNELS);
	}
	void OnUpdateMessage(Snowflake channelID, const Message& msg) override {
		Frontend_Posix::OnUpdateMessage(channelID, msg);
		App::MarkDirty(App::MESSAGES);
	}
	void OnDeleteMessage(Snowflake message) override { App::MarkDirty(App::MESSAGES); }
	void OnStartTyping(Snowflake userID, Snowflake guildID, Snowflake channelID, time_t startTime) override {
		Typing::Started(userID, channelID);
	}
	void OnFailedToSendMessage(Snowflake channel, Snowflake message) override { App::MarkDirty(App::MESSAGES); }
	void UpdateSelectedGuild() override { App::MarkDirty(App::LISTS); }
	void UpdateSelectedChannel() override { App::OnChannelChanged(); }
	void UpdateChannelList() override { App::MarkDirty(App::LIST_CHANNELS); }
	void UpdateMemberList() override { App::MarkDirty(App::LIST_MEMBERS); }
	void UpdateChannelAcknowledge(Snowflake channelID, Snowflake messageID) override { App::MarkDirty(App::LIST_CHANNELS | App::LIST_GUILDS); }
	void RepaintGuildList() override { App::MarkDirty(App::LIST_GUILDS); }
	void RefreshMessages(ScrollDir::eScrollDir sd, Snowflake gapCulprit) override { App::MarkDirty(App::MESSAGES); }
	void RefreshMembers(const std::set<Snowflake>& members) override { App::MarkDirty(App::LIST_MEMBERS); }
	void UpdateUserData(Snowflake userID) override { App::MarkDirty(App::LIST_MEMBERS); }
	void UpdateProfileAvatar(Snowflake userID, const std::string& resid) override { App::MarkDirty(App::LIST_MEMBERS); }
	// the QR login's gateway is not the session's
	void OnWebsocketMessage(int gatewayID, const std::string& payload) override {
		if (gatewayID >= 0 && gatewayID == QrLogin::GatewayId()) {
			MainQueue::Post([payload] { QrLogin::OnGatewayMessage(payload); });
			return;
		}
		Frontend_Posix::OnWebsocketMessage(gatewayID, payload);
	}
	void OnWebsocketClose(int gatewayID, int errorCode, const std::string& message) override {
		if (gatewayID >= 0 && gatewayID == QrLogin::GatewayId()) {
			MainQueue::Post([errorCode, message] { QrLogin::OnGatewayClosed(errorCode, message); });
			return;
		}
		Frontend_Posix::OnWebsocketClose(gatewayID, errorCode, message);
	}
	void OnWebsocketFail(int gatewayID, int errorCode, const std::string& message, bool isTLSError, bool mayRetry) override {
		if (gatewayID >= 0 && gatewayID == QrLogin::GatewayId()) {
			MainQueue::Post([errorCode, message] { QrLogin::OnGatewayClosed(errorCode, message); });
			return;
		}
		Frontend_Posix::OnWebsocketFail(gatewayID, errorCode, message, isTLSError, mayRetry);
	}
	void OnAttachmentDownloaded(bool bIsProfilePicture, const uint8_t* pData, size_t nSize, const std::string& additData) override {
		ImageCache::Downloaded(additData, pData, nSize);
	}
	void OnAttachmentFailed(bool bIsProfilePicture, const std::string& additData) override {
		ImageCache::DownloadFailed(additData);
	}
	void SetHeartbeatInterval(int timeMs, int firstMs) override {
		Timers::Cancel(m_heartbeat);
		m_heartbeat = 0;
		m_heartbeatMs = timeMs;
		if (timeMs > 0)
			m_heartbeat = Timers::After(firstMs, [this] { Heartbeat(); });
	}
	void RequestQuit() override { g_bQuit = true; }
	// a mention or a direct message: a sound
	void OnNotification() override {
		if (IsNotifyOn(NOTIFY_SOUND))
			Sound::PlayNotification(nullptr);
	}
	bool IsWindowFocused() override {
		return g_window && glfwGetWindowAttrib(g_window, GLFW_FOCUSED);
	}

protected:
	void ShowError(const std::string& message) override { App::ShowError(message); }
	void ScheduleReconnect(int ms, std::function<void()> fn) override {
		App::SetStatus("Reconnecting...");
		Timers::After(ms, fn);
	}

private:
	void Heartbeat() {
		m_heartbeat = Timers::After(m_heartbeatMs, [this] { Heartbeat(); });
		GetDiscordInstance()->SendHeartbeat();
	}
	int m_heartbeat = 0;
	int m_heartbeatMs = 0;
};

static Frontend_ImGui* g_pFrontend;
static NetworkerThreadManager* g_pHTTPClient;

Frontend* GetFrontend()
{
	return g_pFrontend;
}

HTTPClient* GetHTTPClient()
{
	return g_pHTTPClient;
}

void StartWithToken()
{
	GetHTTPClient()->StopAllRequests();
	if (g_pDiscordInstance) {
		g_pDiscordInstance->CloseGatewaySession();
		delete g_pDiscordInstance;
	}
	g_pDiscordInstance = new DiscordInstance(GetLocalSettings()->GetToken());
	App::MarkDirty(App::LISTS | App::MESSAGES);
	g_pFrontend->StartSession();
}

void RequestLogout()
{
	if (g_pDiscordInstance)
		g_pDiscordInstance->CloseGatewaySession();
	g_pFrontend->CancelReconnect();
	// the account's messages and pictures do not stay behind it
	GetMessageCache()->ClearDiskCache();
	ImageCache::ClearDisk();
	GetLocalSettings()->SetToken("");
	GetLocalSettings()->Save();
	App::ShowLogin("");
}

void RequestReconnect()
{
	// (the session resumes: closing it first would end it)
	if (g_pDiscordInstance)
		g_pDiscordInstance->ReconnectNow();
}

// DM_SNAPSHOT: frames go into this framebuffer instead of the window.
static GLuint g_snapFbo, g_snapTex;

static void MakeSnapshotTarget(int w, int h)
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

// The frame just drawn, as a PNG (DM_SNAPSHOT).
static void Snapshot(const char* path, int w, int h)
{
	std::vector<unsigned char> px((size_t) w * h * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
	// OpenGL's rows run bottom up
	std::vector<unsigned char> flipped(px.size());
	for (int y = 0; y < h; y++)
		memcpy(&flipped[(size_t) y * w * 4], &px[(size_t) (h - 1 - y) * w * 4], (size_t) w * 4);
	for (size_t i = 3; i < flipped.size(); i += 4)
		flipped[i] = 255;
	stbi_write_png(path, w, h, 4, flipped.data(), w * 4);
}

static size_t CacheLimit(const char* name, size_t def)
{
	const char* v = getenv(name);
	long mb = v ? atol(v) : 0;
	return (mb > 0 ? (size_t) mb : def) * 1024 * 1024;
}

// The system's theme, every few seconds where asking is cheap.
static void PollSystemTheme()
{
	App::CheckSystemTheme();
	Timers::After(3000, PollSystemTheme);
}

static void SaveHistory()
{
	GetMessageCache()->SaveDirty();
	Timers::After(30000, SaveHistory);
}

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>

// In an app bundle (Contents/MacOS/the program): the fonts are in
// Contents/Resources/fonts, unless DM_FONT_DIR names others.
static void UseBundleResources()
{
	char path[PATH_MAX], real[PATH_MAX];
	uint32_t size = sizeof path;
	if (_NSGetExecutablePath(path, &size) != 0 || !realpath(path, real))
		return;
	std::string dir = real;
	dir = dir.substr(0, dir.rfind('/'));          // Contents/MacOS
	std::string res = dir.substr(0, dir.rfind('/')) + "/Resources";
	struct stat st;
	if (!getenv("DM_FONT_DIR") && stat((res + "/fonts").c_str(), &st) == 0)
		setenv("DM_FONT_DIR", (res + "/fonts").c_str(), 1);
}
#endif

#if defined(__linux__)
#include <unistd.h>

// A copy to run from anywhere (the .tar.gz, the AppImage): the fonts beside
// the program, unless DM_FONT_DIR names others.  (Installed, they are in
// DM_DATADIR.)
static void UseProgramResources()
{
	char path[4096];
	ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
	if (n <= 0)
		return;
	path[n] = 0;
	std::string fonts = std::string(path, strrchr(path, '/') - path) + "/fonts";
	struct stat st;
	if (!getenv("DM_FONT_DIR") && stat((fonts + "/DejaVuSans.ttf").c_str(), &st) == 0)
		setenv("DM_FONT_DIR", fonts.c_str(), 1);
}
#endif

int main(int argc, char** argv)
{
	bool demo = argc > 1 && !strcmp(argv[1], "--demo");
#if defined(__APPLE__)
	UseBundleResources();
#elif defined(_WIN32) || defined(__linux__)
	UseProgramResources();
#endif

	srand((unsigned) time(NULL));
	MainQueue::Init();
	SetupPosixPaths();
	Timers::SetBackend({
		[](int ms, std::function<void()> fn) -> void* {
			g_timers.push_back(Timer{ Clock::now() + std::chrono::milliseconds(ms), fn, false });
			return &g_timers.back();
		},
		[](void* handle) { ((Timer*) handle)->cancelled = true; }
	});

	if (!glfwInit()) {
		fprintf(stderr, "dm: GLFW could not start (is there a display?)\n");
		return 1;
	}
#if defined(__APPLE__)
	const char* glsl = "#version 150";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
	const char* glsl = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
	int winW = 1180, winH = 820;
	const char* snapshot = getenv("DM_SNAPSHOT");
	if (snapshot)
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	g_window = glfwCreateWindow(winW, winH, "Discord Messenger", nullptr, nullptr);
	if (!g_window) {
		fprintf(stderr, "dm: no OpenGL 3 window\n");
		return 1;
	}
	glfwMakeContextCurrent(g_window);
	glfwSwapInterval(snapshot ? 0 : 1);
	// network threads wake the loop
	MainQueue::SetWakeHook([] { glfwPostEmptyEvent(); });

	SetDefaultTextSize(15); // Discord's body text
	LoadClientConfig("imgui.conf");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	std::string fontErr;
	if (!Fonts::Init(fontErr, Fonts::INTER) || !Gfx::LoadUiFont(fontErr)) {
		fprintf(stderr, "dm: %s\n", fontErr.c_str());
		return 1;
	}
	ImGui::GetStyle().FontSizeBase = (float) GetTextSize();
	ImGui_ImplGlfw_InitForOpenGL(g_window, !snapshot);
	ImGui_ImplOpenGL3_Init(glsl);
	if (snapshot) {
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
		MakeSnapshotTarget(winW, winH);
	}

	g_pFrontend = new Frontend_ImGui;
	g_pHTTPClient = new NetworkerThreadManager;
	GetLocalSettings()->Load();

	// Caches in ~/.discordmessenger/cache, as in the Motif client
	ImageCache::SetDiskLimit(CacheLimit("DM_CACHE_MB", 64));
	if (!demo && !getenv("DM_NO_HISTORY")) {
		std::string dir = GetCachePath() + "/messages";
		mkdir(dir.c_str(), 0700);
		GetMessageCache()->SetDiskCache(dir, CacheLimit("DM_HISTORY_MB", 32));
	}
	ImageCache::SetChangedCallback([] { glfwPostEmptyEvent(); });

	g_pHTTPClient->Init();
	GetWebsocketClient()->Init();

	// DM_TOKEN logs in for this run only; it is not saved.
	std::string token = GetLocalSettings()->GetToken();
	const char* envToken = getenv("DM_TOKEN");
	if (envToken && *envToken)
		token = envToken;

	g_pDiscordInstance = new DiscordInstance(demo ? "" : token);
	if (const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor()))
		App::SetScreenSize(mode->width, mode->height);
	App::Init(demo);
	// the theme: as the system and the setting say, the window frames to match
	App::SetFrameHook([](bool dark, bool followSystem) { SystemTheme::FrameWindows(g_window, dark, followSystem); });
	App::ApplyTheme();
	if (SystemTheme::CheapToPoll())
		PollSystemTheme();
	else
		App::CheckSystemTheme();
	// DM_TEST_WEBLOGIN: discord.com's login page (or =captcha, the
	// captcha's) in a hidden browser view, then quit (a check that the page
	// and the token watcher load)
	if (getenv("DM_TEST_WEBLOGIN"))
		WebLogin::SelfTest([] { g_bQuit = true; });
	if (!demo) {
		Timers::After(30000, SaveHistory);
		if (token.empty())
			App::ShowLogin("");
		else
			g_pFrontend->StartSession();
	}

	// frames for a little while after anything happens (ImGui settles over
	// a couple of frames), then sleep until the next event or timer
	int busyFrames = 3;
	bool wasFocused = true;
	while (!g_bQuit && !App::QuitRequested() && !glfwWindowShouldClose(g_window))
	{
		if (busyFrames > 0)
			glfwPollEvents();
		else
			glfwWaitEventsTimeout(TimeToNextTimer(0.5));
		busyFrames = std::max(0, busyFrames - 1);
		MainQueue::Drain();
		RunDueTimers();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		if (snapshot)
			io.DisplayFramebufferScale = ImVec2(1, 1); // the off-screen frame
		ImGui::NewFrame();
		ImGui::GetStyle().FontSizeBase = (float) GetTextSize();
		// back in front: the system's theme may have changed meanwhile
		bool isFocused = glfwGetWindowAttrib(g_window, GLFW_FOCUSED) != 0;
		if (isFocused && !wasFocused)
			App::CheckSystemTheme();
		wasFocused = isFocused;
		App::Frame(isFocused);
		ImGui::Render();
		int fbW, fbH;
		glfwGetFramebufferSize(g_window, &fbW, &fbH);
		if (snapshot) {
			fbW = winW;
			fbH = winH;
			glBindFramebuffer(GL_FRAMEBUFFER, g_snapFbo);
		}
		glViewport(0, 0, fbW, fbH);
		glClearColor(0.19f, 0.2f, 0.22f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		if (snapshot) {
			const char* after = getenv("DM_SNAPSHOT_AFTER");
			if (glfwGetTime() > (after ? atof(after) : 15.0)) {
				Snapshot(snapshot, fbW, fbH);
				break;
			}
			busyFrames = 3; // frames keep coming until then
		}
		glfwSwapBuffers(g_window);

		if (ImGui::IsAnyItemActive() || io.MouseDown[0] || io.MouseWheel != 0)
			busyFrames = 3;
	}

	if (!demo)
		GetMessageCache()->SaveDirty();
	GetLocalSettings()->Save();

	g_pDiscordInstance->CloseGatewaySession();
	GetWebsocketClient()->Kill();
	MainQueue::Shutdown();
	g_pHTTPClient->Kill();

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(g_window);
	glfwTerminate();
	return 0;
}
