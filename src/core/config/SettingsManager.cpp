#include "SettingsManager.hpp"
#include "../utils/Base64.hpp"
#include "../utils/ProtoReader.hpp"

#include <cstdio>

static SettingsManager g_SMSingleton;
SettingsManager* GetSettingsManager()
{
	return &g_SMSingleton;
}

// Field numbers in PreloadedUserSettings and the messages inside it.
namespace Field
{
	enum {
		STATUS = 11,          // StatusSettings
		GUILD_FOLDERS = 14,   // GuildFolders
	};
	namespace Status {
		enum {
			STATUS = 1,         // StringValue
			CUSTOM_STATUS = 2,  // CustomStatus
		};
	}
	namespace CustomStatus {
		enum { TEXT = 1 };
	}
	namespace GuildFolders {
		enum { FOLDERS = 1 };   // repeated GuildFolder
	}
	namespace GuildFolder {
		enum {
			GUILD_IDS = 1,  // repeated fixed64, packed
			ID = 2,         // Int64Value
			NAME = 3,       // StringValue
		};
	}
	namespace Wrapper {
		enum { VALUE = 1 };     // google.protobuf.*Value
	}
}

// The value inside a wrapper message (StringValue, Int64Value).
static bool WrappedString(ProtoReader msg, std::string& out)
{
	while (msg.Next())
		if (msg.Field() == Field::Wrapper::VALUE && msg.Type() == ProtoReader::LEN)
			out = msg.String();
	return !msg.Broken();
}

static bool WrappedInt(ProtoReader msg, uint64_t& out)
{
	while (msg.Next())
		if (msg.Field() == Field::Wrapper::VALUE && msg.Type() == ProtoReader::VARINT)
			out = msg.Value();
	return !msg.Broken();
}

static bool ReadStatus(ProtoReader msg, std::string& status, std::string& custom)
{
	while (msg.Next())
	{
		if (msg.Type() != ProtoReader::LEN)
			continue;
		if (msg.Field() == Field::Status::STATUS && !WrappedString(msg.Message(), status))
			return false;
		if (msg.Field() == Field::Status::CUSTOM_STATUS) {
			ProtoReader cs = msg.Message();
			while (cs.Next())
				if (cs.Field() == Field::CustomStatus::TEXT && cs.Type() == ProtoReader::LEN)
					custom = cs.String();
			if (cs.Broken())
				return false;
		}
	}
	return !msg.Broken();
}

static bool ReadFolder(ProtoReader msg, std::map<Snowflake, std::string>& folders, std::vector<std::pair<Snowflake, Snowflake>>& guilds)
{
	uint64_t id = 0;
	std::string name;
	std::vector<Snowflake> ids;
	while (msg.Next())
	{
		switch (msg.Field())
		{
			case Field::GuildFolder::GUILD_IDS:
				// packed: little-endian 64-bit numbers; or one at a time
				if (msg.Type() == ProtoReader::LEN) {
					if (msg.Size() % 8)
						return false;
					const uint8_t* p = msg.Data();
					for (size_t i = 0; i < msg.Size(); i += 8) {
						uint64_t v = 0;
						for (int b = 7; b >= 0; b--)
							v = (v << 8) | p[i + b];
						ids.push_back(v);
					}
				}
				else if (msg.Type() == ProtoReader::I64)
					ids.push_back(msg.Value());
				break;
			case Field::GuildFolder::ID:
				if (msg.Type() == ProtoReader::LEN && !WrappedInt(msg.Message(), id))
					return false;
				break;
			case Field::GuildFolder::NAME:
				if (msg.Type() == ProtoReader::LEN && !WrappedString(msg.Message(), name))
					return false;
				break;
		}
	}
	if (msg.Broken())
		return false;
	if (id)
		folders[id] = name;
	for (Snowflake g : ids)
		guilds.push_back(std::make_pair((Snowflake) id, g));
	return true;
}

void SettingsManager::LoadData(const uint8_t* data, size_t size, bool partial)
{
	// read into copies: broken data changes nothing
	std::string status = partial ? m_status : "", custom = partial ? m_customStatus : "";
	std::map<Snowflake, std::string> folders;
	std::vector<std::pair<Snowflake, Snowflake>> guilds;
	bool haveFolders = false, ok = true;

	ProtoReader msg(data, size);
	while (ok && msg.Next())
	{
		if (msg.Type() != ProtoReader::LEN)
			continue;
		if (msg.Field() == Field::STATUS) {
			// (what the update holds of the status replaces all of it)
			status.clear();
			custom.clear();
			ok = ReadStatus(msg.Message(), status, custom);
		}
		else if (msg.Field() == Field::GUILD_FOLDERS) {
			folders.clear();
			guilds.clear();
			haveFolders = true;
			ProtoReader gf = msg.Message();
			while (ok && gf.Next())
				if (gf.Field() == Field::GuildFolders::FOLDERS && gf.Type() == ProtoReader::LEN)
					ok = ReadFolder(gf.Message(), folders, guilds);
			ok = ok && !gf.Broken();
		}
	}
	if (!ok || msg.Broken()) {
		fprintf(stderr, "dm: the settings Discord sent could not be read (%u bytes)\n", (unsigned) size);
		return;
	}

	m_status = status;
	m_customStatus = custom;
	if (haveFolders || !partial) {
		m_folders = folders;
		m_folderGuilds = guilds;
	}
}

void SettingsManager::LoadDataBase64(const std::string& base64, bool partial)
{
	std::vector<uint8_t> data = Base64Decode(base64);
	LoadData(data.data(), data.size(), partial);
}

eActiveStatus SettingsManager::GetOnlineIndicator() const
{
	return GetStatusFromString(m_status);
}
