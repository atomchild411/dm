// The formatted text's drawing interface (text/TextInterface) on an ImGui
// draw list; the measuring is shared/TextLayout's.

#include <algorithm>
#include <vector>

#include "DrawingContext.hpp"
#include "Gfx.hpp"
#include "text/FormattedText.hpp"
#include "text/TextInterface.hpp"
#include "shared/ImageCache.hpp"
#include "shared/TextLayout.hpp"

static uint32_t Mix(uint32_t a, uint32_t b, int num, int den)
{
	uint32_t out = 0;
	for (int sh = 0; sh <= 16; sh += 8) {
		int ca = (a >> sh) & 0xff, cb = (b >> sh) & 0xff;
		out |= (uint32_t) (ca + (cb - ca) * num / den) << sh;
	}
	return out;
}

static ImVec2 At(DrawingContext* ctx, int x, int y)
{
	return ImVec2(ctx->origin.x + x, ctx->origin.y + y);
}

Point MdMeasureString(DrawingContext* ctx, const String& word, int styleFlags, bool& outWasWordWrapped, int maxWidth)
{
	return TextLayout::MeasureWord(Gfx::Metrics(), ctx->px, word.GetWrapped(), styleFlags, outWasWordWrapped, maxWidth);
}

int MdLineHeight(DrawingContext* ctx, int styleFlags)
{
	return TextLayout::LineHeight(Gfx::Metrics(), ctx->px, styleFlags);
}

int MdSpaceWidth(DrawingContext* ctx, int styleFlags)
{
	return TextLayout::SpaceWidth(Gfx::Metrics(), ctx->px, styleFlags);
}

void MdDrawString(DrawingContext* ctx, const Rect& rect, const String& str, int styleFlags)
{
	if (!ctx->dl)
		return;
	ImDrawList* dl = ctx->dl;
	const std::string& s = str.GetWrapped();

	if (styleFlags & WORD_CEMOJI) {
		// <:name:id> or <a:name:id>: the id follows the second colon
		Snowflake id = 0;
		int colons = 0;
		for (char ch : s) {
			if (ch == ':')
				colons++;
			else if (colons == 2 && ch >= '0' && ch <= '9')
				id = id * 10 + (Snowflake) (ch - '0');
		}
		int h = rect.Height();
		const Image* img = id ? ImageCache::Get(ImageCache::EMOJI, "", id, h, h) : nullptr;
		if (img) {
			ImVec2 p = At(ctx, rect.left + (h - img->w) / 2, rect.top + (h - img->h) / 2);
			Gfx::DrawImage(dl, *img, p.x, p.y);
			return;
		}
		// until it arrives: a box with the emoji name's initial
		dl->AddRectFilled(At(ctx, rect.left, rect.top), At(ctx, rect.left + h, rect.top + h), Col(Mix(ctx->bg, ctx->muted, 1, 3)), 4.0f);
		size_t a = s.find(':'), b = s.find(':', a + 1);
		std::string name = (a != std::string::npos && b != std::string::npos) ? s.substr(a + 1, b - a - 1) : "?";
		if (!name.empty())
			Gfx::Text(dl, At(ctx, rect.left + 2, rect.top + h / 4), std::string(name, 0, 1), FS_BOLD, h / 2, ctx->fg);
		return;
	}

	FontStyle st;
	int px;
	TextLayout::StyleFor(ctx->px, styleFlags, st, px);
	int asc = Gfx::Ascent(st, px);
	int lh = Gfx::LineHeight(st, px);

	uint32_t color = ctx->fg;
	if (styleFlags & WORD_LINK)
		color = ctx->link;
	if (styleFlags & (WORD_MENTION | WORD_EVERYONE)) {
		color = ctx->mention;
		dl->AddRectFilled(At(ctx, rect.left - 1, rect.top), At(ctx, rect.right + 1, rect.bottom), Col(Mix(ctx->bg, ctx->mention, 15, 100)), 3.0f);
	}
	if (styleFlags & WORD_SMALLER)
		color = ctx->muted;
	if ((styleFlags & WORD_CODE) && !(styleFlags & WORD_MLCODE))
		dl->AddRectFilled(At(ctx, rect.left - 1, rect.top), At(ctx, rect.right + 1, rect.bottom), Col(ctx->codeBg), 3.0f);

	if ((styleFlags & (WORD_AFNEWLINE | WORD_QUOTE)) == (WORD_AFNEWLINE | WORD_QUOTE))
		dl->AddRectFilled(At(ctx, rect.left - TextLayout::QUOTE_INDENT, rect.top), At(ctx, rect.left - TextLayout::QUOTE_INDENT + 3, rect.bottom), Col(ctx->quoteBar));

	Rect rc = rect;
	if (styleFlags & WORD_MLCODE) {
		rc.left += TextLayout::CODE_PAD;
		rc.top += TextLayout::CODE_PAD;
		rc.right -= TextLayout::CODE_PAD;
		rc.bottom -= TextLayout::CODE_PAD;
	}

	bool block = (styleFlags & (WORD_MLCODE | WORD_NOFORMAT)) != 0;
	std::vector<std::string> lines;
	bool wrapped;
	TextLayout::WrapLines(Gfx::Metrics(), s, st, px, block ? rc.Width() : 0, lines, wrapped);

	int y = rc.top;
	for (auto& l : lines) {
		int w = Gfx::Text(dl, At(ctx, rc.left, y), l, st, px, color);
		if (styleFlags & (WORD_UNDERL | WORD_LINK))
			dl->AddLine(At(ctx, rc.left, y + asc + 2), At(ctx, rc.left + w, y + asc + 2), Col(color));
		if (styleFlags & WORD_STRIKE) {
			// through the middle of the lower-case letters
			int mid = y + asc - asc * 3 / 10;
			dl->AddLine(At(ctx, rc.left, mid), At(ctx, rc.left + w, mid), Col(color), (float) std::max(1, px / 14));
		}
		y += lh;
	}
}

void MdDrawCodeBackground(DrawingContext* ctx, const Rect& rect)
{
	if (!ctx->dl)
		return;
	ctx->dl->AddRectFilled(At(ctx, rect.left, rect.top), At(ctx, rect.right, rect.bottom), Col(ctx->codeBg), 4.0f);
	ctx->dl->AddRect(At(ctx, rect.left, rect.top), At(ctx, rect.right, rect.bottom), Col(ctx->codeFrame), 4.0f);
}

void MdDrawForwardBackground(DrawingContext* ctx, const Rect& rect)
{
	if (!ctx->dl)
		return;
	ctx->dl->AddRectFilled(At(ctx, rect.left - 6, rect.top), At(ctx, rect.left - 3, rect.bottom), Col(ctx->quoteBar));
}

int MdGetQuoteIndentSize()
{
	return TextLayout::QUOTE_INDENT;
}

void MdSetClippingRect(DrawingContext* ctx, const Rect& rect)
{
	if (ctx->dl)
		ctx->dl->PushClipRect(At(ctx, rect.left, rect.top), At(ctx, rect.right, rect.bottom), true);
}

void MdClearClippingRect(DrawingContext* ctx)
{
	if (ctx->dl)
		ctx->dl->PopClipRect();
}
