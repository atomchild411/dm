#include "Xm.hpp"
#include "MessageView.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <Xm/DrawingA.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Form.h>
#include <Xm/ScrollBar.h>

#include "DiscordInstance.hpp"
#include "Frontend.hpp"
#include "state/MessageCache.hpp"
#include "Theme.hpp"
#include "shared/ImageCache.hpp"
#include "ImageViewer.hpp"
#include "MainWindow.hpp"
#include "Notifier.hpp"
#include "ReactionPicker.hpp"
#include "shared/Perf.hpp"
#include "shared/Lists.hpp"
#include "shared/MessageList.hpp"

// Geometry, in pixels (shared/MessageList lays the items out)
static const int MARGIN = MessageList::MARGIN;
static const int AVATAR = MessageList::AVATAR;
static const int TEXT_X = MessageList::TEXT_X;
static const int DATE_SEP = MessageList::DATE_SEP;
static const int PILL_PAD = MessageList::PILL_PAD;

MessageView::MessageView(Widget parent, const PixelFormat& fmt) : m_fmt(fmt), m_list(Fonts::Metrics())
{
	m_form = XtVaCreateWidget("messageView", xmFormWidgetClass, parent, NULL);

	m_scroll = XtVaCreateManagedWidget("messageScroll", xmScrollBarWidgetClass, m_form,
		XmNorientation, XmVERTICAL,
		XmNtopAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		XmNminimum, 0,
		XmNmaximum, 1,
		XmNsliderSize, 1,
		NULL);

	m_area = XtVaCreateManagedWidget("messageArea", xmDrawingAreaWidgetClass, m_form,
		XmNtopAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_WIDGET,
		XmNrightWidget, m_scroll,
		XmNwidth, 500,
		XmNheight, 400,
		XmNresizePolicy, XmRESIZE_NONE,
		XmNtraversalOn, False,
		NULL);

	XtAddCallback(m_area, XmNexposeCallback, ExposeCB, this);
	XtAddCallback(m_area, XmNresizeCallback, ResizeCB, this);
	XtAddCallback(m_scroll, XmNvalueChangedCallback, ScrollCB, this);
	XtAddCallback(m_scroll, XmNdragCallback, ScrollCB, this);
	XtAddEventHandler(m_area, ButtonPressMask, False, InputEH, this);
	XtAddEventHandler(m_area, VisibilityChangeMask, False, VisibilityEH, this);
	XtAddEventHandler(m_area, NoEventMask, True, GraphicsExposeEH, this);

	XtManageChild(m_form);
	ApplyTheme(m_ctx);
}

MessageView::~MessageView()
{
	if (m_repaintTimer)
		XtRemoveTimeOut(m_repaintTimer);
	if (m_gc)
		XFreeGC(XtDisplay(m_area), m_gc);
}

int MessageView::ContentWidth() const
{
	return std::max(200, m_viewW);
}

void MessageView::SetChannel(Snowflake guild, Snowflake channel)
{
	m_guild = guild;
	m_channel = channel;
	m_list.SetChannel(guild, channel);
	m_scrollY = 0;
	m_stickToBottom = true;
	m_canvasValid = false;
	m_justOpened = true;
	Refresh();
}

void MessageView::Refresh()
{
	Perf::Scope perf(Perf::MV_REFRESH);
	// remember what is on screen: the first message whose top is visible
	Snowflake anchor = 0;
	int anchorOffset = 0;
	bool atBottom = m_stickToBottom || m_scrollY + m_viewH >= m_list.ContentHeight() - 4;
	for (auto& it : m_list.Items()) {
		if (it.y + it.height > m_scrollY && !it.msg->IsLoadGap()) {
			anchor = it.msg->m_snowflake;
			anchorOffset = it.y - m_scrollY;
			break;
		}
	}

	auto& items = m_list.Items();
	int oldEnd = items.empty() ? 0 : items.back().y + items.back().height;
	bool appendOnly = m_list.Rebuild();
	LayoutAll();
	// what is on screen stays valid when messages only came after it
	if (!appendOnly)
		m_canvasValid = false;
	else if (m_dirtyFromY < 0 || oldEnd < m_dirtyFromY)
		m_dirtyFromY = oldEnd;

	if (atBottom) {
		ScrollToBottom();
		return;
	}

	for (auto& it : m_list.Items()) {
		if (it.msg->m_snowflake == anchor) {
			SetScroll(it.y - anchorOffset);
			return;
		}
	}
	SetScroll(m_scrollY);
}

