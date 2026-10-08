#include "Xm.hpp"
#include "MainWindow.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

#include <X11/Xutil.h>
#include <Xm/CascadeB.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MainW.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/Text.h>
#include <Xm/ToggleB.h>

#include "DiscordInstance.hpp"
#include "Frontend.hpp"
#include "config/LocalSettings.hpp"
#include "state/MessageCache.hpp"
#include "state/ProfileCache.hpp"
#include "MessageView.hpp"
#include "Theme.hpp"
#include "IconList.hpp"
#include "ImageViewer.hpp"
#include "ReactionPicker.hpp"
#include "Shortcodes.hpp"
#include "Notifier.hpp"
#include "ConversationWindow.hpp"
#include "Fonts.hpp"
#include "Perf.hpp"
#include "models/ActiveStatus.hpp"

static MainWindow* g_pMainWindow;

MainWindow* GetMainWindow()
{
	return g_pMainWindow;
}

// Menu items, as client data of the push buttons
enum
{
	MI_RECONNECT = 1,
	MI_LOGOUT,
	MI_QUIT,
	MI_BIGGER,
	MI_SMALLER,
	MI_GUILDS,
	MI_CHANNELS,
	MI_MEMBERS,
	MI_NOTIFY_SOUND,
	MI_NOTIFY_POPUP,
	MI_MARKREAD,
	MI_ABOUT,
};

void RequestLogout();   // Main.cpp
void RequestReconnect(); // Main.cpp
int AddVisualArgs(Arg* args, int n); // Main.cpp


