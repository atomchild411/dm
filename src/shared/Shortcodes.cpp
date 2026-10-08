#include "Shortcodes.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "DiscordInstance.hpp"
#include "Utf8.hpp"

namespace
{
	struct Named
	{
		const char* name;
		const char* emoji;
	};

	// the picker's emoji first, in its order; then more names to type
	const Named g_names[] = {
	{ "thumbsup", "\xf0\x9f\x91\x8d" },
	{ "thumbsdown", "\xf0\x9f\x91\x8e" },
	{ "heart", "\xe2\x9d\xa4\xef\xb8\x8f" },
	{ "joy", "\xf0\x9f\x98\x82" },
	{ "rofl", "\xf0\x9f\xa4\xa3" },
	{ "blush", "\xf0\x9f\x98\x8a" },
	{ "heart_eyes", "\xf0\x9f\x98\x8d" },
	{ "open_mouth", "\xf0\x9f\x98\xae" },
	{ "cry", "\xf0\x9f\x98\xa2" },
	{ "sob", "\xf0\x9f\x98\xad" },
	{ "rage", "\xf0\x9f\x98\xa1" },
	{ "pray", "\xf0\x9f\x99\x8f" },
	{ "tada", "\xf0\x9f\x8e\x89" },
	{ "fire", "\xf0\x9f\x94\xa5" },
	{ "100", "\xf0\x9f\x92\xaf" },
	{ "eyes", "\xf0\x9f\x91\x80" },
	{ "white_check_mark", "\xe2\x9c\x85" },
	{ "x", "\xe2\x9d\x8c" },
	{ "thinking", "\xf0\x9f\xa4\x94" },
	{ "sunglasses", "\xf0\x9f\x98\x8e" },
	{ "partying_face", "\xf0\x9f\xa5\xb3" },
	{ "sweat_smile", "\xf0\x9f\x98\x85" },
	{ "raised_hands", "\xf0\x9f\x99\x8c" },
	{ "clap", "\xf0\x9f\x91\x8f" },
	{ "muscle", "\xf0\x9f\x92\xaa" },
	{ "handshake", "\xf0\x9f\xa4\x9d" },
	{ "ok_hand", "\xf0\x9f\x91\x8c" },
	{ "sparkles", "\xe2\x9c\xa8" },
	{ "rocket", "\xf0\x9f\x9a\x80" },
	{ "skull", "\xf0\x9f\x92\x80" },
	{ "sleeping", "\xf0\x9f\x98\xb4" },
	{ "exploding_head", "\xf0\x9f\xa4\xaf" },
	{ "pleading_face", "\xf0\x9f\xa5\xba" },
	{ "grimacing", "\xf0\x9f\x98\xac" },
	{ "upside_down", "\xf0\x9f\x99\x83" },
	{ "innocent", "\xf0\x9f\x98\x87" },
	{ "robot", "\xf0\x9f\xa4\x96" },
	{ "wave", "\xf0\x9f\x91\x8b" },
	{ "star", "\xe2\xad\x90" },
	{ "purple_heart", "\xf0\x9f\x92\x9c" },
	{ "blue_heart", "\xf0\x9f\x92\x99" },
	{ "green_heart", "\xf0\x9f\x92\x9a" },
	{ "yellow_heart", "\xf0\x9f\x92\x9b" },
	{ "orange_heart", "\xf0\x9f\xa7\xa1" },
	{ "black_heart", "\xf0\x9f\x96\xa4" },
	{ "pizza", "\xf0\x9f\x8d\x95" },
	{ "coffee", "\xe2\x98\x95" },
	{ "penguin", "\xf0\x9f\x90\xa7" },
	{ "+1", "\xf0\x9f\x91\x8d" },
	{ "-1", "\xf0\x9f\x91\x8e" },
	{ "smile", "\xf0\x9f\x98\x84" },
	{ "smiley", "\xf0\x9f\x98\x83" },
	{ "grin", "\xf0\x9f\x98\x81" },
	{ "slight_smile", "\xf0\x9f\x99\x82" },
	{ "wink", "\xf0\x9f\x98\x89" },
	{ "laughing", "\xf0\x9f\x98\x86" },
	{ "stuck_out_tongue", "\xf0\x9f\x98\x9b" },
	{ "neutral_face", "\xf0\x9f\x98\x90" },
	{ "unamused", "\xf0\x9f\x98\x92" },
	{ "rolling_eyes", "\xf0\x9f\x99\x84" },
	{ "confused", "\xf0\x9f\x98\x95" },
	{ "sweat", "\xf0\x9f\x98\x93" },
	{ "shrug", "\xf0\x9f\xa4\xb7" },
	{ "facepalm", "\xf0\x9f\xa4\xa6" },
	{ "poop", "\xf0\x9f\x92\xa9" },
	{ "thumbs_up", "\xf0\x9f\x91\x8d" },
	};
	const int COUNT = sizeof g_names / sizeof g_names[0];