void MessageView::Relayout()
{
	m_canvasValid = false;
	ApplyTheme(m_ctx);
	m_list.ForgetLayout();
	Refresh();
}

std::string MessageView::LayoutSignature() const
{
	return m_list.LayoutSignature();
}

void MessageView::LayoutAll()
{
	Perf::Scope perf(Perf::MV_LAYOUT);
	m_list.Layout(&m_ctx, m_ctx.px, ContentWidth());
	UpdateScrollbar();
}

void MessageView::UpdateScrollbar()
{
	int maxv = std::max(m_list.ContentHeight(), m_viewH);
	int value = std::min(std::max(0, m_scrollY), maxv - m_viewH);
	XtVaSetValues(m_scroll,
		XmNmaximum, maxv,
		XmNsliderSize, std::max(1, std::min(m_viewH, maxv)),
		XmNvalue, value,
		XmNincrement, 40,
		XmNpageIncrement, std::max(40, m_viewH - 40),
		NULL);
}

void MessageView::SetScroll(int y)
{
	int maxY = std::max(0, m_list.ContentHeight() - m_viewH);
	m_scrollY = std::min(std::max(0, y), maxY);
	m_stickToBottom = m_scrollY >= maxY;
	UpdateScrollbar();
	Update();
}

void MessageView::ScrollToBottom()
{
	SetScroll(m_list.ContentHeight());
	m_stickToBottom = true;
}

void MessageView::ExposeCB(Widget, XtPointer client, XtPointer call)
{
	MessageView* self = (MessageView*) client;
	XmDrawingAreaCallbackStruct* cbs = (XmDrawingAreaCallbackStruct*) call;
	if (cbs && cbs->event && cbs->event->type == Expose && cbs->event->xexpose.count > 0)
		return;
	self->Paint();
}

void MessageView::ResizeCB(Widget w, XtPointer client, XtPointer)
{
	MessageView* self = (MessageView*) client;
	Dimension width = 0, height = 0;
	XtVaGetValues(w, XmNwidth, &width, XmNheight, &height, NULL);
	bool widthChanged = width != self->m_viewW;
	self->m_viewW = width;
	self->m_viewH = height;
	if (widthChanged)
		self->LayoutAll();
	if (self->m_stickToBottom)
		self->ScrollToBottom();
	else
		self->SetScroll(self->m_scrollY);
}

void MessageView::ScrollCB(Widget, XtPointer client, XtPointer call)
{
	MessageView* self = (MessageView*) client;
	XmScrollBarCallbackStruct* cbs = (XmScrollBarCallbackStruct*) call;
	self->m_scrollY = cbs->value;
	self->m_stickToBottom = self->m_scrollY >= self->m_list.ContentHeight() - self->m_viewH;
	self->Update();
}

void MessageView::VisibilityEH(Widget, XtPointer client, XEvent* ev, Boolean*)
{
	if (ev->type == VisibilityNotify)
		((MessageView*) client)->m_unobscured = ev->xvisibility.state == VisibilityUnobscured;
}

// A copy that found part of its source hidden after all: draw it all.
void MessageView::GraphicsExposeEH(Widget, XtPointer client, XEvent* ev, Boolean*)
{
	if (ev->type == GraphicsExpose && ev->xgraphicsexpose.count == 0)
		((MessageView*) client)->Paint();
}

void MessageView::InputEH(Widget, XtPointer client, XEvent* ev, Boolean*)
{
	MessageView* self = (MessageView*) client;
	if (ev->type != ButtonPress)
		return;
	int step = 3 * (Fonts::LineHeight(FS_REGULAR, self->m_ctx.px) + 2);
	switch (ev->xbutton.button) {
		case Button1: self->OnClick(ev->xbutton.x, ev->xbutton.y); break;
		case Button3: self->ShowMenu(ev->xbutton); break;
		case Button4: self->SetScroll(self->m_scrollY - step); break;
		case Button5: self->SetScroll(self->m_scrollY + step); break;
	}
}

int AddVisualArgs(Arg* args, int n); // Main.cpp

enum { MENU_REACT = 1, MENU_REPLY, MENU_EDIT, MENU_DELETE };