MainWindow::MainWindow(Widget toplevel, const PixelFormat& fmt)
{
	g_pMainWindow = this;
	m_shell = toplevel;
	m_fmt = &fmt;

	m_main = XtVaCreateManagedWidget("main", xmMainWindowWidgetClass, toplevel, NULL);

	Widget menubar = XmCreateMenuBar(m_main, (char*) "menubar", NULL, 0);
	BuildMenus(menubar);
	BuildMessagesMenu(menubar);
	XtManageChild(menubar);

	m_form = XtVaCreateWidget("form", xmFormWidgetClass, m_main,
		XmNwidth, 1000,
		XmNheight, 680,
		NULL);
	InitPalette(m_form);

	Arg args[16];
	int n;

	// guilds on the left, then channels; members on the right
	m_guilds = new IconList(m_form, "guilds", fmt, 28, true);
	m_guildList = m_guilds->GetWidget();
	XtVaSetValues(m_guildList,
		XmNtopAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNtopOffset, 4, XmNleftOffset, 4, XmNbottomOffset, 4,
		XmNwidth, 200,
		NULL);
	m_guilds->SetSelectCallback([this](Snowflake sf) { OnGuildPicked(sf); });

	m_channels = new IconList(m_form, "channels", fmt, 22, false);
	m_channelList = m_channels->GetWidget();
	XtVaSetValues(m_channelList,
		XmNtopAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, m_guildList,
		XmNtopOffset, 4, XmNleftOffset, 2, XmNbottomOffset, 4,
		XmNwidth, 210,
		NULL);
	m_channels->SetSelectCallback([this](Snowflake sf) { OnChannelPicked(sf); });

	m_members = new IconList(m_form, "members", fmt, 24, false);
	m_memberList = m_members->GetWidget();
	XtVaSetValues(m_memberList,
		XmNtopAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		XmNtopOffset, 4, XmNrightOffset, 4, XmNbottomOffset, 4,
		XmNwidth, 200,
		NULL);
	m_memberPane = m_memberList;
	m_members->SetSelectCallback([](Snowflake) {});

	// the middle: header, messages, editor, status
	m_header = XtVaCreateManagedWidget("header", xmLabelWidgetClass, m_form,
		XmNtopAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, m_channelList,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, m_memberPane,
		XmNtopOffset, 6,
		XmNleftOffset, 6,
		XmNrightOffset, 6,
		XmNalignment, XmALIGNMENT_BEGINNING,
		NULL);

	m_status = XtVaCreateManagedWidget("status", xmLabelWidgetClass, m_form,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, m_channelList,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, m_memberPane,
		XmNbottomOffset, 4,
		XmNleftOffset, 6,
		XmNrightOffset, 6,
		XmNalignment, XmALIGNMENT_BEGINNING,
		NULL);

	m_sendButton = XtVaCreateManagedWidget("Send", xmPushButtonWidgetClass, m_form,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, m_status,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, m_memberPane,
		XmNbottomOffset, 4,
		XmNrightOffset, 6,
		NULL);
	XtAddCallback(m_sendButton, XmNactivateCallback, SendCB, this);

	// emoji for the message, as shortcodes (the editor shows ISO 8859-1 only)
	m_emojiButton = XtVaCreateManagedWidget("emoji", xmPushButtonWidgetClass, m_form,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, m_status,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, m_sendButton,
		XmNbottomOffset, 4,
		XmNrightOffset, 4,
		NULL);
	XtAddCallback(m_emojiButton, XmNactivateCallback, EmojiCB, this);
	if ((m_emojiPixmap = MakeEmojiPixmap(m_emojiButton)) != 0)
		XtVaSetValues(m_emojiButton, XmNlabelType, XmPIXMAP, XmNlabelPixmap, m_emojiPixmap, NULL);

	n = 0;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNbottomWidget, m_status); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNleftWidget, m_channelList); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNrightWidget, m_emojiButton); n++;
	XtSetArg(args[n], XmNbottomOffset, 4); n++;
	XtSetArg(args[n], XmNleftOffset, 6); n++;
	XtSetArg(args[n], XmNrightOffset, 6); n++;
	XtSetArg(args[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
	XtSetArg(args[n], XmNrows, 3); n++;
	XtSetArg(args[n], XmNwordWrap, True); n++;
	XtSetArg(args[n], XmNscrollHorizontal, False); n++;
	m_editor = XmCreateScrolledText(m_form, (char*) "editor", args, n);
	XtManageChild(m_editor);
	// Return sends; Shift+Return starts a new line.
	XtOverrideTranslations(m_editor, XtParseTranslationTable(
		"Shift<Key>Return: newline()\n"
		"<Key>Return: activate()\n"
		"<Key>KP_Enter: activate()"));
	XtAddCallback(m_editor, XmNactivateCallback, SendCB, this);
	XtAddCallback(m_editor, XmNvalueChangedCallback, EditorChangedCB, this);

	// "Replying to ...": shown above the editor while a reply is being written
	m_replyBar = XtVaCreateWidget("replyBar", xmFormWidgetClass, m_form,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, XtParent(m_editor),
		XmNbottomOffset, 4,
		NULL);
	Widget cancelReply = XtVaCreateManagedWidget("Cancel", xmPushButtonWidgetClass, m_replyBar,
		XmNtopAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		NULL);
	XtAddCallback(cancelReply, XmNactivateCallback, CancelReplyCB, this);
	m_replyLabel = XtVaCreateManagedWidget("replyLabel", xmLabelWidgetClass, m_replyBar,
		XmNtopAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, cancelReply,
		XmNalignment, XmALIGNMENT_BEGINNING,
		NULL);

	m_messages = new MessageView(m_form, fmt);
	XtVaSetValues(m_messages->GetWidget(),
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, m_header,
		XmNbottomAttachment, XmATTACH_WIDGET,
		XmNbottomWidget, XtParent(m_editor),
		XmNleftAttachment, XmATTACH_WIDGET,
		XmNleftWidget, m_channelList,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, m_memberPane,
		XmNtopOffset, 6,
		XmNbottomOffset, 6,
		XmNleftOffset, 6,
		XmNrightOffset, 6,
		NULL);

	ApplyPanes();
	XtManageChild(m_form);
	XmMainWindowSetAreas(m_main, menubar, NULL, NULL, NULL, m_form);

	SetStatus("");
	UpdateHeader();
}

void MainWindow::BuildMenus(Widget menubar)
{
	struct Item { const char* label; int id; char mnemonic; };
	struct Menu { const char* label; char mnemonic; std::vector<Item> items; };
	std::vector<Menu> menus = {
		{ "File", 'F', {
			{ "Reconnect to Discord", MI_RECONNECT, 'R' },
			{ "Mark Channel Read", MI_MARKREAD, 'M' },
			{ "-", 0, 0 },
			{ "Log Out", MI_LOGOUT, 'L' },
			{ "Quit", MI_QUIT, 'Q' },
		} },
		{ "View", 'V', {
			{ "Larger Text", MI_BIGGER, 'L' },
			{ "Smaller Text", MI_SMALLER, 'S' },
			{ "-", 0, 0 },
			{ "Server List", MI_GUILDS, 'v' },
			{ "Channel List", MI_CHANNELS, 'C' },
			{ "Member List", MI_MEMBERS, 'M' },
			{ "-", 0, 0 },
			{ "Sound for Mentions and DMs", MI_NOTIFY_SOUND, 'o' },
			{ "Popups for Mentions and DMs", MI_NOTIFY_POPUP, 'P' },
		} },
		{ "Help", 'H', {
			{ "About Discord Messenger", MI_ABOUT, 'A' },
		} },
	};

	for (auto& menu : menus)
	{
		Arg args[4];
		int n = AddVisualArgs(args, 0);
		Widget pulldown = XmCreatePulldownMenu(menubar, (char*) "pulldown", args, n);
		Widget cascade = XtVaCreateManagedWidget(menu.label, xmCascadeButtonWidgetClass, menubar,
			XmNsubMenuId, pulldown,
			XmNmnemonic, (KeySym) menu.mnemonic,
			NULL);
		if (!strcmp(menu.label, "Help"))
			XtVaSetValues(menubar, XmNmenuHelpWidget, cascade, NULL);

		for (auto& item : menu.items)
		{
			if (!strcmp(item.label, "-")) {
				XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, pulldown, NULL);
				continue;
			}
			Widget b;
			if (item.id == MI_NOTIFY_SOUND || item.id == MI_NOTIFY_POPUP) {
				Notify kind = item.id == MI_NOTIFY_SOUND ? NOTIFY_SOUND : NOTIFY_POPUP;
				b = XtVaCreateManagedWidget(item.label, xmToggleButtonWidgetClass, pulldown,
					XmNset, IsNotifyOn(kind) ? True : False, XmNmnemonic, (KeySym) item.mnemonic, NULL);
				XtAddCallback(b, XmNvalueChangedCallback, MenuCB, (XtPointer) (long) item.id);
			}
			else if (item.id == MI_GUILDS || item.id == MI_CHANNELS || item.id == MI_MEMBERS) {
				Pane pane = item.id == MI_GUILDS ? PANE_GUILDS : item.id == MI_CHANNELS ? PANE_CHANNELS : PANE_MEMBERS;
				b = XtVaCreateManagedWidget(item.label, xmToggleButtonWidgetClass, pulldown,
					XmNset, IsPaneShown(pane) ? True : False, XmNmnemonic, (KeySym) item.mnemonic, NULL);
				XtAddCallback(b, XmNvalueChangedCallback, MenuCB, (XtPointer) (long) item.id);
			}
			else {
				b = XtVaCreateManagedWidget(item.label, xmPushButtonWidgetClass, pulldown,
					XmNmnemonic, (KeySym) item.mnemonic, NULL);
				XtAddCallback(b, XmNactivateCallback, MenuCB, (XtPointer) (long) item.id);
			}
		}
	}
}

// Messages, after View: the direct messages, made afresh each time the menu
// opens; its title counts the unread ones.
void MainWindow::BuildMessagesMenu(Widget menubar)
{
	Arg args[4];
	int n = AddVisualArgs(args, 0);
	m_dmMenu = XmCreatePulldownMenu(menubar, (char*) "messagesMenu", args, n);
	m_dmCascade = XtVaCreateManagedWidget("Messages", xmCascadeButtonWidgetClass, menubar,
		XmNsubMenuId, m_dmMenu,
		XmNmnemonic, (KeySym) 'M',
		NULL);
	XtAddCallback(m_dmCascade, XmNcascadingCallback, MessagesCascadingCB, this);
}

