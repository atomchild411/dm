#include "Fonts.hpp"

int Fonts::Draw(Canvas& c, int x, int y, const char* s, size_t n, FontStyle st, int px, Rgb color)
{
	int x0 = x;
	for (auto& g : Glyphs(s, n, st, px))
	{
		if (g.argb)
			c.BlendArgb(x0 + g.x + g.left, y - g.top, g.argb, g.w, g.h, g.w);
		else if (g.coverage)
			c.BlendMask(x0 + g.x + g.left, y - g.top, g.coverage, g.w, g.h, g.w, color);
		x = x0 + g.x + g.advance;
	}
	return x - x0;
}

int Fonts::Draw(Canvas& c, int x, int y, const std::string& s, FontStyle st, int px, Rgb color)
{
	return Draw(c, x, y, s.data(), s.size(), st, px, color);
}