// Right-click on a message: react to it, or reply.
void MessageView::ShowMenu(XButtonEvent& ev)
{
	m_menuMessage.reset();
	Item* it = m_list.ItemAt(ev.y + m_scrollY);
	if (it && MessageList::IsActionable(*it->msg))
		m_menuMessage = it->msg;
	if (!m_menuMessage)
		return;
	m_menuX = ev.x_root;
	m_menuY = ev.y_root;

	if (!m_menu) {
		Arg args[4];
		int n = AddVisualArgs(args, 0);
		m_menu = XmCreatePopupMenu(m_area, (char*) "messageMenu", args, n);
		struct { const char* label; int id; char mnemonic; } items[] = {
			{ "Add Reaction...", MENU_REACT, 'A' },
			{ "Reply", MENU_REPLY, 'R' },
			{ "Edit Message", MENU_EDIT, 'E' },
			{ "Delete Message", MENU_DELETE, 'D' },
		};
		for (auto& item : items) {
			Widget b = XtVaCreateManagedWidget(item.label, xmPushButtonWidgetClass, m_menu,
				XmNmnemonic, (KeySym) item.mnemonic, XmNuserData, (XtPointer) (long) item.id, NULL);
			XtAddCallback(b, XmNactivateCallback, MenuCB, this);
			if (item.id == MENU_EDIT)
				m_menuEdit = b;
			if (item.id == MENU_DELETE)
				m_menuDelete = b;
		}
	}
	// only the user's own messages can be edited; deleted too, or anyone's
	// where the user may manage messages
	if (m_list.CanEdit(*m_menuMessage))
		XtManageChild(m_menuEdit);
	else
		XtUnmanageChild(m_menuEdit);
	if (m_list.CanDelete(*m_menuMessage))
		XtManageChild(m_menuDelete);
	else
		XtUnmanageChild(m_menuDelete);
	XmMenuPosition(m_menu, &ev);
	XtManageChild(m_menu);
}

void MessageView::MenuCB(Widget w, XtPointer client, XtPointer)
{
	MessageView* self = (MessageView*) client;
	XtPointer data = nullptr;
	XtVaGetValues(w, XmNuserData, &data, NULL);
	MessagePtr msg = self->m_menuMessage;
	if (!msg)
		return;
	Snowflake channel = self->m_channel, id = msg->m_snowflake;
	switch ((int) (long) data) {
		case MENU_REACT:
			ReactionPicker::Show(self->m_area, self->m_fmt, "Add Reaction", self->m_menuX, self->m_menuY, self->m_guild, [channel, id](const Reaction& r) {
				GetDiscordInstance()->RequestReaction(channel, id, r, true);
			});
			break;
		case MENU_REPLY:
			if (self->m_onReply)
				self->m_onReply(id, msg->m_author);
			else
				GetMainWindow()->BeginReply(id, msg->m_author);
			break;
		case MENU_EDIT:
			if (self->m_onEdit)
				self->m_onEdit(id, msg->m_message);
			else
				GetMainWindow()->BeginEdit(id, msg->m_message);
			break;
		case MENU_DELETE:
			self->ConfirmDelete(msg);
			break;
	}
}

// Deleting cannot be undone: asked first, with the start of the message.
void MessageView::ConfirmDelete(MessagePtr msg)
{
	m_deleteChannel = m_channel;
	m_deleteMessage = msg->m_snowflake;

	std::string question = MessageList::DeleteQuestion(*msg);

	Arg args[10];
	int n = 0;
	XmString xs = XmStringCreateLtoR((char*) Utf8ToLatin1(question).c_str(), (char*) XmFONTLIST_DEFAULT_TAG);
	XmString ok = XmStringCreateLocalized((char*) "Delete");
	XtSetArg(args[n], XmNmessageString, xs); n++;
	XtSetArg(args[n], XmNokLabelString, ok); n++;
	XtSetArg(args[n], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL); n++;
	XtSetArg(args[n], XmNtitle, "Delete Message"); n++;
	XtSetArg(args[n], XmNdefaultButtonType, XmDIALOG_CANCEL_BUTTON); n++;
	n = AddVisualArgs(args, n);
	Widget dlg = XmCreateQuestionDialog(m_area, (char*) "deleteMessage", args, n);
	XmStringFree(xs);
	XmStringFree(ok);
	XtUnmanageChild(XmMessageBoxGetChild(dlg, XmDIALOG_HELP_BUTTON));
	XtAddCallback(dlg, XmNokCallback, DeleteCB, this);
	XtAddCallback(dlg, XmNcancelCallback, [](Widget w, XtPointer, XtPointer) { XtDestroyWidget(XtParent(w)); }, NULL);
	XtManageChild(dlg);
}

