#pragma once

#include <cstdint>
#include <string>

#include "models/Snowflake.hpp"
#include "ImageCache.hpp"

// One row of a list of servers, channels, members or conversations, as
// shared/Lists builds it from the core; each front end draws it its way.
struct ListRow
{
	enum Type { ITEM, HEADER, SPACE };
	Type type = ITEM;
	Snowflake id = 0;          // what selecting the row reports
	bool selectable = true;
	std::string text;          // UTF-8
	int indent = 0;            // pixels

	// the icon: an image, else a glyph (e.g. "#"), else initials on a disc
	bool hasImage = false;
	ImageCache::Kind imageKind = ImageCache::AVATAR;
	std::string imagePlace;
	Snowflake imageSf = 0;
	bool roundImage = true;    // circle; else a rounded square
	std::string glyph;
	bool initials = false;     // initials of text when there is no image
	Snowflake colorSeed = 0;   // picks the initials' disc colour

	uint32_t textColor = 0;    // 0xRRGGBB; 0: the front end's own
	bool unread = false;       // bold, with a mark on the left
	bool dim = false;          // muted text (voice channels, offline members)
	int mentions = 0;          // a red badge
	int status = -1;           // presence dot: eActiveStatus, -1 none

	bool operator==(const ListRow& o) const
	{
		return type == o.type && id == o.id && selectable == o.selectable && text == o.text &&
			indent == o.indent && hasImage == o.hasImage && imageKind == o.imageKind &&
			imagePlace == o.imagePlace && imageSf == o.imageSf && roundImage == o.roundImage &&
			glyph == o.glyph && initials == o.initials && colorSeed == o.colorSeed &&
			textColor == o.textColor && unread == o.unread && dim == o.dim &&
			mentions == o.mentions && status == o.status;
	}
	bool operator!=(const ListRow& o) const { return !(*this == o); }
};