void MainWindow::MessagesCascadingCB(Widget, XtPointer client, XtPointer)
{
	MainWindow* self = (MainWindow*) client;
	WidgetList kids = nullptr;
	Cardinal count = 0;
	XtVaGetValues(self->m_dmMenu, XmNchildren, &kids, XmNnumChildren, &count, NULL);
	std::vector<Widget> old(kids, kids + count);
	for (Widget w : old)
		XtDestroyWidget(w);
	self->m_dmItems.clear();

	auto add = [self](const std::string& label, Snowflake channel) {
		XmString xs = MakeXmString(label);
		Widget b = XtVaCreateManagedWidget("dm", xmPushButtonWidgetClass, self->m_dmMenu,
			XmNlabelString, xs, NULL);
		XmStringFree(xs);
		XtAddCallback(b, XmNactivateCallback, MessagesItemCB, (XtPointer) (long) self->m_dmItems.size());
		self->m_dmItems.push_back(channel);
	};
	add("All Direct Messages", 0);

	DiscordInstance* pInst = GetDiscordInstance();
	Guild* dms = pInst ? pInst->GetGuild(0) : nullptr;
	if (!dms || dms->m_channels.empty())
		return;
	XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, self->m_dmMenu, NULL);

	// unread first, then the most recently active
	std::vector<const Channel*> chans;
	for (auto& ch : dms->m_channels)
		if (ch.IsDM() || ch.m_channelType == Channel::GROUPDM)
			chans.push_back(&ch);
	std::sort(chans.begin(), chans.end(), [](const Channel* a, const Channel* b) {
		bool ua = a->m_mentionCount > 0, ub = b->m_mentionCount > 0;
		if (ua != ub)
			return ua;
		return a->m_lastSentMsg > b->m_lastSentMsg;
	});
	const size_t MAX_ITEMS = 25;
	for (size_t i = 0; i < chans.size() && i < MAX_ITEMS; i++) {
		const Channel* ch = chans[i];
		std::string label = ch->m_name.empty() ? std::string("(unnamed)") : ch->m_name;
		if (ch->m_mentionCount > 0)
			label += "  (" + std::to_string(ch->m_mentionCount) + ")";
		add(label, ch->m_snowflake);
	}
}

void MainWindow::MessagesItemCB(Widget, XtPointer client, XtPointer)
{
	MainWindow* self = g_pMainWindow;
	size_t i = (size_t) (long) client;
	DiscordInstance* pInst = GetDiscordInstance();
	if (!pInst || i >= self->m_dmItems.size())
		return;
	Snowflake channel = self->m_dmItems[i];
	// a conversation opens in a window of its own: this one keeps its place
	if (channel)
		Conversations::Open(channel);
	else
		pInst->OnSelectGuild(0);
}

void MainWindow::MenuCB(Widget w, XtPointer client, XtPointer)
{
	MainWindow* self = g_pMainWindow;
	switch ((int) (long) client)
	{
		case MI_RECONNECT:
			RequestReconnect();
			break;
		case MI_LOGOUT:
			RequestLogout();
			break;
		case MI_QUIT:
			GetFrontend()->RequestQuit();
			break;
		case MI_BIGGER:
		case MI_SMALLER:
			SetTextSize(GetTextSize() + ((int) (long) client == MI_BIGGER ? 1 : -1));
			SaveMotifConfig();
			self->m_messages->Relayout();
			Conversations::Relayout();
			self->UpdateGuildList();
			self->UpdateChannelList();
			self->UpdateMemberList();
			break;
		case MI_NOTIFY_SOUND:
		case MI_NOTIFY_POPUP:
			SetNotifyOn((int) (long) client == MI_NOTIFY_SOUND ? NOTIFY_SOUND : NOTIFY_POPUP, XmToggleButtonGetState(w));
			SaveMotifConfig();
			break;
		case MI_GUILDS:
		case MI_CHANNELS:
		case MI_MEMBERS: {
			int id = (int) (long) client;
			Pane pane = id == MI_GUILDS ? PANE_GUILDS : id == MI_CHANNELS ? PANE_CHANNELS : PANE_MEMBERS;
			SetPaneShown(pane, XmToggleButtonGetState(w));
			SaveMotifConfig();
			if (pane == PANE_MEMBERS && IsPaneShown(PANE_MEMBERS))
				self->UpdateMemberList(); // not kept up to date while hidden
			self->ApplyPanes();
			break;
		}
		case MI_MARKREAD: {
			DiscordInstance* pInst = GetDiscordInstance();
			if (pInst && pInst->GetCurrentChannelID())
				pInst->RequestAcknowledgeChannel(pInst->GetCurrentChannelID());
			break;
		}
		case MI_ABOUT:
			self->ShowError("Discord Messenger for IRIX\n\nA Discord-compatible messenger by iProgramInCpp and contributors,\nported to IRIX with Motif.\n\nNote: third-party clients are against Discord's terms of service.");
			break;
	}
}

void MainWindow::ApplyPanes()
{
	bool guilds = IsPaneShown(PANE_GUILDS), channels = IsPaneShown(PANE_CHANNELS), members = IsPaneShown(PANE_MEMBERS);
	struct { Widget w; bool shown; } panes[] = {
		{ m_guildList, guilds }, { m_channelList, channels }, { m_memberPane, members },
	};

	// The form lays its children out again at every change, and an
	// attachment to a widget it is not managing confuses it ("Bailed out
	// of edge synchronization"): so the panes that show are managed first,
	// then attached to, and those that hide are let go of last.
	for (auto& p : panes)
		if (p.shown)
			XtManageChild(p.w);

	// left to right: servers, channels, the middle, members; each attaches
	// to the one before it that shows, or to the window's edge
	if (guilds)
		XtVaSetValues(m_channelList, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, m_guildList, XmNleftOffset, 2, NULL);
	else
		XtVaSetValues(m_channelList, XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 4, NULL);

	Widget left = channels ? m_channelList : guilds ? m_guildList : NULL;
	Widget middle[] = { m_header, m_messages->GetWidget(), XtParent(m_editor), m_status, m_replyBar };
	for (Widget mw : middle) {
		if (left)
			XtVaSetValues(mw, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, left, XmNleftOffset, 6, NULL);
		else
			XtVaSetValues(mw, XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 4, NULL);
	}
	Widget right[] = { m_header, m_messages->GetWidget(), m_sendButton, m_status, m_replyBar };
	for (Widget rw : right) {
		if (members)
			XtVaSetValues(rw, XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, m_memberPane, XmNrightOffset, 6, NULL);
		else
			XtVaSetValues(rw, XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 4, NULL);
	}

	for (auto& p : panes)
		if (!p.shown)
			XtUnmanageChild(p.w);
}

