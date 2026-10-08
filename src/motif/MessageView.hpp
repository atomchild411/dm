#pragma once

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
	int GetScroll() const { return m_scrollY; }
	// Each item's message, place and height, to compare layouts (--bench).
	std::string LayoutSignature() const;

	// The text size changed, or the colours: lay everything out again.
	void Relayout();

	// Images arrived: repaint soon (once for a burst).
	void ImagesChanged();

private:
	struct ItemExtra;
	struct Item
	{
		MessagePtr msg;
		bool grouped = false;   // follows a message by the same author
		bool systemLine = false;
		std::string systemText;
		FormattedText text;
		std::vector<InteractableItem> interactables;
		std::unique_ptr<FormattedText> reply;
		int y = 0;              // top, in content coordinates
		int height = 0;
		int textTop = 0;        // offsets inside the item
		int day = -1;           // DayNumber of the message
		// what the layout was made for: it is kept while these hold
		int laidOutWidth = -1;
		int laidOutPx = 0;
		bool laidOutGrouped = false;
		bool laidOutDateSep = false;
		std::shared_ptr<ItemExtra> extra;
	};

	static void ExposeCB(Widget, XtPointer, XtPointer);
	static void ResizeCB(Widget, XtPointer, XtPointer);
	static void ScrollCB(Widget, XtPointer, XtPointer);
	static void InputEH(Widget, XtPointer, XEvent*, Boolean*);
	static void VisibilityEH(Widget, XtPointer, XEvent*, Boolean*);
	static void GraphicsExposeEH(Widget, XtPointer, XEvent*, Boolean*);
	static void MenuCB(Widget, XtPointer, XtPointer);
	void ShowMenu(XButtonEvent& ev);

	// Brings the items up to date with the cache; true when the only change
	// is messages added after the last one.
	bool Rebuild();
	void LayoutAll();
	void LayoutItem(Item& item, int width);
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
	void OnClick(int x, int y);
	void DrawPicture(const Rect& r, const std::string& url, int top, const std::string& label);
	static void RepaintTimerCB(XtPointer, XtIntervalId*);
	int ContentWidth() const;
	static int ReactionHeight(int px);
	static int ReactionWidth(const Reaction& r, int px);

	Widget m_form, m_area, m_scroll;
	const PixelFormat& m_fmt;
	GC m_gc = nullptr;
	Canvas m_canvas;
	DrawingContext m_ctx;

	Snowflake m_guild = 0, m_channel = 0;
	std::list<Item> m_items;
	std::set<Snowflake> m_requestedGaps;
	int m_contentHeight = 0;
	int m_scrollY = 0;
	int m_viewW = 1, m_viewH = 1;
	bool m_stickToBottom = true;
	XtIntervalId m_repaintTimer = 0;

	// the right-click menu: Add Reaction, Reply
	Widget m_menu = nullptr;
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
