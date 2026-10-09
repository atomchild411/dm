// Discord Messenger for X11/Motif (IRIX first).

#include "Xm.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

#include <X11/Xutil.h>
#include <Xm/Protocols.h>

#include "DiscordInstance.hpp"
#include "config/LocalSettings.hpp"
#include "network/WebsocketClient.hpp"
#include "state/MessageCache.hpp"
#include "posix/Frontend_Posix.hpp"
#include "posix/MainQueue.hpp"
#include "posix/NetworkerThread.hpp"
#include "posix/SecretStore.hpp"
#include "utils/Util.hpp"

#include "Canvas.hpp"
#include "Fonts.hpp"
#include "shared/ImageCache.hpp"
#include "shared/AppIcon.hpp"
#include "Bench.hpp"
#include "LogonDialog.hpp"
#include "QrLoginDialog.hpp"
#include "shared/QrLogin.hpp"
#include "MainWindow.hpp"
#include "MessageView.hpp"
#include "Notifier.hpp"
#include "ConversationWindow.hpp"
#include "shared/Perf.hpp"
#include "shared/Demo.hpp"
#include "shared/Timers.hpp"
#include "Theme.hpp"

static XtAppContext g_app;
static Widget g_toplevel;
static PixelFormat g_pixelFormat;
static DiscordInstance* g_pDiscordInstance;
static bool g_bQuit;

static Visual* g_visual;
static int g_depth;
static Colormap g_colormap;

DiscordInstance* GetDiscordInstance()
{
	return g_pDiscordInstance;
}

// Shells must be told the visual the application runs on, or they get the
// screen's default one (and X refuses them a colormap of another depth).
int AddVisualArgs(Arg* args, int n)
{
	XtSetArg(args[n], XmNvisual, g_visual); n++;
	XtSetArg(args[n], XmNdepth, g_depth); n++;
	XtSetArg(args[n], XmNcolormap, g_colormap); n++;
	return n;
}

static void ShowLogon(const std::string& why);