void MainWindow::BeginReply(Snowflake message, const std::string& author)
{
	m_replyTo = message;
	XmString xs = MakeXmString("Replying to " + author);
	XtVaSetValues(m_replyLabel, XmNlabelString, xs, NULL);
	XmStringFree(xs);
	// shown first, then the messages made to end above it (see ApplyPanes)
	if (!XtIsManaged(m_replyBar)) {
		XtManageChild(m_replyBar);
		XtVaSetValues(m_messages->GetWidget(), XmNbottomWidget, m_replyBar, NULL);
	}
	XmProcessTraversal(m_editor, XmTRAVERSE_CURRENT);
}

void MainWindow::RestoreLastChannel()
{
	if (m_restoredLast)
		return;
	m_restoredLast = true;
	DiscordInstance* pInst = GetDiscordInstance();
	Snowflake guild = 0, channel = 0;
	GetLastChannel(guild, channel);
	Guild* pGuild = pInst->GetGuild(guild);
	if (!channel || !pGuild || !pGuild->GetChannel(channel))
		return; // gone (or never saved): the login's choice stays
	pInst->OnSelectGuild(guild, channel);
}

void MainWindow::BeginEdit(Snowflake message, const std::string& text)
{
	m_replyTo = 0;
	m_editing = message;
	XmString xs = MakeXmString("Editing your message");
	XtVaSetValues(m_replyLabel, XmNlabelString, xs, NULL);
	XmStringFree(xs);
	if (!XtIsManaged(m_replyBar)) {
		XtManageChild(m_replyBar);
		XtVaSetValues(m_messages->GetWidget(), XmNbottomWidget, m_replyBar, NULL);
	}
	std::string shown = Utf8ToLatin1(Shortcodes::ToEditor(text));
	XmTextSetString(m_editor, (char*) shown.c_str());
	XmTextSetInsertionPosition(m_editor, XmTextGetLastPosition(m_editor));
	XmProcessTraversal(m_editor, XmTRAVERSE_CURRENT);
}

// The emoji button's picture: a smiling face from the colour emoji font, on
// the button's own background.
Pixmap MainWindow::MakeEmojiPixmap(Widget button)
{
	Display* dpy = XtDisplay(button);
	Pixel bgPixel = 0;
	Colormap cmap = 0;
	XtVaGetValues(button, XmNbackground, &bgPixel, XmNcolormap, &cmap, NULL);
	XColor xc;
	xc.pixel = bgPixel;
	XQueryColor(dpy, cmap, &xc);
	Rgb bg = MakeRgb(xc.red >> 8, xc.green >> 8, xc.blue >> 8);

	int px = 17, w = 26, h = 24;
	Canvas c;
	c.Resize(w, h);
	c.Fill(0, 0, w, h, bg);
	const char* face = "\xf0\x9f\x99\x82"; // U+1F642
	int tw = Fonts::Measure(face, FS_REGULAR, px);
	Fonts::Draw(c, (w - tw) / 2, h - 5, face, FS_REGULAR, px, 0);
	Pixmap pm = XCreatePixmap(dpy, RootWindow(dpy, DefaultScreen(dpy)), w, h, m_fmt->GetDepth());
	GC gc = XCreateGC(dpy, pm, 0, NULL);
	c.Present(*m_fmt, pm, gc, 0, 0, w, h, 0, 0);
	XFreeGC(dpy, gc);
	return pm;
}

void MainWindow::EmojiCB(Widget w, XtPointer client, XtPointer)
{
	MainWindow* self = (MainWindow*) client;
	Position x = 0, y = 0;
	XtTranslateCoords(w, 0, 0, &x, &y);
	// above the button, where the editor is
	ReactionPicker::Show(self->m_shell, *self->m_fmt, "Insert Emoji", x, y - 380,
		GetDiscordInstance() ? GetDiscordInstance()->GetCurrentGuildID() : 0,
		[self](const Reaction& r) {
			std::string code = Utf8ToLatin1(Shortcodes::For(r));
			XmTextPosition at = XmTextGetInsertionPosition(self->m_editor);
			XmTextInsert(self->m_editor, at, (char*) code.c_str());
			XmTextSetInsertionPosition(self->m_editor, at + (XmTextPosition) code.size());
			XmProcessTraversal(self->m_editor, XmTRAVERSE_CURRENT);
		});
}

void MainWindow::CancelReply()
{
	if (m_editing)
		XmTextSetString(m_editor, (char*) ""); // the edit is dropped
	m_editing = 0;
	m_replyTo = 0;
	if (XtIsManaged(m_replyBar)) {
		XtVaSetValues(m_messages->GetWidget(), XmNbottomWidget, XtParent(m_editor), NULL);
		XtUnmanageChild(m_replyBar);
	}
}

void MainWindow::CancelReplyCB(Widget, XtPointer client, XtPointer)
{
	((MainWindow*) client)->CancelReply();
}

