#include "MessageList.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "DiscordInstance.hpp"
#include "state/MessageCache.hpp"

std::string MessageList::FormatSize(int bytes)
{
	char buf[64];
	if (bytes >= 1024 * 1024)
		snprintf(buf, sizeof buf, "%.1f MB", bytes / (1024.0 * 1024.0));
	else if (bytes >= 1024)
		snprintf(buf, sizeof buf, "%.0f KB", bytes / 1024.0);
	else
		snprintf(buf, sizeof buf, "%d bytes", bytes);
	return buf;
}

std::string MessageList::PreviewURL(const std::string& base, int w, int h, int origW, int origH)
{
	if (base.empty() || (w == origW && h == origH))
		return base;
	return base + (base.find('?') == std::string::npos ? "?" : "&") +
		"width=" + std::to_string(w) + "&height=" + std::to_string(h);
}

void MessageList::FitBox(int& w, int& h, int maxW, int maxH)
{
	if (w <= 0 || h <= 0) {
		w = maxW;
		h = maxH / 2;
		return;
	}
	if (w > maxW) { h = h * maxW / w; w = maxW; }
	if (h > maxH) { w = w * maxH / h; h = maxH; }
	if (w < 1) w = 1;
	if (h < 1) h = 1;
}

static std::string DayOf(time_t t)
{
	struct tm tmv = *localtime(&t);
	char buf[64];
	strftime(buf, sizeof buf, "%A, %e %B %Y", &tmv);
	return buf;
}

static int DayNumber(time_t t)
{
	struct tm tmv = *localtime(&t);
	return tmv.tm_year * 400 + tmv.tm_yday;
}

// The one-line text of a system message, or "" for ordinary messages.
static std::string SystemText(const Message& m, const std::string& channelName)
{
	using namespace MessageType;
	switch (m.m_type)
	{
		case USER_JOIN:              return m.m_author + " joined the server.";
		case CHANNEL_PINNED_MESSAGE: return m.m_author + " pinned a message to this channel.";
		case RECIPIENT_ADD:          return m.m_author + " added someone to the group.";
		case RECIPIENT_REMOVE:       return m.m_author + " removed someone from the group.";
		case CALL:                   return m.m_author + " started a call.";
		case CHANNEL_NAME_CHANGE:    return m.m_author + " changed the channel name: " + m.m_message;
		case CHANNEL_ICON_CHANGE:    return m.m_author + " changed the channel icon.";
		case GUILD_BOOST:
		case GUILD_BOOST_TIER_1:
		case GUILD_BOOST_TIER_2:
		case GUILD_BOOST_TIER_3:     return m.m_author + " boosted the server!";
		case CHANNEL_FOLLOW_ADD:     return m.m_author + " added " + m.m_message + " to this channel.";
		case THREAD_CREATED:         return m.m_author + " started a thread: " + m.m_message;
		case STAGE_START:            return m.m_author + " started the stage.";
		case STAGE_END:              return m.m_author + " ended the stage.";
		case CANT_VIEW_MSG_HISTORY:  return "You do not have permission to read the message history of #" + channelName + ".";
		case CHANNEL_HEADER:         return "This is the start of #" + channelName + ".";
		case GAP_UP:
		case GAP_DOWN:
		case GAP_AROUND:             return "Loading messages\xe2\x80\xa6";
		default:                     return "";
	}
}

void MessageList::SetChannel(Snowflake guild, Snowflake channel)
{
	m_guild = guild;
	m_channel = channel;
	m_items.clear();
	m_requestedGaps.clear();
}

void MessageList::ForgetLayout()
{
	for (auto& it : m_items) {
		it.text.Clear();
		it.laidOutWidth = -1;
	}
}