void MessageView::DeleteCB(Widget w, XtPointer client, XtPointer)
{
	MessageView* self = (MessageView*) client;
	if (self->m_deleteMessage)
		GetDiscordInstance()->RequestDeleteMessage(self->m_deleteChannel, self->m_deleteMessage);
	self->m_deleteMessage = 0;
	XtDestroyWidget(XtParent(w));
}

void MessageView::OnClick(int x, int y)
{
	MessageList::Hit hit = m_list.HitTest(x, y + m_scrollY);
	switch (hit.kind) {
		case MessageList::Hit::LINK:
			GetFrontend()->LaunchURL(hit.url);
			break;
		case MessageList::Hit::REACTION: {
			// the user's own taken away, another added
			const Reaction& r = hit.message->m_reactions[hit.reaction];
			GetDiscordInstance()->RequestReaction(m_channel, hit.message->m_snowflake, r, !r.m_bMe);
			break;
		}
		case MessageList::Hit::PICTURE:
			ImageViewer::Show(m_area, m_fmt, hit.picture);
			break;
		case MessageList::Hit::NONE:
			break;
	}
}

void MessageView::RequestVisibleGaps()
{
	m_list.RequestGaps(m_scrollY, m_scrollY + m_viewH);
}

static Rgb AvatarColor(Snowflake sf)
{
	static const Rgb colors[] = { 0x5865f2, 0x3ba55c, 0xfaa61a, 0xed4245, 0xeb459e, 0x747f8d, 0x2d8f9e, 0x9b59b6 };
	return colors[(sf >> 22) % (sizeof colors / sizeof colors[0])];
}

void MessageView::DrawPicture(const Rect& r, const std::string& url, int top, const std::string& label)
{
	Canvas& c = m_canvas;
	int w = r.Width(), h = r.Height();
	int x = r.left, y = r.top + top;
	if (y + h < 0 || y > m_viewH)
		return; // not on screen: not fetched either
	const Image* img = ImageCache::Get(ImageCache::URL, url, 0, w, h);
	if (img) {
		// opaque images on the background; centred in their box
		c.BlendArgb(x + (w - img->w) / 2, y + (h - img->h) / 2, img->px.data(), img->w, img->h, img->w);
		return;
	}
	c.Fill(x, y, w, h, m_ctx.codeBg);
	c.Frame(x, y, w, h, m_ctx.codeFrame);
	bool failed = ImageCache::Failed(ImageCache::URL, url, 0, w, h);
	std::string text = failed ? (label.empty() ? std::string("Image not available") : label) : std::string("Loading\xe2\x80\xa6");
	int spx = m_ctx.px - 2;
	text = Fonts::Elide(text, FS_ITALIC, spx, w - 8);
	int tw = Fonts::Measure(text, FS_ITALIC, spx);
	if (h > Fonts::LineHeight(FS_ITALIC, spx))
		Fonts::Draw(c, x + (w - tw) / 2, y + h / 2 + Fonts::Ascent(FS_ITALIC, spx) / 2, text, FS_ITALIC, spx, m_ctx.muted);
}

void MessageView::ImagesChanged()
{
	// Downloads come in bursts: repaint once for each burst.
	if (m_repaintTimer)
		return;
	m_repaintTimer = XtAppAddTimeOut(XtWidgetToApplicationContext(m_area), 80, RepaintTimerCB, this);
}

void MessageView::RepaintTimerCB(XtPointer client, XtIntervalId*)
{
	MessageView* self = (MessageView*) client;
	self->m_repaintTimer = 0;
	self->Paint();
}

