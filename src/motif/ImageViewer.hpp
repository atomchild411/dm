#pragma once

#include <string>

#include "Xm.hpp"
#include "Canvas.hpp"

// A picture from a message, as large as fits the screen, in a dialog of its
// own: Close, Escape or a click on the picture shuts it.
namespace ImageViewer
{
	struct Picture
	{
		std::string url;             // a media proxy URL (it can be asked for a size)
		int width = 0, height = 0;   // the original's size; 0 when not known
		std::string title;
	};

	void Show(Widget parent, const PixelFormat& fmt, const Picture& pic);

	// Images arrived: draws the picture if it was waiting for it.
	void ImagesChanged();
}