bool MessageList::Rebuild()
{
	std::list<MessagePtr> msgs;
	if (m_channel)
		GetMessageCache()->GetLoadedMessages(m_channel, m_guild, msgs);

	DiscordInstance* pInst = GetDiscordInstance();
	Channel* pChan = pInst ? pInst->GetChannelGlobally(m_channel) : nullptr;
	std::string chanName = pChan ? pChan->m_name : "";

	// The cache lists a channel's messages in snowflake order, and the items
	// follow it: walking both, the items of messages still there stay where
	// they are with their parsed text and layout (an edited message is a new
	// object in the cache, and gets a new item).
	bool appendOnly = true;
	auto cur = m_items.begin();
	const Message* prev = nullptr;
	bool prevSystem = false;
	int prevDay = -1;
	for (auto& mp : msgs)
	{
		while (cur != m_items.end() && cur->msg->m_snowflake < mp->m_snowflake) {
			cur = m_items.erase(cur);
			appendOnly = false;
		}
		bool isNew = true;
		if (cur != m_items.end() && cur->msg->m_snowflake == mp->m_snowflake) {
			if (cur->msg == mp)
				isNew = false;
			else
				cur = m_items.erase(cur); // edited
		}
		if (isNew && cur != m_items.end())
			appendOnly = false; // not at the end
		Item& item = isNew ? *m_items.emplace(cur) : *cur++;

		bool isGap = mp->IsLoadGap();
		if (isNew) {
			item.msg = mp;
			item.extra = std::make_shared<ItemExtra>();
			item.systemText = SystemText(*mp, chanName);
			item.systemLine = !item.systemText.empty();
			if (!isGap)
				item.day = DayNumber(mp->m_dateTime);
		}

		ItemExtra& ex = *item.extra;
		int day = isGap ? prevDay : item.day;
		bool hadSep = !ex.dateSep.empty(), wasGrouped = item.grouped;
		if (!isGap && day != prevDay && mp->m_dateTime) {
			if (ex.dateSep.empty())
				ex.dateSep = DayOf(mp->m_dateTime);
		}
		else
			ex.dateSep.clear();

		item.grouped = prev && !item.systemLine && !prev->IsLoadGap() &&
			ex.dateSep.empty() &&
			!prevSystem &&
			prev->m_author_snowflake == mp->m_author_snowflake &&
			prev->m_author == mp->m_author &&
			!mp->IsReply() &&
			mp->m_dateTime - prev->m_dateTime < GROUP_SECONDS;
		if (!isNew && (item.grouped != wasGrouped || ex.dateSep.empty() == hadSep))
			appendOnly = false; // an old message looks different

		if (!isGap)
			prevDay = day;
		prev = mp.get();
		prevSystem = item.systemLine;
	}
	if (cur != m_items.end()) {
		m_items.erase(cur, m_items.end());
		appendOnly = false;
	}
	return appendOnly;
}

std::string MessageList::LayoutSignature() const
{
	std::string s;
	for (auto& it : m_items)
		s += std::to_string(it.msg->m_snowflake) + ":" + std::to_string(it.y) + "+" + std::to_string(it.height) +
			(it.grouped ? "g" : "") + (it.extra->dateSep.empty() ? "" : "d") + ";";
	return s;
}

void MessageList::Layout(DrawingContext* ctx, int px, int width)
{
	int y = 8;
	for (auto& it : m_items) {
		bool dateSep = !it.extra->dateSep.empty();
		if (it.laidOutWidth != width || it.laidOutPx != px ||
			it.laidOutGrouped != it.grouped || it.laidOutDateSep != dateSep)
		{
			LayoutItem(it, ctx, px, width);
			it.laidOutWidth = width;
			it.laidOutPx = px;
			it.laidOutGrouped = it.grouped;
			it.laidOutDateSep = dateSep;
		}
		it.y = y;
		y += it.height;
	}
	m_contentHeight = y + 12;
}

