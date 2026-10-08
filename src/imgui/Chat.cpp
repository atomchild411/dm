// The chat: the channel's header bar, its messages (shared/MessageList,
// drawn here the way Discord draws them) and the message box.

#include "Ui.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>

#include "App.hpp"
#include "DiscordInstance.hpp"
#include "Frontend.hpp"
#include "shared/ClientConfig.hpp"
#include "shared/ImageCache.hpp"
#include "shared/Lists.hpp"
#include "shared/Typing.hpp"
#include "shared/Utf8.hpp"
#include "text/TextInterface.hpp"

using namespace Ui;

namespace
{
	const uint32_t PLACEHOLDER = 0x6d6f78;

	// Replies' one-line previews, formatted (mentions, custom emoji), by the
	// replying message.
	std::map<Snowflake, std::unique_ptr<FormattedText>> g_snippets;

	void ApplyTheme()
	{
		const Palette& p = GetPalette();
		ctx.px = GetTextSize();
		ctx.fg = TEXT;
		ctx.bg = CHAT_BG;
		ctx.link = p.link;
		ctx.mention = p.mention;
		ctx.codeBg = 0x2b2d31;
		ctx.codeFrame = 0x1e1f22;
		ctx.quoteBar = 0x4e5058;
		ctx.muted = MUTED;
	}

	// The open channel's name and topic, for the header and the message box.
	void ChannelNames(std::string& name, std::string& topic, bool& dm)
	{
		dm = false;
		if (demo) {
			name = "general";
			topic = "Talk about Indigo2s, Octanes and the rest of the SGI family";
			return;
		}
		DiscordInstance* pInst = GetDiscordInstance();
		Channel* ch = pInst && list->GetChannel() ? pInst->GetChannelGlobally(list->GetChannel()) : nullptr;
		if (!ch) {
			name.clear();
			topic.clear();
			return;
		}
		name = ch->m_name;
		topic = ch->m_topic;
		dm = ch->m_channelType == Channel::DM || ch->m_channelType == Channel::GROUPDM;
	}

