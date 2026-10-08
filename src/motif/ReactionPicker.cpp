#include "Xm.hpp"
#include "ReactionPicker.hpp"

#include <algorithm>
#include <vector>

#include <X11/keysym.h>
#include <Xm/DialogS.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/ScrollBar.h>
#include <Xm/Separator.h>

#include "DiscordInstance.hpp"
#include "Fonts.hpp"
#include "ImageCache.hpp"
#include "Shortcodes.hpp"
#include "Theme.hpp"

int AddVisualArgs(Arg* args, int n); // Main.cpp

namespace
{
	const int COLUMNS = 8;
	const int CELL = 38;
	const int HEADER = 24;
	const int VISIBLE_ROWS = 7;

	// a row of the grid: a section's title, or up to COLUMNS emoji
	struct Row
	{
		std::string title;
		int first = 0, count = 0; // into Picker::emoji
		int top = 0, height = 0;
	};

	struct Picker
	{
		Widget shell = nullptr, area = nullptr, scroll = nullptr, name = nullptr;
		const PixelFormat* fmt = nullptr;
		std::function<void(const Reaction&)> picked;
		std::vector<Reaction> emoji;
		std::vector<Row> rows;
		int contentHeight = 0, scrollY = 0;
		int hover = -1;
		Canvas canvas;
		GC gc = nullptr;
	};

	Picker* g_picker;

	void Close()
	{
		Picker* p = g_picker;
		if (!p)
			return;
		g_picker = nullptr;
		if (p->gc)
			XFreeGC(XtDisplay(p->area), p->gc);
		XtDestroyWidget(p->shell);
		delete p;
	}

	// the emoji at (x, y) in the window, or -1
	int EmojiAt(Picker* p, int x, int y)
	{
		int cy = y + p->scrollY;
		for (auto& r : p->rows) {
			if (cy < r.top || cy >= r.top + r.height || !r.count)
				continue;
			int c = x / CELL;
			return c >= 0 && c < r.count ? r.first + c : -1;
		}
		return -1;
	}

	void Paint(Picker* p)
	{
		if (!XtIsRealized(p->area))
			return;
		Dimension aw = 0, ah = 0;
		XtVaGetValues(p->area, XmNwidth, &aw, XmNheight, &ah, NULL);
		if (p->canvas.Width() != aw || p->canvas.Height() != ah)
			p->canvas.Resize(aw, ah);
		if (!p->gc)
			p->gc = XCreateGC(XtDisplay(p->area), XtWindow(p->area), 0, NULL);

		const Palette& pal = GetPalette();
		Canvas& c = p->canvas;
		c.Fill(0, 0, aw, ah, pal.msgBg);
		int px = CELL * 2 / 3; // the colour emoji come out a fifth larger
		int img = CELL - 10;
		for (auto& r : p->rows)
		{
			int y = r.top - p->scrollY;
			if (y + r.height < 0 || y > ah)
				continue;
			if (!r.count) {
				int hpx = GetTextSize() - 2;
				Fonts::Draw(c, 8, y + r.height - 7, Fonts::Elide(r.title, FS_BOLD, hpx, aw - 16), FS_BOLD, hpx, pal.msgMuted);
				continue;
			}
			for (int k = 0; k < r.count; k++)
			{
				int i = r.first + k, x = k * CELL;
				const Reaction& e = p->emoji[i];
				if (i == p->hover)
					c.FillRounded(x + 1, y + 1, CELL - 2, CELL - 2, 6, pal.selBg);
				if (e.m_emojiId) {
					const Image* im = ImageCache::Get(ImageCache::EMOJI, "", e.m_emojiId, img, img);
					if (im)
						c.BlendArgb(x + (CELL - im->w) / 2, y + (CELL - im->h) / 2, im->px.data(), im->w, im->h, im->w);
					else
						c.FillRounded(x + 5, y + 5, img, img, 4, LerpRgb(pal.msgBg, pal.msgMuted, 1, 4));
				}
				else {
					int w = Fonts::Measure(e.m_emojiName, FS_REGULAR, px);
					Fonts::Draw(c, x + (CELL - w) / 2, y + CELL * 3 / 4, e.m_emojiName, FS_REGULAR, px, pal.msgFg);
				}
			}
		}
		c.Present(*p->fmt, XtWindow(p->area), p->gc, 0, 0, aw, ah, 0, 0);
	}

