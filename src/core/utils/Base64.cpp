#include "Base64.hpp"

static const char g_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const void* data, size_t size)
{
	const uint8_t* in = (const uint8_t*) data;
	std::string out;
	out.reserve((size + 2) / 3 * 4);
	for (size_t i = 0; i < size; i += 3)
	{
		uint32_t v = uint32_t(in[i]) << 16;
		if (i + 1 < size) v |= uint32_t(in[i + 1]) << 8;
		if (i + 2 < size) v |= in[i + 2];
		out += g_alphabet[(v >> 18) & 63];
		out += g_alphabet[(v >> 12) & 63];
		out += i + 1 < size ? g_alphabet[(v >> 6) & 63] : '=';
		out += i + 2 < size ? g_alphabet[v & 63] : '=';
	}
	return out;
}

static int Base64Value(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

std::vector<uint8_t> Base64Decode(const std::string& text)
{
	std::vector<uint8_t> out;
	out.reserve(text.size() / 4 * 3 + 2);
	uint32_t v = 0;
	int n = 0;
	for (char c : text)
	{
		int x = Base64Value(c);
		if (x < 0)
			break;
		v = (v << 6) | uint32_t(x);
		if (++n == 4) {
			out.push_back(uint8_t(v >> 16));
			out.push_back(uint8_t(v >> 8));
			out.push_back(uint8_t(v));
			v = 0;
			n = 0;
		}
	}
	if (n >= 2) {
		v <<= 6 * (4 - n);
		out.push_back(uint8_t(v >> 16));
		if (n == 3)
			out.push_back(uint8_t(v >> 8));
	}
	return out;
}
