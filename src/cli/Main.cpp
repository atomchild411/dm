// dm-cli: a text client that drives the Discord Messenger core without a
// GUI.  It is a test tool for ports: it fetches the gateway address over
// HTTPS, connects the gateway websocket over TLS, and prints what arrives.
//
//   dm-cli            log in with the token in the settings file or DM_TOKEN
//   dm-cli --probe    connect without a token: Discord greets the client,
//                     then refuses it (close code 4004), which proves HTTPS,
//                     TLS and the websocket work.
//   dm-cli --connect wss://host/
//                     open a websocket to that address and print whether
//                     TLS let it through (a wrong host name must not).
//   dm-cli --get https://host/path [--times N]
//                     fetch that address over HTTPS (N times, one after
//                     the other) and print the status (a certificate that
//                     is not trusted must fail); with DM_TOKEN, as the user
//                     (for tests against a stand-in for Discord's API).

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/select.h>
#endif
#include <sys/time.h>

#include "DiscordInstance.hpp"
#include "config/LocalSettings.hpp"
#include "network/WebsocketClient.hpp"
#include "text/TextInterface.hpp"
#include "posix/Frontend_Posix.hpp"
#include "posix/MainQueue.hpp"
#include "posix/NetworkerThread.hpp"

static DiscordInstance* g_pDiscordInstance;
static bool g_bQuit;
static bool g_bProbe;
static const char* g_connectUrl; // --connect: one websocket, no session
static const char* g_getUrl;     // --get: one HTTPS request, no session
static int g_getTimes = 1;        // --times
static std::string g_getToken;    // DM_TOKEN, for --get

static void Fetch(int left)
{
	GetHTTPClient()->PerformRequest(true, NetRequest::GET, g_getUrl, 0, 0, "", g_getToken, std::to_string(left), [](NetRequest* req) {
		if (req->result < 0)
			printf("* could not fetch it: %s\n", req->response.c_str());
		else
			printf("* HTTP %d, %zu bytes\n", req->result, req->response.size());
		int left = GetIntFromString(req->additional_data);
		if (left > 1)
			MainQueue::Post([left] { Fetch(left - 1); });
		else
			MainQueue::Post([] { g_bQuit = true; });
	});
}

DiscordInstance* GetDiscordInstance()
{
	return g_pDiscordInstance;
}

// ---- timers --------------------------------------------------------------

static long long NowMs()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long) tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

struct Timer
{
	long long due;
	int interval; // 0: once
	std::function<void()> fn;
};

static std::map<int, Timer> g_timers;
static int g_nextTimerId = 1;

static int AddTimer(int ms, bool repeat, std::function<void()> fn)
{
	int id = g_nextTimerId++;
	g_timers[id] = Timer{ NowMs() + ms, repeat ? ms : 0, fn };
	return id;
}

// fn after firstMs, then every periodMs
static int AddRepeating(int firstMs, int periodMs, std::function<void()> fn)
{
	int id = g_nextTimerId++;
	g_timers[id] = Timer{ NowMs() + firstMs, periodMs, fn };
	return id;
}

static void RunDueTimers()
{
	// the due ones first: a timer's function may add or remove timers
	long long now = NowMs();
	std::vector<int> due;
	for (auto& t : g_timers)
		if (t.second.due <= now)
			due.push_back(t.first);
	for (int id : due)
	{
		auto it = g_timers.find(id);
		if (it == g_timers.end())
			continue; // removed by one before it
		std::function<void()> fn = it->second.fn;
		if (it->second.interval)
			it->second.due = now + it->second.interval;
		else
			g_timers.erase(it);
		fn();
	}
}

static int MsToNextTimer()
{
	long long now = NowMs(), next = -1;
	for (auto& t : g_timers)
		if (next < 0 || t.second.due < next)
			next = t.second.due;
	if (next < 0)
		return 1000;
	return next <= now ? 0 : (int) (next - now);
}

// ---- frontend --------------------------------------------------------------

class Frontend_CLI : public Frontend_Posix
{
public:
	void OnConnecting() override {
		printf("* connecting to the gateway\n");
	}
	void OnConnected() override {
		printf("* connected as user %llu\n", (unsigned long long) GetDiscordInstance()->GetUserID());
	}
	void OnSessionClosed(int errorCode) override {
		printf("* session closed (%d)\n", errorCode);
	}
	void OnLoggedOut() override {
		printf("* logged out\n");
		RequestQuit();
	}
	void OnAddMessage(Snowflake channelID, const Message& msg) override {
		Frontend_Posix::OnAddMessage(channelID, msg);
		Channel* pChan = GetDiscordInstance()->GetChannelGlobally(channelID);
		printf("[#%s] %s: %s\n", pChan ? pChan->m_name.c_str() : "?",
			msg.m_author.c_str(), msg.m_message.c_str());
	}
	void OnWebsocketMessage(int gatewayID, const std::string& payload) override {
		if (g_connectUrl) {
			printf("* connected; the server says: %.200s\n", payload.c_str());
			MainQueue::Post([] { g_bQuit = true; });
			return;
		}
		if (g_bProbe)
			printf("* gateway says: %.200s\n", payload.c_str());
		Frontend_Posix::OnWebsocketMessage(gatewayID, payload);
	}
	void OnWebsocketFail(int gatewayID, int errorCode, const std::string& message, bool isTLSError, bool mayRetry) override {
		if (g_connectUrl) {
			printf("* could not connect: %d %s%s\n", errorCode, message.c_str(), isTLSError ? " (TLS)" : "");
			MainQueue::Post([] { g_bQuit = true; });
			return;
		}
		Frontend_Posix::OnWebsocketFail(gatewayID, errorCode, message, isTLSError, mayRetry);
	}
	void OnWebsocketClose(int gatewayID, int errorCode, const std::string& message) override {
		printf("* gateway closed the connection: %d %s\n", errorCode, message.c_str());
		if (g_bProbe || g_connectUrl) {
			MainQueue::Post([] { g_bQuit = true; });
			return;
		}
		Frontend_Posix::OnWebsocketClose(gatewayID, errorCode, message);
	}
	void SetHeartbeatInterval(int timeMs, int firstMs) override {
		if (timeMs > 0)
			printf("* heartbeat every %d ms, the first in %d ms\n", timeMs, firstMs);
		if (m_heartbeatTimer)
			g_timers.erase(m_heartbeatTimer);
		m_heartbeatTimer = timeMs > 0 ?
			AddRepeating(firstMs, timeMs, [] { GetDiscordInstance()->SendHeartbeat(); }) : 0;
	}
	void RequestQuit() override {
		g_bQuit = true;
	}

protected:
	void ShowError(const std::string& message) override {
		fprintf(stderr, "error: %s\n", message.c_str());
	}
	void ScheduleReconnect(int ms, std::function<void()> fn) override {
		printf("* reconnecting in %d ms\n", ms);
		AddTimer(ms, false, fn);
	}

private:
	int m_heartbeatTimer = 0;
};

