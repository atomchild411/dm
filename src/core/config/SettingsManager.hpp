#pragma once

#include <map>
#include <string>
#include <vector>
#include "../models/Snowflake.hpp"
#include "../models/ActiveStatus.hpp"

// thanks https://github.com/discord-net/Discord.Net/blob/dev/src/Discord.Net.Entities/Activities/ActivityType.cs
enum eActivityType
{
	ACTIVITY_PLAYING,
	ACTIVITY_STREAMING,
	ACTIVITY_LISTENING,
	ACTIVITY_WATCHING,
	ACTIVITY_CUSTOM_STATUS,
	ACTIVITY_COMPETING,
};

// The account's settings that Discord keeps for its clients (the
// PreloadedUserSettings protobuf message, as in
// https://github.com/dolfies/discord-protos): the few this client uses.
class SettingsManager
{
public:
	// Discord's settings, base64 protobuf: all of them (partial false: in
	// READY, or an update with everything), or only what changed.
	void LoadDataBase64(const std::string& base64, bool partial = false);
	void LoadData(const uint8_t* data, size_t size, bool partial = false);

	eActiveStatus GetOnlineIndicator() const;
	std::string GetCustomStatusText() const { return m_customStatus; }

	// The folders (id -> name) and the guilds in the order the user set
	// them, each with its folder's id (0: none).
	void GetGuildFoldersEx(std::map<Snowflake, std::string>& folders, std::vector<std::pair<Snowflake, Snowflake>>& guilds) const
	{
		folders = m_folders;
		guilds = m_folderGuilds;
	}

private:
	std::string m_status;        // "online", "idle", "dnd", "invisible"
	std::string m_customStatus;
	std::map<Snowflake, std::string> m_folders;
	std::vector<std::pair<Snowflake, Snowflake>> m_folderGuilds;
};

SettingsManager* GetSettingsManager();