	void SetScroll(Picker* p, int y)
	{
		Dimension ah = 0;
		XtVaGetValues(p->area, XmNheight, &ah, NULL);
		p->scrollY = std::max(0, std::min(y, p->contentHeight - (int) ah));
		XtVaSetValues(p->scroll, XmNvalue, p->scrollY, NULL);
		Paint(p);
	}

	void ShowName(Picker* p)
	{
		std::string text = p->hover >= 0 ? Shortcodes::For(p->emoji[p->hover]) : " ";
		XmString xs = MakeXmString(text);
		XtVaSetValues(p->name, XmNlabelString, xs, NULL);
		XmStringFree(xs);
	}

	void ExposeCB(Widget, XtPointer client, XtPointer call)
	{
		XmDrawingAreaCallbackStruct* cbs = (XmDrawingAreaCallbackStruct*) call;
		if (cbs && cbs->event && cbs->event->type == Expose && cbs->event->xexpose.count > 0)
			return;
		Paint((Picker*) client);
	}

	void ScrollCB(Widget, XtPointer client, XtPointer call)
	{
		Picker* p = (Picker*) client;
		p->scrollY = ((XmScrollBarCallbackStruct*) call)->value;
		Paint(p);
	}

	void InputCB(Widget, XtPointer client, XtPointer call)
	{
		Picker* p = (Picker*) client;
		XEvent* ev = ((XmDrawingAreaCallbackStruct*) call)->event;
		if (!ev)
			return;
		if (ev->type == KeyPress && XLookupKeysym(&ev->xkey, 0) == XK_Escape) {
			Close();
			return;
		}
		if (ev->type != ButtonPress)
			return;
		if (ev->xbutton.button == Button4 || ev->xbutton.button == Button5) {
			SetScroll(p, p->scrollY + (ev->xbutton.button == Button4 ? -CELL * 2 : CELL * 2));
			return;
		}
		if (ev->xbutton.button != Button1)
			return;
		int i = EmojiAt(p, ev->xbutton.x, ev->xbutton.y);
		if (i < 0)
			return;
		auto picked = p->picked;
		Reaction r = p->emoji[i];
		Close();
		picked(r);
	}

	// the emoji under the pointer is lit, and a server one's name shown
	void MotionEH(Widget, XtPointer client, XEvent* ev, Boolean*)
	{
		Picker* p = (Picker*) client;
		int i = ev->type == MotionNotify ? EmojiAt(p, ev->xmotion.x, ev->xmotion.y) : -1;
		if (i != p->hover) {
			p->hover = i;
			ShowName(p);
			Paint(p);
		}
	}

	void CloseCB(Widget, XtPointer, XtPointer)
	{
		Close();
	}

	void AddSection(Picker* p, const std::string& title, int first, int count)
	{
		int y = p->contentHeight;
		if (!title.empty()) {
			Row h;
			h.title = title;
			h.top = y;
			h.height = HEADER;
			p->rows.push_back(h);
			y += HEADER;
		}
		for (int k = 0; k < count; k += COLUMNS) {
			Row r;
			r.first = first + k;
			r.count = std::min(COLUMNS, count - k);
			r.top = y;
			r.height = CELL;
			p->rows.push_back(r);
			y += CELL;
		}
		p->contentHeight = y;
	}
}

