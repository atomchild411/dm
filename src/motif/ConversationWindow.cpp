#include "Xm.hpp"
#include "ConversationWindow.hpp"

#include <ctime>
#include <map>

#include <X11/Shell.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/Text.h>

#include "DiscordInstance.hpp"
#include "state/MessageCache.hpp"
#include "MainWindow.hpp"
#include "MessageView.hpp"
#include "ReactionPicker.hpp"
#include "Shortcodes.hpp"
#include "Theme.hpp"

int AddVisualArgs(Arg* args, int n); // Main.cpp

namespace
{
	struct Conversation
	{
		Snowflake channel = 0;
		Widget shell = nullptr, editor = nullptr, emoji = nullptr, send = nullptr;
		Widget replyBar = nullptr, replyLabel = nullptr, status = nullptr;
		MessageView* view = nullptr;
		Snowflake replyTo = 0, editing = 0;
		bool focused = false;
		time_t lastTyping = 0;
	};

	Widget g_toplevel;
	const PixelFormat* g_fmt;
	std::map<Snowflake, Conversation*> g_windows;

	void Close(Conversation* w)
	{
		g_windows.erase(w->channel);
		delete w->view;
		XtDestroyWidget(w->shell);
		delete w;
	}

	void CloseCB(Widget, XtPointer client, XtPointer)
	{
		Close((Conversation*) client);
	}

	std::string Title(Snowflake channel)
	{
		DiscordInstance* pInst = GetDiscordInstance();
		Channel* pChan = pInst ? pInst->GetChannelGlobally(channel) : nullptr;
		return pChan ? "@" + pChan->m_name : std::string("Direct message");
	}

	// the reply or edit bar shown first, then the messages made to end
	// above it (and the other way round): XmForm cannot follow an
	// attachment to a widget it does not manage
	void ShowBar(Conversation* w, const std::string& text)
	{
		XmString xs = MakeXmString(text);
		XtVaSetValues(w->replyLabel, XmNlabelString, xs, NULL);
		XmStringFree(xs);
		if (!XtIsManaged(w->replyBar)) {
			XtManageChild(w->replyBar);
			XtVaSetValues(w->view->GetWidget(), XmNbottomWidget, w->replyBar, NULL);
		}
		XmProcessTraversal(w->editor, XmTRAVERSE_CURRENT);
	}

	void CancelCompose(Conversation* w)
	{
		if (w->editing)
			XmTextSetString(w->editor, (char*) "");
		w->editing = 0;
		w->replyTo = 0;
		if (XtIsManaged(w->replyBar)) {
			XtVaSetValues(w->view->GetWidget(), XmNbottomWidget, XtParent(w->editor), NULL);
			XtUnmanageChild(w->replyBar);
		}
	}

	void CancelCB(Widget, XtPointer client, XtPointer)
	{
		CancelCompose((Conversation*) client);
	}

	void Send(Conversation* w)
	{
		DiscordInstance* pInst = GetDiscordInstance();
		char* raw = XmTextGetString(w->editor);
		std::string text = raw ? raw : "";
		XtFree(raw);
		size_t a = text.find_first_not_of(" \t\n"), b = text.find_last_not_of(" \t\n");
		if (a == std::string::npos || !pInst)
			return;
		text = text.substr(a, b - a + 1);

		// shortcodes become the emoji (see Shortcodes)
		std::string utf8 = Shortcodes::FromEditor(Latin1ToUtf8(text), 0);
		if (w->editing) {
			pInst->RequestEditMessage(w->channel, w->editing, utf8);
			w->editing = 0;
			CancelCompose(w);
			XmTextSetString(w->editor, (char*) "");
			return;
		}
		Snowflake tempSf = 0;
		if (pInst->SendMessageToChannel(0, w->channel, utf8, tempSf, w->replyTo)) {
			CancelCompose(w);
			XmTextSetString(w->editor, (char*) "");
			w->lastTyping = 0;
			w->view->ScrollToBottom();
		}
	}

	void SendCB(Widget, XtPointer client, XtPointer)
	{
		Send((Conversation*) client);
	}

	void EditorChangedCB(Widget, XtPointer client, XtPointer)
	{
		Conversation* w = (Conversation*) client;
		char* text = XmTextGetString(w->editor);
		bool empty = !text || !*text;
		XtFree(text);
		time_t now = time(NULL);
		if (!empty && now - w->lastTyping >= 8 && GetDiscordInstance()) {
			w->lastTyping = now;
			GetDiscordInstance()->Typing(w->channel);
		}
	}

	void EmojiCB(Widget b, XtPointer client, XtPointer)
	{
		Conversation* w = (Conversation*) client;
		Position x = 0, y = 0;
		XtTranslateCoords(b, 0, 0, &x, &y);
		Widget editor = w->editor;
		ReactionPicker::Show(w->shell, *g_fmt, "Insert Emoji", x, y - 380, 0, [editor](const Reaction& r) {
			std::string code = Utf8ToLatin1(Shortcodes::For(r));
			XmTextPosition at = XmTextGetInsertionPosition(editor);
			XmTextInsert(editor, at, (char*) code.c_str());
			XmTextSetInsertionPosition(editor, at + (XmTextPosition) code.size());
			XmProcessTraversal(editor, XmTRAVERSE_CURRENT);
		});
	}

