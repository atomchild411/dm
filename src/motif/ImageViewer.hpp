#pragma once

#include <string>

#include "Xm.hpp"
#include "Canvas.hpp"
#include "shared/PictureInfo.hpp"

// A picture from a message, as large as fits the screen, in a dialog of its
// own: Close, Escape or a click on the picture shuts it.
namespace ImageViewer
{
	typedef PictureInfo Picture;

	void Show(Widget parent, const PixelFormat& fmt, const Picture& pic);

	// Images arrived: draws the picture if it was waiting for it.
	void ImagesChanged();
}
