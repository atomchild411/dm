#include "App.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "DiscordInstance.hpp"
#include "Frontend.hpp"
#include "config/LocalSettings.hpp"
#include "state/MessageCache.hpp"
#include "shared/ClientConfig.hpp"
#include "shared/Composer.hpp"
#include "shared/Demo.hpp"
#include "shared/ImageCache.hpp"
#include "shared/Lists.hpp"
#include "shared/MessageList.hpp"
#include "shared/QrLogin.hpp"
#include "shared/Typing.hpp"
#include "shared/Utf8.hpp"
#include "DrawingContext.hpp"
#include "Gfx.hpp"

namespace
{
	const int GUILD_W = 210, CHANNEL_W = 220, MEMBER_W = 220;
	const int MARGIN = MessageList::MARGIN;
	const int AVATAR = MessageList::AVATAR;
	const int TEXT_X = MessageList::TEXT_X;
	const int DATE_SEP = MessageList::DATE_SEP;
	const int PILL_PAD = MessageList::PILL_PAD;

	bool g_demo = false;
	bool g_quit = false;
	int g_dirty = App::LISTS | App::MESSAGES; // marked since the last frame
	int g_frameDirty = 0;   // what this frame brings up to date

	std::vector<ListRow> g_guildRows, g_channelRows, g_memberRows;
	Snowflake g_selGuild = 0, g_selChannel = 0;

	MessageList* g_list;
	DrawingContext g_ctx;
	Composer g_composer;
	char g_input[4000];
	std::string g_bar;          // "Replying to ...", "Editing your message"
	bool g_stick = true;        // the view follows the newest messages
	bool g_justOpened = false;
	bool g_restoredLast = false;
	bool g_focused = true;
	std::string g_status;

	std::vector<std::string> g_errors;
	MessagePtr g_menuMessage, g_deleteMessage;

	// the login dialog
	bool g_loginShown = false;
	bool g_tokenMode = false;
	std::string g_loginWhy;
	char g_token[256];

	uint32_t Mix(uint32_t a, uint32_t b, int num, int den)
	{
		uint32_t out = 0;
		for (int sh = 0; sh <= 16; sh += 8) {
			int ca = (a >> sh) & 0xff, cb = (b >> sh) & 0xff;
			out |= (uint32_t) (ca + (cb - ca) * num / den) << sh;
		}
		return out;
	}

	uint32_t AvatarColor(Snowflake sf)
	{
		static const uint32_t colors[] = { 0x5865f2, 0x3ba55c, 0xfaa61a, 0xed4245, 0xeb459e, 0x747f8d, 0x2d8f9e, 0x9b59b6 };
		return colors[(sf >> 22) % (sizeof colors / sizeof colors[0])];
	}

	// Text with its baseline at y (as the shared layout places it).
	int TextAt(ImDrawList* dl, float x, float y, const std::string& s, FontStyle st, int px, uint32_t color)
	{
		return Gfx::Text(dl, ImVec2(x, y - Gfx::Ascent(st, px)), s, st, px, color);
	}

	std::string Initials(const std::string& text)
	{
		const char* p = text.c_str();
		const char* end = p + text.size();
		std::string out;
		bool start = true;
		while (p < end && out.size() < 8) {
			const char* q = p;
			unsigned cp = DecodeUtf8(q, end);
			if (cp == ' ')
				start = true;
			else if (start) {
				out.append(p, q);
				start = false;
				if (out.size() >= 2)
					break;
			}
			p = q;
		}
		return out;
	}

	// ---- lists -----------------------------------------------------------

	void UpdateLists()
	{
		if (g_demo) {
			if (g_frameDirty & App::LISTS) {
				g_guildRows = Demo::GuildRows();
				g_channelRows = Demo::ChannelRows();
				g_memberRows = Demo::MemberRows();
				g_selGuild = Demo::SELECTED_GUILD;
				g_selChannel = Demo::SELECTED_CHANNEL;
			}
			return;
		}
		DiscordInstance* pInst = GetDiscordInstance();
		if (g_frameDirty & App::LIST_GUILDS)
			g_guildRows = Lists::GuildRows();
		if (g_frameDirty & App::LIST_CHANNELS)
			g_channelRows = Lists::ChannelRows();
		if ((g_frameDirty & App::LIST_MEMBERS) && IsPaneShown(PANE_MEMBERS))
			g_memberRows = Lists::MemberRows();
		g_selGuild = pInst->GetCurrentGuildID();
		g_selChannel = pInst->GetCurrentChannelID();
	}

	int RowHeight(const ListRow& r, int icon)
	{
		int px = GetTextSize();
		switch (r.type) {
			case ListRow::HEADER: return Gfx::LineHeight(FS_BOLD, px - 3) + 12;
			case ListRow::SPACE:  return 10;
			default:              return std::max(icon, Gfx::LineHeight(FS_REGULAR, px)) + 8;
		}
	}

