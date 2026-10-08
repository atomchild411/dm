#pragma once

#include <string>

// A picture from a message, as a viewer opens it.
struct PictureInfo
{
	std::string url;             // a media proxy URL (it can be asked for a size)
	int width = 0, height = 0;   // the original's size; 0 when not known
	std::string title;
};

// What a viewer fetches for a picture: as large as maxW x maxH leaves room
// for (the original's shape kept), asked of Discord's media proxy at that
// size, so a 4000-pixel photo is not decoded here at full size.
struct PictureFetch
{
	std::string url;
	int w = 0, h = 0;
};
PictureFetch FetchFor(const PictureInfo& pic, int maxW, int maxH);
