#pragma once

#include <string>
#include "Canvas.hpp"
#include "shared/Fonts.hpp"
#include "shared/Utf8.hpp"

// shared/Fonts' text, drawn into a Canvas.
namespace Fonts
{
	// Draws UTF-8 text with its baseline at y; returns the advance.
	int Draw(Canvas& c, int x, int y, const std::string& s, FontStyle st, int px, Rgb color);
	int Draw(Canvas& c, int x, int y, const char* s, size_t n, FontStyle st, int px, Rgb color);
}