void MessageView::PaintItem(Item& item, int top)
{
	Canvas& c = m_canvas;
	ItemExtra& ex = *item.extra;
	const Message& m = *item.msg;
	int right = m_viewW - MARGIN;

	if (!ex.dateSep.empty()) {
		int mid = top + DATE_SEP / 2 + 2;
		int tw = Fonts::Measure(ex.dateSep, FS_BOLD, m_ctx.px - 3);
		int cx = (m_viewW - tw) / 2;
		c.HLine(MARGIN, mid, cx - MARGIN - 8, m_ctx.codeFrame);
		c.HLine(cx + tw + 8, mid, right - (cx + tw + 8), m_ctx.codeFrame);
		Fonts::Draw(c, cx, mid + 4, ex.dateSep, FS_BOLD, m_ctx.px - 3, m_ctx.muted);
	}

	if (item.systemLine) {
		int y = top + ex.headerTop + Fonts::Ascent(FS_ITALIC, m_ctx.px);
		if (m.IsLoadGap()) {
			int tw = Fonts::Measure(item.systemText, FS_ITALIC, m_ctx.px);
			Fonts::Draw(c, (m_viewW - tw) / 2, y, item.systemText, FS_ITALIC, m_ctx.px, m_ctx.muted);
		}
		else {
			Fonts::Draw(c, TEXT_X - 22, y, "\xe2\x86\x92", FS_BOLD, m_ctx.px, 0x3ba55c);
			Fonts::Draw(c, TEXT_X, y, Fonts::Elide(item.systemText, FS_ITALIC, m_ctx.px, right - TEXT_X), FS_ITALIC, m_ctx.px, m_ctx.muted);
		}
		return;
	}

	bool pending = m.m_type == MessageType::SENDING_MESSAGE || m.m_type == MessageType::UNSENT_MESSAGE;

	if (m.IsReply() && !item.grouped && m.m_pReferencedMessage) {
		int y = top + ex.replyTop;
		int spx = m_ctx.px - 2;
		int asc = Fonts::Ascent(FS_ITALIC, spx);
		// the reply's elbow from the avatar column
		c.VLine(MARGIN + AVATAR / 2, y + asc / 2, Fonts::LineHeight(FS_ITALIC, spx), m_ctx.quoteBar);
		c.HLine(MARGIN + AVATAR / 2, y + asc / 2, TEXT_X - 6 - (MARGIN + AVATAR / 2), m_ctx.quoteBar);
		std::string who = "@" + m.m_pReferencedMessage->m_author + " ";
		int x = TEXT_X;
		x += Fonts::Draw(c, x, y + asc, who, FS_BOLD, spx, m_ctx.muted);
		std::string snippet = m.m_pReferencedMessage->m_message;
		std::replace(snippet.begin(), snippet.end(), '\n', ' ');
		if (snippet.empty())
			snippet = m.m_pReferencedMessage->m_bHasAttachments ? "Click to see attachment" : "Original message was deleted";
		Fonts::Draw(c, x, y + asc, Fonts::Elide(snippet, FS_ITALIC, spx, right - x), FS_ITALIC, spx, m_ctx.muted);
	}

	if (!item.grouped)
	{
		int y = top + ex.headerTop;
		// the avatar; a coloured disc with the initial until it arrives
		const Image* av = m.m_avatar.empty() ?
			ImageCache::Get(ImageCache::DEFAULT_AVATAR, "", m.m_author_snowflake, AVATAR, AVATAR) :
			ImageCache::Get(ImageCache::AVATAR, m.m_avatar, m.m_author_snowflake, AVATAR, AVATAR);
		if (av)
			c.BlendArgbCircle(MARGIN + (AVATAR - av->w) / 2, y + (AVATAR - av->h) / 2, av->px.data(), av->w, av->h, av->w);
		else
			c.FillCircle(MARGIN, y, AVATAR, AVATAR, AvatarColor(m.m_author_snowflake));
		if (!av && !m.m_author.empty()) {
			const char* p = m.m_author.c_str();
			const char* end = p + m.m_author.size();
			DecodeUtf8(p, end);
			std::string initial(m.m_author.c_str(), p - m.m_author.c_str());
			int iw = Fonts::Measure(initial, FS_BOLD, 17);
			Fonts::Draw(c, MARGIN + (AVATAR - iw) / 2, y + AVATAR / 2 + 6, initial, FS_BOLD, 17, 0xffffff);
		}

		int asc = Fonts::Ascent(FS_BOLD, m_ctx.px);
		int x = TEXT_X;
		std::string when = m.m_dateFull.empty() ? m.m_dateCompact : m.m_dateFull;
		if (!m.m_editedText.empty())
			when += "  (edited)";
		int whenW = Fonts::Measure(when, FS_REGULAR, m_ctx.px - 3);
		Rgb nameColor = Lists::RoleColor(m.m_author_snowflake, m_guild);
		std::string name = Fonts::Elide(m.m_author, FS_BOLD, m_ctx.px, std::max(40, right - x - whenW - 60));
		x += Fonts::Draw(c, x, y + asc, name, FS_BOLD, m_ctx.px, nameColor ? nameColor : m_ctx.fg) + 8;
		if (m.m_bIsAuthorBot || m.IsWebHook()) {
			int bw = Fonts::Measure("BOT", FS_BOLD, m_ctx.px - 4) + 8;
			c.FillRounded(x, y + 2, bw, asc, 3, 0x5865f2);
			Fonts::Draw(c, x + 4, y + asc - 1, "BOT", FS_BOLD, m_ctx.px - 4, 0xffffff);
			x += bw + 6;
		}
		// the time on the right, as Abaddon and IRC clients put it
		Fonts::Draw(c, right - whenW, y + asc, when, FS_REGULAR, m_ctx.px - 3, m_ctx.muted);
	}

	if (!item.text.Empty()) {
		Rgb fg = m_ctx.fg;
		if (pending)
			m_ctx.fg = m_ctx.muted;
		item.text.Draw(&m_ctx, top);
		m_ctx.fg = fg;
	}

	// attachments
	int ay = top + ex.attachTop;
	int lh = Fonts::LineHeight(FS_REGULAR, m_ctx.px) + 4;
	for (size_t ai = 0; ai < m.m_attachments.size(); ai++) {
		const Attachment& att = m.m_attachments[ai];
		if (ai < ex.attachPics.size() && !ex.attachPics[ai].url.empty()) {
			DrawPicture(ex.attachPics[ai].rect, ex.attachPics[ai].url, top, att.m_fileName);
			continue;
		}
		ay = top + (ai < ex.attachPics.size() ? ex.attachPics[ai].rect.top : ay - top);
		int asc = Fonts::Ascent(FS_REGULAR, m_ctx.px);
		// a page with a folded corner
		int ih = asc + 1, iw = ih * 3 / 4, iy = ay + 3, fold = iw / 3;
		c.Fill(TEXT_X, iy, iw, ih, 0xffffff);
		c.HLine(TEXT_X, iy, iw - fold, m_ctx.muted);
		c.VLine(TEXT_X, iy, ih, m_ctx.muted);
		c.HLine(TEXT_X, iy + ih - 1, iw, m_ctx.muted);
		c.VLine(TEXT_X + iw - 1, iy + fold, ih - fold, m_ctx.muted);
		for (int k = 0; k <= fold; k++)
			c.Fill(TEXT_X + iw - fold - 1 + k, iy + k, 1, 1, m_ctx.muted);
		c.VLine(TEXT_X + iw - fold - 1, iy, fold, m_ctx.muted);
		c.HLine(TEXT_X + iw - fold - 1, iy + fold, fold + 1, m_ctx.muted);
		int x = TEXT_X + iw + 6;
		int nameW = Fonts::Draw(c, x, ay + asc + 2, att.m_fileName, FS_REGULAR, m_ctx.px, m_ctx.link);
		c.HLine(x, ay + asc + 4, nameW, m_ctx.link);
		x += nameW;
		Fonts::Draw(c, x + 8, ay + asc + 2, "(" + MessageList::FormatSize(att.m_size) + ")", FS_REGULAR, m_ctx.px - 3, m_ctx.muted);
		ay += lh;
	}

	// reactions; the user's own stand out
	int rpx = m_ctx.px - 1;
	for (size_t i = 0; i < m.m_reactions.size() && i < ex.reactionRects.size(); i++)
	{
		const Reaction& r = m.m_reactions[i];
		const Rect& rc = ex.reactionRects[i];
		int x = rc.left, y = top + rc.top, w = rc.Width(), h = rc.Height();
		c.FillRounded(x, y, w, h, 7, r.m_bMe ? m_ctx.link : m_ctx.codeFrame);
		c.FillRounded(x + 1, y + 1, w - 2, h - 2, 6, r.m_bMe ? LerpRgb(m_ctx.bg, m_ctx.link, 18, 100) : m_ctx.codeBg);
		int base = y + (h + Fonts::Ascent(FS_REGULAR, rpx) - Fonts::Descent(FS_REGULAR, rpx)) / 2;
		int ex2 = x + PILL_PAD;
		if (r.m_emojiId) {
			int s = h - 8;
			const Image* img = ImageCache::Get(ImageCache::EMOJI, "", r.m_emojiId, s, s);
			if (img)
				c.BlendArgb(ex2 + (s - img->w) / 2, y + 4 + (s - img->h) / 2, img->px.data(), img->w, img->h, img->w);
			else
				c.FillRounded(ex2, y + 4, s, s, 3, LerpRgb(m_ctx.bg, m_ctx.muted, 1, 3));
			ex2 += s;
		}
		else
			ex2 += Fonts::Draw(c, ex2, base, r.m_emojiName, FS_REGULAR, rpx, m_ctx.fg);
		Fonts::Draw(c, ex2 + 5, base, std::to_string(r.m_count), FS_BOLD, rpx, r.m_bMe ? m_ctx.link : m_ctx.muted);
	}

	// embeds
	for (size_t i = 0; i < m.m_embeds.size() && i < ex.embedTops.size(); i++)
	{
		const RichEmbed& em = m.m_embeds[i];
		int y = top + ex.embedTops[i];
		int w = std::min(right, TEXT_X + 520) - TEXT_X;
		c.Fill(TEXT_X, y, w, ex.embedHeights[i], m_ctx.codeBg);
		c.Fill(TEXT_X, y, 4, ex.embedHeights[i], em.m_color ? (Rgb) em.m_color : m_ctx.codeFrame);
		int x = TEXT_X + 12;
		y += 6;
		if (!em.m_providerName.empty()) {
			int spx = m_ctx.px - 3;
			Fonts::Draw(c, x, y + Fonts::Ascent(FS_REGULAR, spx), Fonts::Elide(em.m_providerName, FS_REGULAR, spx, w - 16), FS_REGULAR, spx, m_ctx.muted);
			y += Fonts::LineHeight(FS_REGULAR, spx) + 2;
		}
		if (!em.m_authorName.empty()) {
			int spx = m_ctx.px - 2;
			Fonts::Draw(c, x, y + Fonts::Ascent(FS_BOLD, spx), Fonts::Elide(em.m_authorName, FS_BOLD, spx, w - 16), FS_BOLD, spx, m_ctx.fg);
			y += Fonts::LineHeight(FS_BOLD, spx) + 2;
		}
		if (!em.m_title.empty()) {
			Fonts::Draw(c, x, y + Fonts::Ascent(FS_BOLD, m_ctx.px), Fonts::Elide(em.m_title, FS_BOLD, m_ctx.px, w - 16), FS_BOLD, m_ctx.px,
				em.m_url.empty() ? m_ctx.fg : m_ctx.link);
		}
		if (ex.embedTexts[i])
			ex.embedTexts[i]->Draw(&m_ctx, top);
		if (i < ex.embedThumbs.size() && !ex.embedThumbs[i].url.empty())
			DrawPicture(ex.embedThumbs[i].rect, ex.embedThumbs[i].url, top, "");
		if (i < ex.embedImages.size() && !ex.embedImages[i].url.empty())
			DrawPicture(ex.embedImages[i].rect, ex.embedImages[i].url, top, "");
		if (!em.m_footerText.empty()) {
			int spx = m_ctx.px - 3;
			int fy = top + ex.embedTops[i] + ex.embedHeights[i] - 6 - Fonts::Descent(FS_REGULAR, spx);
			Fonts::Draw(c, x, fy, Fonts::Elide(em.m_footerText, FS_REGULAR, spx, w - 16), FS_REGULAR, spx, m_ctx.muted);
		}
	}
}

