#include "Typing.hpp"

#include <ctime>
#include <map>
#include <vector>

#include "DiscordInstance.hpp"
#include "state/ProfileCache.hpp"
#include "Timers.hpp"

namespace
{
	std::map<Snowflake, std::map<Snowflake, time_t>> g_typing; // channel -> user -> until
	std::function<void()> g_changed;
	int g_timer = 0;

	void Changed()
	{
		if (g_changed)
			g_changed();
	}

	// Once a second while anyone types: those whose time ran out go.
	void Tick()
	{
		g_timer = 0;
		time_t now = time(NULL);
		bool any = false;
		for (auto& ch : g_typing) {
			for (auto it = ch.second.begin(); it != ch.second.end(); ) {
				if (it->second <= now)
					it = ch.second.erase(it);
				else {
					++it;
					any = true;
				}
			}
		}
		Changed();
		if (any)
			g_timer = Timers::After(1000, Tick);
	}
}

void Typing::SetChangedCallback(std::function<void()> fn)
{
	g_changed = fn;
}

void Typing::Started(Snowflake user, Snowflake channel)
{
	DiscordInstance* pInst = GetDiscordInstance();
	if (user == pInst->GetUserID())
		return;
	g_typing[channel][user] = time(NULL) + 10;
	Changed();
	if (!g_timer)
		g_timer = Timers::After(1000, Tick);
}

void Typing::Stopped(Snowflake channel, Snowflake user)
{
	auto it = g_typing.find(channel);
	if (it == g_typing.end())
		return;
	it->second.erase(user);
	Changed();
}

std::string Typing::Text(Snowflake channel)
{
	DiscordInstance* pInst = GetDiscordInstance();
	auto it = g_typing.find(channel);
	if (!pInst || it == g_typing.end() || it->second.empty())
		return "";
	Channel* pChan = pInst->GetChannelGlobally(channel);
	Snowflake guild = pChan ? pChan->m_parentGuild : 0;
	std::vector<std::string> names;
	for (auto& t : it->second) {
		Profile* p = GetProfileCache()->LookupProfile(t.first, "", "", "", false);
		names.push_back(p ? p->GetName(guild) : "Someone");
	}
	if (names.size() > 3)
		return "Several people are typing...";
	std::string who;
	for (size_t i = 0; i < names.size(); i++)
		who += (i ? (i + 1 == names.size() ? " and " : ", ") : "") + names[i];
	return who + (names.size() == 1 ? " is typing..." : " are typing...");
}
