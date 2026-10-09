#pragma once

#include <string>
#include <vector>

#include "ListRow.hpp"
#include "models/Message.hpp"

// Sample data for --demo: servers, channels, members and messages that need
// no login, for trying the client out, screenshots and performance tests.
// The demo has a selection like a session's: picking a server or a channel
// changes the lists, opening a channel marks it read, and the user's own
// messages and reactions are kept (in memory only).
namespace Demo
{
	// The channel the sample messages start in (#general), and the server
	// it is in.
	const Snowflake CHANNEL = 302;
	const Snowflake SELECTED_GUILD = 100;
	const Snowflake SELECTED_CHANNEL = CHANNEL;
	// The server row that stands for the direct messages.
	const Snowflake DIRECT_MESSAGES = 1;
	// The user, as the demo's messages name them.
	const Snowflake ME = (Snowflake) 1099 << 22;

	// Puts #general's messages in the message cache (other channels'
	// come when they are opened).
	void LoadMessages();

	std::vector<ListRow> GuildRows();
	std::vector<ListRow> ChannelRows();
	std::vector<ListRow> MemberRows();

	Snowflake Guild();
	Snowflake Channel();
	// Picks a server; returns the channel to open there (the one open there
	// last, else its first).
	Snowflake SelectGuild(Snowflake guild);
	// Opens a channel: its messages go into the message cache (the first
	// time) and it is read from now on.  Returns what was read before it
	// opened (the NEW line goes after that message), or 0.
	Snowflake OpenChannel(Snowflake channel);
	// A channel by name, in any server ("stress-test"), or 0.
	Snowflake FindChannel(const std::string& name);

	std::string GuildName();
	std::string ChannelName(Snowflake channel);
	std::string ChannelTopic(Snowflake channel);
	bool IsDirect(Snowflake channel);

	// The user's message, at once (replyTo: the message it answers, or
	// null).  Returns false for an empty one.
	bool Send(Snowflake channel, const std::string& text, const Message* replyTo);
	// The user's reaction on a message added or taken back.
	void React(Snowflake channel, const Message& message, const Reaction& emoji, bool add);
}
