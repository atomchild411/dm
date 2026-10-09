#include "ImageCache.hpp"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <list>
#include <map>
#include <unordered_map>
#include <sys/stat.h>
#include <dirent.h>
#include <utime.h>
#include <vector>

#include <openssl/evp.h>

#include "Frontend.hpp"
#include "network/DiscordAPI.hpp"
#include "network/DiscordRequest.hpp"
#include "network/HTTPClient.hpp"
#include "utils/Util.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
// Pictures come from other people: a small file can claim a huge size.
#define STBI_MAX_DIMENSIONS 8192
#include <stb/stb_image.h>

#ifndef DISABLE_WEBP
#include <webp/decode.h>
#endif

// Images kept decoded in memory; the least recently used go first.
static const size_t MAX_IMAGES = 400;

namespace
{
	enum State { LOADING, READY, FAILED };

	struct Entry
	{
		State state = LOADING;
		Image image;
		std::list<std::string>::iterator lru;
		bool inLru = false;
	};

	// One download serves every size wanted of a source.
	struct Source
	{
		std::string url;
		std::vector<std::pair<int, int>> sizes;
		bool requested = false;
		bool anyFormat = false; // not only WebP (Discord's own pictures)
	};

	std::map<std::string, Entry> g_entries;     // key: source id + size
	std::map<std::string, Source> g_sources;    // key: source id
	std::list<std::string> g_lru;               // front: most recent
	std::function<void()> g_changed;
	bool g_offline = false;
	size_t g_diskLimit = 0;        // 0: no limit
	size_t g_writtenSinceTrim = 0;
	const size_t TRIM_EVERY = 4 * 1024 * 1024;

