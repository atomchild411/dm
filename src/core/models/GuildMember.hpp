#pragma once
#include <string>
#include <vector>
#include "Snowflake.hpp"

struct GuildMember
{
	bool m_bIsGroup = false;

	// if group
	Snowflake m_groupId = 0;
	int m_groupCount = 0;

	// if user
	Snowflake m_user = 0;
	std::vector<Snowflake> m_roles;
	std::string m_avatar;
	std::string m_nick;
	std::string m_status;
	std::string m_pronouns;
	std::string m_bio;
	time_t m_joinedAt = 0;
	bool m_bIsLoadedFromChunk = false;
};
