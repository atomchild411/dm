#include "TextLayout.hpp"

#include <algorithm>

#include "text/FormattedText.hpp"

void TextLayout::StyleFor(int basePx, int f, FontStyle& st, int& px)
{
	px = basePx;
	if (f & (WORD_CODE | WORD_MLCODE)) {
		st = (f & WORD_STRONG) ? FS_MONOBOLD : FS_MONO;
		px = basePx - 1;
		return;
	}

	bool bold = (f & (WORD_STRONG | WORD_HEADER1 | WORD_HEADER2)) != 0;
	bool italic = (f & (WORD_ITALIC | WORD_ITALIE)) != 0;
	st = bold ? (italic ? FS_BOLDITALIC : FS_BOLD) : (italic ? FS_ITALIC : FS_REGULAR);

	if (f & WORD_HEADER1)
		px = basePx * 3 / 2 + 2;
	else if (f & WORD_HEADER2)
		px = basePx * 5 / 4 + 1;
	else if (f & WORD_SMALLER)
		px = basePx - 3;
}

void TextLayout::WrapLines(TextMetrics& m, const std::string& s, FontStyle st, int px, int maxWidth,
	std::vector<std::string>& lines, bool& wrapped)
{
	wrapped = false;
	size_t pos = 0;
	for (;;)
	{
		size_t nl = s.find('\n', pos);
		std::string para = s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		if (maxWidth <= 0) {
			lines.push_back(para);
		}
		else {
			size_t off = 0;
			do {
				size_t n = m.FitBytes(para.data() + off, para.size() - off, st, px, maxWidth);
				if (n < para.size() - off)
					wrapped = true;
				lines.push_back(para.substr(off, n));
				off += n;
			} while (off < para.size());
		}
		if (nl == std::string::npos)
			break;
		pos = nl + 1;
	}
}

Point TextLayout::MeasureWord(TextMetrics& m, int basePx, const std::string& s, int styleFlags, bool& outWasWordWrapped, int maxWidth)
{
	outWasWordWrapped = false;

	if (styleFlags & WORD_CEMOJI) {
		int h = LineHeight(m, basePx, styleFlags);
		return Point(h, h);
	}

	FontStyle st;
	int px;
	StyleFor(basePx, styleFlags, st, px);
	int lh = m.LineHeight(st, px);

	bool block = (styleFlags & (WORD_MLCODE | WORD_NOFORMAT)) != 0;
	int pad = (styleFlags & WORD_MLCODE) ? 2 * CODE_PAD : 0;

	std::vector<std::string> lines;
	WrapLines(m, s, st, px, block && maxWidth > 0 ? maxWidth - pad : 0, lines, outWasWordWrapped);

	int w = 0;
	for (auto& l : lines)
		w = std::max(w, m.Measure(l, st, px));

	// a code block spans the whole width it was given
	if ((styleFlags & WORD_MLCODE) && maxWidth > 0)
		w = maxWidth - pad;

	return Point(w + pad, (int) lines.size() * lh + pad);
}

int TextLayout::LineHeight(TextMetrics& m, int basePx, int styleFlags)
{
	FontStyle st;
	int px;
	StyleFor(basePx, styleFlags, st, px);
	return m.LineHeight(st, px) + 2;
}

int TextLayout::SpaceWidth(TextMetrics& m, int basePx, int styleFlags)
{
	FontStyle st;
	int px;
	StyleFor(basePx, styleFlags, st, px);
	return m.Measure(" ", st, px);
}
