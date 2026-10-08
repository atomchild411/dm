#pragma once

#include "Xm.hpp"
#include "Canvas.hpp"
#include "models/Snowflake.hpp"

// A direct message conversation in a window of its own (opened from the
// Messages menu or a notification), so the main window keeps its server and
// channel: messages, the message box, the emoji button and Send.  Closing
// it leaves everything as it was.
namespace Conversations
{
	void Init(Widget toplevel, const PixelFormat& fmt);

	// Opens the conversation with a direct message channel, or raises it.
	void Open(Snowflake channel);

	// Its messages changed (0: any of them).
	void Refresh(Snowflake channel);
	// After connecting again: their messages fetched afresh.
	void Reload();
	// The text size changed.
	void Relayout();
	void ImagesChanged();
	// Someone started or stopped typing.
	void UpdateTyping();
	// Whether that conversation's window has the keyboard (no notification
	// is needed then).
	bool IsFocusedOn(Snowflake channel);
	// Logging out.
	void CloseAll();
}
