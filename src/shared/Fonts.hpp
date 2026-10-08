#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "TextStyle.hpp"

// Text set with FreeType: DejaVu in its styles, fallback faces for what it
// lacks, and Noto Color Emoji with its ligatures (flags, skin tones,
// keycaps, ZWJ sequences).  The front ends draw the glyphs it renders (the
// X server's fonts have neither Unicode coverage nor antialiasing on most
// SGI displays; a GPU front end puts them in a texture).  UI thread.
namespace Fonts
{
	// Loads the faces from DM_FONT_DIR, the installed fonts directory, or
	// pkgsrc's TrueType directory.  On failure, err says what is missing.
	bool Init(std::string& err);

	int Ascent(FontStyle st, int px);
	int Descent(FontStyle st, int px);
	inline int LineHeight(FontStyle st, int px) { return Ascent(st, px) + Descent(st, px); }

	// Width of UTF-8 text on one line.
	int Measure(const std::string& s, FontStyle st, int px);
	int Measure(const char* s, size_t n, FontStyle st, int px);

	// How many bytes of s fit in maxWidth, broken after a space when one is
	// there (at least one character).
	size_t FitBytes(const char* s, size_t n, FontStyle st, int px, int maxWidth);

	// Truncates text to maxWidth with an ellipsis.
	std::string Elide(const std::string& s, FontStyle st, int px, int maxWidth);

	// One glyph of a line of text: its pen position from the line's start,
	// its bitmap's place from the pen on the baseline (left, and top above
	// the baseline), and its pixels: coverage to tint with the text's
	// colour, or colour pixels (ARGB, straight alpha) for emoji.  The
	// pixels stay valid while the program runs.
	struct PlacedGlyph
	{
		int x, left, top, w, h, advance;
		const uint8_t* coverage;
		const uint32_t* argb;
		const char* start;   // the text it draws
		const char* end;
	};
	// The glyphs of UTF-8 text on one line (valid until the next call).
	const std::vector<PlacedGlyph>& Glyphs(const char* s, size_t n, FontStyle st, int px);

	// These fonts' measurements, for the shared layout.
	TextMetrics& Metrics();
}