	void HeaderBar(float width)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(width, HEADER_H));
		std::string name, topic;
		bool dm;
		ChannelNames(name, topic, dm);
		float cy = pos.y + HEADER_H / 2.0f;
		float x = pos.x + 16;
		if (!name.empty()) {
			const char* glyph = dm ? "@" : "#";
			x += TextMid(dl, x, cy, glyph, FS_REGULAR, 22, FAINT) + 8;
			x += TextMid(dl, x, cy, Gfx::Elide(name, FS_BOLD, 15, (int) (width / 2)), FS_BOLD, 15, TEXT_BRIGHT) + 12;
			if (!topic.empty()) {
				std::string t = topic;
				std::replace(t.begin(), t.end(), '\n', ' ');
				dl->AddLine(ImVec2(x, cy - 10), ImVec2(x, cy + 10), Col(0x3f4147));
				x += 12;
				TextMid(dl, x, cy, Gfx::Elide(t, FS_REGULAR, 13, (int) (pos.x + width - 16 - x)), FS_REGULAR, 13, MUTED);
			}
		}
		dl->AddLine(ImVec2(pos.x, pos.y + HEADER_H - 1), ImVec2(pos.x + width, pos.y + HEADER_H - 1), Col(DIVIDER), 1.5f);
	}

	void DrawPicture(ImDrawList* dl, const Rect& r, const std::string& url, ImVec2 o, const std::string& label, const ImVec4& clip)
	{
		float x = o.x + r.left, y = o.y + r.top;
		int w = r.Width(), h = r.Height();
		if (y + h < clip.y || y > clip.w)
			return; // not on screen: not fetched either
		const Image* img = ImageCache::Get(ImageCache::URL, url, 0, w, h);
		if (img) {
			float ix = x + (w - img->w) / 2.0f, iy = y + (h - img->h) / 2.0f;
			dl->AddImageRounded(Gfx::Texture(*img), ImVec2(ix, iy), ImVec2(ix + img->w, iy + img->h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
			return;
		}
		Fill(dl, x, y, (float) w, (float) h, 0x2b2d31, 6.0f);
		bool failed = ImageCache::Failed(ImageCache::URL, url, 0, w, h);
		std::string text = failed ? (label.empty() ? std::string("Image not available") : label) : std::string("Loading\xe2\x80\xa6");
		int spx = ctx.px - 2;
		text = Gfx::Elide(text, FS_ITALIC, spx, w - 8);
		int tw = Gfx::Measure(text, FS_ITALIC, spx);
		if (h > Gfx::LineHeight(FS_ITALIC, spx))
			TextMid(dl, x + (w - tw) / 2.0f, y + h / 2.0f, text, FS_ITALIC, spx, MUTED);
	}

	// The preview of the message replied to: one line, its emoji drawn.
	void ReplySnippet(ImDrawList* dl, const Message& m, float x, float cy, float right)
	{
		std::string snippet = m.m_pReferencedMessage->m_message;
		std::replace(snippet.begin(), snippet.end(), '\n', ' ');
		if (snippet.empty()) {
			const char* t = m.m_pReferencedMessage->m_bHasAttachments ? "Click to see attachment" : "Original message was deleted";
			TextMid(dl, x, cy, t, FS_ITALIC, ctx.px - 2, MUTED);
			return;
		}
		auto& ft = g_snippets[m.m_snowflake];
		if (!ft) {
			ft.reset(new FormattedText);
			ft->SetDefaultStyle(WORD_SMALLER);
			ft->SetAllowBiggerText(false);
			ft->SetMessage(snippet);
			std::vector<InteractableItem> ignored;
			if (!demo)
				GetDiscordInstance()->ResolveLinks(ft.get(), ignored, list->GetGuild());
		}
		uint32_t fg = ctx.fg;
		ImVec2 origin = ctx.origin;
		ctx.fg = MUTED;
		ctx.origin = ImVec2(x, cy - MdLineHeight(&ctx, WORD_SMALLER) / 2.0f);
		ft->Layout(&ctx, Rect(0, 0, 100000, 1000));
		dl->PushClipRect(ImVec2(x, cy - 20), ImVec2(right, cy + 20), true);
		ft->Draw(&ctx, 0);
		dl->PopClipRect();
		ctx.fg = fg;
		ctx.origin = origin;
	}

	// One item, its content (0, 0) at o (the shared layout's coordinates).
	void PaintItem(ImDrawList* dl, MessageList::Item& item, ImVec2 o, float viewW, const ImVec4& clip, bool hovered)
	{
		const MessageList::Geometry& geo = list->Geo();
		MessageList::ItemExtra& ex = *item.extra;
		const Message& m = *item.msg;
		float top = o.y + item.y;
		float right = o.x + viewW - geo.margin;
		float textX = o.x + geo.textX;
		int px = ctx.px;

		if (!ex.dateSep.empty()) {
			float mid = top + geo.dateSep / 2.0f;
			int tw = Gfx::Measure(ex.dateSep, FS_BOLD, 12);
			float cx = o.x + (viewW - tw) / 2;
			dl->AddLine(ImVec2(o.x + geo.margin, mid), ImVec2(cx - 8, mid), Col(0x3f4147));
			dl->AddLine(ImVec2(cx + tw + 8, mid), ImVec2(right, mid), Col(0x3f4147));
			TextMid(dl, cx, mid, ex.dateSep, FS_BOLD, 12, MUTED);
		}
		float bodyTop = top + (ex.dateSep.empty() ? 0 : geo.dateSep);
		if (hovered && !m.IsLoadGap())
			Fill(dl, o.x, bodyTop, viewW, top + item.height - bodyTop, MSG_HOVER);

		if (item.systemLine) {
			float cy = top + ex.headerTop + Gfx::LineHeight(FS_ITALIC, px) / 2.0f;
			if (m.IsLoadGap()) {
				int tw = Gfx::Measure(item.systemText, FS_ITALIC, px);
				TextMid(dl, o.x + (viewW - tw) / 2, cy, item.systemText, FS_ITALIC, px, MUTED);
			}
			else {
				TextMid(dl, textX - 30, cy, "\xe2\x86\x92", FS_BOLD, px + 2, 0x23a55a);
				TextMid(dl, textX, cy, Gfx::Elide(item.systemText, FS_REGULAR, px, (int) (right - textX)), FS_REGULAR, px, MUTED);
			}
			return;
		}

		bool pending = m.m_type == MessageType::SENDING_MESSAGE || m.m_type == MessageType::UNSENT_MESSAGE;
		float avX = o.x + geo.margin;

		if (m.IsReply() && !item.grouped && m.m_pReferencedMessage) {
			const ReferenceMessage& ref = *m.m_pReferencedMessage;
			float lh = (float) Gfx::LineHeight(FS_REGULAR, px - 2);
			float cy = top + ex.replyTop + lh / 2;
			// the curve from the avatar column to the message replied to
			float ex0 = avX + geo.avatar / 2.0f;
			dl->PathLineTo(ImVec2(ex0, cy + lh / 2 + 4));
			dl->PathLineTo(ImVec2(ex0, cy + 4));
			dl->PathArcTo(ImVec2(ex0 + 6, cy + 4), 6.0f, 3.14159f, 4.71239f);
			dl->PathLineTo(ImVec2(textX - 6, cy - 2));
			dl->PathStroke(Col(0x4e5058), 0, 2.0f);
			float x = textX;
			UserAvatar(dl, x, cy - 8, 16, ref.m_author_snowflake, ref.m_avatar, ref.m_author, -1, CHAT_BG);
			x += 20;
			x += TextMid(dl, x, cy, "@" + ref.m_author, FS_BOLD, px - 2, MUTED) + 6;
			ReplySnippet(dl, m, x, cy, right);
		}

		if (!item.grouped)
		{
			float y = top + ex.headerTop;
			const Image* av = m.m_avatar.empty() ?
				ImageCache::Get(ImageCache::DEFAULT_AVATAR, "", m.m_author_snowflake, geo.avatar, geo.avatar) :
				ImageCache::Get(ImageCache::AVATAR, m.m_avatar, m.m_author_snowflake, geo.avatar, geo.avatar);
			if (av)
				Gfx::DrawImageCircle(dl, *av, avX + (geo.avatar - av->w) / 2.0f, y + (geo.avatar - av->h) / 2.0f);
			else {
				dl->AddCircleFilled(ImVec2(avX + geo.avatar / 2.0f, y + geo.avatar / 2.0f), geo.avatar / 2.0f, Col(AvatarColor(m.m_author_snowflake)));
				std::string ini = Initials(m.m_author).substr(0, 1);
				int iw = Gfx::Measure(ini, FS_BOLD, 18);
				TextMid(dl, avX + (geo.avatar - iw) / 2.0f, y + geo.avatar / 2.0f, ini, FS_BOLD, 18, 0xffffff);
			}

			// the name, a BOT tag, then the time, as Discord sets them
			int asc = Gfx::Ascent(FS_BOLD, px);
			float x = textX;
			uint32_t nameColor = demo ? 0 : Lists::RoleColor(m.m_author_snowflake, list->GetGuild());
			std::string name = Gfx::Elide(m.m_author, FS_BOLD, px, std::max(40, (int) (right - x - 160)));
			x += TextAt(dl, x, y + asc, name, FS_BOLD, px, nameColor ? nameColor : TEXT_BRIGHT) + 6;
			if (m.m_bIsAuthorBot || m.IsWebHook()) {
				int bw = Gfx::Measure("BOT", FS_BOLD, 10) + 8;
				Fill(dl, x, y + asc - 13, (float) bw, 15, BLURPLE, 3.0f);
				TextMid(dl, x + 4, y + asc - 5.5f, "BOT", FS_BOLD, 10, 0xffffff);
				x += bw + 6;
			}
			std::string when = m.m_dateFull.empty() ? m.m_dateCompact : m.m_dateFull;
			if (!m.m_editedText.empty())
				when += "  (edited)";
			TextAt(dl, x, y + asc, when, FS_REGULAR, 12, MUTED);
		}
		else if (hovered && !m.m_dateCompact.empty()) {
			// a grouped message shows its time in the gutter when pointed at
			int tw = Gfx::Measure(m.m_dateCompact, FS_REGULAR, 11);
			TextAt(dl, textX - 8 - tw, top + item.textTop + Gfx::Ascent(FS_REGULAR, px), m.m_dateCompact, FS_REGULAR, 11, MUTED);
		}

		if (!item.text.Empty()) {
			uint32_t fg = ctx.fg;
			if (pending)
				ctx.fg = MUTED;
			item.text.Draw(&ctx, item.y);
			ctx.fg = fg;
		}

		// attachments: pictures, or a file's card
		for (size_t ai = 0; ai < m.m_attachments.size(); ai++) {
			const Attachment& att = m.m_attachments[ai];
			if (ai >= ex.attachPics.size())
				break;
			if (!ex.attachPics[ai].url.empty()) {
				DrawPicture(dl, ex.attachPics[ai].rect, ex.attachPics[ai].url, ImVec2(o.x, top), att.m_fileName, clip);
				continue;
			}
			const Rect& r = ex.attachPics[ai].rect;
			float y = top + r.top, h = (float) r.Height();
			int asc = Gfx::Ascent(FS_REGULAR, px);
			float ih = asc + 1, iw = ih * 3 / 4, iy = y + (h - ih) / 2, fold = iw / 3, ix = textX;
			dl->AddRectFilled(ImVec2(ix, iy), ImVec2(ix + iw - fold, iy + ih), Col(TEXT));
			dl->AddTriangleFilled(ImVec2(ix + iw - fold, iy), ImVec2(ix + iw, iy + fold), ImVec2(ix + iw - fold, iy + fold), Col(MUTED));
			dl->AddRectFilled(ImVec2(ix + iw - fold, iy + fold), ImVec2(ix + iw, iy + ih), Col(TEXT));
			float x = ix + iw + 8;
			int nameW = TextMid(dl, x, y + h / 2, att.m_fileName, FS_REGULAR, px, ctx.link);
			TextMid(dl, x + nameW + 8, y + h / 2, MessageList::FormatSize(att.m_size), FS_REGULAR, 12, MUTED);
		}

		// reactions; the user's own stand out
		int rpx = px - 1;
		for (size_t i = 0; i < m.m_reactions.size() && i < ex.reactionRects.size(); i++)
		{
			const Reaction& r = m.m_reactions[i];
			const Rect& rc = ex.reactionRects[i];
			float x = o.x + rc.left, y = top + rc.top, w = (float) rc.Width(), h = (float) rc.Height();
			Fill(dl, x, y, w, h, r.m_bMe ? 0x373a54 : 0x2b2d31, 8.0f);
			if (r.m_bMe)
				dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), Col(BLURPLE), 8.0f);
			float cy = y + h / 2;
			float ex2 = x + geo.pillPad;
			if (r.m_emojiId) {
				int s = (int) h - 8;
				const Image* img = ImageCache::Get(ImageCache::EMOJI, "", r.m_emojiId, s, s);
				if (img)
					Gfx::DrawImage(dl, *img, ex2 + (s - img->w) / 2.0f, y + 4 + (s - img->h) / 2.0f);
				ex2 += s;
			}
			else
				ex2 += TextMid(dl, ex2, cy, r.m_emojiName, FS_REGULAR, rpx, TEXT);
			TextMid(dl, ex2 + 5, cy, std::to_string(r.m_count), FS_BOLD, rpx, r.m_bMe ? 0xc9cdfb : MUTED);
		}

		// embeds
		for (size_t i = 0; i < m.m_embeds.size() && i < ex.embedTops.size(); i++)
		{
			const RichEmbed& em = m.m_embeds[i];
			float y = top + ex.embedTops[i];
			float w = std::min(right, textX + geo.embedWidth) - textX;
			float bx = textX;
			Fill(dl, bx, y, w, (float) ex.embedHeights[i], 0x2b2d31, 4.0f);
			Fill(dl, bx, y, 4, (float) ex.embedHeights[i], em.m_color ? (uint32_t) em.m_color : 0x1e1f22, 4.0f);
			float x = bx + 12;
			y += 6;
			if (!em.m_providerName.empty()) {
				int spx = px - 3;
				TextAt(dl, x, y + Gfx::Ascent(FS_REGULAR, spx), Gfx::Elide(em.m_providerName, FS_REGULAR, spx, (int) w - 16), FS_REGULAR, spx, MUTED);
				y += Gfx::LineHeight(FS_REGULAR, spx) + 2;
			}
			if (!em.m_authorName.empty()) {
				int spx = px - 2;
				TextAt(dl, x, y + Gfx::Ascent(FS_BOLD, spx), Gfx::Elide(em.m_authorName, FS_BOLD, spx, (int) w - 16), FS_BOLD, spx, TEXT_BRIGHT);
				y += Gfx::LineHeight(FS_BOLD, spx) + 2;
			}
			if (!em.m_title.empty())
				TextAt(dl, x, y + Gfx::Ascent(FS_BOLD, px), Gfx::Elide(em.m_title, FS_BOLD, px, (int) w - 16), FS_BOLD, px,
					em.m_url.empty() ? TEXT_BRIGHT : ctx.link);
			if (ex.embedTexts[i])
				ex.embedTexts[i]->Draw(&ctx, item.y);
			if (i < ex.embedThumbs.size() && !ex.embedThumbs[i].url.empty())
				DrawPicture(dl, ex.embedThumbs[i].rect, ex.embedThumbs[i].url, ImVec2(o.x, top), "", clip);
			if (i < ex.embedImages.size() && !ex.embedImages[i].url.empty())
				DrawPicture(dl, ex.embedImages[i].rect, ex.embedImages[i].url, ImVec2(o.x, top), "", clip);
			if (!em.m_footerText.empty()) {
				int spx = px - 3;
				float fy = top + ex.embedTops[i] + ex.embedHeights[i] - 6 - (Gfx::LineHeight(FS_REGULAR, spx) - Gfx::Ascent(FS_REGULAR, spx));
				TextAt(dl, x, fy, Gfx::Elide(em.m_footerText, FS_REGULAR, spx, (int) w - 16), FS_REGULAR, spx, MUTED);
			}
		}
	}

	// The red line above the first message that came after the user last
	// read the channel.
	void NewLine(ImDrawList* dl, const MessageList::Item& item, ImVec2 o, float viewW)
	{
		const MessageList::Geometry& geo = list->Geo();
		float y = o.y + item.y + (item.extra->dateSep.empty() ? 0 : geo.dateSep) + 1;
		float right = o.x + viewW - geo.margin;
		dl->AddLine(ImVec2(o.x + geo.margin, y), ImVec2(right - 34, y), Col(RED), 1.0f);
		Fill(dl, right - 36, y - 7, 36, 14, RED, 3.0f);
		TextMid(dl, right - 31, y, "NEW", FS_BOLD, 10, 0xffffff);
	}

	void Messages(float width, float height)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Col(CHAT_BG)));
		ImGui::BeginChild("messages", ImVec2(width, height), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		float viewW = std::max(200.0f, ImGui::GetContentRegionAvail().x);
		float viewH = ImGui::GetWindowHeight();

		if (!list->GetChannel()) {
			const char* hint = selGuild ? "Pick a channel on the left." : "Pick a conversation on the left.";
			int tw = Gfx::Measure(hint, FS_REGULAR, ctx.px + 2);
			ImVec2 wp = ImGui::GetWindowPos();
			TextMid(dl, wp.x + (viewW - tw) / 2, wp.y + viewH / 2, hint, FS_REGULAR, ctx.px + 2, MUTED);
			ImGui::EndChild();
			ImGui::PopStyleColor();
			return;
		}

		// following the bottom: stays there when messages come
		float scrollY = ImGui::GetScrollY();
		bool atBottom = stick || scrollY >= ImGui::GetScrollMaxY() - 4;
		Snowflake anchor = 0;
		float anchorOffset = 0;
		if (frameDirty & App::MESSAGES) {
			for (auto& it : list->Items()) {
				if (it.y + it.height > scrollY && !it.msg->IsLoadGap()) {
					anchor = it.msg->m_snowflake;
					anchorOffset = it.y - scrollY;
					break;
				}
			}
			list->Rebuild();
		}
		ApplyTheme();
		list->Layout(&ctx, ctx.px, (int) viewW);

		ImVec2 o = ImGui::GetCursorScreenPos();
		ctx.dl = dl;
		ctx.origin = o;
		ImGui::Dummy(ImVec2(viewW, (float) list->ContentHeight() + 8));
		if ((frameDirty & App::MESSAGES) && !atBottom && anchor) {
			for (auto& it : list->Items())
				if (it.msg->m_snowflake == anchor)
					ImGui::SetScrollY(it.y - anchorOffset);
		}
		if (atBottom)
			ImGui::SetScrollY((float) list->ContentHeight() + 8);
		stick = atBottom;

		ImVec4 clip(o.x, ImGui::GetWindowPos().y, o.x + viewW, ImGui::GetWindowPos().y + viewH);
		ImVec2 mp = ImGui::GetMousePos();
		bool overView = ImGui::IsWindowHovered();
		Snowflake me = demo || !GetDiscordInstance() ? 0 : GetDiscordInstance()->GetUserID();
		bool newShown = false;
		for (auto& it : list->Items()) {
			float top = o.y + it.y;
			bool isNew = !newShown && unreadAfter && !it.msg->IsLoadGap() && !it.systemLine &&
				it.msg->m_snowflake > unreadAfter && it.msg->m_author_snowflake != me;
			if (isNew)
				newShown = true;
			if (top + it.height < clip.y || top > clip.w)
				continue;
			bool hovered = overView && mp.y >= top && mp.y < top + it.height && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
			PaintItem(dl, it, o, viewW, clip, hovered);
			if (isNew)
				NewLine(dl, it, o, viewW);
		}
		ctx.dl = nullptr;

		// clicks: links, reactions, pictures; the right button's menu
		if (overView) {
			int cx = (int) (mp.x - o.x), cy = (int) (mp.y - o.y);
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				MessageList::Hit hit = list->HitTest(cx, cy);
				if (hit.kind == MessageList::Hit::LINK)
					GetFrontend()->LaunchURL(hit.url);
				else if (hit.kind == MessageList::Hit::REACTION && !demo) {
					const Reaction& r = hit.message->m_reactions[hit.reaction];
					GetDiscordInstance()->RequestReaction(list->GetChannel(), hit.message->m_snowflake, r, !r.m_bMe);
				}
				else if (hit.kind == MessageList::Hit::PICTURE && !hit.picture.url.empty())
					OpenViewer(hit.picture);
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				MessageList::Item* it = list->ItemAt(cy);
				if (it && MessageList::IsActionable(*it->msg)) {
					menuMessage = it->msg;
					ImGui::OpenPopup("message menu");
				}
			}
		}
		if (ImGui::BeginPopup("message menu")) {
			if (menuMessage) {
				if (!demo && ImGui::MenuItem("Add Reaction"))
					OpenPicker(true, menuMessage->m_snowflake, ImGui::GetMousePos());
				if (ImGui::MenuItem("Reply")) {
					composer.BeginReply(menuMessage->m_snowflake);
					bar = "Replying to " + menuMessage->m_author;
					focusInput = true;
				}
				if (list->CanEdit(*menuMessage) && ImGui::MenuItem("Edit Message")) {
					composer.BeginEdit(menuMessage->m_snowflake);
					bar = "Editing message";
					snprintf(input, sizeof input, "%s", menuMessage->m_message.c_str());
					focusInput = true;
				}
				if (list->CanDelete(*menuMessage)) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Col(0xf23f43)));
					if (ImGui::MenuItem("Delete Message"))
						deleteMessage = menuMessage;
					ImGui::PopStyleColor();
				}
			}
			ImGui::EndPopup();
		}

		// the history behind gaps on screen; and read marks
		scrollY = ImGui::GetScrollY();
		if (!demo) {
			list->RequestGaps((int) scrollY, (int) (scrollY + viewH));
			if (list->NewestShown(stick)) {
				bool opened = justOpened;
				justOpened = false;
				if ((opened || focused) && list->AcknowledgeIfUnread())
					App::MarkDirty(App::LIST_CHANNELS | App::LIST_GUILDS);
			}
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	// The message box: rounded, with its placeholder and the emoji button;
	// Enter sends, Shift+Enter (or Ctrl+Enter) starts a new line.
	void MessageBox(float width)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float side = 16;
		float boxW = width - 2 * side;
		ImVec2 start = ImGui::GetCursorScreenPos();
		float x0 = start.x + side;

		// "Replying to ..." above the box
		if (!bar.empty()) {
			Fill(dl, x0, start.y, boxW, 34, 0x2b2d31, 8.0f);
			TextMid(dl, x0 + 16, start.y + 17, bar, FS_REGULAR, 13, MUTED);
			ImGui::SetCursorScreenPos(ImVec2(x0 + boxW - 30, start.y + 5));
			if (ImGui::InvisibleButton("cancel reply", ImVec2(24, 24))) {
				if (composer.Cancel())
					input[0] = 0;
				bar.clear();
			}
			bool h = ImGui::IsItemHovered();
			TextMid(dl, x0 + boxW - 24, start.y + 17, "\xc3\x97", FS_BOLD, 16, h ? TEXT : MUTED); // U+00D7
			start.y += 30;
		}

		std::string name, topic;
		bool dm;
		ChannelNames(name, topic, dm);
		int lines = 1;
		for (const char* p = input; *p; p++)
			if (*p == '\n')
				lines++;
		float lineH = ImGui::GetTextLineHeight();
		float inputH = std::min(8, lines) * lineH + 4;
		float boxH = std::max(44.0f, inputH + 22);
		Fill(dl, x0, start.y, boxW, boxH, COMPOSER_BG, 8.0f);

		// "+": uploading is not there yet
		float pcx = x0 + 28, pcy = start.y + 22;
		dl->AddCircleFilled(ImVec2(pcx, pcy), 12, Col(0xb5bac1));
		dl->AddLine(ImVec2(pcx - 6, pcy), ImVec2(pcx + 6, pcy), Col(COMPOSER_BG), 2.5f);
		dl->AddLine(ImVec2(pcx, pcy - 6), ImVec2(pcx, pcy + 6), Col(COMPOSER_BG), 2.5f);

		float inX = x0 + 56, inW = boxW - 56 - 52;
		ImGui::SetCursorScreenPos(ImVec2(inX, start.y + 11));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 2));
		if (focusInput) {
			ImGui::SetKeyboardFocusHere();
			focusInput = false;
		}
		bool enter = ImGui::InputTextMultiline("##message", input, sizeof input, ImVec2(inW, inputH),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_NoHorizontalScroll);
		bool edited = ImGui::IsItemEdited();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		if (!input[0] && !name.empty())
			TextMid(dl, inX, start.y + 22, Gfx::Elide("Message " + std::string(dm ? "@" : "#") + name, FS_REGULAR, ctx.px, (int) inW), FS_REGULAR, ctx.px, PLACEHOLDER);
		if (edited)
			composer.TextChanged(input[0] == 0);

		// the emoji button
		ImGui::SetCursorScreenPos(ImVec2(x0 + boxW - 44, start.y + 6));
		if (ImGui::InvisibleButton("emoji", ImVec2(32, 32)))
			OpenPicker(false, 0, ImVec2(x0 + boxW - 28, start.y));
		bool eh = ImGui::IsItemHovered();
		if (eh)
			ImGui::SetTooltip("Select emoji");
		const char* face = "\xf0\x9f\x99\x82"; // U+1F642
		int fpx = eh ? 22 : 20;
		int fw = Gfx::Measure(face, FS_REGULAR, fpx);
		Gfx::Text(dl, ImVec2(x0 + boxW - 28 - fw / 2.0f, start.y + 22 - Gfx::LineHeight(FS_REGULAR, fpx) / 2.0f), face, FS_REGULAR, fpx, 0);

		if (enter) {
			if (ImGui::GetIO().KeyShift) {
				// Shift+Enter: a new line, not a send
				size_t n = strlen(input);
				if (n + 1 < sizeof input) {
					input[n] = '\n';
					input[n + 1] = 0;
				}
			}
			else if (!demo) {
				Composer::Result r = composer.Send(input);
				if (r == Composer::SENT || r == Composer::EDITED) {
					input[0] = 0;
					bar.clear();
					stick = true;
				}
			}
			focusInput = true;
		}

		// who is typing, under the box
		ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + boxH));
		ImVec2 tp = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(width, 22));
		std::string typing = list->GetChannel() ? Typing::Text(list->GetChannel()) : "";
		const std::string& line = typing.empty() ? status : typing;
		if (!line.empty())
			TextMid(dl, x0 + 4, tp.y + 11, Gfx::Elide(line, FS_REGULAR, 12, (int) boxW), typing.empty() ? FS_REGULAR : FS_BOLD, 12, MUTED);
	}
}

void Ui::Chat(float height)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Col(CHAT_BG)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	float width = ImGui::GetContentRegionAvail().x - (IsPaneShown(PANE_MEMBERS) && selGuild ? MEMBERS_W : 0);
	ImGui::BeginChild("chat", ImVec2(width, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	HeaderBar(width);
	float boxH = std::max(44.0f, std::min(8, 1 + (int) std::count(input, input + strlen(input), '\n')) * ImGui::GetTextLineHeight() + 26)
		+ 22 + (bar.empty() ? 0 : 30);
	Messages(width, height - HEADER_H - boxH);
	MessageBox(width);
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}
