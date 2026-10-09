#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdarg>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include "Util.hpp"

#include "../Frontend.hpp"

#define PATH_SEP '/'
#define PATH_SEP_STR "/"

std::string g_BasePath = "";
std::string g_ProgramNamePath = "";

void SetProgramNamePath(const std::string& programName)
{
	g_ProgramNamePath = programName;
}

void SetBasePath(const std::string& path)
{
	g_BasePath = path;
	if (!path.empty() && path[path.size() - 1] != PATH_SEP)
		g_BasePath += PATH_SEP;
}

std::string GetBasePath()
{
	return g_BasePath + g_ProgramNamePath;
}

std::string GetCachePath()
{
	return g_BasePath + g_ProgramNamePath + PATH_SEP_STR "cache";
}

int StringCompareCaseInsens(const char* s1, const char* s2)
{
	while (tolower(*s1) == tolower(*s2)) {
		if (*s1 == '\0') break;
		s1++, s2++;
	}

	return tolower(*s1) - tolower(*s2);
}

bool BeginsWith(const std::string& what, const std::string& with)
{
	if (what.size() < with.size())
		return false;

	return strncmp(what.c_str(), with.c_str(), with.size()) == 0;
}

bool EndsWithCaseInsens(const std::string& what, const std::string& with)
{
	if (what.size() < with.size())
		return false;

	size_t diff = what.size() - with.size();
	return StringCompareCaseInsens(what.c_str() + diff, with.c_str()) == 0;
}

std::string GetFieldSafe(const nlohmann::json& j, const std::string& key)
{
	if (j.contains(key) && !j[key].is_null())
		return j[key];

	return "";
}

int GetFieldSafeInt(const nlohmann::json& j, const std::string& key)
{
	if (j.contains(key) && j[key].is_number_integer())
		return j[key];

	return 0;
}

std::string FormatDiscrim(int discrim)
{
	char chr[16];
	snprintf(chr, sizeof chr, "%04d", discrim);
	return std::string(chr);
}

std::string GetGlobalName(const nlohmann::json& j)
{
	if (j.contains("global_name") && !j["global_name"].is_null())
		return GetFieldSafe(j, "global_name");
	else
		return GetFieldSafe(j, "username");
}

std::string GetUsername(const nlohmann::json& j)
{
	std::string username = GetFieldSafe(j, "username");
	int discrim = int(GetIntFromString(GetFieldSafe(j, "discriminator")));

	if (discrim > 0)
		username += "#" + FormatDiscrim(discrim);

	return username;
}

int64_t GetIntFromString(const std::string& str)
{
	std::stringstream sstr(str);
	int64_t t = 0;
	if (!(sstr >> t))
		return 0;
	return t;
}

bool GetFieldSafeBool(const nlohmann::json& j, const std::string& key, bool default1)
{
	if (j.contains(key) && j[key].is_boolean())
		return j[key];
	return default1;
}

Snowflake GetSnowflakeFromJsonObject(const nlohmann::json& j) {
	if (j.is_number_integer())
		return Snowflake(int64_t(j));
	if (j.is_number_unsigned())
		return Snowflake(uint64_t(j));
	if (j.is_string())
		return Snowflake(GetIntFromString(j));
	return 0;
}

Snowflake GetSnowflake(const nlohmann::json& j, const std::string& key)
{
	auto ji = j.find(key);
	if (ji == j.end())
		return 0;

	return GetSnowflakeFromJsonObject(ji.value());
}

using Json = nlohmann::json;

const uint64_t GetTimeMs() noexcept
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
const uint64_t GetTimeUs() noexcept
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

time_t ParseTime(const std::string& iso8601)
{
	// tokenize date
	std::vector<std::string> dateTokens;
	std::string currentToken = "";
	bool encounteredT = false;
	for (auto c : iso8601)
	{
		if (c == 'T' || c == 'Z' || c == '+' || (c == '-' && encounteredT)) {
			dateTokens.push_back(currentToken);
			currentToken.clear();
		}
		if (c == 'T') encounteredT = true;
		currentToken += c;
	}

	if (!currentToken.empty())
		dateTokens.push_back(currentToken);

	struct tm ptime { 0 };

	// Date string format: yyyy-mm-ddThh:mm:ss.wwwzzz+oo:pp
	// w - millisecond, z - microsecond (0)
	// oo - offset hr
	// pp - offset min

	for (auto& tk : dateTokens)
	{
		if (tk.empty())
			continue;

		if (tk[0] == 'T') {
			// time mode
			int h = 0, m = 0, s = 0;
			sscanf(tk.c_str() + 1, "%d:%d:%d", &h, &m, &s);
			ptime.tm_hour = h;
			ptime.tm_min  = m;
			ptime.tm_sec  = s;
			continue;
		}

		if (tk[0] == '+' || tk[0] == '-') {
			// time zone offset mode TODO
			continue;
		}

		if (isdigit(tk[0])) {
			// date mode
			int y = 0, m = 0, d = 0;
			sscanf(tk.c_str(), "%d-%d-%d", &y, &m, &d);
			ptime.tm_year = y - 1900;
			ptime.tm_mon  = m - 1;
			ptime.tm_mday = d;
			continue;
		}

		assert(!"unknown date mode");
	}

	// Convert to time_t
	// XXX timegm on linux
	time_t t = timegm(&ptime);
	return t;
}

