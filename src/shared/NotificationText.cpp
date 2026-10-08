#include "NotificationText.hpp"

#include "DiscordInstance.hpp"
#include "state/NotificationManager.hpp"

std::string NotificationText::Where(const Notification& n)
{
	DiscordInstance* pInst = GetDiscordInstance();
	Channel* pChan = pInst ? pInst->GetChannelGlobally(n.m_sourceChannel) : nullptr;
	Guild* pGuild = pInst && n.m_sourceGuild ? pInst->GetGuild(n.m_sourceGuild) : nullptr;
	if (!pChan || pChan->IsDM())
		return "direct message";
	std::string s = "#" + pChan->m_name;
	if (pGuild)
		s += ", " + pGuild->m_name;
	return s;
}

std::string NotificationText::OneLine(const Notification& n)
{
	std::string text = n.m_contents.empty() ? std::string("(an attachment)") : n.m_contents;
	for (auto& ch : text)
		if (ch == '\n' || ch == '\t')
			ch = ' ';
	return text;
}
