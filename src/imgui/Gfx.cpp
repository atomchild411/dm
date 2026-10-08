#include "Gfx.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sys/stat.h>
#include <vector>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include "shared/Fonts.hpp"

#ifndef DM_DATADIR
#define DM_DATADIR "/usr/local/share/discord-messenger"
#endif

namespace
{
	Palette g_palette = {
		0x1e1f22, 0x2b2d31, 0xdbdee1, 0x949ba4, 0x949ba4,
		0x404249, 0xffffff,
		0xf23f42, 0xffffff,
		0x313338, 0xdbdee1, 0x949ba4, 0x00a8fc, 0xc9cdfb, 0x2b2d31, 0x1e1f22, 0x4e5058,
		0x23a55a, 0xf0b232, 0xf23f43, 0x80848e,
	};

	bool Exists(const std::string& path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0;
	}

	GLuint NewTexture(int w, int h, const void* rgba)
	{
		GLuint id = 0;
		glGenTextures(1, &id);
		glBindTexture(GL_TEXTURE_2D, id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		return id;
	}

	// ---- the glyph atlas: shared/Fonts' glyphs, packed in rows --------------

	const int ATLAS = 1024;
	struct AtlasSlot { GLuint tex; float u0, v0, u1, v1; };
	struct Atlas { GLuint tex = 0; int x = 1, y = 1, rowH = 0; };
	std::vector<Atlas> g_atlases;
	std::map<const void*, AtlasSlot> g_slots; // the glyph's pixels -> its place

	Atlas& NewAtlas()
	{
		std::vector<uint8_t> empty((size_t) ATLAS * ATLAS * 4, 0);
		Atlas a;
		a.tex = NewTexture(ATLAS, ATLAS, empty.data());
		g_atlases.push_back(a);
		return g_atlases.back();
	}

	const AtlasSlot& Slot(const Fonts::PlacedGlyph& g)
	{
		const void* key = g.argb ? (const void*) g.argb : (const void*) g.coverage;
		auto it = g_slots.find(key);
		if (it != g_slots.end())
			return it->second;

		Atlas* a = g_atlases.empty() ? &NewAtlas() : &g_atlases.back();
		if (a->x + g.w + 1 > ATLAS) {
			a->x = 1;
			a->y += a->rowH + 1;
			a->rowH = 0;
		}
		if (a->y + g.h + 1 > ATLAS)
			a = &NewAtlas();

		// coverage as white with that alpha (tinted when drawn); colour as is
		std::vector<uint8_t> rgba((size_t) g.w * g.h * 4);
		for (int i = 0; i < g.w * g.h; i++) {
			if (g.argb) {
				uint32_t p = g.argb[i];
				rgba[i * 4 + 0] = (p >> 16) & 0xff;
				rgba[i * 4 + 1] = (p >> 8) & 0xff;
				rgba[i * 4 + 2] = p & 0xff;
				rgba[i * 4 + 3] = (p >> 24) & 0xff;
			}
			else {
				rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
				rgba[i * 4 + 3] = g.coverage[i];
			}
		}
		glBindTexture(GL_TEXTURE_2D, a->tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, a->x, a->y, g.w, g.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		AtlasSlot s;
		s.tex = a->tex;
		s.u0 = (float) a->x / ATLAS;
		s.v0 = (float) a->y / ATLAS;
		s.u1 = (float) (a->x + g.w) / ATLAS;
		s.v1 = (float) (a->y + g.h) / ATLAS;
		a->x += g.w + 1;
		a->rowH = std::max(a->rowH, g.h);
		return g_slots[key] = s;
	}

	struct Tex { GLuint id; int lastUsed; };
	std::map<uint64_t, Tex> g_textures; // image serial -> texture
	int g_frame = 0;
}

const Palette& GetPalette()
{
	return g_palette;
}

bool Gfx::LoadUiFont(std::string& err)
{
	std::vector<std::string> dirs;
	if (const char* d = getenv("DM_FONT_DIR"))
		dirs.push_back(d);
	dirs.push_back(DM_DATADIR "/fonts");
	dirs.push_back("/usr/share/fonts/truetype/dejavu");
	dirs.push_back("/usr/local/share/fonts");
	for (auto& d : dirs) {
		std::string path = d + "/DejaVuSans.ttf";
		if (Exists(path) && ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 14.0f))
			return true;
	}
	err = "DejaVuSans.ttf was not found (DM_FONT_DIR names a directory with the DejaVu fonts).";
	return false;
}

TextMetrics& Gfx::Metrics()
{
	return Fonts::Metrics();
}

int Gfx::Ascent(FontStyle st, int px)
{
	return Fonts::Ascent(st, px);
}

int Gfx::LineHeight(FontStyle st, int px)
{
	return Fonts::LineHeight(st, px);
}

int Gfx::Measure(const std::string& s, FontStyle st, int px)
{
	return Fonts::Measure(s, st, px);
}

std::string Gfx::Elide(const std::string& s, FontStyle st, int px, int maxWidth)
{
	return Fonts::Elide(s, st, px, maxWidth);
}

int Gfx::Text(ImDrawList* dl, ImVec2 pos, const std::string& s, FontStyle st, int px, uint32_t color)
{
	if (s.empty())
		return 0;
	float baseline = pos.y + Fonts::Ascent(st, px);
	int width = 0;
	for (auto& g : Fonts::Glyphs(s.data(), s.size(), st, px)) {
		width = g.x + g.advance;
		if (!g.w || !g.h || (!g.argb && !g.coverage))
			continue;
		const AtlasSlot& slot = Slot(g);
		float x = pos.x + g.x + g.left, y = baseline - g.top;
		dl->AddImage((ImTextureID) (intptr_t) slot.tex, ImVec2(x, y), ImVec2(x + g.w, y + g.h),
			ImVec2(slot.u0, slot.v0), ImVec2(slot.u1, slot.v1), g.argb ? IM_COL32_WHITE : Col(color));
	}
	return width;
}

ImTextureID Gfx::Texture(const Image& img)
{
	auto it = g_textures.find(img.serial);
	if (it != g_textures.end()) {
		it->second.lastUsed = g_frame;
		return (ImTextureID) (intptr_t) it->second.id;
	}
	std::vector<uint8_t> rgba((size_t) img.w * img.h * 4);
	for (size_t i = 0; i < img.px.size(); i++) {
		uint32_t p = img.px[i];
		rgba[i * 4 + 0] = (p >> 16) & 0xff;
		rgba[i * 4 + 1] = (p >> 8) & 0xff;
		rgba[i * 4 + 2] = p & 0xff;
		rgba[i * 4 + 3] = (p >> 24) & 0xff;
	}
	GLuint id = NewTexture(img.w, img.h, rgba.data());
	g_textures[img.serial] = Tex{ id, g_frame };
	return (ImTextureID) (intptr_t) id;
}

void Gfx::CollectTextures()
{
	g_frame++;
	for (auto it = g_textures.begin(); it != g_textures.end(); ) {
		if (g_frame - it->second.lastUsed > 600) {
			glDeleteTextures(1, &it->second.id);
			it = g_textures.erase(it);
		}
		else
			++it;
	}
}

void Gfx::DrawImage(ImDrawList* dl, const Image& img, float x, float y)
{
	dl->AddImage(Texture(img), ImVec2(x, y), ImVec2(x + img.w, y + img.h));
}

void Gfx::DrawImageCircle(ImDrawList* dl, const Image& img, float x, float y)
{
	dl->AddImageRounded(Texture(img), ImVec2(x, y), ImVec2(x + img.w, y + img.h),
		ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, img.w * 0.5f);
}
