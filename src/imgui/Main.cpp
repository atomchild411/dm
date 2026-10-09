// Discord Messenger with Dear ImGui: on GLFW and OpenGL 3 (Linux, macOS,
// Windows), or on X11 and OpenGL 1.1 (IRIX); see Platform.hpp.
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

#if defined(_WIN32)
// Resources_win.cpp
void UseProgramResources();
#endif

#include "imgui.h"
#include "imgui_internal.h" // InputEventsQueue
#include "GL.hpp"
#include "Platform.hpp"

#include <png.h> // DM_SNAPSHOT's PNG

#include "DiscordInstance.hpp"
#include "config/LocalSettings.hpp"
#include "network/WebsocketClient.hpp"
#include "state/MessageCache.hpp"
#include "posix/Frontend_Posix.hpp"
#include "posix/MainQueue.hpp"
#include "posix/NetworkerThread.hpp"
#include "posix/SecretStore.hpp"
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

	// (true when one ran)
	bool RunDueTimers()
	{
		bool ran = false;
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
				ran = true;
				continue;
			}
			++it;
		}
		return ran;
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
		return Platform::Focused();
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
	// (and nothing more is asked with the token)
	if (g_pDiscordInstance) {
		g_pDiscordInstance->CloseGatewaySession();
		g_pDiscordInstance->SetToken("");
	}
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
	png_image img;
	memset(&img, 0, sizeof img);
	img.version = PNG_IMAGE_VERSION;
	img.width = (png_uint_32) w;
	img.height = (png_uint_32) h;
	img.format = PNG_FORMAT_RGBA;
	if (!png_image_write_to_file(&img, path, 0, flipped.data(), w * 4, nullptr))
		fprintf(stderr, "dm: could not write %s: %s\n", path, img.message);
}

static size_t CacheLimit(const char* name, size_t def)
{
	const char* v = getenv(name);
	long mb = v ? atol(v) : 0;
	return (mb > 0 ? (size_t) mb : def) * 1024 * 1024;
}

// The system's theme, every few seconds where asking is cheap.
// --bench: how long frames take.  The first second is not counted (the
// pictures arrive and the caches fill then).
class Bench
{
public:
	explicit Bench(int frames) : m_frames(frames) {}
	bool Running() const { return m_frames > 0; }
	void Start() { if (Running()) m_t0 = Platform::Time(); }
	void Built() { if (Running()) m_t1 = Platform::Time(); }
	void Drawn()
	{
		if (!Running())
			return;
		glFinish();
		m_t2 = Platform::Time();
	}

	// The mouse over the messages, the wheel turning: 40 frames up, 40 down.
	void Scroll(ImGuiIO& io)
	{
		io.AddMousePosEvent(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
		io.AddMouseWheelEvent(0, (m_n / 40) % 2 ? -1.0f : 1.0f);
	}

	// True when the frames are done (the times are printed then).
	bool Swapped(ImDrawData* dd)
	{
		double t3 = Platform::Time();
		if (t3 < 1.0)
			return false;
		m_build.push_back(m_t1 - m_t0);
		m_draw.push_back(m_t2 - m_t1);
		m_swap.push_back(t3 - m_t2);
		m_total.push_back(t3 - m_t0);
		m_vtx += dd ? dd->TotalVtxCount : 0;
		if (++m_n < m_frames)
			return false;
		double sum = 0;
		for (double t : m_total)
			sum += t;
		printf("dm bench: %d frames, %.1f frames a second, %ld vertices a frame\n", m_n, m_n / sum, m_vtx / m_n);
		printf("dm bench: %-6s %8s %8s %8s %8s (ms)\n", "", "mean", "median", "95%", "max");
		Row("build", m_build);
		Row("draw", m_draw);
		Row("swap", m_swap);
		Row("frame", m_total);
		fflush(stdout);
		return true;
	}

private:
	static void Row(const char* name, std::vector<double> v)
	{
		std::sort(v.begin(), v.end());
		double sum = 0;
		for (double t : v)
			sum += t;
		printf("dm bench: %-6s %8.2f %8.2f %8.2f %8.2f\n", name, 1000 * sum / v.size(), 1000 * v[v.size() / 2],
			1000 * v[v.size() * 95 / 100], 1000 * v.back());
	}

	int m_frames, m_n = 0;
	long m_vtx = 0;
	double m_t0 = 0, m_t1 = 0, m_t2 = 0;
	std::vector<double> m_build, m_draw, m_swap, m_total;
};

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
	// --bench [frames]: the demo drawn frame after frame while the messages
	// scroll up and down, then the times a frame took (build, draw, swap)
	int benchFrames = argc > 1 && !strcmp(argv[1], "--bench") ? (argc > 2 ? atoi(argv[2]) : 300) : 0;
	if (benchFrames)
		demo = true;
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

	int winW = 1180, winH = 820;
	const char* snapshot = getenv("DM_SNAPSHOT");
	std::string err;
	if (!Platform::Open(winW, winH, "Discord Messenger", snapshot != nullptr, err)) {
		fprintf(stderr, "dm: %s\n", err.c_str());
		return 1;
	}
	// network threads wake the loop
	MainQueue::SetWakeHook([] { Platform::Wake(); });

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
	Platform::InitImGui(!snapshot);
	if (snapshot && !benchFrames) {
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
	}
	if (snapshot)
		Platform::MakeSnapshotTarget(winW, winH);

