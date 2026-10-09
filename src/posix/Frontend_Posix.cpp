#include "Frontend_Posix.hpp"
#include "MainQueue.hpp"
#include "SecretStore.hpp"

#include "DiscordInstance.hpp"
#include "config/LocalSettings.hpp"
#include "network/DiscordAPI.hpp"
#include "network/DiscordRequest.hpp"
#include "state/MessageCache.hpp"
#include "utils/Util.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#endif

DiscordInstance* GetDiscordInstance();

void SetupPosixPaths()
{
	std::string base;
	const char* dmHome = getenv("DM_HOME");
	if (dmHome && *dmHome) {
		base = dmHome;
		SetBasePath(base);
		SetProgramNamePath("");
	}
	else {
#ifdef _WIN32
		// %APPDATA%\DiscordMessenger, as Windows programs keep their settings
		const char* home = getenv("APPDATA");
		base = std::string(home && *home ? home : ".");
		SetBasePath(base);
		SetProgramNamePath("DiscordMessenger");
#else
		const char* home = getenv("HOME");
		base = std::string(home && *home ? home : ".");
		SetBasePath(base);
		SetProgramNamePath(".discordmessenger");
#endif
	}

	mkdir(GetBasePath().c_str(), 0700);
	mkdir(GetCachePath().c_str(), 0700);
}

void Frontend_Posix::StartSession()
{
	m_sessionGen++;
	DiscordInstance* pInst = GetDiscordInstance();
	if (pInst->HasGatewayURL()) {
		pInst->StartGatewaySession();
	}
	else {
		GetHTTPClient()->PerformRequest(
			false,
			NetRequest::GET,
			GetDiscordAPI() + "gateway",
			DiscordRequest::GATEWAY,
			0
		);
	}
}

void Frontend_Posix::OnLoginAgain(int delayMs)
{
	int gen = m_sessionGen;
	ScheduleReconnect(delayMs, [this, gen] {
		if (gen == m_sessionGen) // (no session started meanwhile)
			StartSession();
	});
}

void Frontend_Posix::OnAddMessage(Snowflake channelID, const Message& msg)
{
	GetMessageCache()->AddMessage(channelID, msg);
}

void Frontend_Posix::OnUpdateMessage(Snowflake channelID, const Message& msg)
{
	GetMessageCache()->EditMessage(channelID, msg);
}

void Frontend_Posix::OnRequestDone(NetRequest* pRequest)
{
	// Called on a network thread; the request lives on its stack.
	NetRequest copy = *pRequest;
	MainQueue::Post([copy]() mutable {
		GetDiscordInstance()->HandleRequest(&copy);
	});
}

void Frontend_Posix::OnWebsocketMessage(int gatewayID, const std::string& payload)
{
	MainQueue::Post([gatewayID, payload] {
		DiscordInstance* pInst = GetDiscordInstance();
		if (pInst->GetGatewayID() == gatewayID)
			pInst->HandleGatewayMessage(payload);
	});
}

void Frontend_Posix::OnWebsocketClose(int gatewayID, int errorCode, const std::string& message)
{
	MainQueue::Post([gatewayID, errorCode] {
		DiscordInstance* pInst = GetDiscordInstance();
		if (pInst->GetGatewayID() == gatewayID)
			pInst->GatewayClosed(errorCode);
		else
			DbgPrintF("Unknown gateway connection %d closed: %d", gatewayID, errorCode);
	});
}

void Frontend_Posix::OnWebsocketFail(int gatewayID, int errorCode, const std::string& message, bool isTLSError, bool mayRetry)
{
	std::string text = "Could not connect to the websocket gateway.\nConnection was closed with code: " +
		std::to_string(errorCode) + "\n\nMessage: " + message;
	if (isTLSError)
		text += "\n\nYour connection may not be private: Discord Messenger could not verify "
			"that it is connecting to Discord's real-time service.";

	MainQueue::Post([this, text, isTLSError, mayRetry] {
		OnConnectFailed(text, isTLSError, mayRetry);
	});
}