void MainWindow::ShowError(const std::string& text)
{
	Arg args[8];
	int n = 0;
	XmString msg = XmStringCreateLtoR((char*) Utf8ToLatin1(text).c_str(), (char*) XmFONTLIST_DEFAULT_TAG);
	XtSetArg(args[n], XmNmessageString, msg); n++;
	XtSetArg(args[n], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL); n++;
	XtSetArg(args[n], XmNtitle, "Discord Messenger"); n++;
	n = AddVisualArgs(args, n);
	Widget dlg = XmCreateInformationDialog(m_shell, (char*) "message", args, n);
	XmStringFree(msg);
	XtUnmanageChild(XmMessageBoxGetChild(dlg, XmDIALOG_CANCEL_BUTTON));
	XtUnmanageChild(XmMessageBoxGetChild(dlg, XmDIALOG_HELP_BUTTON));
	XtAddCallback(dlg, XmNokCallback, [](Widget w, XtPointer, XtPointer) { XtDestroyWidget(XtParent(w)); }, NULL);
	XtManageChild(dlg);
}

bool MainWindow::IsIconic() const
{
	if (!XtIsRealized(m_shell))
		return false;
	XWindowAttributes wa;
	XGetWindowAttributes(XtDisplay(m_shell), XtWindow(m_shell), &wa);
	return wa.map_state != IsViewable;
}

void MainWindow::UpdateGuildList()
{
	Perf::Scope perf(Perf::GUILDS);
	DiscordInstance* pInst = GetDiscordInstance();
	std::vector<Snowflake> ids;
	pInst->GetGuildIDsOrdered(ids, true);

	std::vector<IconRow> rows;
	bool inFolder = false;
	for (Snowflake sf : ids)
	{
		if (sf == 1) {
			// the gap after Direct Messages
			IconRow r;
			r.type = IconRow::SPACE;
			rows.push_back(r);
			continue;
		}
		if (sf & BIT_FOLDER) {
			inFolder = sf != BIT_FOLDER;
			if (inFolder) {
				IconRow r;
				r.type = IconRow::HEADER;
				r.text = pInst->GetGuildFolderName(sf & ~BIT_FOLDER);
				rows.push_back(r);
			}
			continue;
		}
		IconRow r;
		r.id = sf;
		r.indent = inFolder ? 8 : 0;
		if (sf == 0) {
			r.text = GetFrontend()->GetDirectMessagesText();
			r.glyph = "@";
			// unread direct messages (each counts as a mention)
			if (Guild* dms = pInst->GetGuild(0)) {
				for (auto& ch : dms->m_channels)
					r.mentions += ch.m_mentionCount;
				r.unread = r.mentions > 0;
			}
		}
		else {
			Guild* pGuild = pInst->GetGuild(sf);
			if (!pGuild)
				continue;
			r.text = pGuild->m_name;
			r.initials = true;
			if (!pGuild->m_avatarlnk.empty()) {
				r.hasImage = true;
				r.imageKind = ImageCache::ICON;
				r.imagePlace = pGuild->m_avatarlnk;
				r.imageSf = sf;
			}
			// unread: any channel the user can see with newer messages
			int mentions = 0;
			bool unread = false;
			for (auto& ch : pGuild->m_channels) {
				mentions += ch.m_mentionCount;
				if (ch.HasUnreadMessages() && ch.HasPermissionConst(PERM_VIEW_CHANNEL) &&
					!pInst->IsChannelMuted(sf, ch.m_snowflake))
					unread = true;
			}
			r.unread = unread;
			r.mentions = mentions;
		}
		rows.push_back(r);
	}
	m_guilds->SetRows(rows, pInst->GetCurrentGuildID());
}

void MainWindow::UpdateSelectedGuild()
{
	UpdateGuildList();
	UpdateChannelList();
	UpdateMemberList();
	UpdateHeader();
	UpdateTitle();
}

static bool IsTextChannel(const Channel& ch)
{
	switch (ch.m_channelType) {
		case Channel::TEXT: case Channel::DM: case Channel::GROUPDM: case Channel::NEWS:
		case Channel::NEWSTHREAD: case Channel::PUBTHREAD: case Channel::PRIVTHREAD:
			return true;
		default:
			return false;
	}
}

void MainWindow::UpdateChannelList()
{
	Perf::Scope perf(Perf::CHANNELS);
	DiscordInstance* pInst = GetDiscordInstance();
	Guild* pGuild = pInst->GetCurrentGuild();
	std::vector<IconRow> rows;

	if (pGuild && !pGuild->m_bChannelsLoaded) {
		pGuild->RequestFetchChannels();
		IconRow r;
		r.type = IconRow::HEADER;
		r.text = GetFrontend()->GetPleaseWaitText();
		rows.push_back(r);
	}
	else if (pGuild)
	{
		std::vector<const Channel*> chans;
		for (auto& ch : pGuild->m_channels)
			chans.push_back(&ch);
		std::stable_sort(chans.begin(), chans.end(), [](const Channel* a, const Channel* b) { return *a < *b; });

		auto addChannel = [&](const Channel* ch) {
			if (!ch->HasPermissionConst(PERM_VIEW_CHANNEL))
				return;
			IconRow r;
			r.id = ch->m_snowflake;
			r.text = ch->m_name;
			r.mentions = ch->m_mentionCount;
			r.unread = ch->HasUnreadMessages() && !pInst->IsChannelMuted(pGuild->m_snowflake, ch->m_snowflake);
			r.selectable = IsTextChannel(*ch);
			r.dim = !r.selectable;
			if (ch->m_channelType == Channel::DM) {
				Snowflake who = ch->m_recipients.empty() ? 0 : ch->m_recipients[0];
				r.hasImage = true;
				r.imageKind = ch->m_avatarLnk.empty() ? ImageCache::DEFAULT_AVATAR : ImageCache::AVATAR;
				r.imagePlace = ch->m_avatarLnk;
				r.imageSf = who;
				r.colorSeed = who;
				Profile* pf = who ? GetProfileCache()->LookupProfile(who, "", "", "", false) : nullptr;
				r.status = pf ? (int) pf->m_activeStatus : -1;
			}
			else if (ch->m_channelType == Channel::GROUPDM) {
				r.initials = true;
				if (!ch->m_avatarLnk.empty()) {
					r.hasImage = true;
					r.imageKind = ImageCache::CHANNEL_ICON;
					r.imagePlace = ch->m_avatarLnk;
					r.imageSf = ch->m_snowflake;
				}
			}
			else if (ch->m_channelType == Channel::VOICE || ch->m_channelType == Channel::STAGEVOICE)
				r.glyph = "\xe2\x99\xaa"; // a note: voice
			else if (ch->m_channelType == Channel::FORUM || ch->m_channelType == Channel::MEDIA)
				r.glyph = "\xe2\x96\xa4";
			else
				r.glyph = "#";
			rows.push_back(r);
		};

		// channels outside categories first, then each category's
		for (const Channel* ch : chans)
			if (!ch->IsCategory() && ch->m_parentCateg == 0)
				addChannel(ch);
		for (const Channel* cat : chans)
		{
			if (!cat->IsCategory())
				continue;
			IconRow h;
			h.type = IconRow::HEADER;
			h.text = cat->m_name;
			for (auto& c : h.text)
				if (c >= 'a' && c <= 'z') c -= 32;
			size_t before = rows.size();
			rows.push_back(h);
			for (const Channel* ch : chans)
				if (!ch->IsCategory() && ch->m_parentCateg == cat->m_snowflake)
					addChannel(ch);
			if (rows.size() == before + 1)
				rows.pop_back(); // an empty (or wholly hidden) category
		}
	}

	m_channels->SetRows(rows, pInst->GetCurrentChannelID());
}

