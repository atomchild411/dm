#include "Gfx.hpp"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sys/stat.h>
#include <vector>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include "misc/freetype/imgui_freetype.h"
#include "shared/Utf8.hpp"

#ifndef DM_DATADIR
#define DM_DATADIR "/usr/local/share/discord-messenger"
#endif

namespace
{
	ImFont* g_fonts[FS_COUNT];

	const char* const FILES[FS_COUNT] = {
		"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSans-Oblique.ttf",
		"DejaVuSans-BoldOblique.ttf", "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf",
	};

	bool Exists(const std::string& path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0;
	}

	// The first directory that has the file.
	std::string Find(const std::string& file)
	{
		std::vector<std::string> dirs;
		if (const char* d = getenv("DM_FONT_DIR"))
			dirs.push_back(d);
		dirs.push_back(DM_DATADIR "/fonts");
		dirs.push_back("/usr/share/fonts/truetype/dejavu");
		dirs.push_back("/usr/share/fonts/truetype/noto");
		dirs.push_back("/usr/share/fonts/TTF");
		dirs.push_back("/usr/share/fonts/noto");
		dirs.push_back("/usr/local/share/fonts");
		dirs.push_back("/opt/homebrew/share/fonts");
		if (const char* home = getenv("HOME"))
			dirs.push_back(std::string(home) + "/Library/Fonts");
		dirs.push_back("/Library/Fonts");
		for (auto& d : dirs)
			if (Exists(d + "/" + file))
				return d + "/" + file;
		return "";
	}

	struct ImGuiMetrics : TextMetrics
	{
		int Ascent(FontStyle st, int px) override { return Gfx::Ascent(st, px); }
		int Descent(FontStyle st, int px) override
		{
			return (int) std::ceil(-g_fonts[st]->GetFontBaked((float) px)->Descent);
		}
		int Measure(const std::string& s, FontStyle st, int px) override { return Gfx::Measure(s, st, px); }
		size_t FitBytes(const char* s, size_t n, FontStyle st, int px, int maxWidth) override
		{
			ImFontBaked* baked = g_fonts[st]->GetFontBaked((float) px);
			const char* p = s;
			const char* end = s + n;
			const char* lastSpace = nullptr;
			float w = 0;
			while (p < end && *p != '\n') {
				const char* start = p;
				unsigned cp = DecodeUtf8(p, end);
				w += baked->GetCharAdvance((ImWchar) cp);
				if (w > maxWidth) {
					if (lastSpace)
						return lastSpace - s;
					return start == s ? (size_t) (p - s) : (size_t) (start - s);
				}
				if (cp == ' ')
					lastSpace = p;
			}
			return p - s;
		}
	};

	struct Tex { GLuint id; int lastUsed; };
	std::map<uint64_t, Tex> g_textures; // image serial -> texture
	int g_frame = 0;

	Palette g_palette = {
		0x1e1f22, 0x2b2d31, 0xdbdee1, 0x949ba4, 0x949ba4,
		0x404249, 0xffffff,
		0xf23f42, 0xffffff,
		0x313338, 0xdbdee1, 0x949ba4, 0x00a8fc, 0xc9cdfb, 0x2b2d31, 0x1e1f22, 0x4e5058,
		0x23a55a, 0xf0b232, 0xf23f43, 0x80848e,
	};
}

const Palette& GetPalette()
{
	return g_palette;
}

bool Gfx::LoadFonts(std::string& err)
{
	ImGuiIO& io = ImGui::GetIO();
	std::string emoji = Find("NotoColorEmoji.ttf");
	for (int st = 0; st < FS_COUNT; st++) {
		std::string path = Find(FILES[st]);
		if (path.empty()) {
			err = std::string("The font ") + FILES[st] + " was not found (DM_FONT_DIR names a directory with the DejaVu fonts).";
			return false;
		}
		ImFontConfig cfg;
		// DejaVu's own black-and-white emoji give way to the colour ones
		static const ImWchar emojiBlocks[] = { 0x1f000, 0x1faff, 0 };
		if (!emoji.empty())
			cfg.GlyphExcludeRanges = emojiBlocks;
		g_fonts[st] = io.Fonts->AddFontFromFileTTF(path.c_str(), 14.0f, &cfg);
		if (!g_fonts[st]) {
			err = "The font " + path + " could not be read.";
			return false;
		}
		// colour emoji in every face
		if (!emoji.empty()) {
			ImFontConfig ecfg;
			ecfg.MergeMode = true;
			ecfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LoadColor | ImGuiFreeTypeLoaderFlags_Bitmap;
			io.Fonts->AddFontFromFileTTF(emoji.c_str(), 14.0f, &ecfg);
		}
	}
	return true;
}

ImFont* Gfx::Font(FontStyle st)
{
	return g_fonts[st];
}

TextMetrics& Gfx::Metrics()
{
	static ImGuiMetrics metrics;
	return metrics;
}

int Gfx::Ascent(FontStyle st, int px)
{
	return (int) std::ceil(g_fonts[st]->GetFontBaked((float) px)->Ascent);
}

int Gfx::LineHeight(FontStyle st, int px)
{
	return Metrics().LineHeight(st, px);
}

int Gfx::Measure(const std::string& s, FontStyle st, int px)
{
	if (s.empty())
		return 0;
	ImVec2 sz = g_fonts[st]->CalcTextSizeA((float) px, FLT_MAX, 0.0f, s.data(), s.data() + s.size());
	return (int) std::ceil(sz.x);
}

std::string Gfx::Elide(const std::string& s, FontStyle st, int px, int maxWidth)
{
	if (Measure(s, st, px) <= maxWidth)
		return s;
	static const std::string dots = "\xe2\x80\xa6"; // U+2026
	int room = maxWidth - Measure(dots, st, px);
	if (room <= 0)
		return dots;
	// as much as fits (not only up to a space)
	const char* p = s.data();
	const char* end = s.data() + s.size();
	while (p < end) {
		const char* q = p;
		DecodeUtf8(q, end);
		if (Measure(std::string(s.data(), q), st, px) > room)
			break;
		p = q;
	}
	return std::string(s.data(), p) + dots;
}

int Gfx::Text(ImDrawList* dl, ImVec2 pos, const std::string& s, FontStyle st, int px, uint32_t color)
{
	if (s.empty())
		return 0;
	dl->AddText(g_fonts[st], (float) px, pos, Col(color), s.data(), s.data() + s.size());
	return Measure(s, st, px);
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
	GLuint id = 0;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
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
