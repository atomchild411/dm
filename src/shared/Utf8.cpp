#include "Utf8.hpp"

unsigned DecodeUtf8(const char*& p, const char* end)
{
	unsigned char c = (unsigned char) *p++;
	if (c < 0x80)
		return c;
	int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
	if (extra < 0)
		return 0xfffd;
	unsigned cp = c & (0x3f >> extra);
	for (int i = 0; i < extra; i++) {
		if (p >= end || ((unsigned char) *p & 0xc0) != 0x80)
			return 0xfffd;
		cp = (cp << 6) | ((unsigned char) *p++ & 0x3f);
	}
	return cp;
}