static Frontend_CLI* g_pFrontend;
static NetworkerThreadManager* g_pHTTPClient;

Frontend* GetFrontend()
{
	return g_pFrontend;
}

HTTPClient* GetHTTPClient()
{
	return g_pHTTPClient;
}

// The formatted-text renderer measures and draws words; a text client has no
// such thing, so it measures one unit per character.
struct DrawingContext {};
Point MdMeasureString(DrawingContext*, const String& word, int, bool& outWasWordWrapped, int)
{
	outWasWordWrapped = false;
	return Point((int) word.GetWrapped().size(), 1);
}
int MdLineHeight(DrawingContext*, int) { return 1; }
int MdSpaceWidth(DrawingContext*, int) { return 1; }
void MdDrawString(DrawingContext*, const Rect&, const String&, int) {}
void MdDrawCodeBackground(DrawingContext*, const Rect&) {}
void MdDrawForwardBackground(DrawingContext*, const Rect&) {}
int MdGetQuoteIndentSize() { return 2; }

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--probe"))
			g_bProbe = true;
		else if (!strcmp(argv[i], "--connect") && i + 1 < argc)
			g_connectUrl = argv[++i];
		else if (!strcmp(argv[i], "--get") && i + 1 < argc)
			g_getUrl = argv[++i];
		else if (!strcmp(argv[i], "--times") && i + 1 < argc)
			g_getTimes = atoi(argv[++i]);
		else {
			fprintf(stderr, "usage: %s [--probe | --connect wss://host/ | --get https://host/path [--times N]]\n", argv[0]);
			return 2;
		}
	}

#ifdef _WIN32
	setvbuf(stdout, NULL, _IONBF, 0); // (Windows' C library refuses _IOLBF with no size)
#else
	setvbuf(stdout, NULL, _IOLBF, 0);
#endif
	srand((unsigned) time(NULL));
	MainQueue::Init();
	SetupPosixPaths();

	g_pFrontend = new Frontend_CLI;
	g_pHTTPClient = new NetworkerThreadManager;
	GetLocalSettings()->Load();

	if (g_connectUrl || g_getUrl)
		g_bProbe = true; // no token is read or sent
	std::string token = g_bProbe ? "" : GetLocalSettings()->GetToken();
	const char* envToken = getenv("DM_TOKEN");
	if (!g_bProbe && envToken && *envToken)
		token = envToken;
	if (!g_bProbe && token.empty()) {
		fprintf(stderr, "No token: set DM_TOKEN, or run with --probe to test the connection.\n");
		return 1;
	}

	printf("* trusting: %s\n", TrustDescription().c_str());

	g_pHTTPClient->Init();
	GetWebsocketClient()->Init();
	g_pDiscordInstance = new DiscordInstance(token);
	if (g_getUrl) {
		printf("* fetching %s\n", g_getUrl);
		if (envToken && *envToken)
			g_getToken = envToken;
		Fetch(g_getTimes);
	}
	else if (g_connectUrl) {
		printf("* connecting to %s\n", g_connectUrl);
		if (GetWebsocketClient()->Connect(g_connectUrl) < 0)
			return 1;
	}
	else
		g_pFrontend->StartSession();

#ifdef _WIN32
	// no pipe to select() on: sleep on the queue itself
	while (!g_bQuit)
	{
		MainQueue::Wait(MsToNextTimer());
		MainQueue::Drain();
		RunDueTimers();
	}
#else
	int fd = MainQueue::WakeFd();
	while (!g_bQuit)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		int ms = MsToNextTimer();
		struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
		int n = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (n < 0 && errno != EINTR) {
			perror("select");
			break;
		}
		if (n > 0 && FD_ISSET(fd, &rfds))
			MainQueue::Drain();
		RunDueTimers();
	}
#endif

	printf("* quitting\n");
	g_pDiscordInstance->CloseGatewaySession();
	GetWebsocketClient()->Kill();
	MainQueue::Shutdown();
	g_pHTTPClient->Kill();
	return 0;
}