void MainWindow::UpdateSelectedChannel()
{
	DiscordInstance* pInst = GetDiscordInstance();
	CancelReply(); // a reply belongs to its channel
	// remembered for the next start (not before the last one was opened
	// again: the first channel the login selects is not the user's choice)
	if (m_restoredLast && pInst->GetCurrentChannelID()) {
		Snowflake g = 0, c = 0;
		GetLastChannel(g, c);
		if (g != pInst->GetCurrentGuildID() || c != pInst->GetCurrentChannelID()) {
			SetLastChannel(pInst->GetCurrentGuildID(), pInst->GetCurrentChannelID());
			SaveMotifConfig();
		}
	}
	UpdateChannelList();
	UpdateHeader();
	UpdateTitle();
	UpdateTypingStatus();

	// the channel's saved messages show at once; its newest are fetched as usual
	GetMessageCache()->LoadCachedChannel(pInst->GetCurrentChannelID(), pInst->GetCurrentGuildID());
	m_messages->SetChannel(pInst->GetCurrentGuildID(), pInst->GetCurrentChannelID());
	if (pInst->GetCurrentChannel())
		pInst->HandledChannelSwitch();
	m_messages->Refresh();
}

// The colour of the member's highest coloured role, or 0.
Rgb RoleColor(Snowflake user, Snowflake guild)
{
	Guild* pGuild = GetDiscordInstance()->GetGuild(guild);
	if (!pGuild || !guild)
		return 0;
	Profile* pf = GetProfileCache()->LookupProfile(user, "", "", "", false);
	if (!pf)
		return 0;
	auto gm = pf->m_guildMembers.find(guild);
	if (gm == pf->m_guildMembers.end())
		return 0;
	int bestPos = -1;
	Rgb best = 0;
	for (Snowflake role : gm->second.m_roles) {
		auto it = pGuild->m_roles.find(role);
		if (it == pGuild->m_roles.end())
			continue;
		if (it->second.m_colorOriginal && it->second.m_position > bestPos) {
			bestPos = it->second.m_position;
			best = (Rgb) it->second.m_colorOriginal;
		}
	}
	return best;
}

void MainWindow::UpdateMemberList()
{
	if (!IsPaneShown(PANE_MEMBERS))
		return; // made when it is shown
	Perf::Scope perf(Perf::MEMBERS);
	DiscordInstance* pInst = GetDiscordInstance();
	Guild* pGuild = pInst->GetCurrentGuild();
	std::vector<IconRow> rows;
	if (pGuild && pGuild->m_snowflake != 0)
	{
		for (Snowflake sf : pGuild->m_members)
		{
			GuildMember* gm = pGuild->GetGuildMember(sf);
			if (!gm)
				continue;
			if (gm->m_bIsGroup) {
				if (gm->m_groupCount) {
					IconRow h;
					h.type = IconRow::HEADER;
					h.text = pGuild->GetGroupName(gm->m_groupId) + " \xe2\x80\x94 " + std::to_string(gm->m_groupCount);
					rows.push_back(h);
				}
				continue;
			}
			Profile* p = GetProfileCache()->LookupProfile(gm->m_user, "", "", "", false);
			IconRow r;
			r.id = gm->m_user;
			r.text = p ? p->GetName(pGuild->m_snowflake) : std::string("?");
			r.hasImage = true;
			r.colorSeed = gm->m_user;
			std::string av = !gm->m_avatar.empty() ? gm->m_avatar : (p ? p->m_avatarlnk : "");
			r.imageKind = av.empty() ? ImageCache::DEFAULT_AVATAR : ImageCache::AVATAR;
			r.imagePlace = av;
			r.imageSf = gm->m_user;
			r.status = p ? (int) p->m_activeStatus : -1;
			r.dim = p && p->m_activeStatus == STATUS_OFFLINE;
			r.textColor = RoleColor(gm->m_user, pGuild->m_snowflake);
			rows.push_back(r);
		}
	}
	m_members->SetRows(rows, 0);
}

