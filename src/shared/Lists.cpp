#include "Lists.hpp"

#include <algorithm>

#include "DiscordInstance.hpp"
#include "Frontend.hpp"
#include "state/ProfileCache.hpp"
#include "models/ActiveStatus.hpp"

std::vector<ListRow> Lists::GuildRows()
{
	DiscordInstance* pInst = GetDiscordInstance();
	std::vector<Snowflake> ids;
	pInst->GetGuildIDsOrdered(ids, true);

	std::vector<ListRow> rows;
	bool inFolder = false;
	for (Snowflake sf : ids)
	{
		if (sf == 1) {
			// the gap after Direct Messages
			ListRow r;
			r.type = ListRow::SPACE;
			rows.push_back(r);
			continue;
		}
		if (sf & BIT_FOLDER) {
			inFolder = sf != BIT_FOLDER;
			if (inFolder) {
				ListRow r;
				r.type = ListRow::HEADER;
				r.text = pInst->GetGuildFolderName(sf & ~BIT_FOLDER);
				rows.push_back(r);
			}
			continue;
		}
		ListRow r;
		r.id = sf;
		r.indent = inFolder ? 8 : 0;
		if (sf == 0) {
			r.text = GetFrontend()->GetDirectMessagesText();
			r.glyph = "@";
			// unread direct messages (each counts as a mention)
			if (Guild* dms = pInst->GetGuild(0)) {
				for (auto& ch : dms->m_channels)
					r.mentions += ch.m_mentionCount;
				r.unread = r.mentions > 0;
			}
		}
		else {
			Guild* pGuild = pInst->GetGuild(sf);
			if (!pGuild)
				continue;
			r.text = pGuild->m_name;
			r.initials = true;
			if (!pGuild->m_avatarlnk.empty()) {
				r.hasImage = true;
				r.imageKind = ImageCache::ICON;
				r.imagePlace = pGuild->m_avatarlnk;
				r.imageSf = sf;
			}
			// unread: any channel the user can see with newer messages
			int mentions = 0;
			bool unread = false;
			for (auto& ch : pGuild->m_channels) {
				mentions += ch.m_mentionCount;
				if (ch.HasUnreadMessages() && ch.HasPermissionConst(PERM_VIEW_CHANNEL) &&
					!pInst->IsChannelMuted(sf, ch.m_snowflake))
					unread = true;
			}
			r.unread = unread;
			r.mentions = mentions;
		}
		rows.push_back(r);
	}
	return rows;
}

bool Lists::IsTextChannel(const Channel& ch)
{
	switch (ch.m_channelType) {
		case Channel::TEXT: case Channel::DM: case Channel::GROUPDM: case Channel::NEWS:
		case Channel::NEWSTHREAD: case Channel::PUBTHREAD: case Channel::PRIVTHREAD:
			return true;
		default:
			return false;
	}
}

std::vector<ListRow> Lists::ChannelRows()
{
	DiscordInstance* pInst = GetDiscordInstance();
	Guild* pGuild = pInst->GetCurrentGuild();
	std::vector<ListRow> rows;

	if (pGuild && !pGuild->m_bChannelsLoaded) {
		pGuild->RequestFetchChannels();
		ListRow r;
		r.type = ListRow::HEADER;
		r.text = GetFrontend()->GetPleaseWaitText();
		rows.push_back(r);
	}
	else if (pGuild)
	{
		std::vector<const Channel*> chans;
		for (auto& ch : pGuild->m_channels)
			chans.push_back(&ch);
		std::stable_sort(chans.begin(), chans.end(), [](const Channel* a, const Channel* b) { return *a < *b; });

		auto addChannel = [&](const Channel* ch) {
			if (!ch->HasPermissionConst(PERM_VIEW_CHANNEL))
				return;
			ListRow r;
			r.id = ch->m_snowflake;
			r.text = ch->m_name;
			r.mentions = ch->m_mentionCount;
			r.unread = ch->HasUnreadMessages() && !pInst->IsChannelMuted(pGuild->m_snowflake, ch->m_snowflake);
			r.selectable = IsTextChannel(*ch);
			r.dim = !r.selectable;
			if (ch->m_channelType == Channel::DM) {
				Snowflake who = ch->m_recipients.empty() ? 0 : ch->m_recipients[0];
				r.hasImage = true;
				r.imageKind = ch->m_avatarLnk.empty() ? ImageCache::DEFAULT_AVATAR : ImageCache::AVATAR;
				r.imagePlace = ch->m_avatarLnk;
				r.imageSf = who;
				r.colorSeed = who;
				Profile* pf = who ? GetProfileCache()->LookupProfile(who, "", "", "", false) : nullptr;
				r.status = pf ? (int) pf->m_activeStatus : -1;
			}
			else if (ch->m_channelType == Channel::GROUPDM) {
				r.initials = true;
				if (!ch->m_avatarLnk.empty()) {
					r.hasImage = true;
					r.imageKind = ImageCache::CHANNEL_ICON;
					r.imagePlace = ch->m_avatarLnk;
					r.imageSf = ch->m_snowflake;
				}
			}
			else if (ch->m_channelType == Channel::VOICE || ch->m_channelType == Channel::STAGEVOICE)
				r.glyph = "\xe2\x99\xaa"; // a note: voice
			else if (ch->m_channelType == Channel::FORUM || ch->m_channelType == Channel::MEDIA)
				r.glyph = "\xe2\x96\xa4";
			else
				r.glyph = "#";
			rows.push_back(r);
		};

		// channels outside categories first, then each category's
		for (const Channel* ch : chans)
			if (!ch->IsCategory() && ch->m_parentCateg == 0)
				addChannel(ch);
		for (const Channel* cat : chans)
		{
			if (!cat->IsCategory())
				continue;
			ListRow h;
			h.type = ListRow::HEADER;
			h.text = cat->m_name;
			for (auto& c : h.text)
				if (c >= 'a' && c <= 'z') c -= 32;
			size_t before = rows.size();
			rows.push_back(h);
			for (const Channel* ch : chans)
				if (!ch->IsCategory() && ch->m_parentCateg == cat->m_snowflake)
					addChannel(ch);
			if (rows.size() == before + 1)
				rows.pop_back(); // an empty (or wholly hidden) category
		}
	}
	return rows;
}

