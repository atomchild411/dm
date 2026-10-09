#include "Gfx.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sys/stat.h>
#include <vector>

#include "GL.hpp"
#include "Platform.hpp"

#include "shared/Fonts.hpp"

#ifndef DM_DATADIR
#define DM_DATADIR "/usr/local/share/discord-messenger"
#endif

namespace
{
	const Palette g_darkPalette = {
		0x1e1f22, 0x2b2d31, 0xdbdee1, 0x949ba4, 0x949ba4,
		0x404249, 0xffffff,
		0xf23f42, 0xffffff,
		0x313338, 0xdbdee1, 0x949ba4, 0x00a8fc, 0xc9cdfb, 0x2b2d31, 0x1e1f22, 0x4e5058,
		0x23a55a, 0xf0b232, 0xf23f43, 0x80848e,
	};
	const Palette g_lightPalette = {
		0xe3e5e8, 0xf2f3f5, 0x313338, 0x5c5e66, 0x5c5e66,
		0xd4d7dc, 0x060607,
		0xf23f42, 0xffffff,
		0xffffff, 0x313338, 0x5c5e66, 0x006ce7, 0x505cdc, 0xf2f3f5, 0xe3e5e8, 0xc4c9ce,
		0x23a55a, 0xf0b232, 0xf23f43, 0x80848e,
	};
	const Palette* g_palette = &g_darkPalette;

	bool Exists(const std::string& path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0;
	}

	int PowerOfTwo(int n)
	{
		int p = 1;
		while (p < n)
			p <<= 1;
		return p;
	}

	// A texture of RGBA (or, alpha, GL_ALPHA) pixels; where textures must be
	// powers of two in size (OpenGL 1.1) it is padded, the picture at (1, 1)
	// with its edge pixels copied around it (so that filtering never blends
	// in the padding).  The part the picture takes goes to uv0 and uv1.
	GLuint NewTexture(int w, int h, const uint8_t* px, bool alpha, ImVec2& uv0, ImVec2& uv1)
	{
		const int bpp = alpha ? 1 : 4;
		GLenum format = alpha ? GL_ALPHA : GL_RGBA;
		int tw = w, th = h, ox = 0, oy = 0;
		std::vector<uint8_t> padded;
		if (Platform::PowerOfTwoTextures()) {
			tw = PowerOfTwo(w + 2);
			th = PowerOfTwo(h + 2);
			ox = oy = 1;
			padded.assign((size_t) tw * th * bpp, 0);
			for (int y = 0; y < h + 2; y++) {
				int sy = std::min(std::max(y - 1, 0), h - 1);
				for (int x = 0; x < w + 2; x++) {
					int sx = std::min(std::max(x - 1, 0), w - 1);
					memcpy(&padded[((size_t) y * tw + x) * bpp], &px[((size_t) sy * w + sx) * bpp], bpp);
				}
			}
			px = padded.data();
		}
		GLuint id = 0;
		glGenTextures(1, &id);
		glBindTexture(GL_TEXTURE_2D, id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, Platform::PowerOfTwoTextures() ? GL_CLAMP : GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, Platform::PowerOfTwoTextures() ? GL_CLAMP : GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, format, tw, th, 0, format, GL_UNSIGNED_BYTE, px);
		uv0 = ImVec2((float) ox / tw, (float) oy / th);
		uv1 = ImVec2((float) (ox + w) / tw, (float) (oy + h) / th);
		return id;
	}