	void AppendUtf8(std::string& s, unsigned cp)
	{
		if (cp < 0x80)
			s += (char) cp;
		else if (cp < 0x800) {
			s += (char) (0xc0 | (cp >> 6));
			s += (char) (0x80 | (cp & 0x3f));
		}
		else if (cp < 0x10000) {
			s += (char) (0xe0 | (cp >> 12));
			s += (char) (0x80 | ((cp >> 6) & 0x3f));
			s += (char) (0x80 | (cp & 0x3f));
		}
		else {
			s += (char) (0xf0 | (cp >> 18));
			s += (char) (0x80 | ((cp >> 12) & 0x3f));
			s += (char) (0x80 | ((cp >> 6) & 0x3f));
			s += (char) (0x80 | (cp & 0x3f));
		}
	}

	bool TokenChar(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '_' || c == '+' || c == '-';
	}
}

const char* Shortcodes::PickerEmoji(int i)
{
	return g_names[i].emoji;
}

std::string Shortcodes::For(const Reaction& r)
{
	if (r.m_emojiId)
		return ":" + r.m_emojiName + ":";
	for (int i = 0; i < COUNT; i++)
		if (r.m_emojiName == g_names[i].emoji)
			return std::string(":") + g_names[i].name + ":";
	return ToEditor(r.m_emojiName);
}

std::string Shortcodes::ToEditor(const std::string& utf8)
{
	std::string out;
	const char* p = utf8.c_str();
	const char* end = p + utf8.size();
	std::string run; // code points the box cannot show, as U+X+Y
	auto flush = [&]() {
		if (!run.empty()) {
			out += ":U+" + run + ":";
			run.clear();
		}
	};
	while (p < end)
	{
		// a named emoji (longest names first would matter only for
		// sequences; ours do not overlap)
		int found = -1;
		size_t len = 0;
		for (int i = 0; i < COUNT; i++) {
			size_t n = strlen(g_names[i].emoji);
			if (n > len && (size_t) (end - p) >= n && !memcmp(p, g_names[i].emoji, n)) {
				found = i;
				len = n;
			}
		}
		if (found >= 0) {
			flush();
			out += std::string(":") + g_names[found].name + ":";
			p += len;
			continue;
		}
		const char* start = p;
		unsigned cp = DecodeUtf8(p, end);
		if (cp <= 0xff) {
			flush();
			out.append(start, p - start);
			continue;
		}
		char hex[16];
		snprintf(hex, sizeof hex, "%s%X", run.empty() ? "" : "+", cp);
		run += hex;
	}
	flush();
	return out;
}

std::string Shortcodes::FromEditor(const std::string& utf8, Snowflake guild)
{
	Guild* pGuild = guild ? GetDiscordInstance()->GetGuild(guild) : nullptr;
	return FromEditor(utf8, [pGuild](const std::string& name) -> std::string {
		if (!pGuild)
			return "";
		for (auto& e : pGuild->m_emoji)
			if (e.second.m_name == name && e.second.m_bAvailable)
				return std::string(e.second.m_bAnimated ? "<a:" : "<:") + name + ":" + std::to_string(e.second.m_id) + ">";
		return "";
	});
}

std::string Shortcodes::FromEditor(const std::string& utf8, std::function<std::string(const std::string&)> serverEmoji)
{
	std::string out;
	size_t i = 0;
	while (i < utf8.size())
	{
		// Discord's own tags (<:name:id>, <a:name:id>, <@user>, <#channel>,
		// <t:...>) stay as they are: a message being edited has them, and
		// the :name: inside one is not a shortcode
		if (utf8[i] == '<') {
			size_t close = utf8.find('>', i);
			size_t space = utf8.find_first_of(" \n\t", i);
			bool tag = close != std::string::npos && (space == std::string::npos || close < space) &&
				i + 1 < utf8.size() && strchr(":a@#t", utf8[i + 1]);
			if (tag) {
				out.append(utf8, i, close - i + 1);
				i = close + 1;
				continue;
			}
		}
		if (utf8[i] != ':') {
			out += utf8[i++];
			continue;
		}
		size_t j = i + 1;
		while (j < utf8.size() && TokenChar(utf8[j]))
			j++;
		if (j >= utf8.size() || utf8[j] != ':' || j == i + 1) {
			out += utf8[i++];
			continue;
		}
		std::string name = utf8.substr(i + 1, j - i - 1);
		std::string replacement;
		bool known = false;

		if (name.size() > 2 && name[0] == 'U' && name[1] == '+') {
			// :U+1F600+200D+...: as written by ToEditor
			std::string chars;
			size_t k = 2;
			bool ok = true;
			while (k < name.size()) {
				size_t e = name.find('+', k);
				std::string h = name.substr(k, e == std::string::npos ? std::string::npos : e - k);
				char* stop = nullptr;
				unsigned long cp = strtoul(h.c_str(), &stop, 16);
				if (h.empty() || *stop || cp > 0x10ffff) {
					ok = false;
					break;
				}
				AppendUtf8(chars, (unsigned) cp);
				if (e == std::string::npos)
					break;
				k = e + 1;
			}
			if (ok) {
				replacement = chars;
				known = true;
			}
		}
		for (int k = 0; !known && k < COUNT; k++) {
			if (name == g_names[k].name) {
				replacement = g_names[k].emoji;
				known = true;
			}
		}
		if (!known) {
			replacement = serverEmoji(name);
			known = !replacement.empty();
		}
		if (known) {
			out += replacement;
			i = j + 1;
		}
		else
			out += utf8[i++]; // not ours: as typed
	}
	return out;
}
