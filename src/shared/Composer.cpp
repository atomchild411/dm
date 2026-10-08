#include "Composer.hpp"

#include "DiscordInstance.hpp"
#include "Shortcodes.hpp"

Snowflake Composer::ChannelId() const
{
	DiscordInstance* pInst = GetDiscordInstance();
	if (m_channel)
		return m_channel;
	return pInst ? pInst->GetCurrentChannelID() : 0;
}

void Composer::BeginReply(Snowflake message)
{
	m_editing = 0;
	m_replyTo = message;
}

void Composer::BeginEdit(Snowflake message)
{
	m_replyTo = 0;
	m_editing = message;
}

bool Composer::Cancel()
{
	bool dropped = m_editing != 0;
	m_editing = 0;
	m_replyTo = 0;
	return dropped;
}

Composer::Result Composer::Send(const std::string& raw)
{
	DiscordInstance* pInst = GetDiscordInstance();
	Snowflake channel = ChannelId();

	// trim
	size_t a = raw.find_first_not_of(" \t\n"), b = raw.find_last_not_of(" \t\n");
	if (a == std::string::npos || !pInst || !channel)
		return NOTHING;
	std::string text = raw.substr(a, b - a + 1);

	// shortcodes (:joy:, a server's :name:, :U+...:) become the emoji; a
	// conversation has no server
	Snowflake guild = m_channel ? 0 : pInst->GetCurrentGuildID();
	std::string utf8 = Shortcodes::FromEditor(text, guild);
	if (m_editing) {
		pInst->RequestEditMessage(channel, m_editing, utf8);
		m_editing = 0;
		m_replyTo = 0;
		m_lastTypingSent = 0;
		return EDITED;
	}
	Snowflake tempSf = 0;
	bool sent = m_channel ?
		pInst->SendMessageToChannel(0, channel, utf8, tempSf, m_replyTo) :
		pInst->SendMessageToCurrentChannel(utf8, tempSf, m_replyTo);
	if (!sent)
		return FAILED;
	m_replyTo = 0;
	m_lastTypingSent = 0;
	return SENT;
}

void Composer::TextChanged(bool empty)
{
	DiscordInstance* pInst = GetDiscordInstance();
	if (!pInst || !ChannelId())
		return;
	time_t now = time(NULL);
	if (!empty && now - m_lastTypingSent >= 8) {
		m_lastTypingSent = now;
		if (m_channel)
			pInst->Typing(m_channel);
		else
			pInst->Typing();
	}
}
