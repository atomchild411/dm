#include "PictureInfo.hpp"

#include <algorithm>

PictureFetch FetchFor(const PictureInfo& pic, int maxW, int maxH)
{
	PictureFetch f;
	f.w = pic.width > 0 ? pic.width : maxW / 2;
	f.h = pic.height > 0 ? pic.height : maxH / 2;
	if (f.w > maxW) { f.h = (int) ((long) f.h * maxW / f.w); f.w = maxW; }
	if (f.h > maxH) { f.w = (int) ((long) f.w * maxH / f.h); f.h = maxH; }
	f.w = std::max(1, f.w);
	f.h = std::max(1, f.h);
	f.url = pic.url;
	if (pic.width > 0 && pic.height > 0 && (f.w != pic.width || f.h != pic.height))
		f.url += (f.url.find('?') == std::string::npos ? "?" : "&") +
			std::string("width=") + std::to_string(f.w) + "&height=" + std::to_string(f.h);
	return f;
}
