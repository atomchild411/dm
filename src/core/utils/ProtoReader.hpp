#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Reads protobuf's wire format: the fields of one message, one at a time,
// where they lie (nothing is copied or allocated).  Next() is false at the
// message's end, and also when the data is broken (Broken() says which).
class ProtoReader
{
public:
	enum { VARINT = 0, I64 = 1, LEN = 2, I32 = 5 };

	ProtoReader(const uint8_t* data, size_t size) : m_p(data), m_end(data + size) {}

	bool Next()
	{
		if (m_p == m_end || m_broken)
			return false;
		uint64_t key;
		if (!Varint(key) || (key >> 3) == 0 || (key >> 3) > 0x1fffffff)
			return Break();
		m_field = uint32_t(key >> 3);
		m_type = int(key & 7);
		switch (m_type)
		{
			case VARINT:
				return Varint(m_value) || Break();
			case I64:
				return Fixed(8) || Break();
			case I32:
				return Fixed(4) || Break();
			case LEN: {
				uint64_t n;
				if (!Varint(n) || n > uint64_t(m_end - m_p))
					return Break();
				m_data = m_p;
				m_size = size_t(n);
				m_p += n;
				return true;
			}
			default: // groups (long deprecated) and anything else
				return Break();
		}
	}

	uint32_t Field() const { return m_field; }
	int Type() const { return m_type; }
	bool Broken() const { return m_broken; }

	// VARINT, I64 and I32 fields' values (fixed ones are little-endian).
	uint64_t Value() const { return m_value; }

	// LEN fields' bytes, or the message they hold.
	const uint8_t* Data() const { return m_data; }
	size_t Size() const { return m_size; }
	std::string String() const { return std::string((const char*) m_data, m_size); }
	ProtoReader Message() const { return ProtoReader(m_data, m_size); }

private:
	bool Varint(uint64_t& v)
	{
		v = 0;
		for (int shift = 0; shift < 64; shift += 7) {
			if (m_p == m_end)
				return false;
			uint8_t b = *m_p++;
			v |= uint64_t(b & 0x7f) << shift;
			if (!(b & 0x80))
				return true;
		}
		return false;
	}

	bool Fixed(int bytes)
	{
		if (m_end - m_p < bytes)
			return false;
		m_value = 0;
		for (int i = bytes - 1; i >= 0; i--)
			m_value = (m_value << 8) | m_p[i];
		m_p += bytes;
		return true;
	}

	bool Break()
	{
		m_broken = true;
		return false;
	}

	const uint8_t* m_p;
	const uint8_t* m_end;
	uint32_t m_field = 0;
	int m_type = 0;
	uint64_t m_value = 0;
	const uint8_t* m_data = nullptr;
	size_t m_size = 0;
	bool m_broken = false;
};
