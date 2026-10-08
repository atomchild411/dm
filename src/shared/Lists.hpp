#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ListRow.hpp"

struct Channel;

// The rows of the server, channel and member lists, and the counts and
// conversation lists around them, from the core's state.  UI thread.
namespace Lists
{
	// Direct Messages, a gap, then the servers (in their folders), with
	// unread marks and mention badges.  Select GetCurrentGuildID().
	std::vector<ListRow> GuildRows();

	// The current server's channels: those outside categories, then each
	// category's (empty or hidden categories left out).  While the server's
	// channels load, one "please wait" header (and the fetch is started).
	std::vector<ListRow> ChannelRows();

	// The current server's member list, with its group headers.
	std::vector<ListRow> MemberRows();

	// Mentions across every server and conversation, and those in direct
	// messages alone (each unread direct message counts as one).
	struct Unread { int total = 0; int directMessages = 0; };
	Unread UnreadCounts();

	// Direct and group conversations, unread first, then the most recently
	// active; at most max of them.
	struct Conversation { Snowflake channel; std::string name; int mentions; };
	std::vector<Conversation> Conversations(size_t max);

	// The colour of the member's highest coloured role (0xRRGGBB), or 0.
	uint32_t RoleColor(Snowflake user, Snowflake guild);

	// Channels that hold messages (not voice channels, categories, forums).
	bool IsTextChannel(const Channel& ch);
}
