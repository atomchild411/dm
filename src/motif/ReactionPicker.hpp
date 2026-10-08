#pragma once

#include <functional>
#include <string>

#include "Xm.hpp"
#include "Canvas.hpp"

// "Add Reaction": a grid of common emoji by the pointer (x, y on the
// screen); picked(emoji) with the one clicked.  Close or Escape shuts it.
namespace ReactionPicker
{
	void Show(Widget parent, const PixelFormat& fmt, int x, int y, std::function<void(const std::string&)> picked);
}
