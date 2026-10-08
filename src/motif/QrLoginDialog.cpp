#include "QrLoginDialog.hpp"

#include <algorithm>

#include <Xm/DialogS.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/Separator.h>

#include "utils/Util.hpp"
#include "shared/QrLogin.hpp"
#include "Fonts.hpp"
#include "Theme.hpp"

int AddVisualArgs(Arg* args, int n); // Main.cpp

namespace
{
	const int QR_AREA = 300;

	struct Dialog
	{
		Widget shell = nullptr, area = nullptr, status = nullptr, retry = nullptr;
		const PixelFormat* fmt = nullptr;
		GC gc = nullptr;
		Canvas canvas;
		std::function<void(const std::string&)> done;
		std::function<void()> useToken;
	};

	Dialog* g_dialog;

	void Paint()
	{
		Dialog* s = g_dialog;
		if (!s || !XtIsRealized(s->area))
			return;
		Display* dpy = XtDisplay(s->area);
		Window win = XtWindow(s->area);
		if (!s->gc)
			s->gc = XCreateGC(dpy, win, 0, NULL);
		Dimension w = 0, h = 0;
		XtVaGetValues(s->area, XmNwidth, &w, XmNheight, &h, NULL);
		if (s->canvas.Width() != w || s->canvas.Height() != h)
			s->canvas.Resize(w, h);

		Canvas& c = s->canvas;
		c.Fill(0, 0, w, h, 0xffffff);
		int n = QrLogin::CodeSize();
		if (n > 0)
		{
			int quiet = 4;
			int scale = std::max(1, std::min((int) w, (int) h) / (n + 2 * quiet));
			int x0 = ((int) w - n * scale) / 2, y0 = ((int) h - n * scale) / 2;
			for (int y = 0; y < n; y++)
				for (int x = 0; x < n; x++)
					if (QrLogin::CodeModule(x, y))
						c.Fill(x0 + x * scale, y0 + y * scale, scale, scale, 0x000000);
			if (QrLogin::Scanned()) {
				// scanned: grey the code out, as discord.com does
				for (int y = 0; y < (int) h; y++)
					for (int x = 0; x < (int) w; x++)
						if ((x + y) % 3)
							c.Fill(x, y, 1, 1, 0xffffff);
			}
		}
		else
		{
			const char* text = QrLogin::Failed() ? "No code: see below" : "Preparing a code\xe2\x80\xa6";
			int tw = Fonts::Measure(text, FS_ITALIC, 14);
			Fonts::Draw(c, ((int) w - tw) / 2, (int) h / 2, text, FS_ITALIC, 14, 0x606060);
		}
		c.Present(*s->fmt, win, s->gc, 0, 0, w, h, 0, 0);
	}

	// The login's state changed: the code, the status, Try Again.
	void Update()
	{
		if (!g_dialog)
			return;
		XmString xs = XmStringCreateLtoR((char*) Utf8ToLatin1(QrLogin::StatusText()).c_str(), (char*) XmFONTLIST_DEFAULT_TAG);
		XtVaSetValues(g_dialog->status, XmNlabelString, xs, NULL);
		XmStringFree(xs);
		XtSetSensitive(g_dialog->retry, QrLogin::Failed() ? True : False);
		Paint();
	}

	void ExposeCB(Widget, XtPointer, XtPointer)
	{
		Paint();
	}

	void RetryCB(Widget, XtPointer, XtPointer)
	{
		QrLogin::Retry();
	}

	void Close(const std::string& token, bool wantToken)
	{
		Dialog* s = g_dialog;
		if (!s)
			return;
		g_dialog = nullptr;
		QrLogin::Stop();
		if (s->gc)
			XFreeGC(XtDisplay(s->area), s->gc);
		XtDestroyWidget(s->shell);
		auto done = s->done;
		auto useToken = s->useToken;
		delete s;
		if (wantToken)
			useToken();
		else
			done(token);
	}

	void QuitCB(Widget, XtPointer, XtPointer)
	{
		Close("", false);
	}