void MessageList::LayoutItem(Item& item, DrawingContext* ctx, int px, int width)
{
	ItemExtra& ex = *item.extra;
	const Message& m = *item.msg;
	int right = width - m_geo.margin;
	int y = 0;

	if (!ex.dateSep.empty())
		y += m_geo.dateSep;

	if (item.systemLine) {
		y += m.IsLoadGap() ? 8 : 6;
		ex.headerTop = y;
		y += m_metrics.LineHeight(FS_ITALIC, px) + (m.IsLoadGap() ? 8 : 6);
		item.height = y;
		return;
	}

	y += item.grouped ? m_geo.lineGap : m_geo.groupGap;

	// reply: one small line above the header
	if (m.IsReply() && !item.grouped) {
		ex.replyTop = y;
		if (!item.reply) {
			item.reply.reset(new FormattedText);
			item.reply->SetDefaultStyle(WORD_ITALIC | WORD_SMALLER);
			item.reply->SetAllowBiggerText(false);
			item.reply->SetMessage(m.m_pReferencedMessage->m_message);
			std::vector<InteractableItem> ignored;
			GetDiscordInstance()->ResolveLinks(item.reply.get(), ignored, m_guild);
		}
		y += m_metrics.LineHeight(FS_ITALIC, px - 2) + 4;
	}

	if (!item.grouped) {
		ex.headerTop = y;
		y += m_metrics.LineHeight(FS_BOLD, px) + 2;
	}

	// the text
	item.textTop = y;
	if (item.text.Empty() && !m.m_message.empty()) {
		item.text.SetMessage(m.m_message);
		item.interactables.clear();
		GetDiscordInstance()->ResolveLinks(&item.text, item.interactables, m_guild);
	}
	if (!item.text.Empty()) {
		item.text.Layout(ctx, Rect(m_geo.textX, y, right, y + 100000));
		Rect ext = item.text.GetExtent();
		y = std::max(y, ext.bottom);
	}

	// attachments: images as previews, other files a line each
	ex.attachTop = y;
	ex.links.clear();
	ex.attachPics.clear();
	int lh = m_metrics.LineHeight(FS_REGULAR, px) + 4;
	for (auto& att : m.m_attachments) {
		if (att.IsImage() && att.m_width > 0 && att.m_height > 0) {
			int w = att.m_previewWidth > 0 ? att.m_previewWidth : att.m_width;
			int h = att.m_previewHeight > 0 ? att.m_previewHeight : att.m_height;
			FitBox(w, h, std::min(m_geo.pictureMax, right - m_geo.textX), m_geo.pictureMax);
			y += 4;
			Rect r(m_geo.textX, y, m_geo.textX + w, y + h);
			PictureInfo view;
			view.url = att.m_proxyUrl;
			view.width = att.m_width;
			view.height = att.m_height;
			view.title = att.m_fileName;
			ex.attachPics.push_back({ r, PreviewURL(att.m_proxyUrl, w, h, att.m_width, att.m_height), view });
			ex.links.push_back(Link{ r, att.m_actualUrl });
			y += h + 4;
			continue;
		}
		ex.attachPics.push_back({ Rect(m_geo.textX, y, right, y + lh), "", PictureInfo() });
		ex.links.push_back(Link{ Rect(m_geo.textX, y, right, y + lh), att.m_actualUrl });
		y += lh;
	}

	// embeds: a bar, the title, the text
	ex.embedTexts.clear();
	ex.embedTops.clear();
	ex.embedHeights.clear();
	ex.embedThumbs.clear();
	ex.embedImages.clear();
	for (auto& em : m.m_embeds)
	{
		y += 4;
		int top = y;
		int boxRight = std::min(right, m_geo.textX + m_geo.embedWidth);
		// a thumbnail sits at the top right, beside the text
		bool thumb = em.m_bHasThumbnail && !em.m_thumbnailProxiedUrl.empty() && !em.m_bHasImage;
		int textRight = thumb ? boxRight - m_geo.thumb - 12 : boxRight;
		y += 6;
		if (!em.m_providerName.empty())
			y += m_metrics.LineHeight(FS_REGULAR, px - 3) + 2;
		if (!em.m_authorName.empty())
			y += m_metrics.LineHeight(FS_BOLD, px - 2) + 2;
		if (!em.m_title.empty())
			y += m_metrics.LineHeight(FS_BOLD, px) + 2;

		std::unique_ptr<FormattedText> ft;
		std::string body = em.m_description;
		for (auto& f : em.m_fields)
			body += (body.empty() ? "" : "\n") + std::string("**") + f.m_title + "**\n" + f.m_value;
		if (!body.empty()) {
			ft.reset(new FormattedText);
			ft->SetMessage(body);
			ft->Layout(ctx, Rect(m_geo.textX + 12, y, textRight, y + 100000));
			y = std::max(y, ft->GetExtent().bottom);
		}

		ItemExtra::Pic img, th;
		if (thumb) {
			int w = em.m_thumbnailWidth, h = em.m_thumbnailHeight;
			FitBox(w, h, m_geo.thumb, m_geo.thumb);
			th.rect = Rect(boxRight - 8 - w, top + 8, boxRight - 8, top + 8 + h);
			th.url = PreviewURL(em.m_thumbnailProxiedUrl, w, h, em.m_thumbnailWidth, em.m_thumbnailHeight);
			if (em.m_type != RichEmbed::VIDEO) { // a video plays in the browser
				th.view.url = em.m_thumbnailProxiedUrl;
				th.view.width = em.m_thumbnailWidth;
				th.view.height = em.m_thumbnailHeight;
				th.view.title = !em.m_title.empty() ? em.m_title : em.m_providerName;
			}
			y = std::max(y, th.rect.bottom);
		}
		std::string imgUrl = em.m_bHasImage ? em.m_imageProxiedUrl :
			(em.m_bHasThumbnail && !thumb ? em.m_thumbnailProxiedUrl : "");
		if (!imgUrl.empty()) {
			int ow = em.m_bHasImage ? em.m_imageWidth : em.m_thumbnailWidth;
			int oh = em.m_bHasImage ? em.m_imageHeight : em.m_thumbnailHeight;
			int w = ow, h = oh;
			FitBox(w, h, std::min(m_geo.pictureMax, boxRight - m_geo.textX - 24), m_geo.pictureMax);
			y += 6;
			img.rect = Rect(m_geo.textX + 12, y, m_geo.textX + 12 + w, y + h);
			img.url = PreviewURL(imgUrl, w, h, ow, oh);
			if (em.m_type != RichEmbed::VIDEO) {
				img.view.url = imgUrl;
				img.view.width = ow;
				img.view.height = oh;
				img.view.title = !em.m_title.empty() ? em.m_title : em.m_providerName;
			}
			y += h;
		}
		ex.embedThumbs.push_back(th);
		ex.embedImages.push_back(img);

		if (!em.m_footerText.empty())
			y += m_metrics.LineHeight(FS_REGULAR, px - 3) + 4;
		y += 6;
		ex.embedTops.push_back(top);
		ex.embedHeights.push_back(y - top);
		ex.embedTexts.push_back(std::move(ft));
		if (!em.m_url.empty())
			ex.links.push_back(Link{ Rect(m_geo.textX, top, std::min(right, m_geo.textX + m_geo.embedWidth), top + 40), em.m_url });
	}

	// reactions: a pill each (the emoji and how many), wrapping
	ex.reactionRects.clear();
	if (!m.m_reactions.empty()) {
		int rpx = px - 1;
		int ph = ReactionHeight(rpx);
		int x = m_geo.textX;
		y += 6;
		for (auto& r : m.m_reactions) {
			int w = ReactionWidth(r, rpx);
			if (x > m_geo.textX && x + w > right) {
				x = m_geo.textX;
				y += ph + 4;
			}
			ex.reactionRects.push_back(Rect(x, y, x + w, y + ph));
			x += w + 4;
		}
		y += ph;
	}

	y += 2;
	item.height = y;
}

