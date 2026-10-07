#include "Xm.hpp"
#include "ImageViewer.hpp"

#include <algorithm>

#include <Xm/DialogS.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/Separator.h>

#include "Fonts.hpp"
#include "ImageCache.hpp"
#include "Theme.hpp"

int AddVisualArgs(Arg* args, int n); // Main.cpp

namespace
{
	struct Viewer
	{
		Widget shell = nullptr, area = nullptr;
		const PixelFormat* fmt = nullptr;
		ImageViewer::Picture pic;
		std::string url;   // pic.url, asked for the size shown
		int w = 0, h = 0;  // the size shown
		Canvas canvas;
		GC gc = nullptr;
	};

	Viewer* g_viewer;

	void Close()
	{
		Viewer* v = g_viewer;
		if (!v)
			return;
		g_viewer = nullptr;
		if (v->gc)
			XFreeGC(XtDisplay(v->area), v->gc);
		XtDestroyWidget(v->shell);
		delete v;
	}

	void Paint(Viewer* v)
	{
		if (!XtIsRealized(v->area))
			return;
		Dimension aw = 0, ah = 0;
		XtVaGetValues(v->area, XmNwidth, &aw, XmNheight, &ah, NULL);
		if (aw < 1 || ah < 1)
			return;
		if (v->canvas.Width() != aw || v->canvas.Height() != ah)
			v->canvas.Resize(aw, ah);
		if (!v->gc)
			v->gc = XCreateGC(XtDisplay(v->area), XtWindow(v->area), 0, NULL);

		const Palette& p = GetPalette();
		Canvas& c = v->canvas;
		c.Fill(0, 0, aw, ah, p.msgBg);
		const Image* img = ImageCache::Get(ImageCache::URL, v->url, 0, v->w, v->h);
		if (img) {
			c.BlendArgb((aw - img->w) / 2, (ah - img->h) / 2, img->px.data(), img->w, img->h, img->w);
		}
		else {
			bool failed = ImageCache::Failed(ImageCache::URL, v->url, 0, v->w, v->h);
			const char* text = failed ? "The image could not be loaded." : "Loading\xe2\x80\xa6";
			int px = GetTextSize();
			int tw = Fonts::Measure(text, FS_ITALIC, px);
			Fonts::Draw(c, (aw - tw) / 2, ah / 2, text, FS_ITALIC, px, p.msgMuted);
		}
		c.Present(*v->fmt, XtWindow(v->area), v->gc, 0, 0, aw, ah, 0, 0);
	}

	void ExposeCB(Widget, XtPointer client, XtPointer call)
	{
		XmDrawingAreaCallbackStruct* cbs = (XmDrawingAreaCallbackStruct*) call;
		if (cbs && cbs->event && cbs->event->type == Expose && cbs->event->xexpose.count > 0)
			return;
		Paint((Viewer*) client);
	}

	void InputCB(Widget, XtPointer, XtPointer call)
	{
		XEvent* ev = ((XmDrawingAreaCallbackStruct*) call)->event;
		if (ev && ev->type == ButtonPress && ev->xbutton.button == Button1)
			Close();
	}

	void CloseCB(Widget, XtPointer, XtPointer)
	{
		Close();
	}

	// w x h scaled to fit in maxW x maxH, never enlarged.
	void Fit(int& w, int& h, int maxW, int maxH)
	{
		if (w > maxW) { h = (int) ((long) h * maxW / w); w = maxW; }
		if (h > maxH) { w = (int) ((long) w * maxH / h); h = maxH; }
		w = std::max(1, w);
		h = std::max(1, h);
	}
}

void ImageViewer::Show(Widget parent, const PixelFormat& fmt, const Picture& pic)
{
	Close();
	Viewer* v = new Viewer;
	g_viewer = v;
	v->fmt = &fmt;
	v->pic = pic;

	// as large as the screen leaves room for, with the buttons and the
	// window manager's frame
	Screen* scr = XtScreen(parent);
	int maxW = WidthOfScreen(scr) * 9 / 10, maxH = HeightOfScreen(scr) * 9 / 10 - 80;
	v->w = pic.width > 0 ? pic.width : maxW / 2;
	v->h = pic.height > 0 ? pic.height : maxH / 2;
	Fit(v->w, v->h, maxW, maxH);
	// Discord's media proxy makes the size asked for, so a 4000-pixel photo
	// is not decoded here at full size
	v->url = pic.url;
	if (pic.width > 0 && pic.height > 0 && (v->w != pic.width || v->h != pic.height))
		v->url += (v->url.find('?') == std::string::npos ? "?" : "&") +
			std::string("width=") + std::to_string(v->w) + "&height=" + std::to_string(v->h);

	Arg args[8];
	int n = 0;
	std::string title = Utf8ToLatin1(pic.title.empty() ? std::string("Image") : pic.title);
	XtSetArg(args[n], XmNtitle, title.c_str()); n++;
	XtSetArg(args[n], XmNdeleteResponse, XmDO_NOTHING); n++;
	n = AddVisualArgs(args, n);
	v->shell = XmCreateDialogShell(parent, (char*) "imageViewer", args, n);
	Atom wmDelete = XmInternAtom(XtDisplay(v->shell), (char*) "WM_DELETE_WINDOW", False);
	XmAddWMProtocolCallback(v->shell, wmDelete, CloseCB, NULL);

	Widget form = XtVaCreateWidget("form", xmFormWidgetClass, v->shell,
		XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL,
		XmNautoUnmanage, False,
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

	// the picture; room for the buttons even when it is small
	v->area = XtVaCreateManagedWidget("picture", xmDrawingAreaWidgetClass, form,
		XmNtopAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, sep,
		XmNwidth, std::max(v->w, 320),
		XmNheight, std::max(v->h, 120),
		XmNresizePolicy, XmRESIZE_NONE,
		NULL);
	XtAddCallback(v->area, XmNexposeCallback, ExposeCB, v);
	XtAddCallback(v->area, XmNinputCallback, InputCB, v);

	// Escape closes, Return too
	XtVaSetValues(form, XmNcancelButton, close, XmNdefaultButton, close, NULL);
	XtManageChild(form);
}

void ImageViewer::ImagesChanged()
{
	if (g_viewer)
		Paint(g_viewer);
}