	void FocusEH(Widget, XtPointer client, XEvent* ev, Boolean*)
	{
		Conversation* w = (Conversation*) client;
		if (ev->type == FocusIn && ev->xfocus.detail != NotifyPointer)
			w->focused = true;
		else if (ev->type == FocusOut && ev->xfocus.detail != NotifyPointer && ev->xfocus.detail != NotifyInferior)
			w->focused = false;
		else if (ev->type == UnmapNotify)
			w->focused = false;
	}

	void ShowTyping(Conversation* w)
	{
		std::string text = GetMainWindow() ? GetMainWindow()->TypingText(w->channel) : "";
		XmString xs = MakeXmString(text.empty() ? std::string(" ") : text);
		XtVaSetValues(w->status, XmNlabelString, xs, NULL);
		XmStringFree(xs);
	}

	Conversation* Create(Snowflake channel)
	{
		Conversation* w = new Conversation;
		w->channel = channel;
		std::string title = Utf8ToLatin1(Title(channel));

		Arg args[12];
		int n = 0;
		Pixmap icon = 0;
		XtVaGetValues(g_toplevel, XmNiconPixmap, &icon, NULL);
		std::string full = title + " - Discord Messenger";
		XtSetArg(args[n], XmNtitle, full.c_str()); n++;
		XtSetArg(args[n], XmNiconName, title.c_str()); n++;
		XtSetArg(args[n], XmNiconPixmap, icon); n++;
		XtSetArg(args[n], XmNdeleteResponse, XmDO_NOTHING); n++;
		XtSetArg(args[n], XmNwidth, 620); n++;
		XtSetArg(args[n], XmNheight, 540); n++;
		n = AddVisualArgs(args, n);
		// a window of its own, but under the application's shell, so its
		// resources (the desktop's colour scheme among them) are the app's
		w->shell = XtCreatePopupShell("conversation", topLevelShellWidgetClass, g_toplevel, args, n);
		Atom wmDelete = XmInternAtom(XtDisplay(w->shell), (char*) "WM_DELETE_WINDOW", False);
		XmAddWMProtocolCallback(w->shell, wmDelete, CloseCB, w);
		XtAddEventHandler(w->shell, FocusChangeMask | StructureNotifyMask, False, FocusEH, w);

		Widget form = XtVaCreateWidget("form", xmFormWidgetClass, w->shell, NULL);

		w->status = XtVaCreateManagedWidget("status", xmLabelWidgetClass, form,
			XmNbottomAttachment, XmATTACH_FORM,
			XmNleftAttachment, XmATTACH_FORM,
			XmNrightAttachment, XmATTACH_FORM,
			XmNbottomOffset, 4, XmNleftOffset, 6, XmNrightOffset, 6,
			XmNalignment, XmALIGNMENT_BEGINNING,
			NULL);
		w->send = XtVaCreateManagedWidget("Send", xmPushButtonWidgetClass, form,
			XmNbottomAttachment, XmATTACH_WIDGET,
			XmNbottomWidget, w->status,
			XmNrightAttachment, XmATTACH_FORM,
			XmNbottomOffset, 4, XmNrightOffset, 6,
			NULL);
		XtAddCallback(w->send, XmNactivateCallback, SendCB, w);
		w->emoji = XtVaCreateManagedWidget("emoji", xmPushButtonWidgetClass, form,
			XmNbottomAttachment, XmATTACH_WIDGET,
			XmNbottomWidget, w->status,
			XmNrightAttachment, XmATTACH_WIDGET,
			XmNrightWidget, w->send,
			XmNbottomOffset, 4, XmNrightOffset, 4,
			NULL);
		XtAddCallback(w->emoji, XmNactivateCallback, EmojiCB, w);
		if (Pixmap pm = GetMainWindow()->GetEmojiPixmap())
			XtVaSetValues(w->emoji, XmNlabelType, XmPIXMAP, XmNlabelPixmap, pm, NULL);

		n = 0;
		XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
		XtSetArg(args[n], XmNbottomWidget, w->status); n++;
		XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
		XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
		XtSetArg(args[n], XmNrightWidget, w->emoji); n++;
		XtSetArg(args[n], XmNbottomOffset, 4); n++;
		XtSetArg(args[n], XmNleftOffset, 6); n++;
		XtSetArg(args[n], XmNrightOffset, 6); n++;
		XtSetArg(args[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
		XtSetArg(args[n], XmNrows, 3); n++;
		XtSetArg(args[n], XmNwordWrap, True); n++;
		XtSetArg(args[n], XmNscrollHorizontal, False); n++;
		w->editor = XmCreateScrolledText(form, (char*) "editor", args, n);
		XtManageChild(w->editor);
		// Return sends; Shift+Return starts a new line
		XtOverrideTranslations(w->editor, XtParseTranslationTable(
			"Shift<Key>Return: newline()\n"
			"<Key>Return: activate()\n"
			"<Key>KP_Enter: activate()"));
		XtAddCallback(w->editor, XmNactivateCallback, SendCB, w);
		XtAddCallback(w->editor, XmNvalueChangedCallback, EditorChangedCB, w);

		w->replyBar = XtVaCreateWidget("replyBar", xmFormWidgetClass, form,
			XmNbottomAttachment, XmATTACH_WIDGET,
			XmNbottomWidget, XtParent(w->editor),
			XmNleftAttachment, XmATTACH_FORM,
			XmNrightAttachment, XmATTACH_FORM,
			XmNbottomOffset, 4, XmNleftOffset, 6, XmNrightOffset, 6,
			NULL);
		Widget cancel = XtVaCreateManagedWidget("Cancel", xmPushButtonWidgetClass, w->replyBar,
			XmNtopAttachment, XmATTACH_FORM,
			XmNbottomAttachment, XmATTACH_FORM,
			XmNrightAttachment, XmATTACH_FORM,
			NULL);
		XtAddCallback(cancel, XmNactivateCallback, CancelCB, w);
		w->replyLabel = XtVaCreateManagedWidget("replyLabel", xmLabelWidgetClass, w->replyBar,
			XmNtopAttachment, XmATTACH_FORM,
			XmNbottomAttachment, XmATTACH_FORM,
			XmNleftAttachment, XmATTACH_FORM,
			XmNrightAttachment, XmATTACH_WIDGET,
			XmNrightWidget, cancel,
			XmNalignment, XmALIGNMENT_BEGINNING,
			NULL);

		w->view = new MessageView(form, *g_fmt);
		XtVaSetValues(w->view->GetWidget(),
			XmNtopAttachment, XmATTACH_FORM,
			XmNleftAttachment, XmATTACH_FORM,
			XmNrightAttachment, XmATTACH_FORM,
			XmNbottomAttachment, XmATTACH_WIDGET,
			XmNbottomWidget, XtParent(w->editor),
			XmNtopOffset, 6, XmNleftOffset, 6, XmNrightOffset, 6, XmNbottomOffset, 6,
			NULL);
		w->view->SetOwner(
			[w](Snowflake id, const std::string& author) {
				w->editing = 0;
				w->replyTo = id;
				ShowBar(w, "Replying to " + author);
			},
			[w](Snowflake id, const std::string& text) {
				w->replyTo = 0;
				w->editing = id;
				ShowBar(w, "Editing your message");
				std::string shown = Utf8ToLatin1(Shortcodes::ToEditor(text));
				XmTextSetString(w->editor, (char*) shown.c_str());
				XmTextSetInsertionPosition(w->editor, XmTextGetLastPosition(w->editor));
			},
			[w]() { return w->focused; });

		XtVaSetValues(form, XmNinitialFocus, XtParent(w->editor), NULL);
		XtManageChild(form);
		ShowTyping(w);
		return w;
	}
}

void Conversations::Init(Widget toplevel, const PixelFormat& fmt)
{
	g_toplevel = toplevel;
	g_fmt = &fmt;
}

void Conversations::Open(Snowflake channel)
{
	auto it = g_windows.find(channel);
	if (it != g_windows.end()) {
		XMapRaised(XtDisplay(it->second->shell), XtWindow(it->second->shell));
		return;
	}
	Conversation* w = Create(channel);
	g_windows[channel] = w;
	XtPopup(w->shell, XtGrabNone);
	GetMessageCache()->LoadCachedChannel(channel, 0);
	w->view->SetChannel(0, channel);
	XmProcessTraversal(w->editor, XmTRAVERSE_CURRENT);
}

void Conversations::Refresh(Snowflake channel)
{
	for (auto& e : g_windows)
		if (!channel || e.first == channel)
			e.second->view->Refresh();
}

void Conversations::Reload()
{
	for (auto& e : g_windows) {
		GetMessageCache()->LoadCachedChannel(e.first, 0);
		e.second->view->SetChannel(0, e.first);
	}
}

void Conversations::Relayout()
{
	for (auto& e : g_windows)
		e.second->view->Relayout();
}

void Conversations::ImagesChanged()
{
	for (auto& e : g_windows)
		e.second->view->ImagesChanged();
}

void Conversations::UpdateTyping()
{
	for (auto& e : g_windows)
		ShowTyping(e.second);
}

bool Conversations::IsFocusedOn(Snowflake channel)
{
	auto it = g_windows.find(channel);
	return it != g_windows.end() && it->second->focused;
}

void Conversations::CloseAll()
{
	while (!g_windows.empty())
		Close(g_windows.begin()->second);
}
