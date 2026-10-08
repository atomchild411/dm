#pragma once

#include <functional>
#include <string>

#include "Xm.hpp"
#include "Canvas.hpp"
#include "models/Message.hpp"

// An emoji picker (Add Reaction, Insert Emoji): common emoji, then the
// server's own, by (x, y on the screen).  picked(r) with the one clicked: a
// Unicode emoji in r.m_emojiName, or a custom one's id and name.  Close or
// Escape shuts it.
namespace ReactionPicker
{
	void Show(Widget parent, const PixelFormat& fmt, const char* title, int x, int y, Snowflake guild,
		std::function<void(const Reaction&)> picked);

	// Images arrived: the server's emoji drawn as they come.
	void ImagesChanged();
}
