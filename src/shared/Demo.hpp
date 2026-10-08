#pragma once

#include <vector>

#include "ListRow.hpp"

// Sample data for --demo: messages and lists that need no login, for
// seeing how messages are drawn and for screenshots.
namespace Demo
{
	// The channel the sample messages are in (not a real one).
	const Snowflake CHANNEL = 4242;
	// What the sample lists show as selected.
	const Snowflake SELECTED_GUILD = 100;
	const Snowflake SELECTED_CHANNEL = 302;

	// Puts the sample messages in the message cache, under CHANNEL.
	void LoadMessages();

	std::vector<ListRow> GuildRows();
	std::vector<ListRow> ChannelRows();
	std::vector<ListRow> MemberRows();
}