	// A picture made to fit in max by max (box filtered).
	std::vector<uint8_t> Shrink(const std::vector<uint8_t>& rgba, int& w, int& h, int max)
	{
		float s = std::min((float) max / w, (float) max / h);
		int nw = std::max(1, (int) (w * s)), nh = std::max(1, (int) (h * s));
		std::vector<uint8_t> out((size_t) nw * nh * 4);
		for (int y = 0; y < nh; y++) {
			int y0 = y * h / nh, y1 = std::max(y0 + 1, (y + 1) * h / nh);
			for (int x = 0; x < nw; x++) {
				int x0 = x * w / nw, x1 = std::max(x0 + 1, (x + 1) * w / nw);
				unsigned sum[4] = { 0, 0, 0, 0 }, n = 0;
				for (int sy = y0; sy < y1; sy++)
					for (int sx = x0; sx < x1; sx++, n++)
						for (int c = 0; c < 4; c++)
							sum[c] += rgba[((size_t) sy * w + sx) * 4 + c];
				for (int c = 0; c < 4; c++)
					out[((size_t) y * nw + x) * 4 + c] = (uint8_t) (sum[c] / n);
			}
		}
		w = nw;
		h = nh;
		return out;
	}

	// ---- the glyph atlases: shared/Fonts' glyphs, packed in rows ------------
	// (Colour glyphs in RGBA ones; with OpenGL 1.1 the others go to alpha
	// ones, a quarter the size: High IMPACT has 1 MB of texture memory.)

	int AtlasSize()
	{
		return Platform::PowerOfTwoTextures() ? std::min(512, Platform::MaxTextureSize()) : 1024;
	}

	struct AtlasSlot { GLuint tex; float u0, v0, u1, v1; };
	struct Atlas { GLuint tex = 0; int x = 1, y = 1, rowH = 0; };
	std::vector<Atlas> g_atlases[2]; // [alpha]
	std::map<const void*, AtlasSlot> g_slots; // the glyph's pixels -> its place

