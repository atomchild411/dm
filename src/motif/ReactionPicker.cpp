#include "Xm.hpp"
#include "ReactionPicker.hpp"

#include <algorithm>

#include <X11/keysym.h>
#include <Xm/DialogS.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/Separator.h>

#include "Fonts.hpp"
#include "Theme.hpp"

int AddVisualArgs(Arg* args, int n); // Main.cpp

namespace
{
	const char* const g_emoji[] = {
		"\xf0\x9f\x91\x8d", "\xf0\x9f\x91\x8e", "\xe2\x9d\xa4\xef\xb8\x8f", "\xf0\x9f\x98\x82", "\xf0\x9f\xa4\xa3", "\xf0\x9f\x98\x8a",
		"\xf0\x9f\x98\x8d", "\xf0\x9f\x98\xae", "\xf0\x9f\x98\xa2", "\xf0\x9f\x98\xad", "\xf0\x9f\x98\xa1", "\xf0\x9f\x99\x8f",
		"\xf0\x9f\x8e\x89", "\xf0\x9f\x94\xa5", "\xf0\x9f\x92\xaf", "\xf0\x9f\x91\x80", "\xe2\x9c\x85", "\xe2\x9d\x8c",
		"\xf0\x9f\xa4\x94", "\xf0\x9f\x98\x8e", "\xf0\x9f\xa5\xb3", "\xf0\x9f\x98\x85", "\xf0\x9f\x99\x8c", "\xf0\x9f\x91\x8f",
		"\xf0\x9f\x92\xaa", "\xf0\x9f\xa4\x9d", "\xf0\x9f\x91\x8c", "\xe2\x9c\xa8", "\xf0\x9f\x9a\x80", "\xf0\x9f\x92\x80",
		"\xf0\x9f\x98\xb4", "\xf0\x9f\xa4\xaf", "\xf0\x9f\xa5\xba", "\xf0\x9f\x98\xac", "\xf0\x9f\x99\x83", "\xf0\x9f\x98\x87",
		"\xf0\x9f\xa4\x96", "\xf0\x9f\x91\x8b", "\xe2\xad\x90", "\xf0\x9f\x92\x9c", "\xf0\x9f\x92\x99", "\xf0\x9f\x92\x9a",
		"\xf0\x9f\x92\x9b", "\xf0\x9f\xa7\xa1", "\xf0\x9f\x96\xa4", "\xf0\x9f\x8d\x95", "\xe2\x98\x95", "\xf0\x9f\x90\xa7",
	};
	const int COUNT = sizeof g_emoji / sizeof g_emoji[0];
	const int COLUMNS = 8;
	const int CELL = 38;

	struct Picker
	{
		Widget shell = nullptr, area = nullptr;
		const PixelFormat* fmt = nullptr;
		std::function<void(const std::string&)> picked;
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

	int CellAt(int x, int y)
	{
		int c = x / CELL, r = y / CELL;
		int i = r * COLUMNS + c;
		return c >= 0 && c < COLUMNS && r >= 0 && i < COUNT ? i : -1;
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
		for (int i = 0; i < COUNT; i++) {
			int x = (i % COLUMNS) * CELL, y = (i / COLUMNS) * CELL;
			if (i == p->hover)
				c.FillRounded(x + 1, y + 1, CELL - 2, CELL - 2, 6, pal.selBg);
			int w = Fonts::Measure(g_emoji[i], FS_REGULAR, px);
			Fonts::Draw(c, x + (CELL - w) / 2, y + CELL * 3 / 4, g_emoji[i], FS_REGULAR, px, pal.msgFg);
		}
		c.Present(*p->fmt, XtWindow(p->area), p->gc, 0, 0, aw, ah, 0, 0);
	}

	void ExposeCB(Widget, XtPointer client, XtPointer call)
	{
		XmDrawingAreaCallbackStruct* cbs = (XmDrawingAreaCallbackStruct*) call;
		if (cbs && cbs->event && cbs->event->type == Expose && cbs->event->xexpose.count > 0)
			return;
		Paint((Picker*) client);
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
		if (ev->type != ButtonPress || ev->xbutton.button != Button1)
			return;
		int i = CellAt(ev->xbutton.x, ev->xbutton.y);
		if (i < 0)
			return;
		auto picked = p->picked;
		std::string emoji = g_emoji[i];
		Close();
		picked(emoji);
	}

	// the cell under the pointer is lit
	void MotionEH(Widget, XtPointer client, XEvent* ev, Boolean*)
	{
		Picker* p = (Picker*) client;
		int i = ev->type == MotionNotify ? CellAt(ev->xmotion.x, ev->xmotion.y) : -1;
		if (i != p->hover) {
			p->hover = i;
			Paint(p);
		}
	}

	void CloseCB(Widget, XtPointer, XtPointer)
	{
		Close();
	}
}

void ReactionPicker::Show(Widget parent, const PixelFormat& fmt, int x, int y, std::function<void(const std::string&)> picked)
{
	Close();
	Picker* p = new Picker;
	g_picker = p;
	p->fmt = &fmt;
	p->picked = picked;

	int rows = (COUNT + COLUMNS - 1) / COLUMNS;
	int w = COLUMNS * CELL, h = rows * CELL;
	// by the pointer, but on the screen
	Screen* scr = XtScreen(parent);
	x = std::max(0, std::min(x - w / 2, WidthOfScreen(scr) - w - 20));
	y = std::max(0, std::min(y + 10, HeightOfScreen(scr) - h - 90));

	Arg args[10];
	int n = 0;
	XtSetArg(args[n], XmNtitle, "Add Reaction"); n++;
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
	Widget sep = XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, form,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, close,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		XmNbottomOffset, 8,
		NULL);
	p->area = XtVaCreateManagedWidget("emoji", xmDrawingAreaWidgetClass, form,
		XmNtopAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
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
