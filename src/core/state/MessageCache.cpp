#include "MessageCache.hpp"
#include "../utils/Util.hpp"
#include "ProfileCache.hpp"
#include "../Frontend.hpp"
#include "../DiscordInstance.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>

constexpr int MESSAGES_PER_REQUEST = 50;

using nlohmann::json;
static MessageCache g_MCSingleton;

MessageCache::MessageCache()
{
}

void MessageCache::GetLoadedMessages(Snowflake channel, Snowflake guild, std::list<MessagePtr>& out)
{
	MessageChunkList& lst = m_mapMessages[channel];
	lst.m_guild = guild;

	for (auto& msg : lst.m_messages)
		out.push_back(msg.second);
}

void MessageCache::ProcessRequest(Snowflake channel, ScrollDir::eScrollDir sd, Snowflake anchor, nlohmann::json& j, const std::string& channelName)
{
	MessageChunkList& lst = m_mapMessages[channel];
	lst.ProcessRequest(sd, anchor, j, channelName);
}

void MessageCache::AddMessage(Snowflake channel, const Message& msg)
{
	m_mapMessages[channel].AddMessage(msg);
}

void MessageCache::EditMessage(Snowflake channel, const Message& msg)
{
	m_mapMessages[channel].EditMessage(msg);
}

void MessageCache::DeleteMessage(Snowflake channel, Snowflake msg)
{
	m_mapMessages[channel].DeleteMessage(msg);
}

void MessageCache::ClearAllChannels()
{
	SaveDirty();
	m_mapMessages.clear();
}

void MessageCache::SetDiskCache(const std::string& dir, size_t maxBytes)
{
	m_diskDir = dir;
	m_diskMax = maxBytes;
	Message::s_keepJson = !dir.empty();
	TrimDisk();
}

std::string MessageCache::CachedFile(Snowflake channel) const
{
	return m_diskDir + "/" + std::to_string(channel) + ".json";
}


void MessageCache::LoadCachedChannel(Snowflake channel, Snowflake guild)
{
	if (m_diskDir.empty() || !channel)
		return;
	MessageChunkList& lst = m_mapMessages[channel];
	lst.m_guild = guild;
	if (lst.m_cacheLoaded || lst.m_synced)
		return;
	lst.m_cacheLoaded = true;

	std::string path = CachedFile(channel);
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return;
	std::string text;
	char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		text.append(buf, n);
	fclose(f);

	json j = json::parse(text, nullptr, false);
	if (!j.is_object() || !j.contains("messages") || !j["messages"].is_array()) {
		remove(path.c_str()); // unreadable: fetched afresh
		return;
	}
	bool complete = j.contains("complete") && j["complete"].is_boolean() && (bool) j["complete"];
	lst.LoadCached(j["messages"], complete);
	utime(path.c_str(), NULL); // used now: trimmed last
}

void MessageCache::SaveDirty()
{
	if (m_diskDir.empty())
		return;
	bool wrote = false;
	for (auto& e : m_mapMessages)
	{
		MessageChunkList& lst = e.second;
		if (!lst.m_dirty || !lst.m_synced)
			continue;
		lst.m_dirty = false;
		std::string text;
		if (!lst.SaveCached(HISTORY_MESSAGES, text))
			continue;
		std::string path = CachedFile(e.first), tmp = path + ".new";
		int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0)
			continue;
		bool ok = write(fd, text.data(), text.size()) == (ssize_t) text.size();
		ok = close(fd) == 0 && ok;
		if (!ok || !RenameOver(tmp, path))
			remove(tmp.c_str());
		wrote = true;
	}
	if (wrote)
		TrimDisk();
}

void MessageCache::TrimDisk()
{
	if (m_diskDir.empty() || !m_diskMax)
		return;
	struct Entry { std::string path; off_t size; time_t mtime; };
	std::vector<Entry> files;
	size_t total = 0;
	DIR* d = opendir(m_diskDir.c_str());
	if (!d)
		return;
	while (struct dirent* de = readdir(d)) {
		std::string name = de->d_name;
		if (name.size() < 6 || name.compare(name.size() - 5, 5, ".json") != 0)
			continue;
		std::string path = m_diskDir + "/" + name;
		struct stat st;
		if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		files.push_back({ path, st.st_size, st.st_mtime });
		total += st.st_size;
	}
	closedir(d);
	if (total <= m_diskMax)
		return;
	// the channels used longest ago first, down to 90% of the bound
	std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
	for (auto& e : files) {
		if (total <= m_diskMax / 10 * 9)
			break;
		if (remove(e.path.c_str()) == 0)
			total -= e.size;
	}
}