uint32_t Lists::RoleColor(Snowflake user, Snowflake guild)
{
	Guild* pGuild = GetDiscordInstance()->GetGuild(guild);
	if (!pGuild || !guild)
		return 0;
	Profile* pf = GetProfileCache()->LookupProfile(user, "", "", "", false);
	if (!pf)
		return 0;
	auto gm = pf->m_guildMembers.find(guild);
	if (gm == pf->m_guildMembers.end())
		return 0;
	int bestPos = -1;
	uint32_t best = 0;
	for (Snowflake role : gm->second.m_roles) {
		auto it = pGuild->m_roles.find(role);
		if (it == pGuild->m_roles.end())
			continue;
		if (it->second.m_colorOriginal && it->second.m_position > bestPos) {
			bestPos = it->second.m_position;
			best = (uint32_t) it->second.m_colorOriginal;
		}
	}
	return best;
}

std::vector<ListRow> Lists::MemberRows()
{
	DiscordInstance* pInst = GetDiscordInstance();
	Guild* pGuild = pInst->GetCurrentGuild();
	std::vector<ListRow> rows;
	if (pGuild && pGuild->m_snowflake != 0)
	{
		for (Snowflake sf : pGuild->m_members)
		{
			GuildMember* gm = pGuild->GetGuildMember(sf);
			if (!gm)
				continue;
			if (gm->m_bIsGroup) {
				if (gm->m_groupCount) {
					ListRow h;
					h.type = ListRow::HEADER;
					h.text = pGuild->GetGroupName(gm->m_groupId) + " \xe2\x80\x94 " + std::to_string(gm->m_groupCount);
					rows.push_back(h);
				}
				continue;
			}
			Profile* p = GetProfileCache()->LookupProfile(gm->m_user, "", "", "", false);
			ListRow r;
			r.id = gm->m_user;
			r.text = p ? p->GetName(pGuild->m_snowflake) : std::string("?");
			r.hasImage = true;
			r.colorSeed = gm->m_user;
			std::string av = !gm->m_avatar.empty() ? gm->m_avatar : (p ? p->m_avatarlnk : "");
			r.imageKind = av.empty() ? ImageCache::DEFAULT_AVATAR : ImageCache::AVATAR;
			r.imagePlace = av;
			r.imageSf = gm->m_user;
			r.status = p ? (int) p->m_activeStatus : -1;
			r.dim = p && p->m_activeStatus == STATUS_OFFLINE;
			r.textColor = RoleColor(gm->m_user, pGuild->m_snowflake);
			r.subtext = p ? p->m_status : std::string();
			rows.push_back(r);
		}
	}
	return rows;
}

Lists::Unread Lists::UnreadCounts()
{
	Unread u;
	DiscordInstance* pInst = GetDiscordInstance();
	if (!pInst)
		return u;
	std::vector<Snowflake> ids;
	pInst->GetGuildIDsOrdered(ids, true);
	for (Snowflake sf : ids) {
		if (sf == 1 || (sf & BIT_FOLDER))
			continue;
		Guild* pGuild = pInst->GetGuild(sf); // 0: the direct messages
		if (!pGuild)
			continue;
		for (auto& ch : pGuild->m_channels) {
			u.total += ch.m_mentionCount;
			if (sf == 0)
				u.directMessages += ch.m_mentionCount;
		}
	}
	return u;
}

std::vector<Lists::Conversation> Lists::Conversations(size_t max)
{
	std::vector<Conversation> out;
	DiscordInstance* pInst = GetDiscordInstance();
	Guild* dms = pInst ? pInst->GetGuild(0) : nullptr;
	if (!dms)
		return out;

	// unread first, then the most recently active
	std::vector<const Channel*> chans;
	for (auto& ch : dms->m_channels)
		if (ch.IsDM() || ch.m_channelType == Channel::GROUPDM)
			chans.push_back(&ch);
	std::sort(chans.begin(), chans.end(), [](const Channel* a, const Channel* b) {
		bool ua = a->m_mentionCount > 0, ub = b->m_mentionCount > 0;
		if (ua != ub)
			return ua;
		return a->m_lastSentMsg > b->m_lastSentMsg;
	});
	for (size_t i = 0; i < chans.size() && i < max; i++)
		out.push_back(Conversation{ chans[i]->m_snowflake, chans[i]->m_name, chans[i]->m_mentionCount });
	return out;
}
