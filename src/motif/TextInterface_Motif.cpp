#include "TextInterface_Motif.hpp"

#include <algorithm>
#include <vector>

#include "text/FormattedText.hpp"
#include "text/TextInterface.hpp"
#include "shared/ImageCache.hpp"
#include "shared/TextLayout.hpp"

static const int CODE_PAD = TextLayout::CODE_PAD;
static const int QUOTE_INDENT = TextLayout::QUOTE_INDENT;

void MdFontFor(const DrawingContext* ctx, int f, FontStyle& st, int& px)
{
	TextLayout::StyleFor(ctx->px, f, st, px);
}

static void WrapLines(const std::string& s, FontStyle st, int px, int maxWidth, std::vector<std::string>& lines, bool& wrapped)
{
	TextLayout::WrapLines(Fonts::Metrics(), s, st, px, maxWidth, lines, wrapped);
}

Point MdMeasureString(DrawingContext* ctx, const String& word, int styleFlags, bool& outWasWordWrapped, int maxWidth)
{
	return TextLayout::MeasureWord(Fonts::Metrics(), ctx->px, word.GetWrapped(), styleFlags, outWasWordWrapped, maxWidth);
}

int MdLineHeight(DrawingContext* ctx, int styleFlags)
{
	return TextLayout::LineHeight(Fonts::Metrics(), ctx->px, styleFlags);
}

int MdSpaceWidth(DrawingContext* ctx, int styleFlags)
{
	return TextLayout::SpaceWidth(Fonts::Metrics(), ctx->px, styleFlags);
}

void MdDrawString(DrawingContext* ctx, const Rect& rect, const String& str, int styleFlags)
{
	if (!ctx->canvas)
		return;
	Canvas& c = *ctx->canvas;
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
			c.BlendArgb(rect.left + (h - img->w) / 2, rect.top + (h - img->h) / 2, img->px.data(), img->w, img->h, img->w);
			return;
		}
		// until it arrives: a box with the emoji name's initial
		c.FillRounded(rect.left, rect.top, h, h, 4, LerpRgb(ctx->bg, ctx->muted, 1, 3));
		size_t a = s.find(':'), b = s.find(':', a + 1);
		std::string name = (a != std::string::npos && b != std::string::npos) ? s.substr(a + 1, b - a - 1) : "?";
		if (!name.empty())
			Fonts::Draw(c, rect.left + 2, rect.top + h * 3 / 4, std::string(name, 0, 1), FS_BOLD, h / 2, ctx->fg);
		return;
	}

	FontStyle st;
	int px;
	MdFontFor(ctx, styleFlags, st, px);
	int asc = Fonts::Ascent(st, px);
	int lh = Fonts::LineHeight(st, px);

	Rgb color = ctx->fg;
	if (styleFlags & WORD_LINK)
		color = ctx->link;
	if (styleFlags & (WORD_MENTION | WORD_EVERYONE)) {
		color = ctx->mention;
		c.FillRounded(rect.left - 1, rect.top, rect.Width() + 2, rect.Height(), 3, LerpRgb(ctx->bg, ctx->mention, 15, 100));
	}
	if (styleFlags & WORD_SMALLER)
		color = ctx->muted;
	if ((styleFlags & WORD_CODE) && !(styleFlags & WORD_MLCODE))
		c.FillRounded(rect.left - 1, rect.top, rect.Width() + 2, rect.Height(), 3, ctx->codeBg);

	if ((styleFlags & (WORD_AFNEWLINE | WORD_QUOTE)) == (WORD_AFNEWLINE | WORD_QUOTE))
		c.Fill(rect.left - QUOTE_INDENT, rect.top, 3, rect.Height(), ctx->quoteBar);

	Rect rc = rect;
	if (styleFlags & WORD_MLCODE) {
		rc.left += CODE_PAD;
		rc.top += CODE_PAD;
		rc.right -= CODE_PAD;
		rc.bottom -= CODE_PAD;
	}

	bool block = (styleFlags & (WORD_MLCODE | WORD_NOFORMAT)) != 0;
	std::vector<std::string> lines;
	bool wrapped;
	WrapLines(s, st, px, block ? rc.Width() : 0, lines, wrapped);

	int y = rc.top;
	for (auto& l : lines) {
		int w = Fonts::Draw(c, rc.left, y + asc, l, st, px, color);
		if (styleFlags & (WORD_UNDERL | WORD_LINK))
			c.HLine(rc.left, y + asc + 2, w, color);
		if (styleFlags & WORD_STRIKE) {
			// through the middle of the lower-case letters, thicker for
			// larger text
			int mid = y + asc - asc * 3 / 10;
			for (int t = 0; t < std::max(1, px / 14); t++)
				c.HLine(rc.left, mid + t, w, color);
		}
		y += lh;
	}
}

void MdDrawCodeBackground(DrawingContext* ctx, const Rect& rect)
{
	if (!ctx->canvas)
		return;
	ctx->canvas->Fill(rect.left, rect.top, rect.Width(), rect.Height(), ctx->codeBg);
	ctx->canvas->Frame(rect.left, rect.top, rect.Width(), rect.Height(), ctx->codeFrame);
}

void MdDrawForwardBackground(DrawingContext* ctx, const Rect& rect)
{
	if (!ctx->canvas)
		return;
	ctx->canvas->Fill(rect.left - 6, rect.top, 3, rect.Height(), ctx->quoteBar);
}

int MdGetQuoteIndentSize()
{
	return QUOTE_INDENT;
}

void MdSetClippingRect(DrawingContext* ctx, const Rect& rect)
{
	if (ctx->canvas)
		ctx->canvas->SetClip(rect.left, rect.top, rect.Width(), rect.Height());
}

void MdClearClippingRect(DrawingContext* ctx)
{
	if (ctx->canvas)
		ctx->canvas->ClearClip();
}
