#include "Base64.hpp"

#include <openssl/evp.h>

static bool InAlphabet(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
}

std::string Base64Encode(const void* data, size_t size)
{
	std::string out((size + 2) / 3 * 4 + 1, '\0');
	int n = EVP_EncodeBlock((unsigned char*) &out[0], (const unsigned char*) data, (int) size);
	out.resize(n > 0 ? (size_t) n : 0);
	return out;
}

std::vector<uint8_t> Base64Decode(const std::string& text)
{
	// the characters up to the first that is not in the alphabet ("=" too),
	// padded again as OpenSSL wants them; a lone last character is dropped
	size_t n = 0;
	while (n < text.size() && InAlphabet(text[n]))
		n++;
	std::string in = text.substr(0, n - (n % 4 == 1));
	int pad = (4 - in.size() % 4) % 4;
	in.append(pad, '=');

	std::vector<uint8_t> out(in.size() / 4 * 3);
	int got = in.empty() ? 0 : EVP_DecodeBlock(out.data(), (const unsigned char*) in.data(), (int) in.size());
	// (EVP_DecodeBlock counts the padding's zero bytes too)
	out.resize(got > pad ? (size_t) (got - pad) : 0);
	return out;
}
