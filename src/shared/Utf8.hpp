#pragma once

#include <cstddef>

// The next code point of UTF-8 text at *p (advancing it), U+FFFD on errors.
unsigned DecodeUtf8(const char*& p, const char* end);