std::string FormatTimeLong(time_t time, bool relativity)
{
	// Full time: [date noun] at [time] OR [date] [time]
	struct tm ptime { 0 };
	ptime = *localtime(&time);
	char buff[2048];
	bool istoday = false, isyday = false;
	if (relativity) {
		time_t todaytime = ::time(NULL);
		struct tm todaytm = *localtime(&todaytime); // fetch today's time
		todaytime -= 86400;
		struct tm ydaytm = *localtime(&todaytime); // fetch yesterday's time
		istoday = todaytm.tm_yday == ptime.tm_yday && todaytm.tm_year == ptime.tm_year;
		isyday  = todaytm.tm_yday == ptime.tm_yday && ydaytm.tm_year  == ptime.tm_year;
	}

	/**/ if (istoday) strftime(buff, sizeof buff, GetFrontend()->GetTodayAtText().c_str(), &ptime);
	else if (isyday)  strftime(buff, sizeof buff, GetFrontend()->GetYesterdayAtText().c_str(), &ptime);
	else              strftime(buff, sizeof buff, GetFrontend()->GetFormatTimeLongText().c_str(), &ptime);
	buff[sizeof buff - 1] = 0;

	return std::string(buff);
}

std::string FormatTimeShorter(time_t time)
{
	// Compact time: dd/mm hh:mm
	struct tm ptime { 0 };
	ptime = *localtime(&time);
	char buff[2048];
	strftime(buff, sizeof buff, GetFrontend()->GetFormatTimeShorterText().c_str(), &ptime);
	buff[sizeof buff - 1] = 0;
	return std::string(buff);
}

std::string FormatTimestampTimeShort(time_t time)
{
	struct tm ptime { 0 };
	ptime = *localtime(&time);
	char buffer[256];
	strftime(buffer, sizeof buffer, GetFrontend()->GetFormatTimestampTimeShort().c_str(), &ptime);
	return std::string(buffer);
}

std::string FormatTimestampTimeLong(time_t time)
{
	struct tm ptime { 0 };
	ptime = *localtime(&time);
	char buffer[256];
	strftime(buffer, sizeof buffer, GetFrontend()->GetFormatTimestampTimeLong().c_str(), &ptime);
	return std::string(buffer);
}

std::string FormatTimestampDateShort(time_t time)
{
	struct tm ptime { 0 };
	ptime = *localtime(&time);
	char buffer[256];
	strftime(buffer, sizeof buffer, GetFrontend()->GetFormatTimestampDateShort().c_str(), &ptime);
	return std::string(buffer);
}

std::string FormatTimestampDateLong(time_t time)
{
	struct tm ptime { 0 };
	ptime = *localtime(&time);
	char buffer[256];
	strftime(buffer, sizeof buffer, GetFrontend()->GetFormatTimestampDateLong().c_str(), &ptime);
	return std::string(buffer);
}

std::string FormatTimestampDateLongTimeShort(time_t time)
{
	struct tm ptime { 0 };
	ptime = *localtime(&time);
	char buffer[256];
	strftime(buffer, sizeof buffer, GetFrontend()->GetFormatTimestampDateLongTimeShort().c_str(), &ptime);
	return std::string(buffer);
}

std::string FormatTimestampDateLongTimeLong(time_t time)
{
	struct tm ptime { 0 };
	ptime = *localtime(&time);
	char buffer[256];
	strftime(buffer, sizeof buffer, GetFrontend()->GetFormatTimestampDateLongTimeLong().c_str(), &ptime);
	return std::string(buffer);
}

std::string FormatTimestampRelative(time_t t)
{
	time_t ct = time(NULL);

	auto diff = ct - t;
	bool future = diff < 0;
	diff = abs(diff);
	
	auto pfx = future ? "in " : "";
	auto sfx = future ? "" : " ago";

	if (diff >= 365 * 24 * 60 * 60) {
		// years
		diff = round(diff / double(365 * 24 * 60 * 60));
		return pfx + std::to_string(diff) + (diff == 1 ? " year" : " years") + sfx;
	}

	if (diff >= 30 * 24 * 60 * 60) {
		// months - TODO, just an approximation
		diff = round(diff / double(30 * 24 * 60 * 60));
		return pfx + std::to_string(diff) + (diff == 1 ? " month" : " months") + sfx;
	}

	if (diff >= 24 * 60 * 60) {
		// days
		diff = round(diff / double(24 * 60 * 60));
		return pfx + std::to_string(diff) + (diff == 1 ? " day" : " days") + sfx;
	}

	if (diff >= 60 * 60) {
		// hours
		diff = round(diff / double(60 * 60));
		return pfx + std::to_string(diff) + (diff == 1 ? " hour" : " hours") + sfx;
	}

	if (diff >= 60) {
		// minutes
		diff = round(diff / double(60));
		return pfx + std::to_string(diff) + (diff == 1 ? " minute" : " minutes") + sfx;
	}

	return pfx + std::to_string(diff) + (diff == 1 ? " second" : " seconds") + sfx;
}

// C/C++ macro memes
#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

#ifdef USE_DEBUG_PRINTS
void DbgPrintF(const char* fmt, ...)
{
	va_list vl;
	va_start(vl, fmt);
	GetFrontend()->DebugPrint(fmt, vl);
	va_end(vl);
}
#endif

bool RenameOver(const std::string& from, const std::string& to)
{
#ifdef _WIN32
	return dm_win_rename(from.c_str(), to.c_str()) == 0; // compat/win
#else
	return rename(from.c_str(), to.c_str()) == 0;
#endif
}
