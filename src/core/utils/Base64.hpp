#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Standard base64 (RFC 4648, "+/" and "=" padding).
std::string Base64Encode(const void* data, size_t size);

// Decodes up to the first "=" or the first character outside the alphabet;
// a last group of two or three characters gives one or two bytes.
std::vector<uint8_t> Base64Decode(const std::string& text);
