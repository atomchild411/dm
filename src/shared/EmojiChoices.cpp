#include "EmojiChoices.hpp"

#include <algorithm>

#include "DiscordInstance.hpp"
#include "Shortcodes.hpp"

std::vector<EmojiChoices::Section> EmojiChoices::For(Snowflake guild)
{
	std::vector<Section> out;
	Section common;
	for (int i = 0; i < Shortcodes::PICKER_COUNT; i++) {
		Reaction r;
		r.m_emojiName = Shortcodes::PickerEmoji(i);
		common.emoji.push_back(r);
	}
	out.push_back(common);

	Guild* pGuild = guild && GetDiscordInstance() ? GetDiscordInstance()->GetGuild(guild) : nullptr;
	if (pGuild) {
		Section own;
		own.title = pGuild->m_name;
		for (auto& e : pGuild->m_emoji) {
			if (!e.second.m_bAvailable || !e.second.m_id)
				continue;
			Reaction r;
			r.m_emojiId = e.second.m_id;
			r.m_emojiName = e.second.m_name;
			r.m_bAnimated = e.second.m_bAnimated;
			own.emoji.push_back(r);
		}
		std::sort(own.emoji.begin(), own.emoji.end(), [](const Reaction& a, const Reaction& b) { return a.m_emojiName < b.m_emojiName; });
		if (!own.emoji.empty())
			out.push_back(own);
	}
	return out;
}
