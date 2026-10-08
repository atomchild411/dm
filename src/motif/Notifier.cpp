#include "Xm.hpp"
#include "Notifier.hpp"

#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <X11/Shell.h>
#include <Xm/DrawingA.h>

#include "DiscordInstance.hpp"
#include "state/NotificationManager.hpp"
#include "Fonts.hpp"
#include "ImageCache.hpp"
#include "Perf.hpp"
#include "Theme.hpp"

int AddVisualArgs(Arg* args, int n); // Main.cpp

namespace
{
	const char* const DEFAULT_SOUND = "/usr/share/data/sounds/soundscheme/soundfiles/08.ting.aifc";
	const char* const PLAYER = "/usr/sbin/sfplay";
	const int POPUP_W = 360, POPUP_H = 74, AVATAR = 42;
	const unsigned long POPUP_MS = 6000;

	Widget g_toplevel;
	const PixelFormat* g_fmt;
	bool g_focused = false;
	double g_lastSound = 0;

	// the popup
	Widget g_popup, g_area;
	Canvas g_canvas;
	GC g_gc;
	XtIntervalId g_popdownTimer;
	Notification g_shown;
	bool g_up = false;

	bool Exists(const char* path)
	{
		struct stat st;
		return path && stat(path, &st) == 0;
	}

	// sfplay in the background: forked twice, so nothing waits for it
	void PlaySound()
	{
		double now = Perf::Now();
		if (now - g_lastSound < 2.0)
			return; // a burst of mentions: one sound
		g_lastSound = now;

		const char* file = getenv("DM_SOUND");
		if (!file || !*file)
			file = DEFAULT_SOUND;
		if (!Exists(PLAYER) || !Exists(file)) {
			XBell(XtDisplay(g_toplevel), 0);
			return;
		}
		pid_t pid = fork();
		if (pid == 0) {
			if (fork() == 0) {
				int null = open("/dev/null", O_RDWR);
				if (null >= 0) {
					dup2(null, 0);
					dup2(null, 1);
					dup2(null, 2);
				}
				execl(PLAYER, "sfplay", file, (char*) NULL);
				_exit(127);
			}
			_exit(0);
		}
		if (pid > 0)
			waitpid(pid, NULL, 0);
	}

	void Popdown()
	{
		if (g_popdownTimer)
			XtRemoveTimeOut(g_popdownTimer);
		g_popdownTimer = 0;
		if (g_up)
			XtPopdown(g_popup);
		g_up = false;
	}

	void PopdownCB(XtPointer, XtIntervalId*)
	{
		g_popdownTimer = 0;
		Popdown();
	}

	// "#channel, Server" or "Direct message"
	std::string Where(const Notification& n)
	{
		DiscordInstance* pInst = GetDiscordInstance();
		Channel* pChan = pInst ? pInst->GetChannelGlobally(n.m_sourceChannel) : nullptr;
		Guild* pGuild = pInst && n.m_sourceGuild ? pInst->GetGuild(n.m_sourceGuild) : nullptr;
		if (!pChan || pChan->IsDM())
			return "direct message";
		std::string s = "#" + pChan->m_name;
		if (pGuild)
			s += ", " + pGuild->m_name;
		return s;
	}

	void Paint()
	{
		if (!g_up || !XtIsRealized(g_area))
			return;
		if (!g_gc)
			g_gc = XCreateGC(XtDisplay(g_area), XtWindow(g_area), 0, NULL);
		if (g_canvas.Width() != POPUP_W || g_canvas.Height() != POPUP_H)
			g_canvas.Resize(POPUP_W, POPUP_H);
		const Palette& p = GetPalette();
		Canvas& c = g_canvas;
		c.Fill(0, 0, POPUP_W, POPUP_H, p.msgBg);
		c.Frame(0, 0, POPUP_W, POPUP_H, p.listMuted);
		c.Fill(1, 1, 4, POPUP_H - 2, p.link);

		int ax = 14, ay = (POPUP_H - AVATAR) / 2;
		const Image* av = g_shown.m_avatarLnk.empty() ?
			ImageCache::Get(ImageCache::DEFAULT_AVATAR, "", g_shown.m_authorID, AVATAR, AVATAR) :
			ImageCache::Get(ImageCache::AVATAR, g_shown.m_avatarLnk, g_shown.m_authorID, AVATAR, AVATAR);
		if (av)
			c.BlendArgbCircle(ax + (AVATAR - av->w) / 2, ay + (AVATAR - av->h) / 2, av->px.data(), av->w, av->h, av->w);
		else
			c.FillCircle(ax, ay, AVATAR, AVATAR, p.link);

		int px = GetTextSize();
		int tx = ax + AVATAR + 12, right = POPUP_W - 10;
		int y1 = 12 + Fonts::Ascent(FS_BOLD, px);
		int w = Fonts::Draw(c, tx, y1, Fonts::Elide(g_shown.m_author, FS_BOLD, px, (right - tx) / 2), FS_BOLD, px, p.msgFg);
		Fonts::Draw(c, tx + w + 6, y1, Fonts::Elide(Where(g_shown), FS_REGULAR, px - 2, right - tx - w - 6), FS_REGULAR, px - 2, p.msgMuted);

		std::string text = g_shown.m_contents.empty() ? std::string("(an attachment)") : g_shown.m_contents;
		for (auto& ch : text)
			if (ch == '\n' || ch == '\t')
				ch = ' ';
		int y2 = y1 + Fonts::LineHeight(FS_REGULAR, px) + 4;
		Fonts::Draw(c, tx, y2, Fonts::Elide(text, FS_REGULAR, px, right - tx), FS_REGULAR, px, p.msgFg);

		c.Present(*g_fmt, XtWindow(g_area), g_gc, 0, 0, POPUP_W, POPUP_H, 0, 0);
	}

