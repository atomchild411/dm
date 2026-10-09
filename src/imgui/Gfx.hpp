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

// Text in the drawn parts comes from shared/Fonts (FreeType), as in the
// Motif client: its glyphs go into textures here.  ImGui's own widgets
// (menus, buttons, the message box) use ImGui's font: Inter, as the text.
namespace Gfx
{
	// The palette GetPalette gives: Discord's dark theme's, or its light one's.
	void SetPaletteDark(bool dark);

	// ImGui's font for its widgets; shared/Fonts must be loaded first.
	bool LoadUiFont(std::string& err);

	TextMetrics& Metrics();
	int Ascent(FontStyle st, int px);
	int LineHeight(FontStyle st, int px);
	int Measure(const std::string& s, FontStyle st, int px);
	// Truncated to maxWidth with an ellipsis.
	std::string Elide(const std::string& s, FontStyle st, int px, int maxWidth);
	// Draws text with its top-left at pos; returns the width.
	int Text(ImDrawList* dl, ImVec2 pos, const std::string& s, FontStyle st, int px, uint32_t color);

	// The texture for a cached image (uploaded on first use, replaced when
	// the image is), and the part of it the image takes (OpenGL 1.1 wants
	// textures a power of two in size).
	struct TexRef { ImTextureID id; ImVec2 uv0, uv1; };
	TexRef Texture(const Image& img);
	// Textures of images that are gone (once a frame).
	void CollectTextures();

	// An image (from the cache) in a rectangle, or with rounded corners.
	void AddImage(ImDrawList* dl, const ::Image& img, ImVec2 p0, ImVec2 p1);
	void AddImageRounded(ImDrawList* dl, const ::Image& img, ImVec2 p0, ImVec2 p1, float rounding);
	// An image at (x, y), or the image clipped to a circle.
	void DrawImage(ImDrawList* dl, const ::Image& img, float x, float y);
	void DrawImageCircle(ImDrawList* dl, const ::Image& img, float x, float y);
}
