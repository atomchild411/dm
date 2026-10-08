#pragma once

#include <string>

// The faces text is set in, and the measurements the shared layout needs
// from a front end's fonts.
enum FontStyle
{
	FS_REGULAR,
	FS_BOLD,
	FS_ITALIC,
	FS_BOLDITALIC,
	FS_MONO,
	FS_MONOBOLD,
	FS_COUNT
};

struct TextMetrics
{
	virtual ~TextMetrics() {}
	virtual int Ascent(FontStyle st, int px) = 0;
	virtual int Descent(FontStyle st, int px) = 0;
	int LineHeight(FontStyle st, int px) { return Ascent(st, px) + Descent(st, px); }
	// Width of UTF-8 text on one line.
	virtual int Measure(const std::string& s, FontStyle st, int px) = 0;
};