	// A list of rows with icons; returns the id of a row clicked, or -1.
	long long DrawList(const char* id, const std::vector<ListRow>& rows, Snowflake selected, int icon, uint32_t bg)
	{
		const Palette& p = GetPalette();
		long long picked = -1;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Col(bg)));
		ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_None);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		int px = GetTextSize();
		float width = ImGui::GetContentRegionAvail().x;
		for (size_t i = 0; i < rows.size(); i++)
		{
			const ListRow& r = rows[i];
			int h = RowHeight(r, icon);
			ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::PushID((int) i);
			bool clicked = ImGui::InvisibleButton("row", ImVec2(width, (float) h));
			bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();
			if (!ImGui::IsItemVisible())
				continue;

			if (r.type == ListRow::SPACE) {
				dl->AddLine(ImVec2(pos.x + 12, pos.y + h / 2), ImVec2(pos.x + width - 12, pos.y + h / 2), Col(p.listMuted, 90));
				continue;
			}
			if (r.type == ListRow::HEADER) {
				TextAt(dl, pos.x + 10 + r.indent, pos.y + h - 6, Gfx::Elide(r.text, FS_BOLD, px - 3, (int) width - 20), FS_BOLD, px - 3, p.listHeader);
				continue;
			}
			bool sel = r.id == selected && r.selectable;
			if (sel || (hovered && r.selectable))
				dl->AddRectFilled(ImVec2(pos.x + 6, pos.y + 1), ImVec2(pos.x + width - 6, pos.y + h - 1), Col(p.selBg, sel ? 255 : 120), 4.0f);
			if (clicked && r.selectable)
				picked = (long long) r.id;

			float x = pos.x + 12 + r.indent;
			float iy = pos.y + (h - icon) / 2;
			// the icon: an image, a glyph, or initials on a disc
			const Image* img = r.hasImage ? ImageCache::Get(r.imageKind, r.imagePlace, r.imageSf, icon, icon) : nullptr;
			if (img) {
				if (r.roundImage)
					Gfx::DrawImageCircle(dl, *img, x + (icon - img->w) / 2.0f, iy + (icon - img->h) / 2.0f);
				else
					Gfx::DrawImage(dl, *img, x + (icon - img->w) / 2.0f, iy + (icon - img->h) / 2.0f);
			}
			else if (!r.glyph.empty()) {
				int gw = Gfx::Measure(r.glyph, FS_BOLD, px + 2);
				TextAt(dl, x + (icon - gw) / 2.0f, iy + icon / 2.0f + Gfx::Ascent(FS_BOLD, px + 2) / 2.0f - 1, r.glyph, FS_BOLD, px + 2, p.listMuted);
			}
			else if (r.initials || r.hasImage) {
				Snowflake seed = r.colorSeed ? r.colorSeed : r.id;
				dl->AddCircleFilled(ImVec2(x + icon / 2.0f, iy + icon / 2.0f), icon / 2.0f, Col(AvatarColor(seed)));
				std::string ini = Initials(r.text);
				int ipx = std::max(9, icon * 2 / 5);
				int iw = Gfx::Measure(ini, FS_BOLD, ipx);
				TextAt(dl, x + (icon - iw) / 2.0f, iy + icon / 2.0f + Gfx::Ascent(FS_BOLD, ipx) / 2.0f - 1, ini, FS_BOLD, ipx, 0xffffff);
			}
			if (r.status >= 0) {
				// the presence dot
				uint32_t sc = r.status == 1 ? p.online : r.status == 2 ? p.idle : r.status == 3 ? p.dnd : p.offline;
				ImVec2 c(x + icon - 4, iy + icon - 4);
				dl->AddCircleFilled(c, 5.0f, Col(bg));
				dl->AddCircleFilled(c, 3.5f, Col(sc));
			}

			// the text, bold when unread, then the mention badge
			float tx = x + icon + 10;
			float right = pos.x + width - 10;
			std::string badge = r.mentions > 0 ? std::to_string(r.mentions) : "";
			int bw = badge.empty() ? 0 : Gfx::Measure(badge, FS_BOLD, px - 3) + 10;
			FontStyle st = r.unread ? FS_BOLD : FS_REGULAR;
			uint32_t tc = r.textColor ? r.textColor : (r.dim ? p.listMuted : (sel || r.unread ? p.selFg : p.listFg));
			std::string text = Gfx::Elide(r.text, st, px, (int) (right - tx - bw - (bw ? 6 : 0)));
			TextAt(dl, tx, pos.y + h / 2.0f + Gfx::Ascent(st, px) / 2.0f - 1, text, st, px, tc);
			if (r.unread && !sel)
				dl->AddRectFilled(ImVec2(pos.x, pos.y + h / 2 - 4), ImVec2(pos.x + 3, pos.y + h / 2 + 4), Col(p.selFg), 2.0f);
			if (bw) {
				float bx = right - bw, by = pos.y + (h - 16) / 2.0f;
				dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 16), Col(p.badge), 8.0f);
				TextAt(dl, bx + 5, by + 8 + Gfx::Ascent(FS_BOLD, px - 3) / 2.0f - 1, badge, FS_BOLD, px - 3, p.badgeFg);
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		return picked;
	}

	// ---- messages --------------------------------------------------------

	void ApplyTheme()
	{
		const Palette& p = GetPalette();
		g_ctx.px = GetTextSize();
		g_ctx.fg = p.msgFg;
		g_ctx.bg = p.msgBg;
		g_ctx.link = p.link;
		g_ctx.mention = p.mention;
		g_ctx.codeBg = p.codeBg;
		g_ctx.codeFrame = p.codeFrame;
		g_ctx.quoteBar = p.quoteBar;
		g_ctx.muted = p.msgMuted;
	}

	void DrawPicture(ImDrawList* dl, const Rect& r, const std::string& url, ImVec2 o, const std::string& label, const ImVec4& clip)
	{
		float x = o.x + r.left, y = o.y + r.top;
		int w = r.Width(), h = r.Height();
		if (y + h < clip.y || y > clip.w)
			return; // not on screen: not fetched either
		const Image* img = ImageCache::Get(ImageCache::URL, url, 0, w, h);
		if (img) {
			Gfx::DrawImage(dl, *img, x + (w - img->w) / 2.0f, y + (h - img->h) / 2.0f);
			return;
		}
		dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), Col(g_ctx.codeBg));
		dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), Col(g_ctx.codeFrame));
		bool failed = ImageCache::Failed(ImageCache::URL, url, 0, w, h);
		std::string text = failed ? (label.empty() ? std::string("Image not available") : label) : std::string("Loading\xe2\x80\xa6");
		int spx = g_ctx.px - 2;
		text = Gfx::Elide(text, FS_ITALIC, spx, w - 8);
		int tw = Gfx::Measure(text, FS_ITALIC, spx);
		if (h > Gfx::LineHeight(FS_ITALIC, spx))
			TextAt(dl, x + (w - tw) / 2.0f, y + h / 2.0f + Gfx::Ascent(FS_ITALIC, spx) / 2.0f, text, FS_ITALIC, spx, g_ctx.muted);
	}

	// One item, its content (0, 0) at o (the shared layout's coordinates).
	void PaintItem(ImDrawList* dl, MessageList::Item& item, ImVec2 o, float viewW, const ImVec4& clip)
	{
		MessageList::ItemExtra& ex = *item.extra;
		const Message& m = *item.msg;
		float top = o.y + item.y;
		float right = o.x + viewW - MARGIN;
		int px = g_ctx.px;

		if (!ex.dateSep.empty()) {
			float mid = top + DATE_SEP / 2 + 2;
			int tw = Gfx::Measure(ex.dateSep, FS_BOLD, px - 3);
			float cx = o.x + (viewW - tw) / 2;
			dl->AddLine(ImVec2(o.x + MARGIN, mid), ImVec2(cx - 8, mid), Col(g_ctx.codeFrame));
			dl->AddLine(ImVec2(cx + tw + 8, mid), ImVec2(right, mid), Col(g_ctx.codeFrame));
			TextAt(dl, cx, mid + 4, ex.dateSep, FS_BOLD, px - 3, g_ctx.muted);
		}

		if (item.systemLine) {
			float y = top + ex.headerTop + Gfx::Ascent(FS_ITALIC, px);
			if (m.IsLoadGap()) {
				int tw = Gfx::Measure(item.systemText, FS_ITALIC, px);
				TextAt(dl, o.x + (viewW - tw) / 2, y, item.systemText, FS_ITALIC, px, g_ctx.muted);
			}
			else {
				TextAt(dl, o.x + TEXT_X - 22, y, "\xe2\x86\x92", FS_BOLD, px, 0x3ba55c);
				TextAt(dl, o.x + TEXT_X, y, Gfx::Elide(item.systemText, FS_ITALIC, px, (int) (right - o.x - TEXT_X)), FS_ITALIC, px, g_ctx.muted);
			}
			return;
		}

		bool pending = m.m_type == MessageType::SENDING_MESSAGE || m.m_type == MessageType::UNSENT_MESSAGE;

		if (m.IsReply() && !item.grouped && m.m_pReferencedMessage) {
			float y = top + ex.replyTop;
			int spx = px - 2;
			int asc = Gfx::Ascent(FS_ITALIC, spx);
			// the reply's elbow from the avatar column
			float ex0 = o.x + MARGIN + AVATAR / 2;
			dl->AddLine(ImVec2(ex0, y + asc / 2), ImVec2(ex0, y + asc / 2 + Gfx::LineHeight(FS_ITALIC, spx)), Col(g_ctx.quoteBar), 2.0f);
			dl->AddLine(ImVec2(ex0, y + asc / 2), ImVec2(o.x + TEXT_X - 6, y + asc / 2), Col(g_ctx.quoteBar), 2.0f);
			std::string who = "@" + m.m_pReferencedMessage->m_author + " ";
			float x = o.x + TEXT_X;
			x += TextAt(dl, x, y + asc, who, FS_BOLD, spx, g_ctx.muted);
			std::string snippet = m.m_pReferencedMessage->m_message;
			std::replace(snippet.begin(), snippet.end(), '\n', ' ');
			if (snippet.empty())
				snippet = m.m_pReferencedMessage->m_bHasAttachments ? "Click to see attachment" : "Original message was deleted";
			TextAt(dl, x, y + asc, Gfx::Elide(snippet, FS_ITALIC, spx, (int) (right - x)), FS_ITALIC, spx, g_ctx.muted);
		}

		if (!item.grouped)
		{
			float y = top + ex.headerTop;
			// the avatar; a coloured disc with the initial until it arrives
			const Image* av = m.m_avatar.empty() ?
				ImageCache::Get(ImageCache::DEFAULT_AVATAR, "", m.m_author_snowflake, AVATAR, AVATAR) :
				ImageCache::Get(ImageCache::AVATAR, m.m_avatar, m.m_author_snowflake, AVATAR, AVATAR);
			if (av)
				Gfx::DrawImageCircle(dl, *av, o.x + MARGIN + (AVATAR - av->w) / 2.0f, y + (AVATAR - av->h) / 2.0f);
			else {
				dl->AddCircleFilled(ImVec2(o.x + MARGIN + AVATAR / 2.0f, y + AVATAR / 2.0f), AVATAR / 2.0f, Col(AvatarColor(m.m_author_snowflake)));
				if (!m.m_author.empty()) {
					const char* p = m.m_author.c_str();
					DecodeUtf8(p, p + m.m_author.size());
					std::string initial(m.m_author.c_str(), p - m.m_author.c_str());
					int iw = Gfx::Measure(initial, FS_BOLD, 17);
					TextAt(dl, o.x + MARGIN + (AVATAR - iw) / 2.0f, y + AVATAR / 2 + 6, initial, FS_BOLD, 17, 0xffffff);
				}
			}

			int asc = Gfx::Ascent(FS_BOLD, px);
			float x = o.x + TEXT_X;
			std::string when = m.m_dateFull.empty() ? m.m_dateCompact : m.m_dateFull;
			if (!m.m_editedText.empty())
				when += "  (edited)";
			int whenW = Gfx::Measure(when, FS_REGULAR, px - 3);
			uint32_t nameColor = g_demo ? 0 : Lists::RoleColor(m.m_author_snowflake, g_list->GetGuild());
			std::string name = Gfx::Elide(m.m_author, FS_BOLD, px, std::max(40, (int) (right - x - whenW - 60)));
			x += TextAt(dl, x, y + asc, name, FS_BOLD, px, nameColor ? nameColor : g_ctx.fg) + 8;
			if (m.m_bIsAuthorBot || m.IsWebHook()) {
				int bw = Gfx::Measure("BOT", FS_BOLD, px - 4) + 8;
				dl->AddRectFilled(ImVec2(x, y + 2), ImVec2(x + bw, y + 2 + asc), Col(0x5865f2), 3.0f);
				TextAt(dl, x + 4, y + asc - 1, "BOT", FS_BOLD, px - 4, 0xffffff);
				x += bw + 6;
			}
			// the time on the right
			TextAt(dl, right - whenW, y + asc, when, FS_REGULAR, px - 3, g_ctx.muted);
		}

		if (!item.text.Empty()) {
			uint32_t fg = g_ctx.fg;
			if (pending)
				g_ctx.fg = g_ctx.muted;
			item.text.Draw(&g_ctx, item.y);
			g_ctx.fg = fg;
		}

		// attachments
		float ay = top + ex.attachTop;
		int lh = Gfx::LineHeight(FS_REGULAR, px) + 4;
		for (size_t ai = 0; ai < m.m_attachments.size(); ai++) {
			const Attachment& att = m.m_attachments[ai];
			if (ai < ex.attachPics.size() && !ex.attachPics[ai].url.empty()) {
				Rect r = ex.attachPics[ai].rect;
				DrawPicture(dl, r, ex.attachPics[ai].url, ImVec2(o.x, top), att.m_fileName, clip);
				continue;
			}
			ay = ai < ex.attachPics.size() ? top + ex.attachPics[ai].rect.top : ay;
			int asc = Gfx::Ascent(FS_REGULAR, px);
			// a page with a folded corner
			float ih = asc + 1, iw = ih * 3 / 4, iy = ay + 3, fold = iw / 3, ix = o.x + TEXT_X;
			dl->AddRectFilled(ImVec2(ix, iy), ImVec2(ix + iw - fold, iy + ih), Col(0xffffff));
			dl->AddTriangleFilled(ImVec2(ix + iw - fold, iy), ImVec2(ix + iw, iy + fold), ImVec2(ix + iw - fold, iy + fold), Col(g_ctx.muted));
			dl->AddRectFilled(ImVec2(ix + iw - fold, iy + fold), ImVec2(ix + iw, iy + ih), Col(0xffffff));
			float x = ix + iw + 6;
			int nameW = TextAt(dl, x, ay + asc + 2, att.m_fileName, FS_REGULAR, px, g_ctx.link);
			dl->AddLine(ImVec2(x, ay + asc + 4), ImVec2(x + nameW, ay + asc + 4), Col(g_ctx.link));
			TextAt(dl, x + nameW + 8, ay + asc + 2, "(" + MessageList::FormatSize(att.m_size) + ")", FS_REGULAR, px - 3, g_ctx.muted);
			ay += lh;
		}

		// reactions; the user's own stand out
		int rpx = px - 1;
		for (size_t i = 0; i < m.m_reactions.size() && i < ex.reactionRects.size(); i++)
		{
			const Reaction& r = m.m_reactions[i];
			const Rect& rc = ex.reactionRects[i];
			float x = o.x + rc.left, y = top + rc.top, w = rc.Width(), h = rc.Height();
			dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), Col(r.m_bMe ? Mix(g_ctx.bg, g_ctx.link, 25, 100) : g_ctx.codeBg), 7.0f);
			dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), Col(r.m_bMe ? g_ctx.link : g_ctx.codeFrame), 7.0f);
			float base = y + (h + Gfx::Ascent(FS_REGULAR, rpx) - (Gfx::LineHeight(FS_REGULAR, rpx) - Gfx::Ascent(FS_REGULAR, rpx))) / 2;
			float ex2 = x + PILL_PAD;
			if (r.m_emojiId) {
				int s = (int) h - 8;
				const Image* img = ImageCache::Get(ImageCache::EMOJI, "", r.m_emojiId, s, s);
				if (img)
					Gfx::DrawImage(dl, *img, ex2 + (s - img->w) / 2.0f, y + 4 + (s - img->h) / 2.0f);
				ex2 += s;
			}
			else
				ex2 += TextAt(dl, ex2, base, r.m_emojiName, FS_REGULAR, rpx, g_ctx.fg);
			TextAt(dl, ex2 + 5, base, std::to_string(r.m_count), FS_BOLD, rpx, r.m_bMe ? g_ctx.link : g_ctx.muted);
		}

		// embeds
		for (size_t i = 0; i < m.m_embeds.size() && i < ex.embedTops.size(); i++)
		{
			const RichEmbed& em = m.m_embeds[i];
			float y = top + ex.embedTops[i];
			float w = std::min(right, o.x + TEXT_X + MessageList::EMBED_WIDTH) - (o.x + TEXT_X);
			float bx = o.x + TEXT_X;
			dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + w, y + ex.embedHeights[i]), Col(g_ctx.codeBg), 4.0f);
			dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + 4, y + ex.embedHeights[i]), Col(em.m_color ? (uint32_t) em.m_color : g_ctx.codeFrame), 2.0f);
			float x = bx + 12;
			y += 6;
			if (!em.m_providerName.empty()) {
				int spx = px - 3;
				TextAt(dl, x, y + Gfx::Ascent(FS_REGULAR, spx), Gfx::Elide(em.m_providerName, FS_REGULAR, spx, (int) w - 16), FS_REGULAR, spx, g_ctx.muted);
				y += Gfx::LineHeight(FS_REGULAR, spx) + 2;
			}
			if (!em.m_authorName.empty()) {
				int spx = px - 2;
				TextAt(dl, x, y + Gfx::Ascent(FS_BOLD, spx), Gfx::Elide(em.m_authorName, FS_BOLD, spx, (int) w - 16), FS_BOLD, spx, g_ctx.fg);
				y += Gfx::LineHeight(FS_BOLD, spx) + 2;
			}
			if (!em.m_title.empty())
				TextAt(dl, x, y + Gfx::Ascent(FS_BOLD, px), Gfx::Elide(em.m_title, FS_BOLD, px, (int) w - 16), FS_BOLD, px,
					em.m_url.empty() ? g_ctx.fg : g_ctx.link);
			if (ex.embedTexts[i])
				ex.embedTexts[i]->Draw(&g_ctx, item.y);
			if (i < ex.embedThumbs.size() && !ex.embedThumbs[i].url.empty())
				DrawPicture(dl, ex.embedThumbs[i].rect, ex.embedThumbs[i].url, ImVec2(o.x, top), "", clip);
			if (i < ex.embedImages.size() && !ex.embedImages[i].url.empty())
				DrawPicture(dl, ex.embedImages[i].rect, ex.embedImages[i].url, ImVec2(o.x, top), "", clip);
			if (!em.m_footerText.empty()) {
				int spx = px - 3;
				float fy = top + ex.embedTops[i] + ex.embedHeights[i] - 6 - (Gfx::LineHeight(FS_REGULAR, spx) - Gfx::Ascent(FS_REGULAR, spx));
				TextAt(dl, x, fy, Gfx::Elide(em.m_footerText, FS_REGULAR, spx, (int) w - 16), FS_REGULAR, spx, g_ctx.muted);
			}
		}
	}

	void MessageView(float height)
	{
		const Palette& p = GetPalette();
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Col(p.msgBg)));
		ImGui::BeginChild("messages", ImVec2(0, height), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		float viewW = std::max(200.0f, ImGui::GetContentRegionAvail().x);
		float viewH = ImGui::GetWindowHeight();

		if (!g_list->GetChannel()) {
			const char* hint = "Pick a channel on the left.";
			int tw = Gfx::Measure(hint, FS_ITALIC, g_ctx.px + 2);
			ImVec2 wp = ImGui::GetWindowPos();
			TextAt(dl, wp.x + (viewW - tw) / 2, wp.y + viewH / 2, hint, FS_ITALIC, g_ctx.px + 2, g_ctx.muted);
			ImGui::EndChild();
			ImGui::PopStyleColor();
			return;
		}

		// following the bottom: stays there when messages come
		float scrollY = ImGui::GetScrollY();
		bool atBottom = g_stick || scrollY >= ImGui::GetScrollMaxY() - 4;
		Snowflake anchor = 0;
		float anchorOffset = 0;
		if (g_frameDirty & App::MESSAGES) {
			for (auto& it : g_list->Items()) {
				if (it.y + it.height > scrollY && !it.msg->IsLoadGap()) {
					anchor = it.msg->m_snowflake;
					anchorOffset = it.y - scrollY;
					break;
				}
			}
			g_list->Rebuild();
		}
		ApplyTheme();
		g_list->Layout(&g_ctx, g_ctx.px, (int) viewW);

		ImVec2 o = ImGui::GetCursorScreenPos();
		g_ctx.dl = dl;
		g_ctx.origin = o;
		ImGui::Dummy(ImVec2(viewW, (float) g_list->ContentHeight()));
		if (g_frameDirty & App::MESSAGES) {
			if (!atBottom && anchor) {
				for (auto& it : g_list->Items())
					if (it.msg->m_snowflake == anchor)
						ImGui::SetScrollY(it.y - anchorOffset);
			}
		}
		if (atBottom)
			ImGui::SetScrollY((float) g_list->ContentHeight());
		g_stick = atBottom;

		ImVec4 clip(o.x, ImGui::GetWindowPos().y, o.x + viewW, ImGui::GetWindowPos().y + viewH);
		for (auto& it : g_list->Items()) {
			float top = o.y + it.y;
			if (top + it.height < clip.y || top > clip.w)
				continue;
			PaintItem(dl, it, o, viewW, clip);
		}
		g_ctx.dl = nullptr;

		// clicks: links, reactions, pictures; the right button's menu
		if (ImGui::IsWindowHovered()) {
			ImVec2 mp = ImGui::GetMousePos();
			int cx = (int) (mp.x - o.x), cy = (int) (mp.y - o.y);
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				MessageList::Hit hit = g_list->HitTest(cx, cy);
				if (hit.kind == MessageList::Hit::LINK)
					GetFrontend()->LaunchURL(hit.url);
				else if (hit.kind == MessageList::Hit::REACTION && !g_demo) {
					const Reaction& r = hit.message->m_reactions[hit.reaction];
					GetDiscordInstance()->RequestReaction(g_list->GetChannel(), hit.message->m_snowflake, r, !r.m_bMe);
				}
				else if (hit.kind == MessageList::Hit::PICTURE && !hit.picture.url.empty())
					GetFrontend()->LaunchURL(hit.picture.url);
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				MessageList::Item* it = g_list->ItemAt(cy);
				if (it && MessageList::IsActionable(*it->msg)) {
					g_menuMessage = it->msg;
					ImGui::OpenPopup("message menu");
				}
			}
		}
		if (ImGui::BeginPopup("message menu")) {
			if (g_menuMessage) {
				if (ImGui::MenuItem("Reply")) {
					g_composer.BeginReply(g_menuMessage->m_snowflake);
					g_bar = "Replying to " + g_menuMessage->m_author;
				}
				if (g_list->CanEdit(*g_menuMessage) && ImGui::MenuItem("Edit Message")) {
					g_composer.BeginEdit(g_menuMessage->m_snowflake);
					g_bar = "Editing your message";
					snprintf(g_input, sizeof g_input, "%s", g_menuMessage->m_message.c_str());
				}
				if (g_list->CanDelete(*g_menuMessage) && ImGui::MenuItem("Delete Message"))
					g_deleteMessage = g_menuMessage;
			}
			ImGui::EndPopup();
		}

		// the history behind gaps on screen; and read marks
		scrollY = ImGui::GetScrollY();
		if (!g_demo) {
			g_list->RequestGaps((int) scrollY, (int) (scrollY + viewH));
			if (g_list->NewestShown(g_stick)) {
				bool opened = g_justOpened;
				g_justOpened = false;
				if ((opened || g_focused) && g_list->AcknowledgeIfUnread())
					App::MarkDirty(App::LIST_CHANNELS | App::LIST_GUILDS);
			}
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	void MessageBox()
	{
		if (!g_bar.empty()) {
			ImGui::TextDisabled("%s", g_bar.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Cancel")) {
				if (g_composer.Cancel())
					g_input[0] = 0;
				g_bar.clear();
			}
		}
		float sendW = ImGui::CalcTextSize("Send").x + ImGui::GetStyle().FramePadding.x * 2;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - sendW - ImGui::GetStyle().ItemSpacing.x);
		bool enter = ImGui::InputTextMultiline("##message", g_input, sizeof g_input, ImVec2(0, ImGui::GetTextLineHeight() * 3),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
		if (ImGui::IsItemEdited())
			g_composer.TextChanged(g_input[0] == 0);
		ImGui::SameLine();
		bool send = ImGui::Button("Send") || enter;
		if (send && !g_demo) {
			Composer::Result r = g_composer.Send(g_input);
			if (r == Composer::SENT || r == Composer::EDITED) {
				g_input[0] = 0;
				g_bar.clear();
				g_stick = true;
			}
			ImGui::SetKeyboardFocusHere(-1);
		}
		std::string typing = g_list->GetChannel() ? Typing::Text(g_list->GetChannel()) : "";
		const std::string& line = typing.empty() ? g_status : typing;
		ImGui::TextDisabled("%s", line.empty() ? " " : line.c_str());
	}

	// ---- dialogs ---------------------------------------------------------

	void LoginDialog()
	{
		if (!g_loginShown)
			return;
		ImGui::OpenPopup("Log in to Discord");
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		if (!ImGui::BeginPopupModal("Log in to Discord", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		if (!g_loginWhy.empty())
			ImGui::TextWrapped("%s", g_loginWhy.c_str());
		if (!g_tokenMode) {
			ImGui::Text("Log in with a QR code");
			const float area = 300;
			ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(area, area));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(pos, ImVec2(pos.x + area, pos.y + area), Col(0xffffff));
			int n = QrLogin::CodeSize();
			if (n > 0) {
				int scale = std::max(1, (int) area / (n + 8));
				float x0 = pos.x + (area - n * scale) / 2, y0 = pos.y + (area - n * scale) / 2;
				for (int y = 0; y < n; y++)
					for (int x = 0; x < n; x++)
						if (QrLogin::CodeModule(x, y))
							dl->AddRectFilled(ImVec2(x0 + x * scale, y0 + y * scale), ImVec2(x0 + (x + 1) * scale, y0 + (y + 1) * scale), Col(0));
				if (QrLogin::Scanned())
					dl->AddRectFilled(pos, ImVec2(pos.x + area, pos.y + area), IM_COL32(255, 255, 255, 170));
			}
			else {
				const char* text = QrLogin::Failed() ? "No code: see below" : "Preparing a code\xe2\x80\xa6";
				int tw = Gfx::Measure(text, FS_ITALIC, 14);
				TextAt(dl, pos.x + (area - tw) / 2, pos.y + area / 2, text, FS_ITALIC, 14, 0x606060);
			}
			ImGui::TextWrapped("%s", QrLogin::StatusText().c_str());
			ImGui::Separator();
			if (ImGui::Button("Use a Token Instead")) {
				QrLogin::Stop();
				g_tokenMode = true;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!QrLogin::Failed());
			if (ImGui::Button("Try Again"))
				QrLogin::Retry();
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Quit"))
				g_quit = true;
		}
		else {
			ImGui::TextWrapped("The token is the \"authorization\" header of any request a logged-in\n"
				"discord.com page makes (the browser's developer tools, Network).");
			ImGui::SetNextItemWidth(420);
			bool enter = ImGui::InputText("Token", g_token, sizeof g_token, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
			if ((ImGui::Button("Log In") || enter) && g_token[0]) {
				GetLocalSettings()->SetToken(g_token);
				GetLocalSettings()->Save();
				memset(g_token, 0, sizeof g_token);
				g_loginShown = false;
				ImGui::CloseCurrentPopup();
				StartWithToken();
			}
			ImGui::SameLine();
			if (ImGui::Button("Quit"))
				g_quit = true;
		}
		ImGui::EndPopup();
	}

	void Dialogs()
	{
		if (!g_errors.empty()) {
			ImGui::OpenPopup("Discord Messenger");
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopupModal("Discord Messenger", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextWrapped("%s", g_errors.front().c_str());
				if (ImGui::Button("OK")) {
					g_errors.erase(g_errors.begin());
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		if (g_deleteMessage) {
			ImGui::OpenPopup("Delete Message");
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopupModal("Delete Message", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextWrapped("%s", MessageList::DeleteQuestion(*g_deleteMessage).c_str());
				if (ImGui::Button("Delete")) {
					GetDiscordInstance()->RequestDeleteMessage(g_list->GetChannel(), g_deleteMessage->m_snowflake);
					g_deleteMessage.reset();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					g_deleteMessage.reset();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		LoginDialog();
	}

	void Menus()
	{
		if (!ImGui::BeginMenuBar())
			return;
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Reconnect", nullptr, false, !g_demo))
				RequestReconnect();
			if (ImGui::MenuItem("Log Out", nullptr, false, !g_demo))
				RequestLogout();
			ImGui::Separator();
			if (ImGui::MenuItem("Quit"))
				g_quit = true;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			if (ImGui::MenuItem("Larger Text")) {
				SetTextSize(GetTextSize() + 1);
				SaveClientConfig();
				g_list->ForgetLayout();
			}
			if (ImGui::MenuItem("Smaller Text")) {
				SetTextSize(GetTextSize() - 1);
				SaveClientConfig();
				g_list->ForgetLayout();
			}
			ImGui::Separator();
			const char* names[PANE_COUNT] = { "Server List", "Channel List", "Member List" };
			for (int i = 0; i < PANE_COUNT; i++) {
				bool shown = IsPaneShown((Pane) i);
				if (ImGui::MenuItem(names[i], nullptr, &shown)) {
					SetPaneShown((Pane) i, shown);
					SaveClientConfig();
					App::MarkDirty(App::LISTS);
				}
			}
			ImGui::EndMenu();
		}
		if (!g_demo && ImGui::BeginMenu("Messages")) {
			if (ImGui::MenuItem("All Direct Messages"))
				GetDiscordInstance()->OnSelectGuild(0);
			ImGui::Separator();
			for (auto& c : Lists::Conversations(25)) {
				std::string label = c.name.empty() ? std::string("(unnamed)") : c.name;
				if (c.mentions > 0)
					label += "  (" + std::to_string(c.mentions) + ")";
				ImGui::PushID((int) (c.channel & 0x7fffffff));
				if (ImGui::MenuItem(label.c_str()))
					GetDiscordInstance()->OnSelectGuild(0, c.channel);
				ImGui::PopID();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}

void App::Init(bool demo)
{
	g_demo = demo;
	g_list = new MessageList(Gfx::Metrics());
	ApplyTheme();
	if (demo) {
		Demo::LoadMessages();
		g_list->SetChannel(0, Demo::CHANNEL);
		g_status = "Demo: sample messages, not connected.";
	}
	Typing::SetChangedCallback([] {});
}

void App::MarkDirty(int what)
{
	g_dirty |= what;
}

void App::OnChannelChanged()
{
	DiscordInstance* pInst = GetDiscordInstance();
	if (g_composer.Cancel())
		g_input[0] = 0;
	g_bar.clear();
	// remembered for the next start (not before the last one was opened again)
	if (g_restoredLast && pInst->GetCurrentChannelID()) {
		Snowflake g = 0, c = 0;
		GetLastChannel(g, c);
		if (g != pInst->GetCurrentGuildID() || c != pInst->GetCurrentChannelID()) {
			SetLastChannel(pInst->GetCurrentGuildID(), pInst->GetCurrentChannelID());
			SaveClientConfig();
		}
	}
	GetMessageCache()->LoadCachedChannel(pInst->GetCurrentChannelID(), pInst->GetCurrentGuildID());
	g_list->SetChannel(pInst->GetCurrentGuildID(), pInst->GetCurrentChannelID());
	if (pInst->GetCurrentChannel())
		pInst->HandledChannelSwitch();
	g_stick = true;
	g_justOpened = true;
	MarkDirty(LISTS | MESSAGES);
}

void App::RestoreLastChannel()
{
	if (g_restoredLast)
		return;
	g_restoredLast = true;
	DiscordInstance* pInst = GetDiscordInstance();
	Snowflake guild = 0, channel = 0;
	GetLastChannel(guild, channel);
	Guild* pGuild = pInst->GetGuild(guild);
	if (!channel || !pGuild || !pGuild->GetChannel(channel))
		return;
	pInst->OnSelectGuild(guild, channel);
}

void App::SetStatus(const std::string& text)
{
	g_status = text;
}

void App::ShowError(const std::string& text)
{
	g_errors.push_back(text);
}

void App::ShowLogin(const std::string& why)
{
	g_loginWhy = why;
	g_loginShown = true;
	g_tokenMode = false;
	QrLogin::Start([] {}, [](const std::string& token) {
		g_loginShown = false;
		GetLocalSettings()->SetToken(token);
		GetLocalSettings()->Save();
		StartWithToken();
	});
}

bool App::QuitRequested()
{
	return g_quit;
}

void App::Frame(bool windowFocused)
{
	g_focused = windowFocused;
	g_frameDirty = g_dirty;
	g_dirty = 0;
	const Palette& p = GetPalette();
	UpdateLists();

	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::Begin("Discord Messenger", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar(2);
	Menus();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	float h = ImGui::GetContentRegionAvail().y;
	if (IsPaneShown(PANE_GUILDS)) {
		ImGui::BeginChild("guildpane", ImVec2(GUILD_W, h));
		long long g = DrawList("guilds", g_guildRows, g_selGuild, 32, p.guildBg);
		ImGui::EndChild();
		ImGui::SameLine();
		if (g >= 0 && !g_demo)
			GetDiscordInstance()->OnSelectGuild((Snowflake) g);
	}
	if (IsPaneShown(PANE_CHANNELS)) {
		ImGui::BeginChild("channelpane", ImVec2(CHANNEL_W, h));
		long long c = DrawList("channels", g_channelRows, g_selChannel, 20, p.listBg);
		ImGui::EndChild();
		ImGui::SameLine();
		if (c >= 0 && !g_demo)
			GetDiscordInstance()->OnSelectChannel((Snowflake) c);
	}
	float centerW = ImGui::GetContentRegionAvail().x - (IsPaneShown(PANE_MEMBERS) ? MEMBER_W : 0);
	ImGui::BeginChild("center", ImVec2(centerW, h));
	ImGui::PopStyleVar();
	float boxH = ImGui::GetTextLineHeight() * 3 + ImGui::GetStyle().FramePadding.y * 2 + ImGui::GetTextLineHeightWithSpacing() * (g_bar.empty() ? 1 : 2) + 12;
	MessageView(ImGui::GetContentRegionAvail().y - boxH);
	ImGui::Dummy(ImVec2(0, 4));
	ImGui::Indent(8);
	MessageBox();
	ImGui::Unindent(8);
	ImGui::EndChild();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	if (IsPaneShown(PANE_MEMBERS)) {
		ImGui::SameLine();
		ImGui::BeginChild("memberpane", ImVec2(MEMBER_W, h));
		DrawList("members", g_memberRows, 0, 24, p.listBg);
		ImGui::EndChild();
	}
	ImGui::PopStyleVar();

	Dialogs();
	ImGui::End();
	Gfx::CollectTextures();
}
