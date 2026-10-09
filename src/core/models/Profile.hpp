#pragma once
#include <string>
#include <vector>
#include <map>
#include "Snowflake.hpp"
#include "ActiveStatus.hpp"
#include "GuildMember.hpp"

struct Profile
{
	// Basic data. Provided with almost every user object.
	bool m_bUsingDefaultData = true;
	Snowflake m_snowflake = 0;
	std::string m_name;
	std::string m_globalName;
	int m_discrim = 0;
	std::string m_avatarlnk = "";
	bool m_bIsBot = false;

	// Activity data. This is updated by presence updates.
	eActiveStatus m_activeStatus = STATUS_OFFLINE;
	std::string m_status = "";

	std::map<Snowflake, GuildMember> m_guildMembers;

	Profile() {}

	bool HasGuildMemberProfile(Snowflake guild) const {
		auto fnd = m_guildMembers.find(guild);
		return fnd != m_guildMembers.end();
	}

	std::string GetName(Snowflake guild) const {
		auto it = m_guildMembers.find(guild);
		if (it == m_guildMembers.end())
			return m_globalName;
		const GuildMember& gm = it->second;
		if (gm.m_nick.empty())
			return m_globalName;
		return gm.m_nick;
	}

	std::string GetStatus(Snowflake guild) const {
		auto it = m_guildMembers.find(guild);
		if (it == m_guildMembers.end())
			return m_status;
		const GuildMember& gm = it->second;
		if (gm.m_nick.empty())
			return m_status;
		return gm.m_status;
	}

	const std::string& GetUsername() const { return m_name; }

};