void MainWindow::ShowDemoLists()
{
	auto item = [](Snowflake id, const char* text) { IconRow r; r.id = id; r.text = text; return r; };
	auto header = [](const char* text) { IconRow r; r.type = IconRow::HEADER; r.text = text; return r; };
	std::vector<IconRow> g;
	IconRow dm = item(1, "Direct Messages"); dm.glyph = "@"; g.push_back(dm);
	IconRow sp; sp.type = IconRow::SPACE; g.push_back(sp);
	const char* names[] = { "Silicon Graphics User Group", "Vintage Computer CH", "Demoscene", "IRIX Network \xe2\x9c\xa8", "Octane Owners" };
	for (int i = 0; i < 5; i++) {
		IconRow r = item(100 + i, names[i]);
		r.initials = true;
		r.colorSeed = (Snowflake) (i * 3 + 1) << 22;
		r.unread = i == 1;
		r.mentions = i == 2 ? 3 : 0;
		if (i == 0) {
			r.hasImage = true;
			r.imageKind = ImageCache::DEFAULT_AVATAR;
			r.imageSf = (Snowflake) 2 << 22;
		}
		g.push_back(r);
	}
	g.push_back(header("Folder"));
	IconRow f = item(200, "Tezro Fans"); f.initials = true; f.indent = 8; g.push_back(f);
	m_guilds->SetRows(g, 100);

	std::vector<IconRow> c;
	c.push_back(header("INFORMATION"));
	IconRow c1 = item(300, "rules"); c1.glyph = "#"; c.push_back(c1);
	IconRow c2 = item(301, "announcements \xf0\x9f\x93\xa2"); c2.glyph = "#"; c2.unread = true; c.push_back(c2);
	c.push_back(header("TEXT CHANNELS"));
	IconRow c3 = item(302, "general"); c3.glyph = "#"; c.push_back(c3);
	IconRow c4 = item(303, "marketplace"); c4.glyph = "#"; c4.mentions = 2; c4.unread = true; c.push_back(c4);
	IconRow c5 = item(304, "Lounge"); c5.glyph = "\xe2\x99\xaa"; c5.selectable = false; c5.dim = true; c.push_back(c5);
	m_channels->SetRows(c, 302);

	std::vector<IconRow> m;
	m.push_back(header("Online \xe2\x80\x94 3"));
	const char* who[] = { "Ada", "Grace", "Dennis" };
	Rgb colors[] = { 0xe91e63, 0x3498db, 0 };
	for (int i = 0; i < 3; i++) {
		IconRow r = item(400 + i, who[i]);
		r.hasImage = true;
		r.imageKind = ImageCache::DEFAULT_AVATAR;
		r.imageSf = (Snowflake) (i + 3) << 22;
		r.status = i + 1;
		r.textColor = colors[i];
		m.push_back(r);
	}
	m.push_back(header("Offline \xe2\x80\x94 1"));
	IconRow off = item(410, "Linus");
	off.hasImage = true; off.imageKind = ImageCache::DEFAULT_AVATAR; off.imageSf = (Snowflake) 7 << 22;
	off.status = 0; off.dim = true;
	m.push_back(off);
	m_members->SetRows(m, 0);
}

void MainWindow::OnImagesChanged()
{
	m_messages->ImagesChanged();
	ImageViewer::ImagesChanged();
	ReactionPicker::ImagesChanged();
	Conversations::ImagesChanged();
	Notifier::ImagesChanged();
	if (!m_listRepaintTimer)
		m_listRepaintTimer = XtAppAddTimeOut(XtWidgetToApplicationContext(m_shell), 100, ListRepaintCB, this);
}

void MainWindow::ScheduleListUpdate(int lists)
{
	m_pendingLists |= lists;
	if (!m_listUpdateTimer)
		m_listUpdateTimer = XtAppAddTimeOut(XtWidgetToApplicationContext(m_shell), 250, ListUpdateCB, this);
}

void MainWindow::ListUpdateCB(XtPointer client, XtIntervalId*)
{
	MainWindow* self = (MainWindow*) client;
	self->m_listUpdateTimer = 0;
	int lists = self->m_pendingLists;
	self->m_pendingLists = 0;
	if (lists & LIST_GUILDS)
		self->UpdateGuildList();
	if (lists & LIST_CHANNELS)
		self->UpdateChannelList();
	if (lists & LIST_MEMBERS)
		self->UpdateMemberList();
	self->UpdateIconName();
}

void MainWindow::UpdateIconName()
{
	DiscordInstance* pInst = GetDiscordInstance();
	if (!pInst)
		return;
	std::vector<Snowflake> ids;
	pInst->GetGuildIDsOrdered(ids, true);
	int total = 0, dms = 0;
	for (Snowflake sf : ids) {
		if (sf == 1 || (sf & BIT_FOLDER))
			continue;
		Guild* pGuild = pInst->GetGuild(sf); // 0: the direct messages
		if (!pGuild)
			continue;
		for (auto& ch : pGuild->m_channels) {
			total += ch.m_mentionCount;
			if (sf == 0)
				dms += ch.m_mentionCount;
		}
	}
	if (dms != m_dmUnread && m_dmCascade) {
		m_dmUnread = dms;
		XmString xs = MakeXmString(dms ? "Messages (" + std::to_string(dms) + ")" : std::string("Messages"));
		XtVaSetValues(m_dmCascade, XmNlabelString, xs, NULL);
		XmStringFree(xs);
	}
	std::string name = total ? "Discord (" + std::to_string(total) + ")" : std::string("Discord");
	if (name != m_iconName) {
		m_iconName = name;
		XtVaSetValues(m_shell, XmNiconName, m_iconName.c_str(), NULL);
	}
}

bool MainWindow::ShowsMember(Snowflake user) const
{
	if (!IsPaneShown(PANE_MEMBERS))
		return false;
	Guild* pGuild = GetDiscordInstance()->GetCurrentGuild();
	if (!pGuild || pGuild->m_snowflake == 0)
		return false;
	return std::find(pGuild->m_members.begin(), pGuild->m_members.end(), user) != pGuild->m_members.end();
}

void MainWindow::ListRepaintCB(XtPointer client, XtIntervalId*)
{
	MainWindow* self = (MainWindow*) client;
	self->m_listRepaintTimer = 0;
	self->m_guilds->Repaint();
	self->m_channels->Repaint();
	self->m_members->Repaint();
}

void MainWindow::UpdateHeader()
{
	DiscordInstance* pInst = GetDiscordInstance();
	Channel* pChan = pInst ? pInst->GetCurrentChannel() : nullptr;
	std::string text;
	if (pChan) {
		text = (pChan->IsDM() ? "@" : "#") + pChan->m_name;
		if (!pChan->m_topic.empty()) {
			std::string topic = pChan->m_topic;
			std::replace(topic.begin(), topic.end(), '\n', ' ');
			if (topic.size() > 120)
				topic = topic.substr(0, 117) + "...";
			text += "   |   " + topic;
		}
	}
	XmString xs = MakeXmString(text.empty() ? std::string(" ") : text);
	XtVaSetValues(m_header, XmNlabelString, xs, NULL);
	XmStringFree(xs);
}

