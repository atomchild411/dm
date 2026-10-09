#include "Ui.hpp"

#include <algorithm>

#include "shared/AppIcon.hpp"
#include "shared/ImageCache.hpp"
#include "shared/Utf8.hpp"

namespace Ui
{
	bool demo = false;
	bool quit = false;
	int frameDirty = 0;
	bool focused = true;

	std::vector<ListRow> guildRows, channelRows, memberRows;
	Snowflake selGuild = 0, selChannel = 0;

	MessageList* list = nullptr;
	DrawingContext ctx;
	Composer composer;
	char input[4000];
	std::string bar;
	bool stick = true;
	bool justOpened = false;
	Snowflake unreadAfter = 0;
	bool focusInput = false;
	std::string status;

	std::vector<std::string> errors;
	MessagePtr menuMessage, deleteMessage;
}

uint32_t Ui::Mix(uint32_t a, uint32_t b, int num, int den)
{
	uint32_t out = 0;
	for (int sh = 0; sh <= 16; sh += 8) {
		int ca = (a >> sh) & 0xff, cb = (b >> sh) & 0xff;
		out |= (uint32_t) (ca + (cb - ca) * num / den) << sh;
	}
	return out;
}

uint32_t Ui::AvatarColor(Snowflake sf)
{
	static const uint32_t colors[] = { 0x5865f2, 0x3ba55c, 0xfaa61a, 0xed4245, 0xeb459e, 0x747f8d, 0x2d8f9e, 0x9b59b6 };
	return colors[(sf >> 22) % (sizeof colors / sizeof colors[0])];
}

const Message* Ui::FindListed(Snowflake id)
{
	if (!id || !list)
		return nullptr;
	for (auto& it : list->Items())
		if (it.msg && it.msg->m_snowflake == id)
			return it.msg.get();
	return nullptr;
}

std::string Ui::Initials(const std::string& text)
{
	const char* p = text.c_str();
	const char* end = p + text.size();
	std::string out;
	bool start = true;
	int n = 0;
	while (p < end && n < 3) {
		const char* q = p;
		unsigned cp = DecodeUtf8(q, end);
		if (cp == ' ')
			start = true;
		else if (start) {
			out.append(p, q);
			start = false;
			n++;
		}
		p = q;
	}
	return out;
}

int Ui::TextAt(ImDrawList* dl, float x, float y, const std::string& s, FontStyle st, int px, uint32_t color)
{
	return Gfx::Text(dl, ImVec2(x, y - Gfx::Ascent(st, px)), s, st, px, color);
}

int Ui::TextMid(ImDrawList* dl, float x, float cy, const std::string& s, FontStyle st, int px, uint32_t color)
{
	return Gfx::Text(dl, ImVec2(x, cy - Gfx::LineHeight(st, px) / 2.0f), s, st, px, color);
}

void Ui::Fill(ImDrawList* dl, float x, float y, float w, float h, uint32_t color, float rounding)
{
	dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), Col(color), rounding);
}

void Ui::PresenceDot(ImDrawList* dl, float cx, float cy, int status, uint32_t bg, float r)
{
	if (status < 0)
		return;
	const Palette& p = GetPalette();
	uint32_t c = status == 1 ? p.online : status == 2 ? p.idle : status == 3 ? p.dnd : p.offline;
	dl->AddCircleFilled(ImVec2(cx, cy), r + 2.5f, Col(bg));
	dl->AddCircleFilled(ImVec2(cx, cy), r, Col(c));
	if (status == 0) // offline: a ring
		dl->AddCircleFilled(ImVec2(cx, cy), r * 0.45f, Col(bg));
}

void Ui::Avatar(ImDrawList* dl, float x, float y, int size, const ListRow& r, uint32_t bg)
{
	const Image* img = r.hasImage ? ImageCache::Get(r.imageKind, r.imagePlace, r.imageSf, size, size) : nullptr;
	if (img)
		Gfx::DrawImageCircle(dl, *img, x + (size - img->w) / 2.0f, y + (size - img->h) / 2.0f);
	else {
		Snowflake seed = r.colorSeed ? r.colorSeed : r.id;
		dl->AddCircleFilled(ImVec2(x + size / 2.0f, y + size / 2.0f), size / 2.0f, Col(AvatarColor(seed)));
		std::string ini = Initials(r.text);
		int ipx = std::max(9, size * 2 / 5);
		int iw = Gfx::Measure(ini, FS_BOLD, ipx);
		TextMid(dl, x + (size - iw) / 2.0f, y + size / 2.0f, ini, FS_BOLD, ipx, ON_ACCENT);
	}
	if (r.status >= 0)
		PresenceDot(dl, x + size - size * 0.15f, y + size - size * 0.15f, r.status, bg, size >= 32 ? 5.0f : 4.0f);
}

void Ui::UserAvatar(ImDrawList* dl, float x, float y, int size, Snowflake user, const std::string& avatar,
	const std::string& name, int status, uint32_t bg)
{
	ListRow r;
	r.text = name;
	r.hasImage = true;
	r.imageKind = avatar.empty() ? ImageCache::DEFAULT_AVATAR : ImageCache::AVATAR;
	r.imagePlace = avatar;
	r.imageSf = user;
	r.colorSeed = user;
	r.status = status;
	Avatar(dl, x, y, size, r, bg);
}

void Ui::Badge(ImDrawList* dl, float cx, float cy, int count, uint32_t border)
{
	std::string s = count > 99 ? "99+" : std::to_string(count);
	int px = 12;
	float w = std::max(16.0f, (float) Gfx::Measure(s, FS_BOLD, px) + 9);
	float h = 16;
	dl->AddRectFilled(ImVec2(cx - w / 2 - 3, cy - h / 2 - 3), ImVec2(cx + w / 2 + 3, cy + h / 2 + 3), Col(border), h);
	dl->AddRectFilled(ImVec2(cx - w / 2, cy - h / 2), ImVec2(cx + w / 2, cy + h / 2), Col(RED), h);
	int tw = Gfx::Measure(s, FS_BOLD, px);
	TextMid(dl, cx - tw / 2.0f, cy, s, FS_BOLD, px, ON_ACCENT);
}

const Image& Ui::AppIconImage()
{
	static Image img;
	if (img.px.empty()) {
		img.w = img.h = 64;
		img.px.assign(g_appIcon, g_appIcon + 64 * 64);
		img.serial = 0xffffffffffff0001ULL; // not one the image cache makes
	}
	return img;
}