void Frontend_Posix::OnConnectFailed(const std::string& message, bool isTLSError, bool mayRetry)
{
	if (!mayRetry) {
		ShowError(message);
		return;
	}

	DbgPrintF("%s\nTrying to connect again later", message.c_str());
	GetDiscordInstance()->ReconnectLater();
}

void Frontend_Posix::OnGenericError(const std::string& message)
{
	MainQueue::Post([this, message] { ShowError(message); });
}

void Frontend_Posix::OnJsonException(const std::string& message)
{
	OnGenericError("A bug has occurred and Discord Messenger failed to parse a piece of JSON received from the server.\n\nMessage:" + message);
}

void Frontend_Posix::OnCantViewChannel(const std::string& channelName)
{
	OnGenericError("You do not have permission to view the channel #" + channelName + ".");
}

void Frontend_Posix::OnGatewayConnectFailure()
{
	OnGenericError("Could not connect to Discord servers.\n\nThis could be because you aren't connected to the Internet, or because Discord servers are down.");
}

void Frontend_Posix::LaunchURL(const std::string& url)
{
#ifndef _WIN32
	// The browser to open links with: DM_BROWSER, then BROWSER.
	const char* browser = getenv("DM_BROWSER");
	if (!browser || !*browser)
		browser = getenv("BROWSER");
	if (!browser || !*browser) {
		OnGenericError("No web browser is set (DM_BROWSER or BROWSER) to open:\n\n" + url);
		return;
	}
#endif

	// Links come from other people's messages: hand the browser only web
	// addresses (no file: or other schemes, nothing it could read as an
	// option), and nothing with control characters in it.
	bool web = !strncasecmp(url.c_str(), "https://", 8) || !strncasecmp(url.c_str(), "http://", 7);
	for (size_t i = 0; web && i < url.size(); i++)
		if ((unsigned char) url[i] < 0x20 || url[i] == 0x7f)
			web = false;
	if (!web) {
		OnGenericError("Only web links (http and https) are opened:\n\n" + url);
		return;
	}

#ifdef _WIN32
	// the user's default browser
	int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
	std::wstring wurl(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1)
		MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], n);
	ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
	// Fork twice so the browser is not our child: nothing is left to reap.
	pid_t pid = fork();
	if (pid == 0) {
		if (fork() == 0) {
			execlp(browser, browser, url.c_str(), (char*) NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
#endif
}

static std::string ReadFile(const std::string& path)
{
	std::ifstream in(path.c_str(), std::ios::binary);
	if (!in)
		return "";
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// The login token lives in the system's store of secrets where there is
// one (SecretStore): settings.json then holds everything but the token, and
// the token is added back on loading.  A token found in the file (from an
// older release, or one the store refused) moves into the store; where the
// store fails or is missing, the file keeps it, as before.
namespace
{
	bool g_storeKnown = false; // g_stored is what the store holds
	std::string g_stored;
	bool g_warned = false;

	void Warn(const char* what)
	{
		if (g_warned)
			return;
		g_warned = true;
		fprintf(stderr, "dm: could not %s the login token in %s; it stays in settings.json.\n", what, SecretStore::Name());
	}

	// The profile the token is filed under: the settings directory (without
	// a trailing slash, so DM_HOME=/x/ and /x are one)
	std::string TokenProfile()
	{
		std::string p = GetBasePath();
		while (p.size() > 1 && (p.back() == '/' || p.back() == '\\'))
			p.pop_back();
		return p;
	}

	// Writes a new file and renames it over the old one, so a crash never
	// leaves half a settings file.
	bool WriteSettings(const std::string& body)
	{
		std::string path = GetBasePath() + "/settings.json";
		std::string tmp = path + ".new";
		FILE* f = fopen(tmp.c_str(), "wb");
		if (!f)
			return false;
		chmod(tmp.c_str(), 0600); // it may hold the login token
		bool ok = fwrite(body.data(), 1, body.size(), f) == body.size();
		ok = (fclose(f) == 0) && ok;
		if (!ok || !RenameOver(tmp, path)) {
			unlink(tmp.c_str());
			return false;
		}
		return true;
	}
}

std::string Frontend_Posix::LoadConfig()
{
	std::string text = ReadFile(GetBasePath() + "/settings.json");
	if (!SecretStore::Usable())
		return text;
	nlohmann::json j = nlohmann::json::parse(text.empty() ? std::string("{}") : text, nullptr, false);
	if (j.is_discarded() || !j.is_object())
		return text;
	std::string inFile = j.contains("Token") && j["Token"].is_string() ? j["Token"].get<std::string>() : "";
	std::string stored;
	SecretStore::Result r = SecretStore::Load(TokenProfile(), stored);
	if (r != SecretStore::FAILED) {
		g_storeKnown = true;
		g_stored = r == SecretStore::FOUND ? stored : "";
	}
	if (!inFile.empty()) {
		// the file's is the newer (written by an older release, or by hand):
		// into the store with it, and out of the file
		if (inFile == g_stored || SecretStore::Save(TokenProfile(), inFile)) {
			g_storeKnown = true;
			g_stored = inFile;
			nlohmann::json rest = j;
			rest.erase("Token");
			WriteSettings(rest.dump());
		}
		else
			Warn("keep");
		return text;
	}
	if (r == SecretStore::FOUND)
		j["Token"] = stored;
	return j.dump();
}

bool Frontend_Posix::SaveConfig(const std::string& configJson)
{
	std::string body = configJson;
	if (SecretStore::Usable()) {
		nlohmann::json j = nlohmann::json::parse(configJson, nullptr, false);
		if (!j.is_discarded() && j.is_object()) {
			std::string token = j.contains("Token") && j["Token"].is_string() ? j["Token"].get<std::string>() : "";
			// no token, and the store could not be read: leave it alone
			// (a refused Keychain prompt must not cost the stored token)
			bool kept = (g_storeKnown && token == g_stored) || (!g_storeKnown && token.empty());
			if (!kept && SecretStore::Save(TokenProfile(), token)) {
				g_storeKnown = true;
				g_stored = token;
				kept = true;
			}
			if (kept) {
				j.erase("Token");
				body = j.dump();
			}
			else
				Warn("keep");
		}
	}

	return WriteSettings(body);
}

std::string Frontend_Posix::GetDirectMessagesText() { return "Direct Messages"; }
std::string Frontend_Posix::GetPleaseWaitText() { return "Please wait..."; }

std::string Frontend_Posix::GetTodayAtText() { return "Today at " + GetFormatTimestampTimeShort(); }
std::string Frontend_Posix::GetYesterdayAtText() { return "Yesterday at " + GetFormatTimestampTimeShort(); }
std::string Frontend_Posix::GetFormatTimeLongText() { return "%d-%m-%Y at " + GetFormatTimestampTimeShort(); }
std::string Frontend_Posix::GetFormatTimeShorterText() { return GetFormatTimestampTimeShort(); }

std::string Frontend_Posix::GetFormatTimestampTimeShort()
{
	return GetLocalSettings()->Use12HourTime() ? "%I:%M %p" : "%H:%M";
}

std::string Frontend_Posix::GetFormatTimestampTimeLong() { return "%H:%M:%S"; }
std::string Frontend_Posix::GetFormatTimestampDateShort() { return "%d/%m/%Y"; }
std::string Frontend_Posix::GetFormatTimestampDateLong() { return "%e %B %Y"; }

std::string Frontend_Posix::GetFormatTimestampDateLongTimeShort()
{
	return GetFormatTimestampDateLong() + " " + GetFormatTimestampTimeShort();
}

std::string Frontend_Posix::GetFormatTimestampDateLongTimeLong()
{
	return "%A, " + GetFormatTimestampDateLong() + " " + GetFormatTimestampTimeShort();
}

#ifdef USE_DEBUG_PRINTS
void Frontend_Posix::DebugPrint(const char* fmt, va_list vl)
{
	// DM_DEBUG=1 prints the core's debug messages on stderr.
	static int s_enabled = -1;
	if (s_enabled < 0) {
		const char* e = getenv("DM_DEBUG");
		s_enabled = e && *e && *e != '0';
	}
	if (!s_enabled)
		return;

	vfprintf(stderr, fmt, vl);
	fputc('\n', stderr);
}
#endif
