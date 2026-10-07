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
		std::string url;   // pic.url, asked for the size fetched
		int w = 0, h = 0;  // the size fetched: the original, or as much as fits the screen
		Image source;      // that picture, once it is here
		bool haveSource = false;
		// the picture as drawn: scaled to fit the window, keeping its shape;
		// quickly while the window is being resized, smoothly once it rests
		Image scaled;
		bool smooth = false;
		XtIntervalId smoothTimer = 0;
		int firstW = -1, firstH = -1; // the window's first size: no enlarging until it changes
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
		if (v->smoothTimer)
			XtRemoveTimeOut(v->smoothTimer);
		if (v->gc)
			XFreeGC(XtDisplay(v->area), v->gc);
		XtDestroyWidget(v->shell);
		delete v;
	}

	// Nearest pixel: quick, for while the window is being resized.
	void ScaleNearest(const Image& s, int w, int h, Image& out)
	{
		out.w = w;
		out.h = h;
		out.px.resize((size_t) w * h);
		for (int y = 0; y < h; y++) {
			const uint32_t* row = &s.px[(size_t) (y * s.h / h) * s.w];
			uint32_t* d = &out.px[(size_t) y * w];
			for (int x = 0; x < w; x++)
				d[x] = row[x * s.w / w];
		}
	}

	// Smooth: when shrinking, the average of the pixels each one covers;
	// when enlarging, bilinear.  Weighted by alpha, so transparent edges do
	// not darken.
	void ScaleSmooth(const Image& s, int w, int h, Image& out)
	{
		out.w = w;
		out.h = h;
		out.px.assign((size_t) w * h, 0);
		bool shrink = w <= s.w && h <= s.h;
		for (int y = 0; y < h; y++)
		{
			for (int x = 0; x < w; x++)
			{
				uint64_t a = 0, r = 0, g = 0, b = 0, n = 0;
				auto add = [&](int sx, int sy, uint64_t weight) {
					uint32_t p = s.px[(size_t) sy * s.w + sx];
					uint64_t pa = (p >> 24) * weight;
					a += pa;
					r += ((p >> 16) & 0xff) * pa;
					g += ((p >> 8) & 0xff) * pa;
					b += (p & 0xff) * pa;
					n += weight;
				};
				if (shrink) {
					int x0 = x * s.w / w, x1 = std::max(x0 + 1, (x + 1) * s.w / w);
					int y0 = y * s.h / h, y1 = std::max(y0 + 1, (y + 1) * s.h / h);
					for (int sy = y0; sy < y1 && sy < s.h; sy++)
						for (int sx = x0; sx < x1 && sx < s.w; sx++)
							add(sx, sy, 1);
				}
				else {
					// the four source pixels around this one's centre, weights in 1/256
					int fx = (int) (((x + 0.5) * s.w / w - 0.5) * 256), fy = (int) (((y + 0.5) * s.h / h - 0.5) * 256);
					fx = std::max(0, fx);
					fy = std::max(0, fy);
					int sx0 = std::min(fx >> 8, s.w - 1), sy0 = std::min(fy >> 8, s.h - 1);
					int sx1 = std::min(sx0 + 1, s.w - 1), sy1 = std::min(sy0 + 1, s.h - 1);
					uint64_t wx = fx & 255, wy = fy & 255;
					add(sx0, sy0, (256 - wx) * (256 - wy));
					add(sx1, sy0, wx * (256 - wy));
					add(sx0, sy1, (256 - wx) * wy);
					add(sx1, sy1, wx * wy);
				}
				if (!n || !a)
					continue;
				out.px[(size_t) y * w + x] = (uint32_t) ((a / n) << 24) |
					(uint32_t) ((r / a) << 16) | (uint32_t) ((g / a) << 8) | (uint32_t) (b / a);
			}
		}
	}

	void Paint(Viewer* v);

	void SmoothCB(XtPointer client, XtIntervalId*)
	{
		Viewer* v = (Viewer*) client;
		v->smoothTimer = 0;
		if (v->haveSource && !v->smooth && v->scaled.w > 0) {
			ScaleSmooth(v->source, v->scaled.w, v->scaled.h, v->scaled);
			v->smooth = true;
			Paint(v);
		}
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
		if (!v->haveSource) {
			const Image* img = ImageCache::Get(ImageCache::URL, v->url, 0, v->w, v->h);
			if (img) {
				v->source = *img; // kept: resizing scales it, it is not fetched again
				v->haveSource = true;
			}
		}
		if (v->firstW < 0) {
			v->firstW = aw;
			v->firstH = ah;
		}
		if (v->haveSource) {
			// as large as fits, the same shape; larger than it came only once
			// the window has been made larger
			const Image& src = v->source;
			double sc = std::min((double) aw / src.w, (double) ah / src.h);
			if (aw == v->firstW && ah == v->firstH && sc > 1.0)
				sc = 1.0;
			int fw = std::max(1, (int) (src.w * sc + 0.5)), fh = std::max(1, (int) (src.h * sc + 0.5));
			if (fw == src.w && fh == src.h) {
				if (v->scaled.w != fw || v->scaled.h != fh || !v->smooth) {
					v->scaled = src;
					v->smooth = true;
				}
			}
			else if (v->scaled.w != fw || v->scaled.h != fh) {
				ScaleNearest(src, fw, fh, v->scaled);
				v->smooth = false;
				if (v->smoothTimer)
					XtRemoveTimeOut(v->smoothTimer);
				v->smoothTimer = XtAppAddTimeOut(XtWidgetToApplicationContext(v->area), 150, SmoothCB, v);
			}
			const Image& img = v->scaled;
			c.BlendArgb((aw - img.w) / 2, (ah - img.h) / 2, img.px.data(), img.w, img.h, img.w);
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

	// Shrinking the window leaves no part of it to expose: draw it again.
	void ResizeCB(Widget, XtPointer client, XtPointer)
	{
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

	// the picture; room for the button even when it is small (it is not
	// enlarged until the window is)
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
	XtAddCallback(v->area, XmNresizeCallback, ResizeCB, v);
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