	void ExposeCB(Widget, XtPointer, XtPointer call)
	{
		XmDrawingAreaCallbackStruct* cbs = (XmDrawingAreaCallbackStruct*) call;
		if (cbs && cbs->event && cbs->event->type == Expose && cbs->event->xexpose.count > 0)
			return;
		Paint();
	}

	// a click opens the channel, with the window brought back to the front
	void InputCB(Widget, XtPointer, XtPointer call)
	{
		XEvent* ev = ((XmDrawingAreaCallbackStruct*) call)->event;
		if (!ev || ev->type != ButtonPress)
			return;
		Notification n = g_shown;
		Popdown();
		if (ev->xbutton.button != Button1)
			return;
		XMapRaised(XtDisplay(g_toplevel), XtWindow(g_toplevel));
		DiscordInstance* pInst = GetDiscordInstance();
		if (pInst && n.m_sourceChannel)
			pInst->OnSelectGuild(n.m_sourceGuild, n.m_sourceChannel);
	}

	void ShowPopup(const Notification& n)
	{
		if (!g_popup) {
			Screen* scr = XtScreen(g_toplevel);
			Arg args[8];
			int k = 0;
			XtSetArg(args[k], XmNx, WidthOfScreen(scr) - POPUP_W - 16); k++;
			XtSetArg(args[k], XmNy, HeightOfScreen(scr) - POPUP_H - 16); k++;
			XtSetArg(args[k], XmNwidth, POPUP_W); k++;
			XtSetArg(args[k], XmNheight, POPUP_H); k++;
			k = AddVisualArgs(args, k);
			g_popup = XtCreatePopupShell("notification", overrideShellWidgetClass, g_toplevel, args, k);
			g_area = XtVaCreateManagedWidget("area", xmDrawingAreaWidgetClass, g_popup,
				XmNwidth, POPUP_W,
				XmNheight, POPUP_H,
				XmNmarginWidth, 0,
				XmNmarginHeight, 0,
				NULL);
			XtAddCallback(g_area, XmNexposeCallback, ExposeCB, NULL);
			XtAddCallback(g_area, XmNinputCallback, InputCB, NULL);
		}
		g_shown = n;
		if (!g_up) {
			XtPopup(g_popup, XtGrabNone);
			g_up = true;
		}
		XRaiseWindow(XtDisplay(g_popup), XtWindow(g_popup));
		Paint();
		if (g_popdownTimer)
			XtRemoveTimeOut(g_popdownTimer);
		g_popdownTimer = XtAppAddTimeOut(XtWidgetToApplicationContext(g_toplevel), POPUP_MS, PopdownCB, NULL);
	}

	// the main window has the keyboard, or lost it
	void FocusEH(Widget, XtPointer, XEvent* ev, Boolean*)
	{
		if (ev->type == FocusIn && ev->xfocus.detail != NotifyPointer)
			g_focused = true;
		else if (ev->type == FocusOut && ev->xfocus.detail != NotifyPointer && ev->xfocus.detail != NotifyInferior)
			g_focused = false;
		else if (ev->type == UnmapNotify)
			g_focused = false; // iconified
	}
}

void Notifier::Init(Widget toplevel, const PixelFormat& fmt)
{
	g_toplevel = toplevel;
	g_fmt = &fmt;
	XtAddEventHandler(toplevel, FocusChangeMask | StructureNotifyMask, False, FocusEH, NULL);
}

bool Notifier::IsFocused()
{
	return g_focused;
}

void Notifier::OnNotification()
{
	Notification* n = GetNotificationManager() ? GetNotificationManager()->GetLatestNotification() : nullptr;
	if (!n)
		return;
	if (IsNotifyOn(NOTIFY_SOUND))
		PlaySound();
	if (IsNotifyOn(NOTIFY_POPUP) && !g_focused)
		ShowPopup(*n);
}

void Notifier::Test()
{
	Notification n;
	n.m_author = "Grace";
	n.m_contents = "@you the Indigo2 boots from the new disk now \xf0\x9f\x8e\x89 come and see";
	n.m_authorID = (Snowflake) 1002 << 22;
	PlaySound();
	ShowPopup(n);
}

void Notifier::ImagesChanged()
{
	Paint();
}
