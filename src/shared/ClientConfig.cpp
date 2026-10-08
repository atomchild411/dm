#include "ClientConfig.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "utils/Util.hpp"

static int g_textSize = 0;
static bool g_paneShown[PANE_COUNT] = { true, true, true };
static const char* const g_paneKeys[PANE_COUNT] = { "servers", "channels", "members" };
static bool g_notify[NOTIFY_COUNT] = { true, true };
static const char* const g_notifyKeys[NOTIFY_COUNT] = { "notifysound", "notifypopup" };
static Snowflake g_lastGuild = 0, g_lastChannel = 0;
static bool g_haveLast = false;
static ColorScheme g_scheme = SCHEME_SYSTEM;
static const char* const g_schemeNames[] = { "system", "dark", "light" };

static std::string g_fileName = "client.conf";

static std::string ConfigPath()
{
	return GetBasePath() + "/" + g_fileName;
}

void LoadClientConfig(const std::string& fileName)
{
	g_fileName = fileName;
	FILE* f = fopen(ConfigPath().c_str(), "r");
	if (f) {
		char line[256];
		while (fgets(line, sizeof line, f)) {
			char key[64], val[128];
			if (sscanf(line, " %63[^= ] = %127s", key, val) != 2)
				continue;
			if (!strcmp(key, "textsize"))
				SetTextSize(atoi(val));
			for (int p = 0; p < PANE_COUNT; p++)
				if (!strcmp(key, g_paneKeys[p]))
					g_paneShown[p] = atoi(val) != 0;
			for (int k = 0; k < NOTIFY_COUNT; k++)
				if (!strcmp(key, g_notifyKeys[k]))
					g_notify[k] = atoi(val) != 0;
			if (!strcmp(key, "lastserver")) {
				g_lastGuild = strtoull(val, NULL, 10);
				g_haveLast = true;
			}
			if (!strcmp(key, "lastchannel"))
				g_lastChannel = strtoull(val, NULL, 10);
			for (int s = 0; s < 3; s++)
				if (!strcmp(key, "theme") && !strcmp(val, g_schemeNames[s]))
					g_scheme = (ColorScheme) s;
		}
		fclose(f);
	}
	if (const char* e = getenv("DM_TEXT_SIZE"))
		SetTextSize(atoi(e));
}

void SaveClientConfig()
{
	std::string path = ConfigPath(), tmp = path + ".new";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f)
		return;
	fprintf(f, "textsize = %d\n", GetTextSize());
	for (int p = 0; p < PANE_COUNT; p++)
		fprintf(f, "%s = %d\n", g_paneKeys[p], g_paneShown[p] ? 1 : 0);
	for (int k = 0; k < NOTIFY_COUNT; k++)
		fprintf(f, "%s = %d\n", g_notifyKeys[k], g_notify[k] ? 1 : 0);
	if (g_haveLast) {
		fprintf(f, "lastserver = %llu\n", (unsigned long long) g_lastGuild);
		fprintf(f, "lastchannel = %llu\n", (unsigned long long) g_lastChannel);
	}
	if (g_scheme != SCHEME_SYSTEM) // (the default goes unwritten)
		fprintf(f, "theme = %s\n", g_schemeNames[g_scheme]);
	if (fclose(f) == 0)
		RenameOver(tmp, path);
	else
		remove(tmp.c_str());
}

static int g_defaultTextSize = 14;

void SetDefaultTextSize(int px)
{
	g_defaultTextSize = px;
}

int GetTextSize()
{
	return g_textSize ? g_textSize : g_defaultTextSize;
}

void SetTextSize(int px)
{
	if (px >= 8 && px <= 40)
		g_textSize = px;
}

bool IsPaneShown(Pane p)
{
	return g_paneShown[p];
}

void SetPaneShown(Pane p, bool shown)
{
	g_paneShown[p] = shown;
}

bool IsNotifyOn(Notify n)
{
	return g_notify[n];
}

void SetNotifyOn(Notify n, bool on)
{
	g_notify[n] = on;
}

void GetLastChannel(Snowflake& guild, Snowflake& channel)
{
	guild = g_haveLast ? g_lastGuild : 0;
	channel = g_haveLast ? g_lastChannel : 0;
}

void SetLastChannel(Snowflake guild, Snowflake channel)
{
	g_lastGuild = guild;
	g_lastChannel = channel;
	g_haveLast = true;
}


ColorScheme GetColorScheme()
{
	return g_scheme;
}

void SetColorScheme(ColorScheme s)
{
	g_scheme = s;
}