void MessageCache::ClearDiskCache()
{
	if (m_diskDir.empty())
		return;
	for (auto& e : m_mapMessages)
		e.second.m_dirty = false;
	DIR* d = opendir(m_diskDir.c_str());
	if (!d)
		return;
	std::vector<std::string> names;
	while (struct dirent* de = readdir(d))
		names.push_back(de->d_name);
	closedir(d);
	for (auto& name : names)
		if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0)
			remove((m_diskDir + "/" + name).c_str());
}


MessagePtr MessageCache::GetLoadedMessage(Snowflake channel, Snowflake message)
{
	return m_mapMessages[channel].GetLoadedMessage(message);
}

MessageCache* GetMessageCache()
{
	return &g_MCSingleton;
}

MessageChunkList::MessageChunkList()
{
	// Add a single gap message to fetch stuff
	MessagePtr msg = MakeMessage();
	msg->m_type = MessageType::GAP_UP;
	msg->m_anchor = 0;
	msg->m_snowflake = 0;
	msg->m_author = GetFrontend()->GetPleaseWaitText();
	msg->m_message = "";
	msg->m_dateFull = "";
	msg->m_dateCompact = "";
	m_messages[msg->m_snowflake] = msg;
}

void MessageChunkList::ProcessRequest(ScrollDir::eScrollDir sd, Snowflake gap, json& j, const std::string& channelName)
{
	Snowflake lowestMsg = (Snowflake) -1LL, highestMsg = 0;

	bool addedMessages = false;

	// remove the anchor; a gap anchored at 0 asked for the newest messages
	bool newest = false;
	auto iter = m_messages.find(gap);
	if (iter != m_messages.end())
	{
		if (iter->second->IsLoadGap()) {
			newest = sd == ScrollDir::BEFORE && iter->second->m_anchor == 0;
			m_messages.erase(iter);
		}
	}

	// for each message
	int receivedMessages = 0;
	std::set<Snowflake> received;
	for (json& data : j)
	{
		auto msg = MakeMessage();
		msg->Load(data, m_guild);

		if (!addedMessages)
		{
			if (m_messages.find(msg->m_snowflake) == m_messages.end())
				addedMessages = true;
		}

		m_messages[msg->m_snowflake] = msg;
		received.insert(msg->m_snowflake);
		receivedMessages++;

		if (lowestMsg > msg->m_snowflake)
			lowestMsg = msg->m_snowflake;
		if (highestMsg < msg->m_snowflake)
			highestMsg = msg->m_snowflake;
	}

	// The newest messages, fetched over ones read from disk: saved ones in
	// that stretch the server no longer has were deleted meanwhile; and if
	// the stretch reaches down into the saved ones, there is no gap between.
	bool joined = false;
	if (newest && m_cachedNewest && receivedMessages > 0)
	{
		for (auto it = m_messages.lower_bound(lowestMsg); it != m_messages.end() && it->first <= highestMsg; )
		{
			MessageType::eType t = it->second->m_type;
			bool real = !it->second->IsLoadGap() && t != MessageType::CHANNEL_HEADER &&
				t != MessageType::SENDING_MESSAGE && t != MessageType::UNSENT_MESSAGE;
			if (real && !received.count(it->first))
				it = m_messages.erase(it);
			else
				++it;
		}
		joined = lowestMsg <= m_cachedNewest;
	}
	if (newest) {
		m_cachedNewest = 0;
		m_synced = true;
	}
	m_dirty = true;

	bool addBefore = sd != ScrollDir::AFTER && receivedMessages >= MESSAGES_PER_REQUEST && !joined;
	bool addAfter  = sd != ScrollDir::BEFORE;

	// fewer than asked for: the channel's first message is here (not so
	// when asking for messages after one: then the newest are)
	if (receivedMessages < MESSAGES_PER_REQUEST && sd != ScrollDir::AFTER)
	{
		auto msg = MakeMessage();
		msg->m_type = MessageType::CHANNEL_HEADER;
		msg->m_snowflake = 1;
		msg->m_author = channelName;
		m_messages[msg->m_snowflake] = msg;
	}

	if (addBefore && addedMessages)
	{
		auto msg = MakeMessage();
		msg->m_author = GetFrontend()->GetPleaseWaitText();
		msg->m_type = MessageType::GAP_UP;
		msg->m_anchor = lowestMsg;
		msg->m_snowflake = lowestMsg - 1;
		m_messages[msg->m_snowflake] = msg;
	}

	if (addAfter && addedMessages)
	{
		auto msg = MakeMessage();
		msg->m_author = GetFrontend()->GetPleaseWaitText();
		msg->m_type = MessageType::GAP_DOWN;
		msg->m_anchor = highestMsg;
		msg->m_snowflake = highestMsg + 1;
		m_messages[msg->m_snowflake] = std::move(msg);
	}

	GetDiscordInstance()->OnFetchedMessages(gap, sd);
}