	// The cache directory down to 90% of the limit, oldest use first (a
	// file's time is set when it is read: see Get).  Only plain files at its
	// top: the message history has a directory of its own in there.
	void TrimDisk()
	{
		g_writtenSinceTrim = 0;
		if (!g_diskLimit)
			return;
		struct Entry { std::string path; off_t size; time_t mtime; };
		std::vector<Entry> files;
		size_t total = 0;
		std::string dir = GetCachePath();
		DIR* d = opendir(dir.c_str());
		if (!d)
			return;
		while (struct dirent* de = readdir(d)) {
			std::string path = dir + "/" + de->d_name;
			struct stat st;
			if (de->d_name[0] == '.' || stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
				continue;
			files.push_back({ path, st.st_size, st.st_mtime });
			total += st.st_size;
		}
		closedir(d);
		if (total <= g_diskLimit)
			return;
		std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
		for (auto& e : files) {
			if (total <= g_diskLimit / 10 * 9)
				break;
			if (remove(e.path.c_str()) == 0)
				total -= e.size;
		}
	}
	bool g_changedPending = false;
	uint64_t g_serial = 0;

	int NearestPowerOfTwo(int x)
	{
		int p = 16;
		while (p < x && p < 4096)
			p <<= 1;
		return p;
	}

#ifndef DISABLE_WEBP
	const bool WEBP = true;
#else
	const bool WEBP = false; // no WebP decoder: take what the servers send
#endif

	// Whether a URL is on Discord's media proxy (media.discordapp.net or
	// images-ext-N.discordapp.net), which converts what it serves when
	// asked (format=webp).
	bool OnMediaProxy(const std::string& url)
	{
		if (url.compare(0, 8, "https://"))
			return false;
		size_t end = url.find('/', 8);
		std::string host = url.substr(8, end == std::string::npos ? std::string::npos : end - 8);
		const std::string tail = ".discordapp.net";
		return host == "media.discordapp.net" ||
			(!host.compare(0, 11, "images-ext-") && host.size() > tail.size() &&
			 !host.compare(host.size() - tail.size(), tail.size(), tail));
	}

	// Where a picture is fetched from.  Everything other people chose is
	// asked for as WebP: the CDN and the media proxy make it anew.
	std::string SourceURL(ImageCache::Kind kind, const std::string& place, Snowflake sf, int size)
	{
		int px = NearestPowerOfTwo(std::max(size * 2, 32)); // some headroom for the scaling
		const std::string ext = WEBP ? ".webp" : ".png";
		switch (kind) {
			case ImageCache::AVATAR:
				return GetDiscordCDN() + "avatars/" + std::to_string(sf) + "/" + place + ext + "?size=" + std::to_string(px);
			case ImageCache::ICON:
				return GetDiscordCDN() + "icons/" + std::to_string(sf) + "/" + place + ext + "?size=" + std::to_string(px);
			case ImageCache::EMOJI:
				return GetDiscordCDN() + "emojis/" + std::to_string(sf) + ext + "?size=" + std::to_string(px);
			case ImageCache::CHANNEL_ICON:
				return GetDiscordCDN() + "channel-icons/" + std::to_string(sf) + "/" + place + ext + "?size=" + std::to_string(px);
			case ImageCache::DEFAULT_AVATAR:
				return GetDiscordCDN() + "embed/avatars/" + std::to_string((sf >> 22) % 6) + ".png";
			case ImageCache::URL:
			default:
				if (WEBP && OnMediaProxy(place))
					return place + (place.find('?') == std::string::npos ? "?" : "&") + "format=webp";
				return place; // not converted: only shown if it is WebP
		}
	}

	// The name of a source's cache file; worked out once (every paint asks).
	std::string SourceId(ImageCache::Kind kind, const std::string& place, Snowflake sf)
	{
		static std::unordered_map<std::string, std::string> ids;
		std::string what = std::to_string((int) kind) + ":" + place + ":" + std::to_string(sf);
		auto it = ids.find(what);
		if (it != ids.end())
			return it->second;
		// MD5 only names the files (as it always has, so caches stay valid)
		unsigned char md[EVP_MAX_MD_SIZE];
		unsigned int mdLen = 0;
		EVP_Digest(what.data(), what.size(), md, &mdLen, EVP_md5(), NULL);
		std::string id;
		for (unsigned int i = 0; i < mdLen; i++) {
			static const char hex[] = "0123456789abcdef";
			id += hex[md[i] >> 4];
			id += hex[md[i] & 15];
		}
		ids.emplace(what, id);
		return id;
	}

	std::string EntryKey(const std::string& id, int w, int h)
	{
		return id + "@" + std::to_string(w) + "x" + std::to_string(h);
	}

	std::string CacheFile(const std::string& id)
	{
		return GetCachePath() + "/" + id;
	}

	void Touch(const std::string& key, Entry& e)
	{
		if (e.inLru)
			g_lru.erase(e.lru);
		g_lru.push_front(key);
		e.lru = g_lru.begin();
		e.inLru = true;

		while (g_lru.size() > MAX_IMAGES) {
			std::string old = g_lru.back();
			g_lru.pop_back();
			g_entries.erase(old);
		}
	}

	// Area-averaging scale into a w x h box, keeping the aspect ratio when
	// fit is set; alpha-weighted so transparent edges do not darken.
	void Scale(const Image& src, int w, int h, bool fit, Image& out)
	{
		if (fit && src.w > 0 && src.h > 0) {
			double s = std::min((double) w / src.w, (double) h / src.h);
			if (s > 1.0) s = 1.0; // never enlarge previews
			w = std::max(1, (int) (src.w * s + 0.5));
			h = std::max(1, (int) (src.h * s + 0.5));
		}
		out.w = w;
		out.h = h;
		out.px.assign((size_t) w * h, 0);
		if (src.w == w && src.h == h) {
			out.px = src.px;
			return;
		}
		for (int y = 0; y < h; y++)
		{
			int y0 = y * src.h / h, y1 = std::max(y0 + 1, (y + 1) * src.h / h);
			for (int x = 0; x < w; x++)
			{
				int x0 = x * src.w / w, x1 = std::max(x0 + 1, (x + 1) * src.w / w);
				uint64_t a = 0, r = 0, g = 0, b = 0;
				int n = 0;
				for (int sy = y0; sy < y1 && sy < src.h; sy++)
				for (int sx = x0; sx < x1 && sx < src.w; sx++) {
					uint32_t p = src.px[(size_t) sy * src.w + sx];
					uint32_t pa = p >> 24;
					a += pa;
					r += ((p >> 16) & 0xff) * pa;
					g += ((p >> 8) & 0xff) * pa;
					b += (p & 0xff) * pa;
					n++;
				}
				if (!n || !a)
					continue;
				out.px[(size_t) y * w + x] = (uint32_t) ((a / n) << 24) |
					(uint32_t) ((r / a) << 16) | (uint32_t) ((g / a) << 8) | (uint32_t) (b / a);
			}
		}
	}

	void NotifyChanged()
	{
		if (!g_changed)
			return;
		g_changed();
	}

	// Fills every entry waiting for this source, from the encoded data.
	// Data that does not decode fails them only when final (a download);
	// a bad cache file just means downloading it again.
	bool Deliver(const std::string& id, const uint8_t* data, size_t size, bool final)
	{
		auto sit = g_sources.find(id);
		if (sit == g_sources.end())
			return false;

		Image full;
		bool ok = ImageCache::Decode(data, size, full, sit->second.anyFormat);
		if (!ok && !final)
			return false;
		for (auto& sz : sit->second.sizes)
		{
			std::string key = EntryKey(id, sz.first, sz.second);
			Entry& e = g_entries[key];
			if (!ok) {
				e.state = FAILED;
				continue;
			}
			bool fit = sz.first != sz.second || full.w != full.h;
			Scale(full, sz.first, sz.second, fit, e.image);
			e.image.serial = ++g_serial;
			e.state = READY;
			Touch(key, e);
		}
		sit->second.sizes.clear();
		return ok;
	}

	bool ReadFile(const std::string& path, std::string& out)
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
			return false;
		char buf[65536];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0)
			out.append(buf, n);
		fclose(f);
		return !out.empty();
	}
}

