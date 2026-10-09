#pragma once

#include <set>
#include <nlohmann/json.h>
#include "../models/Profile.hpp"
#include "../models/Guild.hpp"

class ProfileCache
{
public:
	~ProfileCache();
	
	// Looks up a profile.
	// Username and globalname are filled in to create a default profile if the profile is not cached.
	// Returns NULL if the Discord servers have reported that the user does not exist.
	Profile* LookupProfile(Snowflake user, const std::string& username, const std::string& globalName, const std::string& avatarLink, bool bRequestServer = true);
	Profile* LoadProfile(Snowflake user, const nlohmann::json& j);

	// Forget a profile.
	void ForgetProfile(Snowflake user);

protected:
	friend struct Profile;

private:
	void RequestLoadProfile(Snowflake user, Snowflake guild = 0, bool mutualGuilds = true, bool mutualFriends = true);

	std::map<Snowflake, Profile> m_profileSets;
	std::set<Snowflake> m_processingRequests;
};

ProfileCache* GetProfileCache();
