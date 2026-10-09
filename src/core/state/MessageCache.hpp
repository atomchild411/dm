#pragma once

#include <map>
#include <list>
#include <nlohmann/json.h>
#include "../models/Snowflake.hpp"
#include "../models/ScrollDir.hpp"
#include "../models/Message.hpp"

struct MessageChunkList
{
	// int - Offset. How many messages ago was this message posted
	std::map<Snowflake, MessagePtr> m_messages;

	bool m_lastMessagesLoaded = false;
	Snowflake m_guild = 0;

	// history on disk (MessageCache::SetDiskCache)
	bool m_synced = false;        // the channel's newest messages were fetched this session
	bool m_cacheLoaded = false;   // the disk's copy was read
	bool m_dirty = false;         // changed since the disk's copy was written
	Snowflake m_cachedNewest = 0; // newest message read from disk, until the newest are fetched
	void LoadCached(nlohmann::json& messages, bool complete);
	bool SaveCached(size_t maxMessages, std::string& out) const;

	MessageChunkList();
	void ProcessRequest(ScrollDir::eScrollDir sd, Snowflake anchor, nlohmann::json& j, const std::string& channelName);
	void AddMessage(const Message& msg);
	void EditMessage(const Message& msg);
	void DeleteMessage(Snowflake message);
	MessagePtr GetLoadedMessage(Snowflake message);
};

class MessageCache
{
public:
	MessageCache();
	
	void GetLoadedMessages(Snowflake channel, Snowflake guild, std::list<MessagePtr>& out);

	// note: scroll dir used to add gap message
	void ProcessRequest(Snowflake channel, ScrollDir::eScrollDir sd, Snowflake anchor, nlohmann::json& j, const std::string& channelName);

	void AddMessage(Snowflake channel, const Message& msg);
	void EditMessage(Snowflake channel, const Message& msg);
	void DeleteMessage(Snowflake channel, Snowflake message);
	void ClearAllChannels();

	MessagePtr GetLoadedMessage(Snowflake channel, Snowflake message);

	// History on disk: the newest messages of each channel opened (up to
	// HISTORY_MESSAGES each), one file a channel in dir, maxBytes in all
	// (the channels opened longest ago go first).  Off until set.  A channel
	// shows its saved messages at once; its newest are still fetched, and
	// replace the saved ones they overlap (edits, and deletions, while away).
	void SetDiskCache(const std::string& dir, size_t maxBytes);
	// Before a channel is shown.
	void LoadCachedChannel(Snowflake channel, Snowflake guild);
	// Writes the channels that changed (now and then, and at exit).
	void SaveDirty();
	// Removes every saved channel (logging out).
	void ClearDiskCache();

	static const size_t HISTORY_MESSAGES = 200;

private:
	std::map <Snowflake, MessageChunkList> m_mapMessages;
	std::string m_diskDir;
	size_t m_diskMax = 0;

	std::string CachedFile(Snowflake channel) const;
	void TrimDisk();
};

MessageCache* GetMessageCache();