	Atlas& NewAtlas(bool alpha)
	{
		const int size = AtlasSize();
		std::vector<uint8_t> empty((size_t) size * size * (alpha ? 1 : 4), 0);
		Atlas a;
		GLuint id = 0;
		glGenTextures(1, &id);
		glBindTexture(GL_TEXTURE_2D, id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, Platform::PowerOfTwoTextures() ? GL_CLAMP : GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, Platform::PowerOfTwoTextures() ? GL_CLAMP : GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		GLenum format = alpha ? GL_ALPHA : GL_RGBA;
		glTexImage2D(GL_TEXTURE_2D, 0, format, size, size, 0, format, GL_UNSIGNED_BYTE, empty.data());
		a.tex = id;
		g_atlases[alpha].push_back(a);
		return g_atlases[alpha].back();
	}

	const AtlasSlot& Slot(const Fonts::PlacedGlyph& g)
	{
		const void* key = g.argb ? (const void*) g.argb : (const void*) g.coverage;
		auto it = g_slots.find(key);
		if (it != g_slots.end())
			return it->second;

		const int size = AtlasSize();
		const bool alpha = !g.argb && Platform::PowerOfTwoTextures();
		std::vector<Atlas>& list = g_atlases[alpha];
		Atlas* a = list.empty() ? &NewAtlas(alpha) : &list.back();
		if (a->x + g.w + 1 > size) {
			a->x = 1;
			a->y += a->rowH + 1;
			a->rowH = 0;
		}
		if (a->y + g.h + 1 > size)
			a = &NewAtlas(alpha);

		// coverage as white with that alpha (tinted when drawn), or as alpha
		// alone; colour as is
		std::vector<uint8_t> px((size_t) g.w * g.h * (alpha ? 1 : 4));
		for (int i = 0; i < g.w * g.h; i++) {
			if (alpha)
				px[i] = g.coverage[i];
			else if (g.argb) {
				uint32_t p = g.argb[i];
				px[i * 4 + 0] = (p >> 16) & 0xff;
				px[i * 4 + 1] = (p >> 8) & 0xff;
				px[i * 4 + 2] = p & 0xff;
				px[i * 4 + 3] = (p >> 24) & 0xff;
			}
			else {
				px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = 255;
				px[i * 4 + 3] = g.coverage[i];
			}
		}
		glBindTexture(GL_TEXTURE_2D, a->tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, a->x, a->y, g.w, g.h, alpha ? GL_ALPHA : GL_RGBA, GL_UNSIGNED_BYTE, px.data());
		AtlasSlot s;
		s.tex = a->tex;
		s.u0 = (float) a->x / size;
		s.v0 = (float) a->y / size;
		s.u1 = (float) (a->x + g.w) / size;
		s.v1 = (float) (a->y + g.h) / size;
		a->x += g.w + 1;
		a->rowH = std::max(a->rowH, g.h);
		return g_slots[key] = s;
	}

	struct Tex { GLuint id; ImVec2 uv0, uv1; int lastUsed; };
	std::map<uint64_t, Tex> g_textures; // image serial -> texture
	int g_frame = 0;
}

const Palette& GetPalette()
{
	return *g_palette;
}

void Gfx::SetPaletteDark(bool dark)
{
	g_palette = dark ? &g_darkPalette : &g_lightPalette;
}

bool Gfx::LoadUiFont(std::string& err)
{
	std::vector<std::string> dirs;
	if (const char* d = getenv("DM_FONT_DIR"))
		dirs.push_back(d);
	dirs.push_back(DM_DATADIR "/fonts");
	dirs.push_back("/usr/share/fonts/truetype/dejavu");
	dirs.push_back("/usr/local/share/fonts");
	// Inter, as the text; DejaVu Sans when it is not there
	for (const char* file : { "Inter-Regular.ttf", "DejaVuSans.ttf" })
		for (auto& d : dirs) {
			std::string path = d + "/" + file;
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

Gfx::TexRef Gfx::Texture(const ::Image& img)
{
	auto it = g_textures.find(img.serial);
	if (it != g_textures.end()) {
		it->second.lastUsed = g_frame;
		return TexRef{ (ImTextureID) (intptr_t) it->second.id, it->second.uv0, it->second.uv1 };
	}
	std::vector<uint8_t> rgba((size_t) img.w * img.h * 4);
	for (size_t i = 0; i < img.px.size(); i++) {
		uint32_t p = img.px[i];
		rgba[i * 4 + 0] = (p >> 16) & 0xff;
		rgba[i * 4 + 1] = (p >> 8) & 0xff;
		rgba[i * 4 + 2] = p & 0xff;
		rgba[i * 4 + 3] = (p >> 24) & 0xff;
	}
	// (bigger than the graphics can take: smaller, shown as big)
	int w = img.w, h = img.h, max = Platform::MaxTextureSize() - (Platform::PowerOfTwoTextures() ? 2 : 0);
	if (w > max || h > max)
		rgba = Shrink(rgba, w, h, max);
	Tex t;
	t.id = NewTexture(w, h, rgba.data(), false, t.uv0, t.uv1);
	t.lastUsed = g_frame;
	g_textures[img.serial] = t;
	return TexRef{ (ImTextureID) (intptr_t) t.id, t.uv0, t.uv1 };
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

void Gfx::AddImage(ImDrawList* dl, const ::Image& img, ImVec2 p0, ImVec2 p1)
{
	TexRef t = Texture(img);
	dl->AddImage(t.id, p0, p1, t.uv0, t.uv1);
}

void Gfx::AddImageRounded(ImDrawList* dl, const ::Image& img, ImVec2 p0, ImVec2 p1, float rounding)
{
	TexRef t = Texture(img);
	dl->AddImageRounded(t.id, p0, p1, t.uv0, t.uv1, IM_COL32_WHITE, rounding);
}

void Gfx::DrawImage(ImDrawList* dl, const ::Image& img, float x, float y)
{
	AddImage(dl, img, ImVec2(x, y), ImVec2(x + img.w, y + img.h));
}

void Gfx::DrawImageCircle(ImDrawList* dl, const ::Image& img, float x, float y)
{
	AddImageRounded(dl, img, ImVec2(x, y), ImVec2(x + img.w, y + img.h), img.w * 0.5f);
}
