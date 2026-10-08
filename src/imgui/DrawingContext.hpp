#pragma once

#include <cstdint>

#include "imgui.h"

// What the formatted text (text/FormattedText) draws with: an ImGui draw
// list, where content (0, 0) is on the screen, the body text size and the
// colours.
struct DrawingContext
{
	ImDrawList* dl = nullptr;
	ImVec2 origin;          // screen position of content (0, 0)
	int px = 14;            // body text size in pixels
	uint32_t fg = 0xdbdee1;
	uint32_t bg = 0x313338;
	uint32_t link = 0x00a8fc;
	uint32_t mention = 0xc9cdfb;
	uint32_t codeBg = 0x2b2d31;
	uint32_t codeFrame = 0x1e1f22;
	uint32_t quoteBar = 0x4e5058;
	uint32_t muted = 0x949ba4;
};
