#include "../network/DiscordAPI.hpp"
#include "ProfileCache.hpp"
#include "../utils/Util.hpp"
#include "../Frontend.hpp"
#include "../network/DiscordRequest.hpp"
#include "../DiscordInstance.hpp"

static ProfileCache g_ProfileCache;

ProfileCache* GetProfileCache()
{
	return &g_ProfileCache;
}

ProfileCache::~ProfileCache()
{
}

Profile* ProfileCache::LookupProfile(Snowflake user, const std::string& username, const std::string& globalName, const std::string& avatarLink, bool bRequestServer)
{
	Profile* pProf = &m_profileSets[user];

	if (!pProf->m_bUsingDefaultData)
		return pProf;

	pProf->m_snowflake = user;

	if (pProf->m_name.empty())
		pProf->m_name = username;
	if (pProf->m_globalName.empty())
		pProf->m_globalName = globalName;

	if (!avatarLink.empty()) {
		pProf->m_avatarlnk  = avatarLink;
	}

	if (bRequestServer)
		RequestLoadProfile(user);

	return pProf;
}

Profile* ProfileCache::LoadProfile(Snowflake user, const nlohmann::json& jx)
{
	if (!user) {
		user = GetSnowflake(jx, "id");
		if (!user) {
			DbgPrintF("Dropping profile data with no user id: %s", jx.dump().c_str());
			return nullptr;
		}
	}

	Profile* pf = LookupProfile(user, "", "", "", false);

	bool anythingChanged = false;
	std::string oldName   = pf->m_name;
	std::string oldGName  = pf->m_globalName;
	std::string oldAvatar = pf->m_avatarlnk;
	bool oldIsBot = pf->m_bIsBot;

	auto iter = m_processingRequests.find(user);
	if (iter != m_processingRequests.end())
		m_processingRequests.erase(iter);

	const auto& userData = jx.contains("user") ? jx["user"] : jx;

	pf->m_snowflake  = user;
	pf->m_name       = GetUsername(userData);
	pf->m_discrim    = userData.contains("discriminator") ? int(GetIntFromString(userData["discriminator"])) : 0;
	pf->m_globalName = GetGlobalName(userData);
	pf->m_bIsBot     = GetFieldSafeBool(userData, "bot", false);
	pf->m_bUsingDefaultData = false;

	// Avatar links formatted as https://cdn.discordapp.com/avatars/<userid>/<avatarlnk>
	if (userData["avatar"].is_string()) {
		pf->m_avatarlnk = userData["avatar"];
	}
	else {
		pf->m_avatarlnk = "";
	}

	GetFrontend()->UpdateUserData(pf->m_snowflake);

	return pf;
}

void ProfileCache::ForgetProfile(Snowflake user)
{
	auto iter = m_profileSets.find(user);
	if (iter != m_profileSets.end())
		m_profileSets.erase(iter);
}

void ProfileCache::RequestLoadProfile(Snowflake user, Snowflake guild, bool mutualGuilds, bool mutualFriends)
{
	if (!user)
		return;

	if (m_processingRequests.find(user) != m_processingRequests.end())
		return;

	m_processingRequests.insert(user);

	std::string additionalData = "";
	std::string userSource = "";
	
	additionalData += "&with_mutual_guilds=" + std::string(mutualGuilds ? "true" : "false");
	additionalData += "&with_mutual_friends=" + std::string(mutualFriends ? "true" : "false");
	additionalData += "&with_mutual_friends_count=" + std::string(mutualFriends ? "true" : "false");
	if (guild) additionalData += "&guild_id=" + std::to_string(guild);

	if (!additionalData.empty() && additionalData[0] == '&')
		additionalData[0] = '?';

	GetHTTPClient()->PerformRequest(
		true,
		NetRequest::GET,
		GetDiscordAPI() + "users/" + std::to_string(user) + "/profile",
		DiscordRequest::PROFILE,
		user,
		additionalData,
		GetDiscordInstance()->GetToken(),
		"0"
	);
}