class Frontend_Motif : public Frontend_Posix
{
public:
	void OnConnecting() override {
		GetMainWindow()->SetStatus("Connecting to Discord...");
	}
	void OnConnected() override {
		GetMainWindow()->SetStatus("");
		GetMainWindow()->UpdateGuildList();
		// once the login data is in (this runs as it starts): where the
		// user was last time, and the conversations open in their windows
		MainQueue::Post([] {
			GetMainWindow()->RestoreLastChannel();
			Conversations::Reload();
		});
	}
	void OnSessionClosed(int errorCode) override {
		if (errorCode == CloseCode::TOO_MANY_LOGINS)
			GetMainWindow()->SetStatus("Discord keeps ending the connection, so Discord Messenger stopped reconnecting.  File > Reconnect to try again.");
		else
			GetMainWindow()->SetStatus("Disconnected (" + std::to_string(errorCode) + ").  File > Reconnect to try again.");
	}
	void OnLoggedOut() override {
		ShowLogon("Discord did not accept the token.  Log in again.");
	}
	void OnAddMessage(Snowflake channelID, const Message& msg) override {
		Frontend_Posix::OnAddMessage(channelID, msg);
		// mention counts may have changed: the server badges and the icon's name
		GetMainWindow()->ScheduleListUpdate(MainWindow::LIST_GUILDS);
		Conversations::Refresh(channelID);
		MainWindow* mw = GetMainWindow();
		if (mw->GetMessageView()->GetChannel() == channelID) {
			mw->GetMessageView()->Refresh();
			mw->OnStopTyping(channelID, msg.m_author_snowflake);
		}
		Channel* pChan = GetDiscordInstance()->GetChannelGlobally(channelID);
		if (pChan && pChan->IsDM() && GetDiscordInstance()->ResortChannels(pChan->m_parentGuild))
			UpdateChannelList();
	}
	void OnUpdateMessage(Snowflake channelID, const Message& msg) override {
		Frontend_Posix::OnUpdateMessage(channelID, msg);
		if (GetMainWindow()->GetMessageView()->GetChannel() == channelID)
			GetMainWindow()->GetMessageView()->Refresh();
		Conversations::Refresh(channelID);
	}
	void OnDeleteMessage(Snowflake message) override {
		GetMainWindow()->GetMessageView()->Refresh();
		Conversations::Refresh(0);
	}
	void OnStartTyping(Snowflake userID, Snowflake guildID, Snowflake channelID, time_t startTime) override {
		GetMainWindow()->OnTyping(userID, guildID, channelID, startTime);
	}
	void OnFailedToSendMessage(Snowflake channel, Snowflake message) override {
		if (GetMainWindow()->GetMessageView()->GetChannel() == channel)
			GetMainWindow()->GetMessageView()->Refresh();
		Conversations::Refresh(0);
	}
	void UpdateSelectedGuild() override { GetMainWindow()->UpdateSelectedGuild(); }
	void UpdateSelectedChannel() override { GetMainWindow()->UpdateSelectedChannel(); }
	// List updates from the gateway come in bursts: the rows are made once
	// for each burst (MainWindow::ScheduleListUpdate).
	void UpdateChannelList() override { GetMainWindow()->ScheduleListUpdate(MainWindow::LIST_CHANNELS); }
	void UpdateMemberList() override { GetMainWindow()->ScheduleListUpdate(MainWindow::LIST_MEMBERS); }
	void UpdateChannelAcknowledge(Snowflake channelID, Snowflake messageID) override {
		GetMainWindow()->ScheduleListUpdate(MainWindow::LIST_CHANNELS | MainWindow::LIST_GUILDS);
	}
	void RepaintGuildList() override { GetMainWindow()->ScheduleListUpdate(MainWindow::LIST_GUILDS); }
	void RefreshMessages(ScrollDir::eScrollDir sd, Snowflake gapCulprit) override {
		GetMainWindow()->GetMessageView()->Refresh();
		Conversations::Refresh(0);
	}
	void RefreshMembers(const std::set<Snowflake>& members) override {
		// only changes to the current server's list show
		for (Snowflake sf : members) {
			if (GetMainWindow()->ShowsMember(sf)) {
				GetMainWindow()->ScheduleListUpdate(MainWindow::LIST_MEMBERS);
				return;
			}
		}
	}
	// the QR login's gateway is not the session's
	void OnWebsocketMessage(int gatewayID, const std::string& payload) override {
		if (gatewayID >= 0 && gatewayID == QrLogin::GatewayId()) {
			MainQueue::Post([payload] { QrLogin::OnGatewayMessage(payload); });
			return;
		}
		MainQueue::Post([gatewayID, payload] {
			DiscordInstance* pInst = GetDiscordInstance();
			if (pInst->GetGatewayID() == gatewayID) {
				Perf::Scope perf(Perf::GATEWAY);
				pInst->HandleGatewayMessage(payload);
			}
		});
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
	void UpdateUserData(Snowflake userID) override {
		if (GetMainWindow()->ShowsMember(userID))
			GetMainWindow()->ScheduleListUpdate(MainWindow::LIST_MEMBERS);
	}
	void OnAttachmentDownloaded(bool bIsProfilePicture, const uint8_t* pData, size_t nSize, const std::string& additData) override {
		ImageCache::Downloaded(additData, pData, nSize);
	}
	void OnAttachmentFailed(bool bIsProfilePicture, const std::string& additData) override {
		ImageCache::DownloadFailed(additData);
	}
	void SetHeartbeatInterval(int timeMs, int firstMs) override {
		if (m_heartbeat)
			XtRemoveTimeOut(m_heartbeat);
		m_heartbeat = 0;
		m_heartbeatMs = timeMs;
		if (timeMs > 0)
			m_heartbeat = XtAppAddTimeOut(g_app, firstMs, HeartbeatCB, this);
	}
	void RequestQuit() override {
		g_bQuit = true;
	}
	// a mention or a direct message: a sound, and a popup if the window
	// is not in front
	void OnNotification() override {
		Notifier::OnNotification();
	}
	bool IsWindowFocused() override {
		return Notifier::IsFocused();
	}

protected:
	void ShowError(const std::string& message) override {
		GetMainWindow()->ShowError(message);
	}
	void ScheduleReconnect(int ms, std::function<void()> fn) override {
		GetMainWindow()->SetStatus("Reconnecting...");
		XtAppAddTimeOut(g_app, ms, ReconnectCB, new std::function<void()>(fn));
	}

private:
	static void HeartbeatCB(XtPointer client, XtIntervalId*) {
		Frontend_Motif* self = (Frontend_Motif*) client;
		self->m_heartbeat = XtAppAddTimeOut(g_app, self->m_heartbeatMs, HeartbeatCB, self);
		GetDiscordInstance()->SendHeartbeat();
	}
	static void ReconnectCB(XtPointer client, XtIntervalId*) {
		std::function<void()>* fn = (std::function<void()>*) client;
		(*fn)();
		delete fn;
	}

	XtIntervalId m_heartbeat = 0;
	int m_heartbeatMs = 0;
};

static Frontend_Motif* g_pFrontend;
static NetworkerThreadManager* g_pHTTPClient;

Frontend* GetFrontend()
{
	return g_pFrontend;
}

HTTPClient* GetHTTPClient()
{
	return g_pHTTPClient;
}

// Starts over with the token in the settings: a new DiscordInstance, a new
// gateway session.
static void StartWithToken()
{
	GetHTTPClient()->StopAllRequests();
	if (g_pDiscordInstance) {
		g_pDiscordInstance->CloseGatewaySession();
		delete g_pDiscordInstance;
	}
	g_pDiscordInstance = new DiscordInstance(GetLocalSettings()->GetToken());
	GetMainWindow()->UpdateGuildList();
	GetMainWindow()->UpdateSelectedChannel();
	g_pFrontend->StartSession();
}

static void ShowLogon(const std::string& why)
{
	static bool s_showing = false;
	if (s_showing)
		return;
	s_showing = true;
	auto done = [](const std::string& token) {
		s_showing = false;
		if (token.empty()) {
			g_bQuit = true;
			return;
		}
		GetLocalSettings()->SetToken(token);
		GetLocalSettings()->Save();
		StartWithToken();
	};
	// a pasted token (the QR code only with DM_QR_LOGIN=1: see
	// QrLogin::Enabled)
	if (!QrLogin::Enabled()) {
		ShowLogonDialog(g_toplevel, why, done);
		return;
	}
	// a QR code for the phone app first; a pasted token on request
	QrLoginDialog::Show(g_toplevel, g_pixelFormat, why, done, [why, done] {
		ShowLogonDialog(g_toplevel, why, done);
	});
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
	Conversations::CloseAll();
	GetMessageCache()->ClearDiskCache();
	ImageCache::ClearDisk();
	GetLocalSettings()->SetToken("");
	GetLocalSettings()->Save();
	ShowLogon("");
}

void RequestReconnect()
{
	// (the session resumes: closing it first would end it)
	if (g_pDiscordInstance)
		g_pDiscordInstance->ReconnectNow();
}

// --demo: sample messages and lists, without logging in (to see how
// messages are drawn).
static void LoadDemo()
{
	Demo::LoadMessages();
	const Snowflake chan = Demo::CHANNEL;
	GetMainWindow()->GetMessageView()->SetChannel(0, chan);
	GetMainWindow()->ShowDemoLists();
	GetMainWindow()->SetStatus("Demo: sample messages, not connected.");
	if (getenv("DM_TEST_NOTIFY"))
		XtAppAddTimeOut(g_app, 5000, [](XtPointer, XtIntervalId*) { Notifier::Test(); }, NULL);
}

// Size limits of the caches, in megabytes: name's value, else def.
static size_t CacheLimit(const char* name, size_t def)
{
	const char* v = getenv(name);
	long mb = v ? atol(v) : 0;
	return (mb > 0 ? (size_t) mb : def) * 1024 * 1024;
}

// The message history goes to disk every half minute (and at exit).
static void SaveHistoryCB(XtPointer, XtIntervalId*)
{
	GetMessageCache()->SaveDirty();
	XtAppAddTimeOut(g_app, 30000, SaveHistoryCB, NULL);
}

// DM_PERF: the timings so far, every minute.
static void PerfReportCB(XtPointer, XtIntervalId*)
{
	Perf::Report(stderr, "since the start");
	XtAppAddTimeOut(g_app, 60000, PerfReportCB, NULL);
}

static void WakeCB(XtPointer, int*, XtInputId*)
{
	MainQueue::Drain();
}

static void WmDeleteCB(Widget, XtPointer, XtPointer)
{
	g_bQuit = true;
}

// The visual to draw on: the default one when it is TrueColor, else a
// 24-bit (or deeper) TrueColor one when the screen has it, else the default
// (8-bit colour is dithered).  DM_VISUAL=default keeps the default visual;
// DM_VISUAL=0x2b (an id xdpyinfo lists) picks that one.
static void PickVisual(Display* dpy)
{
	int scr = DefaultScreen(dpy);
	g_visual = DefaultVisual(dpy, scr);
	g_depth = DefaultDepth(dpy, scr);
	g_colormap = DefaultColormap(dpy, scr);

	const char* pref = getenv("DM_VISUAL");
	if (pref && !strncmp(pref, "0x", 2)) {
		XVisualInfo tmpl;
		int n = 0;
		tmpl.visualid = strtoul(pref, NULL, 16);
		tmpl.screen = scr;
		XVisualInfo* vi = XGetVisualInfo(dpy, VisualIDMask | VisualScreenMask, &tmpl, &n);
		if (vi && n > 0) {
			g_visual = vi[0].visual;
			g_depth = vi[0].depth;
			if (g_visual != DefaultVisual(dpy, scr))
				g_colormap = XCreateColormap(dpy, RootWindow(dpy, scr), g_visual, AllocNone);
		}
		if (vi)
			XFree(vi);
		return;
	}
	if ((pref && !strcmp(pref, "default")) || g_visual->c_class == TrueColor)
		return;

	XVisualInfo vi;
	if (XMatchVisualInfo(dpy, scr, 24, TrueColor, &vi) || XMatchVisualInfo(dpy, scr, 32, TrueColor, &vi)) {
		g_visual = vi.visual;
		g_depth = vi.depth;
		g_colormap = XCreateColormap(dpy, RootWindow(dpy, scr), g_visual, AllocNone);
	}
}

// The picture 4Dwm shows for the minimised window: the app icon on a soft
// gradient, at the window manager's largest icon size (85 x 67 on IRIX).
// It is made on the screen's default visual, which window managers draw
// icons with (the app itself may run on a deeper one).
static Pixmap MakeIconPixmap(Display* dpy)
{
	int scr = DefaultScreen(dpy);
	int w = 85, h = 67;
	XIconSize* sizes = nullptr;
	int count = 0;
	if (XGetIconSizes(dpy, RootWindow(dpy, scr), &sizes, &count) && count > 0) {
		w = std::max(sizes[0].min_width, std::min(w, sizes[0].max_width));
		h = std::max(sizes[0].min_height, std::min(h, sizes[0].max_height));
		XFree(sizes);
	}

	Canvas c;
	c.Resize(w, h);
	for (int y = 0; y < h; y++)
		c.HLine(0, y, w, LerpRgb(0xe8eefa, 0xa9b6d8, y, h - 1));

	int s = std::min(APP_ICON_SIZE, std::min(w, h) - 4);
	int x0 = (w - s) / 2, y0 = (h - s) / 2;
	// a soft shadow under the sphere
	for (int k = 3; k >= 1; k--) {
		std::vector<uint32_t> disc((size_t) (s + 2 * k) * (s + 2 * k), ((uint32_t) (18 * (4 - k)) << 24) | 0x1a2040);
		c.BlendArgbCircle(x0 - k + 2, y0 - k + 3, disc.data(), s + 2 * k, s + 2 * k, s + 2 * k);
	}
	if (s == APP_ICON_SIZE) {
		c.BlendArgb(x0, y0, g_appIcon, s, s, s);
	}
	else {
		Image src, out;
		src.w = src.h = APP_ICON_SIZE;
		src.px.assign(g_appIcon, g_appIcon + APP_ICON_SIZE * APP_ICON_SIZE);
		// nearest scaling is enough for the rare smaller icon sizes
		out.w = out.h = s;
		out.px.resize((size_t) s * s);
		for (int y = 0; y < s; y++)
			for (int x = 0; x < s; x++)
				out.px[(size_t) y * s + x] = src.px[(size_t) (y * APP_ICON_SIZE / s) * APP_ICON_SIZE + x * APP_ICON_SIZE / s];
		c.BlendArgb(x0, y0, out.px.data(), s, s, s);
	}

	PixelFormat fmt;
	fmt.Init(dpy, DefaultVisual(dpy, scr), DefaultDepth(dpy, scr), DefaultColormap(dpy, scr));
	Pixmap pm = XCreatePixmap(dpy, RootWindow(dpy, scr), w, h, DefaultDepth(dpy, scr));
	GC gc = XCreateGC(dpy, pm, 0, NULL);
	c.Present(fmt, pm, gc, 0, 0, w, h, 0, 0);
	XFreeGC(dpy, gc);
	return pm;
}

static XtString g_fallbackResources[] = {
	(XtString) "*sgiMode: True",
	(XtString) "*useSchemes: all",
	(XtString) "DiscordMessenger*guilds.visibleItemCount: 20",
	(XtString) "DiscordMessenger*channels.visibleItemCount: 20",
	(XtString) "DiscordMessenger*members.visibleItemCount: 20",
	(XtString) "DiscordMessenger*header.fontList: -*-helvetica-bold-r-normal--14-*-*-*-*-*-iso8859-1",
	NULL
};

int main(int argc, char** argv)
{
	srand((unsigned) time(NULL));
	MainQueue::Init();
	SetupPosixPaths();

	XtToolkitInitialize();
	g_app = XtCreateApplicationContext();
	// the shared layer's timers are Xt timeouts
	struct XtTimer { XtIntervalId id; std::function<void()> fn; };
	Timers::SetBackend({
		[](int ms, std::function<void()> fn) -> void* {
			XtTimer* t = new XtTimer{ 0, fn };
			t->id = XtAppAddTimeOut(g_app, ms, [](XtPointer p, XtIntervalId*) {
				XtTimer* t = (XtTimer*) p;
				auto fn = t->fn;
				delete t;
				fn();
			}, t);
			return t;
		},
		[](void* handle) {
			XtTimer* t = (XtTimer*) handle;
			XtRemoveTimeOut(t->id);
			delete t;
		}
	});
	XtAppSetFallbackResources(g_app, g_fallbackResources);
	Display* dpy = XtOpenDisplay(g_app, NULL, "dm", "DiscordMessenger", NULL, 0, &argc, argv);
	if (!dpy) {
		fprintf(stderr, "dm: cannot open the display (is DISPLAY set?)\n");
		return 1;
	}

	LoadClientConfig("motif.conf");
	PickVisual(dpy);
	g_pixelFormat.Init(dpy, g_visual, g_depth, g_colormap);

	Arg args[8];
	int n = 0;
	n = AddVisualArgs(args, n);
	XtSetArg(args[n], XmNtitle, "Discord Messenger"); n++;
	XtSetArg(args[n], XmNiconName, "Discord"); n++;
	XtSetArg(args[n], XmNiconPixmap, MakeIconPixmap(dpy)); n++;
	g_toplevel = XtAppCreateShell("dm", "DiscordMessenger", applicationShellWidgetClass, dpy, args, n);

	std::string fontErr;
	if (!Fonts::Init(fontErr)) {
		fprintf(stderr, "dm: %s\n", fontErr.c_str());
		return 1;
	}

	g_pFrontend = new Frontend_Motif;
	g_pHTTPClient = new NetworkerThreadManager;
	// the demo and the benchmark have no token, and leave the user's alone
	if (argc > 1 && (!strcmp(argv[1], "--demo") || !strcmp(argv[1], "--bench")))
		SecretStore::Disable();
	GetLocalSettings()->Load();

	// Caches in ~/.discordmessenger/cache: pictures (DM_CACHE_MB, 64 MB) and
	// the newest messages of the channels opened (DM_HISTORY_MB, 32 MB;
	// DM_NO_HISTORY=1 keeps none).
	ImageCache::SetDiskLimit(CacheLimit("DM_CACHE_MB", 64));
	if (!getenv("DM_NO_HISTORY")) {
		std::string dir = GetCachePath() + "/messages";
		mkdir(dir.c_str(), 0700);
		GetMessageCache()->SetDiskCache(dir, CacheLimit("DM_HISTORY_MB", 32));
	}

	new MainWindow(g_toplevel, g_pixelFormat);
	Notifier::Init(g_toplevel, g_pixelFormat);
	Conversations::Init(g_toplevel, g_pixelFormat);
	ImageCache::SetChangedCallback([] { GetMainWindow()->OnImagesChanged(); });

	Atom wmDelete = XmInternAtom(dpy, (char*) "WM_DELETE_WINDOW", False);
	XtVaSetValues(g_toplevel, XmNdeleteResponse, XmDO_NOTHING, NULL);
	XmAddWMProtocolCallback(g_toplevel, wmDelete, WmDeleteCB, NULL);

	XtRealizeWidget(g_toplevel);
	XtAppAddInput(g_app, MainQueue::WakeFd(), (XtPointer) XtInputReadMask, WakeCB, NULL);

	g_pHTTPClient->Init();
	GetWebsocketClient()->Init();

	// DM_TOKEN logs in for this run only; it is not saved.
	std::string token = GetLocalSettings()->GetToken();
	const char* envToken = getenv("DM_TOKEN");
	if (envToken && *envToken)
		token = envToken;

	bool demo = argc > 1 && !strcmp(argv[1], "--demo");
	bool bench = argc > 1 && !strcmp(argv[1], "--bench");
	g_pDiscordInstance = new DiscordInstance(demo || bench ? "" : token);
	if (Perf::Enabled() && !bench)
		XtAppAddTimeOut(g_app, 60000, PerfReportCB, NULL);
	if (!demo && !bench)
		XtAppAddTimeOut(g_app, 30000, SaveHistoryCB, NULL);
	if (bench)
		Bench::Start(g_app, [] { g_bQuit = true; });
	else if (demo)
		LoadDemo();
	else if (token.empty())
		ShowLogon("");
	else
		g_pFrontend->StartSession();

	while (!g_bQuit)
		XtAppProcessEvent(g_app, XtIMAll);
	if (!demo && !bench)
		GetMessageCache()->SaveDirty();
	if (Perf::Enabled() && !bench)
		Perf::Report(stderr, "at exit");

	GetLocalSettings()->Save();
	XtUnrealizeWidget(g_toplevel);
	XFlush(dpy);

	g_pDiscordInstance->CloseGatewaySession();
	GetWebsocketClient()->Kill();
	MainQueue::Shutdown();
	g_pHTTPClient->Kill();
	return 0;
}