	g_pFrontend = new Frontend_ImGui;
	g_pHTTPClient = new NetworkerThreadManager;
	// the demo, benchmarks and snapshots have no token, and leave the
	// user's (in the Keychain or the like) alone
	if (demo || snapshot || getenv("DM_TEST_WEBLOGIN"))
		SecretStore::Disable();
	GetLocalSettings()->Load();

	// Caches in ~/.discordmessenger/cache, as in the Motif client
	ImageCache::SetDiskLimit(CacheLimit("DM_CACHE_MB", 64));
	if (!demo && !getenv("DM_NO_HISTORY")) {
		std::string dir = GetCachePath() + "/messages";
		mkdir(dir.c_str(), 0700);
		GetMessageCache()->SetDiskCache(dir, CacheLimit("DM_HISTORY_MB", 32));
	}
	ImageCache::SetChangedCallback([] { Platform::Wake(); });

	g_pHTTPClient->Init();
	GetWebsocketClient()->Init();

	// DM_TOKEN logs in for this run only; it is not saved.
	std::string token = GetLocalSettings()->GetToken();
	const char* envToken = getenv("DM_TOKEN");
	if (envToken && *envToken)
		token = envToken;

	g_pDiscordInstance = new DiscordInstance(demo ? "" : token);
	int screenW, screenH;
	Platform::ScreenSize(screenW, screenH);
	if (screenW > 0)
		App::SetScreenSize(screenW, screenH);
	App::Init(demo);
	// the theme: as the system and the setting say, the window frames to match
	App::SetFrameHook([](bool dark, bool followSystem) {
		SystemTheme::FrameWindows((GLFWwindow*) Platform::Native(), dark, followSystem);
	});
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
	Bench bench(benchFrames);
	while (!g_bQuit && !App::QuitRequested() && !Platform::ShouldClose())
	{
		bool active = busyFrames > 0;
		if (busyFrames > 0)
			Platform::PollEvents();
		else
			active = Platform::WaitEvents(TimeToNextTimer(0.5));
		busyFrames = std::max(0, busyFrames - 1);
		active = MainQueue::Drain() || active;
		active = RunDueTimers() || active;
		// nothing new: no frame (but for a text box's blinking caret)
		if (!active && !io.WantTextInput && !snapshot && !bench.Running())
			continue;

		// input this frame: what the platform queued for ImGui
		bool input = ImGui::GetCurrentContext()->InputEventsQueue.Size > 0;
		bench.Start();
		Platform::NewFrame();
		if (bench.Running())
			bench.Scroll(io);
		if (snapshot) {
			// the off-screen frame, whatever size the screen let the window be
			io.DisplaySize = ImVec2((float) winW, (float) winH);
			io.DisplayFramebufferScale = ImVec2(1, 1);
		}
		ImGui::NewFrame();
		ImGui::GetStyle().FontSizeBase = (float) GetTextSize();
		// back in front: the system's theme may have changed meanwhile
		bool isFocused = Platform::Focused();
		if (isFocused && !wasFocused)
			App::CheckSystemTheme();
		wasFocused = isFocused;
		App::Frame(isFocused);
		ImGui::Render();
		bench.Built();
		int fbW, fbH;
		Platform::FramebufferSize(fbW, fbH);
		if (snapshot) {
			fbW = winW;
			fbH = winH;
			Platform::BindSnapshotTarget();
		}
		glViewport(0, 0, fbW, fbH);
		glClearColor(0.19f, 0.2f, 0.22f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		Platform::Render(ImGui::GetDrawData());
		bench.Drawn();
		if (snapshot && !bench.Running()) {
			const char* after = getenv("DM_SNAPSHOT_AFTER");
			if (Platform::Time() > (after ? atof(after) : 15.0)) {
				Snapshot(snapshot, fbW, fbH);
				break;
			}
			busyFrames = 3; // frames keep coming until then
		}
		Platform::Swap();
		if (bench.Running()) {
			busyFrames = 3;
			if (bench.Swapped(ImGui::GetDrawData()))
				break;
		}

		// a few more frames after any input, and until ImGui has used all of
		// it: it spreads events that came together over several frames (a
		// press and release within one slow frame would otherwise wait in its
		// queue for the next event, and the click would not happen)
		if (input || ImGui::IsAnyItemActive() || io.MouseDown[0] || io.MouseWheel != 0)
			busyFrames = std::max(busyFrames, 2);
		if (ImGui::GetCurrentContext()->InputEventsQueue.Size > 0)
			busyFrames = std::max(busyFrames, 1);
		// what this frame changed (a channel opened from a click) is drawn
		// over the next frames: the lists, then the messages, then the scroll
		if (App::Pending())
			busyFrames = std::max(busyFrames, 2);
	}

	if (!demo)
		GetMessageCache()->SaveDirty();
	GetLocalSettings()->Save();

	g_pDiscordInstance->CloseGatewaySession();
	GetWebsocketClient()->Kill();
	MainQueue::Shutdown();
	g_pHTTPClient->Kill();

	Platform::Close();
	return 0;
}