bool MessageView::CheckSize()
{
	// the first exposure can come before any resize callback
	Dimension width = 0, height = 0;
	XtVaGetValues(m_area, XmNwidth, &width, XmNheight, &height, NULL);
	bool changed = false;
	if (width != m_viewW || height != m_viewH) {
		bool widthChanged = width != m_viewW;
		m_viewW = width;
		m_viewH = height;
		if (widthChanged)
			LayoutAll();
		if (m_stickToBottom) {
			int maxY = std::max(0, m_list.ContentHeight() - m_viewH);
			m_scrollY = maxY;
		}
		UpdateScrollbar();
		changed = true;
	}
	if (m_canvas.Width() != m_viewW || m_canvas.Height() != m_viewH) {
		m_canvas.Resize(m_viewW, m_viewH);
		changed = true;
	}
	if (changed)
		m_canvasValid = false;
	return changed;
}

// Draws window rows y0..y1 into the canvas and shows them.
void MessageView::PaintBand(int y0, int y1)
{
	Display* dpy = XtDisplay(m_area);
	Window win = XtWindow(m_area);
	if (!m_gc)
		m_gc = XCreateGC(dpy, win, 0, NULL);

	m_canvas.SetClip(0, y0, m_viewW, y1 - y0);
	m_canvas.Fill(0, y0, m_viewW, y1 - y0, m_ctx.bg);
	m_ctx.canvas = &m_canvas;

	if (!m_channel) {
		const char* hint = "Pick a channel on the left.";
		int tw = Fonts::Measure(hint, FS_ITALIC, m_ctx.px + 2);
		Fonts::Draw(m_canvas, (m_viewW - tw) / 2, m_viewH / 2, hint, FS_ITALIC, m_ctx.px + 2, m_ctx.muted);
	}

	for (auto& it : m_list.Items()) {
		int top = it.y - m_scrollY;
		if (top + it.height < y0 || top > y1)
			continue;
		PaintItem(it, top);
	}

	m_ctx.canvas = nullptr;
	m_canvas.ClearClip();
	// the dither pattern follows the content, so moved rows still match
	m_canvas.Present(m_fmt, win, m_gc, 0, y0, m_viewW, y1 - y0, 0, y0, m_scrollY);
}