// A reaction pill: padding, the emoji, a gap, the count, padding.
int MessageList::ReactionHeight(int px)
{
	return m_metrics.LineHeight(FS_REGULAR, px) + 8;
}

int MessageList::ReactionWidth(const Reaction& r, int px)
{
	int emoji = r.m_emojiId ? ReactionHeight(px) - 8 : m_metrics.Measure(r.m_emojiName, FS_REGULAR, px);
	return m_geo.pillPad + emoji + 5 + m_metrics.Measure(std::to_string(r.m_count), FS_BOLD, px) + m_geo.pillPad;
}

MessageList::Item* MessageList::ItemAt(int contentY)
{
	for (auto& it : m_items)
		if (contentY >= it.y && contentY < it.y + it.height)
			return &it;
	return nullptr;
}

static bool Inside(const Rect& r, int x, int y)
{
	return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

MessageList::Hit MessageList::HitTest(int x, int contentY)
{
	Hit hit;
	Item* it = ItemAt(contentY);
	if (!it)
		return hit;
	int iy = contentY - it->y;

	auto& words = it->text.GetWords();
	for (auto& ii : it->interactables) {
		if (ii.m_type != InteractableItem::LINK || ii.m_wordIndex >= words.size())
			continue;
		if (Inside(words[ii.m_wordIndex].m_rect, x, iy)) {
			hit.kind = Hit::LINK;
			hit.url = ii.m_destination;
			return hit;
		}
	}
	// a reaction: the user's own taken away, another added
	const Message& m = *it->msg;
	for (size_t i = 0; i < m.m_reactions.size() && i < it->extra->reactionRects.size(); i++) {
		if (Inside(it->extra->reactionRects[i], x, iy)) {
			hit.kind = Hit::REACTION;
			hit.message = &m;
			hit.reaction = i;
			return hit;
		}
	}

	// a picture opens in the image viewer
	const std::vector<ItemExtra::Pic>* pics[] = { &it->extra->attachPics, &it->extra->embedImages, &it->extra->embedThumbs };
	for (auto* list : pics) {
		for (auto& pic : *list) {
			if (!pic.view.url.empty() && Inside(pic.rect, x, iy)) {
				hit.kind = Hit::PICTURE;
				hit.picture = pic.view;
				return hit;
			}
		}
	}
	for (auto& l : it->extra->links) {
		if (Inside(l.rect, x, iy) && !l.url.empty()) {
			hit.kind = Hit::LINK;
			hit.url = l.url;
			return hit;
		}
	}
	return hit;
}

void MessageList::RequestGaps(int top, int bottom)
{
	for (auto& it : m_items)
	{
		if (it.y + it.height < top || it.y > bottom)
			continue;
		const Message& m = *it.msg;
		if (!m.IsLoadGap() || m_requestedGaps.count(m.m_snowflake))
			continue;

		m_requestedGaps.insert(m.m_snowflake);
		ScrollDir::eScrollDir sd = m.m_type == MessageType::GAP_UP ? ScrollDir::BEFORE :
			m.m_type == MessageType::GAP_DOWN ? ScrollDir::AFTER : ScrollDir::AROUND;
		GetDiscordInstance()->RequestMessages(m_channel, sd, m.m_anchor, m.m_snowflake);
	}
}

bool MessageList::NewestShown(bool atBottom) const
{
	if (!m_channel || !atBottom || m_items.empty())
		return false;
	return !m_items.back().msg->IsLoadGap(); // a gap: the newest are still being fetched
}

bool MessageList::AcknowledgeIfUnread()
{
	DiscordInstance* pInst = GetDiscordInstance();
	Channel* pChan = pInst ? pInst->GetChannelGlobally(m_channel) : nullptr;
	if (!pChan || (!pChan->HasUnreadMessages() && pChan->m_mentionCount == 0))
		return false;
	if (m_ackSent == pChan->m_lastSentMsg)
		return false; // asked already; Discord's answer is on its way
	m_ackSent = pChan->m_lastSentMsg;
	pInst->RequestAcknowledgeChannel(m_channel);
	// at once, not when Discord's read state comes back
	pChan->m_lastViewedMsg = pChan->m_lastSentMsg;
	pChan->m_mentionCount = 0;
	return true;
}

bool MessageList::IsActionable(const Message& m)
{
	return !m.IsLoadGap() && m.m_type != MessageType::CHANNEL_HEADER &&
		m.m_type != MessageType::SENDING_MESSAGE && m.m_type != MessageType::UNSENT_MESSAGE &&
		m.m_snowflake > 1;
}

bool MessageList::CanEdit(const Message& m) const
{
	DiscordInstance* pInst = GetDiscordInstance();
	return pInst && m.m_author_snowflake == pInst->GetUserID() && !m.IsWebHook();
}

bool MessageList::CanDelete(const Message& m) const
{
	DiscordInstance* pInst = GetDiscordInstance();
	Channel* pChan = pInst ? pInst->GetChannelGlobally(m_channel) : nullptr;
	bool manage = pChan && !pChan->IsDM() && pChan->HasPermission(PERM_MANAGE_MESSAGES);
	return CanEdit(m) || manage;
}

std::string MessageList::DeleteQuestion(const Message& msg)
{
	std::string text = msg.m_message;
	for (auto& ch : text)
		if (ch == '\n' || ch == '\t')
			ch = ' ';
	if (text.empty())
		text = msg.m_attachments.empty() ? std::string("(no text)") : "(" + msg.m_attachments[0].m_fileName + ")";
	if (text.size() > 120)
		text = text.substr(0, 117) + "...";
	bool own = msg.m_author_snowflake == GetDiscordInstance()->GetUserID();
	return std::string(own ? "Delete this message?" : "Delete this message by " + msg.m_author + "?") +
		"\n\n" + text + "\n\nThis cannot be undone.";
}
