#include <fstream>
#include <nlohmann/json.h>
#include "LocalSettings.hpp"
#include "../utils/Util.hpp"
#include "../network/DiscordAPI.hpp"
#include "../Frontend.hpp"
using nlohmann::json;

static LocalSettings* g_pInstance;

LocalSettings* GetLocalSettings()
{
	if (!g_pInstance)
		g_pInstance = new LocalSettings;

	return g_pInstance;
}

LocalSettings::LocalSettings()
{
	m_discordApi = OFFICIAL_DISCORD_API;
	m_discordCdn = OFFICIAL_DISCORD_CDN;
}

bool LocalSettings::Load()
{
	std::string data = GetFrontend()->LoadConfig();

	if (data.empty())
		return false;

	json j = json::parse(data);

	// Load properties from the json object.
	if (j.contains("Token"))
		m_token = j["Token"];

	if (j.contains("DiscordAPI"))
		m_discordApi = j["DiscordAPI"];

	if (j.contains("EnableTLSVerification"))
		m_bEnableTLSVerification = j["EnableTLSVerification"];

	if (j.contains("DisableFormatting"))
		m_bDisableFormatting = j["DisableFormatting"];

	if (j.contains("Use12HourTime"))
		m_bUse12HourTime = j["Use12HourTime"];

	if (j.contains("AddExtraHeaders"))
		m_bAddExtraHeaders = j["AddExtraHeaders"];
	return true;
}

bool LocalSettings::Save()
{
	json j;

	j["Token"] = m_token;
	j["DiscordAPI"] = m_discordApi;
	j["EnableTLSVerification"] = m_bEnableTLSVerification;
	j["DisableFormatting"] = m_bDisableFormatting;
	j["AddExtraHeaders"] = m_bAddExtraHeaders;
	j["Use12HourTime"] = m_bUse12HourTime;

	return GetFrontend()->SaveConfig(j.dump());
}
