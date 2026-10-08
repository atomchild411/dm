#pragma once

#include <string>
#include <vector>

#include "models/Message.hpp"

// What an emoji picker offers (Add Reaction, Insert Emoji): the common
// emoji, then the server's own that can be used, by name.  A Unicode emoji
// has its character in m_emojiName; a server's has its id and name.
namespace EmojiChoices
{
	struct Section
	{
		std::string title;            // "" for the common ones; the server's name
		std::vector<Reaction> emoji;
	};
	std::vector<Section> For(Snowflake guild);
}
