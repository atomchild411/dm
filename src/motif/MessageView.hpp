#pragma once

#include <functional>

#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Xm.hpp"

#include "models/Message.hpp"
#include "models/ScrollDir.hpp"
#include "text/FormattedText.hpp"
#include "Canvas.hpp"
#include "TextInterface_Motif.hpp"
#include "shared/MessageList.hpp"

// The channel's messages, drawn into a Canvas: authors and times, the
// formatted text, replies, attachments and embeds, grouped the way Discord
// groups them.  Older history loads when its gap scrolls into view.
class MessageView
{
public:
	MessageView(Widget parent, const PixelFormat& fmt);
	Widget GetWidget() const { return m_form; }

	void SetChannel(Snowflake guild, Snowflake channel);
	Snowflake GetChannel() const { return m_channel; }

	// Reloads the messages from the cache, keeping what is on screen in
	// place (or the bottom in view, when it was).
	void Refresh();
	void ScrollToBottom();
	void ScrollTo(int y) { SetScroll(y); }
	~MessageView();
	// Where Reply and Edit Message from the message menu go, and whether
	// the window has the keyboard (to mark arriving messages read); the
	// main window's when not set.
	void SetOwner(std::function<void(Snowflake, const std::string&)> reply,
		std::function<void(Snowflake, const std::string&)> edit, std::function<bool()> focused)
	{
		m_onReply = reply;
		m_onEdit = edit;
		m_isFocused = focused;
	}
	int GetScroll() const { return m_scrollY; }
	// Each item's message, place and height, to compare layouts (--bench).
	std::string LayoutSignature() const;

	// The text size changed, or the colours: lay everything out again.
	void Relayout();

	// Images arrived: repaint soon (once for a burst).
	void ImagesChanged();

private:
	typedef MessageList::Item Item;
	typedef MessageList::ItemExtra ItemExtra;

	static void ExposeCB(Widget, XtPointer, XtPointer);
	static void ResizeCB(Widget, XtPointer, XtPointer);
	static void ScrollCB(Widget, XtPointer, XtPointer);
	static void InputEH(Widget, XtPointer, XEvent*, Boolean*);
	static void VisibilityEH(Widget, XtPointer, XEvent*, Boolean*);
	static void GraphicsExposeEH(Widget, XtPointer, XEvent*, Boolean*);
	static void MenuCB(Widget, XtPointer, XtPointer);
	void ShowMenu(XButtonEvent& ev);

	void LayoutAll();
	void UpdateScrollbar();
	void SetScroll(int y);
	// Paint draws the whole view; Update only what scrolling or messages
	// added at the end changed, moving the rest (XCopyArea) when it can.
	void Paint();
	void Update();
	bool CheckSize();
	void PaintBand(int y0, int y1);
	void PaintItem(Item& item, int top);
	void RequestVisibleGaps();
	// The newest messages of the open channel are on screen: tell Discord
	// it is read (and drop the unread counts).
	void MarkReadIfSeen();
	bool m_justOpened = false;
	std::function<void(Snowflake, const std::string&)> m_onReply, m_onEdit;
	std::function<bool()> m_isFocused;   // opened by the user: read even before focus is known
	void OnClick(int x, int y);
	void DrawPicture(const Rect& r, const std::string& url, int top, const std::string& label);
	static void RepaintTimerCB(XtPointer, XtIntervalId*);
	int ContentWidth() const;

	Widget m_form, m_area, m_scroll;
	const PixelFormat& m_fmt;
	GC m_gc = nullptr;
	Canvas m_canvas;
	DrawingContext m_ctx;

	Snowflake m_guild = 0, m_channel = 0;
	MessageList m_list;       // the items and their layout
	int m_scrollY = 0;
	int m_viewW = 1, m_viewH = 1;
	bool m_stickToBottom = true;
	XtIntervalId m_repaintTimer = 0;

	// the right-click menu: Add Reaction, Reply
	Widget m_menu = nullptr, m_menuEdit = nullptr, m_menuDelete = nullptr;
	void ConfirmDelete(MessagePtr msg);
	static void DeleteCB(Widget, XtPointer, XtPointer);
	Snowflake m_deleteChannel = 0, m_deleteMessage = 0;
	MessagePtr m_menuMessage;
	int m_menuX = 0, m_menuY = 0; // where it was asked for, on the screen

	// what the canvas (and the window) show
	bool m_canvasValid = false;   // the view as of m_paintedScrollY
	int m_paintedScrollY = 0;
	int m_dirtyFromY = -1;        // content y from which it is out of date; -1: none
	bool m_unobscured = false;    // the window wholly visible: XCopyArea can move it
	// recent cost of an update by moving pixels, and of drawing it all (ms)
	double m_copyMs = 0, m_fullMs = 0;
	int m_updatesSinceProbe = 0;
};