	void UseTokenCB(Widget, XtPointer, XtPointer)
	{
		Close("", true);
	}

	Widget MakeLabel(Widget form, const char* name, const char* text, Widget above, int offset)
	{
		XmString xs = XmStringCreateLtoR((char*) text, (char*) XmFONTLIST_DEFAULT_TAG);
		Widget w = XtVaCreateManagedWidget(name, xmLabelWidgetClass, form,
			XmNlabelString, xs,
			XmNalignment, XmALIGNMENT_BEGINNING,
			XmNtopAttachment, above ? XmATTACH_WIDGET : XmATTACH_FORM,
			XmNtopWidget, above,
			XmNleftAttachment, XmATTACH_FORM,
			XmNrightAttachment, XmATTACH_FORM,
			XmNtopOffset, offset,
			XmNleftOffset, 12,
			XmNrightOffset, 12,
			NULL);
		XmStringFree(xs);
		return w;
	}
}

void QrLoginDialog::Show(Widget parent, const PixelFormat& fmt, const std::string& message,
	std::function<void(const std::string&)> done, std::function<void()> useToken)
{
	if (g_dialog)
		return;
	Dialog* s = new Dialog;
	g_dialog = s;
	s->fmt = &fmt;
	s->done = done;
	s->useToken = useToken;

	Arg args[8];
	int n = 0;
	XtSetArg(args[n], XmNtitle, "Log in to Discord"); n++;
	XtSetArg(args[n], XmNdeleteResponse, XmDO_NOTHING); n++;
	n = AddVisualArgs(args, n);
	s->shell = XmCreateDialogShell(parent, (char*) "qrlogin", args, n);

	Widget form = XtVaCreateWidget("form", xmFormWidgetClass, s->shell,
		XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL,
		XmNautoUnmanage, False,
		NULL);

	Widget title = MakeLabel(form, "title", "Log in with a QR code", NULL, 10);
	Widget prev = title;
	if (!message.empty())
		prev = MakeLabel(form, "message", Utf8ToLatin1(message).c_str(), title, 6);

	s->area = XtVaCreateManagedWidget("code", xmDrawingAreaWidgetClass, form,
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, prev,
		XmNleftAttachment, XmATTACH_POSITION,
		XmNleftPosition, 50,
		XmNleftOffset, -QR_AREA / 2,
		XmNtopOffset, 10,
		XmNwidth, QR_AREA,
		XmNheight, QR_AREA,
		XmNresizePolicy, XmRESIZE_NONE,
		NULL);
	XtAddCallback(s->area, XmNexposeCallback, ExposeCB, NULL);

	s->status = MakeLabel(form, "status", " \n ", s->area, 10);

	Widget sep = XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, form,
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, s->status,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		XmNtopOffset, 10,
		NULL);

	Widget token = XtVaCreateManagedWidget("Use a Token Instead", xmPushButtonWidgetClass, form,
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, sep,
		XmNleftAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNtopOffset, 10, XmNleftOffset, 12, XmNbottomOffset, 10,
		NULL);
	XtAddCallback(token, XmNactivateCallback, UseTokenCB, NULL);

	s->retry = XtVaCreateManagedWidget("Try Again", xmPushButtonWidgetClass, form,
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, sep,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, token,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNtopOffset, 10, XmNleftOffset, 8, XmNbottomOffset, 10,
		XmNsensitive, False,
		NULL);
	XtAddCallback(s->retry, XmNactivateCallback, RetryCB, NULL);

	Widget quit = XtVaCreateManagedWidget("Quit", xmPushButtonWidgetClass, form,
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, sep,
		XmNrightAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNtopOffset, 10, XmNrightOffset, 12, XmNbottomOffset, 10,
		XmNwidth, 90,
		NULL);
	XtAddCallback(quit, XmNactivateCallback, QuitCB, NULL);

	XtVaSetValues(form, XmNwidth, QR_AREA + 160, NULL);
	XtManageChild(form);
	QrLogin::Start(Update, [](const std::string& token) { Close(token, false); });
}