void MainWindow::UpdateTitle()
{
	DiscordInstance* pInst = GetDiscordInstance();
	Channel* pChan = pInst ? pInst->GetCurrentChannel() : nullptr;
	std::string title = "Discord Messenger";
	if (pChan)
		title = (pChan->IsDM() ? "@" : "#") + pChan->m_name + " - " + title;
	std::string l1 = Utf8ToLatin1(title);
	XtVaSetValues(m_shell, XmNtitle, l1.c_str(), XmNiconName, m_iconName.c_str(), NULL);
}

void MainWindow::SetStatus(const std::string& text)
{
	m_statusText = text;
	UpdateTypingStatus();
}

std::string MainWindow::TypingText(Snowflake channel)
{
	DiscordInstance* pInst = GetDiscordInstance();
	auto it = m_typing.find(channel);
	if (!pInst || it == m_typing.end() || it->second.empty())
		return "";
	Channel* pChan = pInst->GetChannelGlobally(channel);
	Snowflake guild = pChan ? pChan->m_parentGuild : 0;
	std::vector<std::string> names;
	for (auto& t : it->second) {
		Profile* p = GetProfileCache()->LookupProfile(t.first, "", "", "", false);
		names.push_back(p ? p->GetName(guild) : "Someone");
	}
	if (names.size() > 3)
		return "Several people are typing...";
	std::string who;
	for (size_t i = 0; i < names.size(); i++)
		who += (i ? (i + 1 == names.size() ? " and " : ", ") : "") + names[i];
	return who + (names.size() == 1 ? " is typing..." : " are typing...");
}

void MainWindow::UpdateTypingStatus()
{
	DiscordInstance* pInst = GetDiscordInstance();
	std::string text = m_statusText;
	std::string typing = pInst ? TypingText(pInst->GetCurrentChannelID()) : "";
	if (!typing.empty())
		text = typing;
	Conversations::UpdateTyping();
	XmString xs = MakeXmString(text.empty() ? std::string(" ") : text);
	XtVaSetValues(m_status, XmNlabelString, xs, NULL);
	XmStringFree(xs);
}

void MainWindow::OnTyping(Snowflake user, Snowflake guild, Snowflake channel, time_t when)
{
	DiscordInstance* pInst = GetDiscordInstance();
	if (user == pInst->GetUserID())
		return;
	m_typing[channel][user] = time(NULL) + 10;
	UpdateTypingStatus();
	if (!m_typingTimer)
		m_typingTimer = XtAppAddTimeOut(XtWidgetToApplicationContext(m_shell), 1000, TypingTimerCB, this);
}

void MainWindow::OnStopTyping(Snowflake channel, Snowflake user)
{
	auto it = m_typing.find(channel);
	if (it == m_typing.end())
		return;
	it->second.erase(user);
	UpdateTypingStatus();
}

void MainWindow::TypingTimerCB(XtPointer client, XtIntervalId*)
{
	MainWindow* self = (MainWindow*) client;
	self->m_typingTimer = 0;
	time_t now = time(NULL);
	bool any = false;
	for (auto& ch : self->m_typing) {
		for (auto it = ch.second.begin(); it != ch.second.end(); ) {
			if (it->second <= now)
				it = ch.second.erase(it);
			else {
				++it;
				any = true;
			}
		}
	}
	self->UpdateTypingStatus();
	if (any)
		self->m_typingTimer = XtAppAddTimeOut(XtWidgetToApplicationContext(self->m_shell), 1000, TypingTimerCB, self);
}

void MainWindow::OnGuildPicked(Snowflake sf)
{
	GetDiscordInstance()->OnSelectGuild(sf);
}

void MainWindow::OnChannelPicked(Snowflake sf)
{
	GetDiscordInstance()->OnSelectChannel(sf);
}

void MainWindow::SendCB(Widget, XtPointer client, XtPointer)
{
	((MainWindow*) client)->SendFromEditor();
}

void MainWindow::EditorChangedCB(Widget, XtPointer client, XtPointer)
{
	MainWindow* self = (MainWindow*) client;
	DiscordInstance* pInst = GetDiscordInstance();
	if (!pInst || !pInst->GetCurrentChannelID())
		return;
	char* text = XmTextGetString(self->m_editor);
	bool empty = !text || !*text;
	XtFree(text);
	time_t now = time(NULL);
	if (!empty && now - self->m_lastTypingSent >= 8) {
		self->m_lastTypingSent = now;
		pInst->Typing();
	}
}

void MainWindow::SendFromEditor()
{
	DiscordInstance* pInst = GetDiscordInstance();
	char* raw = XmTextGetString(m_editor);
	std::string text = raw ? raw : "";
	XtFree(raw);

	// trim
	size_t a = text.find_first_not_of(" \t\n"), b = text.find_last_not_of(" \t\n");
	if (a == std::string::npos || !pInst || !pInst->GetCurrentChannelID())
		return;
	text = text.substr(a, b - a + 1);

	// shortcodes (:joy:, a server's :name:, :U+...:) become the emoji
	std::string utf8 = Shortcodes::FromEditor(Latin1ToUtf8(text), pInst->GetCurrentGuildID());
	if (m_editing) {
		pInst->RequestEditMessage(pInst->GetCurrentChannelID(), m_editing, utf8);
		m_editing = 0;
		CancelReply();
		XmTextSetString(m_editor, (char*) "");
		m_lastTypingSent = 0;
		return;
	}
	Snowflake tempSf = 0;
	if (pInst->SendMessageToCurrentChannel(utf8, tempSf, m_replyTo)) {
		CancelReply();
		XmTextSetString(m_editor, (char*) "");
		m_lastTypingSent = 0;
		m_messages->ScrollToBottom();
	}
}