void MessageView::Paint()
{
	if (!XtIsRealized(m_area))
		return;
	Perf::Scope perf(Perf::MV_PAINT);
	double t0 = Perf::Now();
	CheckSize();
	PaintBand(0, m_viewH);
	double ms = (Perf::Now() - t0) * 1e3;
	m_fullMs = m_fullMs > 0 ? m_fullMs * 0.75 + ms * 0.25 : ms;
	m_canvasValid = true;
	m_paintedScrollY = m_scrollY;
	m_dirtyFromY = -1;
	RequestVisibleGaps();
	MarkReadIfSeen();
}

void MessageView::MarkReadIfSeen()
{
	if (!m_list.NewestShown(m_stickToBottom))
		return;
	// messages arriving while the user is in another window stay unread
	bool opened = m_justOpened;
	m_justOpened = false;
	if (!opened && !(m_isFocused ? m_isFocused() : Notifier::IsFocused()))
		return;
	if (m_list.AcknowledgeIfUnread())
		GetMainWindow()->ScheduleListUpdate(MainWindow::LIST_CHANNELS | MainWindow::LIST_GUILDS);
}

// DM_NO_SCROLLCOPY: always draw the whole view (to compare).
static bool ScrollCopyOff()
{
	static int off = -1;
	if (off < 0)
		off = getenv("DM_NO_SCROLLCOPY") != nullptr;
	return off != 0;
}