void MessageChunkList::AddMessage(const Message& msg)
{
	m_dirty = true;
	if (msg.m_anchor)
		DeleteMessage(msg.m_anchor);

	m_messages[msg.m_snowflake] = std::make_shared<Message>(msg);
}

void MessageChunkList::EditMessage(const Message& msg)
{
	m_dirty = true;
	DeleteMessage(msg.m_snowflake);
	m_messages[msg.m_snowflake] = std::make_shared<Message>(msg);
}

void MessageChunkList::DeleteMessage(Snowflake message)
{
	m_dirty = true;
	auto iter = m_messages.find(message);
	if (m_messages.end() != iter)
		m_messages.erase(iter);
}

// Messages read from disk, oldest first.  The gap that asks for the newest
// messages moves to just above them (where the view shows it, and asks);
// anything the gateway brought meanwhile stays above that.
void MessageChunkList::LoadCached(json& messages, bool complete)
{
	Snowflake oldest = (Snowflake) -1LL, newest = 0;
	std::vector<MessagePtr> loaded;
	for (json& data : messages) {
		auto msg = MakeMessage();
		msg->Load(data, m_guild);
		if (!msg->m_snowflake || msg->IsLoadGap())
			continue;
		loaded.push_back(msg);
		oldest = std::min(oldest, msg->m_snowflake);
		newest = std::max(newest, msg->m_snowflake);
	}
	if (loaded.empty())
		return;

	auto first = m_messages.find(0);
	if (first != m_messages.end() && first->second->IsLoadGap() && first->second->m_anchor == 0)
		m_messages.erase(first);
	for (auto& msg : loaded)
		m_messages.insert(std::make_pair(msg->m_snowflake, msg)); // a newer copy from the gateway wins

	if (complete) {
		auto msg = MakeMessage();
		msg->m_type = MessageType::CHANNEL_HEADER;
		msg->m_snowflake = 1;
		m_messages[msg->m_snowflake] = msg;
	}
	else {
		auto msg = MakeMessage();
		msg->m_author = GetFrontend()->GetPleaseWaitText();
		msg->m_type = MessageType::GAP_UP;
		msg->m_anchor = oldest;
		msg->m_snowflake = oldest - 1;
		m_messages[msg->m_snowflake] = msg;
	}
	auto top = MakeMessage();
	top->m_author = GetFrontend()->GetPleaseWaitText();
	top->m_type = MessageType::GAP_UP;
	top->m_anchor = 0; // the newest
	top->m_snowflake = newest + 1;
	m_messages[top->m_snowflake] = top;
	m_cachedNewest = newest;
}

// The newest unbroken run of messages (no gap in it), oldest first, as
// Discord sent them.
bool MessageChunkList::SaveCached(size_t maxMessages, std::string& out) const
{
	std::vector<const Message*> run;
	bool complete = false;
	for (auto it = m_messages.rbegin(); it != m_messages.rend(); ++it)
	{
		const Message& m = *it->second;
		if (m.IsLoadGap())
			break;
		if (m.m_type == MessageType::CHANNEL_HEADER) {
			complete = true;
			break;
		}
		if (m.m_type == MessageType::SENDING_MESSAGE || m.m_type == MessageType::UNSENT_MESSAGE || m.m_rawJson.empty())
			continue;
		if (run.size() >= maxMessages)
			break;
		run.push_back(&m);
	}
	if (run.empty())
		return false;
	out = std::string("{\"version\":1,\"complete\":") + (complete ? "true" : "false") + ",\"messages\":[";
	for (size_t i = run.size(); i-- > 0; ) {
		out += run[i]->m_rawJson;
		if (i)
			out += ",";
	}
	out += "]}";
	return true;
}

MessagePtr MessageChunkList::GetLoadedMessage(Snowflake message)
{
	auto iter = m_messages.find(message);
	if (m_messages.end() == iter)
		return nullptr;

	return iter->second;
}