// The most pixels a picture may have (64 MB decoded, twice over while it is
// converted): anything bigger is refused before it is decoded.
static const long MAX_PIXELS = 4096L * 4096L;

static bool SizeOK(int w, int h)
{
	return w > 0 && h > 0 && (long) w * h <= MAX_PIXELS;
}

bool ImageCache::Decode(const uint8_t* data, size_t size, Image& out, bool anyFormat)
{
	if (!data || size < 12 || size > 0x7fffffff)
		return false;

#ifndef DISABLE_WEBP
	if (!memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WEBP", 4))
	{
		int w = 0, h = 0;
		if (!WebPGetInfo(data, size, &w, &h) || !SizeOK(w, h))
			return false;
		uint8_t* rgba = WebPDecodeRGBA(data, size, &w, &h);
		if (!rgba)
			return false;
		out.w = w;
		out.h = h;
		out.px.resize((size_t) w * h);
		for (size_t i = 0; i < out.px.size(); i++) {
			const uint8_t* p = rgba + i * 4;
			out.px[i] = ((uint32_t) p[3] << 24) | ((uint32_t) p[0] << 16) | ((uint32_t) p[1] << 8) | p[2];
		}
		WebPFree(rgba);
		return true;
	}
#endif

	if (!anyFormat)
		return false;

	int w = 0, h = 0, comp = 0;
	if (!stbi_info_from_memory(data, (int) size, &w, &h, &comp) || !SizeOK(w, h))
		return false;
	uint8_t* rgba = stbi_load_from_memory(data, (int) size, &w, &h, &comp, 4);
	if (!rgba)
		return false;
	out.w = w;
	out.h = h;
	out.px.resize((size_t) w * h);
	for (size_t i = 0; i < out.px.size(); i++) {
		const uint8_t* p = rgba + i * 4;
		out.px[i] = ((uint32_t) p[3] << 24) | ((uint32_t) p[0] << 16) | ((uint32_t) p[1] << 8) | p[2];
	}
	stbi_image_free(rgba);
	return true;
}

const Image* ImageCache::Get(Kind kind, const std::string& place, Snowflake sf, int w, int h)
{
	if (w <= 0 || h <= 0 || (kind == URL && place.empty()) || g_offline)
		return nullptr;

	std::string id = SourceId(kind, place, sf);
	std::string key = EntryKey(id, w, h);
	auto it = g_entries.find(key);
	if (it != g_entries.end()) {
		if (it->second.state == READY) {
			Touch(key, it->second);
			return &it->second.image;
		}
		return nullptr;
	}

	g_entries[key].state = LOADING;
	Source& src = g_sources[id];
	src.sizes.push_back(std::make_pair(w, h));
	if (src.url.empty()) {
		src.url = SourceURL(kind, place, sf, std::max(w, h));
		src.anyFormat = !WEBP || kind == DEFAULT_AVATAR;
	}

	// on disk already?
	std::string data;
	if (ReadFile(CacheFile(id), data)) {
		utime(CacheFile(id).c_str(), NULL); // used now: trimmed last
		if (Deliver(id, (const uint8_t*) data.data(), data.size(), false)) {
			Entry& e = g_entries[key];
			return e.state == READY ? &e.image : nullptr;
		}
		remove(CacheFile(id).c_str());
	}

	if (!src.requested) {
		src.requested = true;
		GetHTTPClient()->PerformRequest(
			false,
			NetRequest::GET,
			src.url,
			kind == URL ? DiscordRequest::IMAGE_ATTACHMENT : DiscordRequest::IMAGE,
			sf,
			"",
			"",
			id
		);
	}
	return nullptr;
}

bool ImageCache::Failed(Kind kind, const std::string& place, Snowflake sf, int w, int h)
{
	auto it = g_entries.find(EntryKey(SourceId(kind, place, sf), w, h));
	return it != g_entries.end() && it->second.state == FAILED;
}

void ImageCache::Downloaded(const std::string& id, const uint8_t* data, size_t size)
{
	auto sit = g_sources.find(id);
	if (sit != g_sources.end())
		sit->second.requested = false;

	if (Deliver(id, data, size, true)) {
		// keep it for the next run
		std::string path = CacheFile(id), tmp = path + ".new";
		FILE* f = fopen(tmp.c_str(), "wb");
		if (f) {
			bool ok = fwrite(data, 1, size, f) == size;
			ok = fclose(f) == 0 && ok;
			if (!ok || !RenameOver(tmp, path))
				remove(tmp.c_str());
			else if ((g_writtenSinceTrim += size) >= TRIM_EVERY)
				TrimDisk();
		}
	}
	NotifyChanged();
}

void ImageCache::DownloadFailed(const std::string& id)
{
	auto sit = g_sources.find(id);
	if (sit == g_sources.end())
		return;
	sit->second.requested = false;
	for (auto& sz : sit->second.sizes)
		g_entries[EntryKey(id, sz.first, sz.second)].state = FAILED;
	sit->second.sizes.clear();
	NotifyChanged();
}

void ImageCache::SetDiskLimit(size_t maxBytes)
{
	g_diskLimit = maxBytes;
	TrimDisk();
}

void ImageCache::ClearDisk()
{
	size_t limit = g_diskLimit;
	g_diskLimit = 1; // everything goes
	TrimDisk();
	g_diskLimit = limit;
}

void ImageCache::SetOffline(bool offline)
{
	g_offline = offline;
}

void ImageCache::SetChangedCallback(std::function<void()> fn)
{
	g_changed = fn;
}