void MessageView::Update()
{
	if (!XtIsRealized(m_area))
		return;
	if (CheckSize() || !m_canvasValid || !m_unobscured || ScrollCopyOff()) {
		Paint();
		return;
	}
	int dy = m_scrollY - m_paintedScrollY; // > 0: the content moves up
	if (dy >= m_viewH || -dy >= m_viewH) {
		Paint();
		return;
	}

	// the rows scrolling uncovers, and from where messages were added
	int y0 = m_viewH, y1 = 0;
	if (dy > 0) {
		y0 = m_viewH - dy;
		y1 = m_viewH;
	}
	else if (dy < 0) {
		y0 = 0;
		y1 = -dy;
	}
	if (m_dirtyFromY >= 0 && m_dirtyFromY - m_scrollY < m_viewH) {
		y0 = std::min(y0, std::max(0, m_dirtyFromY - m_scrollY));
		y1 = m_viewH;
	}
	if (dy == 0 && y0 >= y1)
		return;

	// Moving pixels is a blit on a real board, but an emulated one can take
	// longer over it than over drawing and sending every pixel again: each
	// way's recent cost decides, and the other is tried every 16 updates.
	bool copy = m_copyMs <= 0 || m_fullMs <= 0 || m_copyMs <= m_fullMs;
	if (++m_updatesSinceProbe >= 16) {
		m_updatesSinceProbe = 0;
		copy = !copy;
	}
	if (!copy) {
		Paint();
		return;
	}

	Perf::Scope perf(Perf::MV_PAINT);
	double t0 = Perf::Now();
	if (dy) {
		m_canvas.Scroll(dy);
		int h = m_viewH - std::abs(dy);
		XCopyArea(XtDisplay(m_area), XtWindow(m_area), XtWindow(m_area), m_gc,
			0, std::max(dy, 0), m_viewW, h, 0, std::max(-dy, 0));
	}
	if (y0 < y1)
		PaintBand(y0, y1);
	double ms = (Perf::Now() - t0) * 1e3;
	m_copyMs = m_copyMs > 0 ? m_copyMs * 0.75 + ms * 0.25 : ms;
	m_paintedScrollY = m_scrollY;
	m_dirtyFromY = -1;
	RequestVisibleGaps();
	MarkReadIfSeen();
}
