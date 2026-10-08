#pragma once

#include <string>

#include "models/Message.hpp"

// The message box (XmText) shows ISO 8859-1 only, so emoji are written in
// it as shortcodes, as Discord's clients allow: :joy:, a server's :name:,
// and :U+1F600: (or :U+1F468+200D+1F469:) for any other character it
// cannot show.  They become the real characters when the message is sent.
namespace Shortcodes
{
	// The emoji the picker offers, in its order (PICKER_COUNT of them).
	const int PICKER_COUNT = 48;
	const char* PickerEmoji(int i);

	// What to type for an emoji (a picked reaction): ":joy:", ":name:".
	std::string For(const Reaction& r);

	// Text for the message box: characters it cannot show as shortcodes.
	std::string ToEditor(const std::string& utf8);
	// Text from the message box: shortcodes as the characters (a server's
	// emoji as <:name:id>, from guild's); unknown ones are left alone.
	std::string FromEditor(const std::string& utf8, Snowflake guild);
}
