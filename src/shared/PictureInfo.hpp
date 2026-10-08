#pragma once

#include <string>

// A picture from a message, as a viewer opens it.
struct PictureInfo
{
	std::string url;             // a media proxy URL (it can be asked for a size)
	int width = 0, height = 0;   // the original's size; 0 when not known
	std::string title;
};
