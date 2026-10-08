#pragma once

#include <functional>
#include <string>

#include "models/Message.hpp"

// Emoji shortcodes, as Discord's clients allow them: :joy:, a server's
// :name:, and :U+1F600: (or :U+1F468+200D+1F469:) for any other character.
// They become the real characters when the message is sent.  Motif's
// message box (XmText) shows ISO 8859-1 only, so that client also writes
// every emoji it cannot show this way (ToEditor).
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
	// The same with the server's emoji looked up by serverEmoji(name), which
	// gives the <:name:id> to send, or "" (for the benchmark's check).
	std::string FromEditor(const std::string& utf8, std::function<std::string(const std::string&)> serverEmoji);
}
