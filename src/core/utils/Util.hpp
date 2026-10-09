#pragma once

#include <string>
#include <nlohmann/json.h>
#include "../models/Snowflake.hpp"

#ifdef _DEBUG
#define USE_DEBUG_PRINTS
#endif

std::string GetBasePath();
std::string GetCachePath();
void SetProgramNamePath(const std::string& programName);
void SetBasePath(const std::string& appDataPath);
// rename() that replaces an existing file, as POSIX's does, on Windows too
bool RenameOver(const std::string& from, const std::string& to);
int StringCompareCaseInsens(const char* s1, const char* s2);
bool BeginsWith(const std::string& what, const std::string& with);
bool EndsWithCaseInsens(const std::string& what, const std::string& with);
const uint64_t GetTimeMs() noexcept;
const uint64_t GetTimeUs() noexcept;
Snowflake GetSnowflakeFromJsonObject(const nlohmann::json& j);
Snowflake GetSnowflake(const nlohmann::json& j, const std::string& key);
int64_t GetIntFromString(const std::string& str);
std::string FormatDiscrim(int discrim);
std::string GetGlobalName(const nlohmann::json& j);
std::string GetUsername(const nlohmann::json& j);
int GetFieldSafeInt(const nlohmann::json& j, const std::string& key);
bool GetFieldSafeBool(const nlohmann::json& j, const std::string& key, bool default1);
std::string GetFieldSafe(const nlohmann::json& j, const std::string& key);
time_t ParseTime(const std::string& iso8601);
std::string FormatTimeLong(time_t time, bool relativity = false); // relativity=true means "Today at" and "Yesterday at" show
std::string FormatTimeShorter(time_t time);
std::string FormatTimestampTimeShort(time_t time);
std::string FormatTimestampTimeLong(time_t time);
std::string FormatTimestampDateShort(time_t time);
std::string FormatTimestampDateLong(time_t time);
std::string FormatTimestampDateLongTimeShort(time_t time);
std::string FormatTimestampDateLongTimeLong(time_t time);
std::string FormatTimestampRelative(time_t time);

#ifdef USE_DEBUG_PRINTS
void DbgPrintF(const char* fmt, ...);
#else
#define DbgPrintF(...)
#endif