void ReactionPicker::Show(Widget parent, const PixelFormat& fmt, const char* title, int x, int y, Snowflake guild,
	std::function<void(const Reaction&)> picked)
{
	Close();
	Picker* p = new Picker;
	g_picker = p;
	p->fmt = &fmt;
	p->picked = picked;

	// the common ones, then the server's own (usable ones, by name)
	for (int i = 0; i < Shortcodes::PICKER_COUNT; i++) {
		Reaction r;
		r.m_emojiName = Shortcodes::PickerEmoji(i);
		p->emoji.push_back(r);
	}
	int common = (int) p->emoji.size();
	std::string server;
	Guild* pGuild = guild ? GetDiscordInstance()->GetGuild(guild) : nullptr;
	if (pGuild) {
		server = pGuild->m_name;
		std::vector<Reaction> own;
		for (auto& e : pGuild->m_emoji) {
			if (!e.second.m_bAvailable || !e.second.m_id)
				continue;
			Reaction r;
			r.m_emojiId = e.second.m_id;
			r.m_emojiName = e.second.m_name;
			r.m_bAnimated = e.second.m_bAnimated;
			own.push_back(r);
		}
		std::sort(own.begin(), own.end(), [](const Reaction& a, const Reaction& b) { return a.m_emojiName < b.m_emojiName; });
		p->emoji.insert(p->emoji.end(), own.begin(), own.end());
	}
	AddSection(p, "", 0, common);
	if ((int) p->emoji.size() > common)
		AddSection(p, server, common, (int) p->emoji.size() - common);

	int w = COLUMNS * CELL;
	int h = std::min(p->contentHeight, VISIBLE_ROWS * CELL + HEADER);
	// by the pointer, but on the screen
	Screen* scr = XtScreen(parent);
	x = std::max(0, std::min(x - w / 2, WidthOfScreen(scr) - w - 40));
	y = std::max(0, std::min(y + 10, HeightOfScreen(scr) - h - 90));

	Arg args[10];
	int n = 0;
	XtSetArg(args[n], XmNtitle, title); n++;
	XtSetArg(args[n], XmNdeleteResponse, XmDO_NOTHING); n++;
	XtSetArg(args[n], XmNx, x); n++;
	XtSetArg(args[n], XmNy, y); n++;
	n = AddVisualArgs(args, n);
	p->shell = XmCreateDialogShell(parent, (char*) "reactionPicker", args, n);
	Atom wmDelete = XmInternAtom(XtDisplay(p->shell), (char*) "WM_DELETE_WINDOW", False);
	XmAddWMProtocolCallback(p->shell, wmDelete, CloseCB, NULL);

	Widget form = XtVaCreateWidget("form", xmFormWidgetClass, p->shell,
		XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL,
		XmNautoUnmanage, False,
		XmNdefaultPosition, False,
		NULL);
	Widget close = XtVaCreateManagedWidget("Close", xmPushButtonWidgetClass, form,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		XmNbottomOffset, 8,
		XmNrightOffset, 10,
		NULL);
	XtAddCallback(close, XmNactivateCallback, CloseCB, NULL);
	p->name = XtVaCreateManagedWidget("name", xmLabelWidgetClass, form,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, close,
		XmNbottomOffset, 12,
		XmNleftOffset, 10,
		XmNalignment, XmALIGNMENT_BEGINNING,
		NULL);
	ShowName(p);
	Widget sep = XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, form,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, close,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		XmNbottomOffset, 8,
		NULL);
	p->scroll = XtVaCreateManagedWidget("scroll", xmScrollBarWidgetClass, form,
		XmNorientation, XmVERTICAL,
		XmNtopAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, sep,
		XmNrightAttachment, XmATTACH_FORM,
		XmNminimum, 0,
		XmNmaximum, std::max(p->contentHeight, h),
		XmNsliderSize, h,
		XmNincrement, CELL,
		XmNpageIncrement, std::max(CELL, h - CELL),
		NULL);
	XtAddCallback(p->scroll, XmNvalueChangedCallback, ScrollCB, p);
	XtAddCallback(p->scroll, XmNdragCallback, ScrollCB, p);
	p->area = XtVaCreateManagedWidget("emoji", xmDrawingAreaWidgetClass, form,
		XmNtopAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, p->scroll,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, sep,
		XmNwidth, w,
		XmNheight, h,
		XmNresizePolicy, XmRESIZE_NONE,
		NULL);
	XtAddCallback(p->area, XmNexposeCallback, ExposeCB, p);
	XtAddCallback(p->area, XmNinputCallback, InputCB, p);
	XtAddEventHandler(p->area, PointerMotionMask | LeaveWindowMask, False, MotionEH, p);

	XtVaSetValues(form, XmNcancelButton, close, XmNinitialFocus, p->area, NULL);
	XtManageChild(form);
}

void ReactionPicker::ImagesChanged()
{
	if (g_picker)
		Paint(g_picker);
}
