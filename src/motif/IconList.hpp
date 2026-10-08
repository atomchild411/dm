#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Xm.hpp"
#include "models/Snowflake.hpp"
#include "Canvas.hpp"
#include "shared/ListRow.hpp"

// A list drawn in a Canvas, with icons and any Unicode text, which Motif's
// XmList cannot show.  Click or Return selects; arrows, Page Up/Down, Home,
// End and the wheel move.
class IconList
{
public:
	IconList(Widget parent, const char* name, const PixelFormat& fmt, int iconSize, bool darker);
	Widget GetWidget() const { return m_form; }

	void SetRows(const std::vector<ListRow>& rows, Snowflake selected);
	void SetSelectCallback(std::function<void(Snowflake)> fn) { m_onSelect = fn; }
	void Repaint();

private:
	static void ExposeCB(Widget, XtPointer, XtPointer);
	static void ResizeCB(Widget, XtPointer, XtPointer);
	static void InputCB(Widget, XtPointer, XtPointer);
	static void ScrollCB(Widget, XtPointer, XtPointer);
	static void FocusEH(Widget, XtPointer, XEvent*, Boolean*);

	int RowHeight(const ListRow& r) const;
	void Layout();
	void UpdateScrollbar();
	void SetScroll(int y);
	void EnsureVisible(int row);
	int RowAt(int y) const;
	void MoveCursor(int delta);
	void Activate(int row);
	void PaintRow(const ListRow& r, int y, int h, bool selected, bool cursor);

	Widget m_form, m_area, m_scroll;
	const PixelFormat& m_fmt;
	GC m_gc = nullptr;
	Canvas m_canvas;
	int m_iconSize;
	bool m_darker;       // the guild column's darker background

	std::vector<ListRow> m_rows;
	std::vector<int> m_tops;
	int m_layoutTextSize = 0; // GetTextSize() of the layout
	int m_contentHeight = 0;
	int m_scrollY = 0;
	int m_viewW = 1, m_viewH = 1;
	int m_selected = -1;  // row index
	int m_cursor = -1;    // keyboard position
	bool m_focused = false;
	std::function<void(Snowflake)> m_onSelect;
};
