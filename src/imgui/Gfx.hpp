#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"
#include "shared/ImageCache.hpp"
#include "shared/TextStyle.hpp"

// Colours (0xRRGGBB) as ImGui wants them.
inline ImU32 Col(uint32_t rgb, int alpha = 255)
{
	return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, alpha);
}

// The palette the drawn parts use (the ImGui widgets keep ImGui's style).
struct Palette
{
	uint32_t guildBg, listBg, listFg, listMuted, listHeader;
	uint32_t selBg, selFg;
	uint32_t badge, badgeFg;
	uint32_t msgBg, msgFg, msgMuted, link, mention, codeBg, codeFrame, quoteBar;
	uint32_t online, idle, dnd, offline;
};
const Palette& GetPalette();

// The text faces (DejaVu, with Noto Color Emoji merged into each), loaded
// with FreeType from DM_FONT_DIR, the installed fonts directory, or the
// system's.
namespace Gfx
{
	bool LoadFonts(std::string& err);
	ImFont* Font(FontStyle st);

	// The faces' measurements, for the shared layout.
	TextMetrics& Metrics();

	int Ascent(FontStyle st, int px);
	int LineHeight(FontStyle st, int px);
	int Measure(const std::string& s, FontStyle st, int px);
	// Truncated to maxWidth with an ellipsis.
	std::string Elide(const std::string& s, FontStyle st, int px, int maxWidth);
	// Draws text with its top-left at pos; returns the width.
	int Text(ImDrawList* dl, ImVec2 pos, const std::string& s, FontStyle st, int px, uint32_t color);

	// The texture for a cached image (uploaded on first use, replaced when
	// the image is).
	ImTextureID Texture(const Image& img);
	// Textures of images that are gone (once a frame).
	void CollectTextures();

	// An image (from the cache) at (x, y), or the image clipped to a circle.
	void DrawImage(ImDrawList* dl, const Image& img, float x, float y);
	void DrawImageCircle(ImDrawList* dl, const Image& img, float x, float y);
}
