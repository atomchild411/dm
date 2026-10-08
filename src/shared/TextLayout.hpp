#pragma once

#include <string>
#include <vector>

#include "models/RectAndPoint.hpp"
#include "TextStyle.hpp"

// The measuring half of the formatted-text interface (text/TextInterface):
// which face and size a word's style flags (WORD_*) ask for, and how big
// a word, a line or a space is, with a front end's font measurements.
// The front ends' Md* functions call these and do the drawing themselves.
namespace TextLayout
{
	const int CODE_PAD = 4;      // inside multi-line code blocks
	const int QUOTE_INDENT = 12;

	// The face and size for style flags, the body text being basePx.
	void StyleFor(int basePx, int flags, FontStyle& st, int& px);

	// Text broken into lines: at newlines, and to fit maxWidth when it is > 0.
	void WrapLines(TextMetrics& m, const std::string& s, FontStyle st, int px, int maxWidth,
		std::vector<std::string>& lines, bool& wrapped);

	// MdMeasureString, MdLineHeight and MdSpaceWidth.
	Point MeasureWord(TextMetrics& m, int basePx, const std::string& s, int flags, bool& wrapped, int maxWidth);
	int LineHeight(TextMetrics& m, int basePx, int flags);
	int SpaceWidth(TextMetrics& m, int basePx, int flags);
}
