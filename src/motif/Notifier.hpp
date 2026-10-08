#pragma once

#include "Xm.hpp"
#include "Canvas.hpp"

// Mentions and direct messages (what the core's notification manager finds
// worth it): a sound (sfplay and a desktop sound scheme file; DM_SOUND names
// another), and, when the window is not in front, a popup in the corner of
// the screen that opens the channel when clicked.  View has switches.
namespace Notifier
{
	void Init(Widget toplevel, const PixelFormat& fmt);
	void OnNotification();
	// Whether the main window has the keyboard (the core asks).
	bool IsFocused();
	// Images arrived: the popup's avatar.
	void ImagesChanged();
	// --demo with DM_TEST_NOTIFY=1: a made-up mention, sound and popup.
	void Test();
}
