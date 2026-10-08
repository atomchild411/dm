#pragma once

#include <map>
#include <string>
#include <vector>

#include "Xm.hpp"

#include "models/Snowflake.hpp"
#include "Canvas.hpp"

class MessageView;
class IconList;

// The main window: guilds, channels, the message view with its editor and
// header, the member list, a status line and the menus.
class MainWindow
{
public:
	MainWindow(Widget toplevel, const PixelFormat& fmt);

	Widget GetShell() const { return m_shell; }
	MessageView* GetMessageView() const { return m_messages; }
	IconList* GetGuildList() const { return m_guilds; }
	IconList* GetChannelList() const { return m_channels; }
	IconList* GetMemberList() const { return m_members; }

	void UpdateGuildList();
	void UpdateSelectedGuild();
	void UpdateChannelList();
	void UpdateSelectedChannel();
	void UpdateMemberList();

	// The same, a little later: a burst of updates from the gateway (presence,
	// member list changes, read marks) makes the rows once.
	enum { LIST_GUILDS = 1, LIST_CHANNELS = 2, LIST_MEMBERS = 4 };
	void ScheduleListUpdate(int lists);
	// Whether a change to this user shows in the member list.
	bool ShowsMember(Snowflake user) const;
	void UpdateHeader();
	// Shows and hides the server, channel and member lists as View says.
	void ApplyPanes();
	// The next message sent replies to this one (a bar above the editor
	// says so, with Cancel); CancelReply takes that back.
	void BeginReply(Snowflake message, const std::string& author);
	void CancelReply();
	// The editor holds one of the user's messages to change; Send saves it.
	void BeginEdit(Snowflake message, const std::string& text);
	// At the first connection: the server and channel open when the
	// client last ran, if they are still there.
	void RestoreLastChannel();
	void UpdateTitle();

	void OnTyping(Snowflake user, Snowflake guild, Snowflake channel, time_t when);
	void OnStopTyping(Snowflake channel, Snowflake user);
	void SetStatus(const std::string& text);

	// Shows a modal error box.
	void ShowError(const std::string& text);

	bool IsIconic() const;

	// Images arrived: repaint what may show them.
	void OnImagesChanged();

	// Sample rows for --demo.
	void ShowDemoLists();

private:
	void OnGuildPicked(Snowflake sf);
	void OnChannelPicked(Snowflake sf);
	static void ListRepaintCB(XtPointer, XtIntervalId*);
	static void ListUpdateCB(XtPointer, XtIntervalId*);
	static void SendCB(Widget, XtPointer, XtPointer);
	static void EditorChangedCB(Widget, XtPointer, XtPointer);
	static void MenuCB(Widget, XtPointer, XtPointer);
	static void TypingTimerCB(XtPointer, XtIntervalId*);

	void BuildMenus(Widget menubar);
	void SendFromEditor();
	void UpdateTypingStatus();

	Widget m_shell, m_main, m_form;
	Widget m_guildList, m_channelList, m_memberList;
	Widget m_header, m_editor, m_sendButton, m_status;
	Widget m_memberPane;
	MessageView* m_messages;
	IconList* m_guilds;
	IconList* m_channels;
	IconList* m_members;
	XtIntervalId m_listRepaintTimer = 0;
	XtIntervalId m_listUpdateTimer = 0;
	Widget m_replyBar = nullptr, m_replyLabel = nullptr;
	Snowflake m_replyTo = 0;
	Snowflake m_editing = 0;
	Widget m_emojiButton = nullptr;
	const PixelFormat* m_fmt = nullptr;
	static void EmojiCB(Widget, XtPointer, XtPointer);
	Pixmap MakeEmojiPixmap(Widget button);
	bool m_restoredLast = false; // the last channel is saved from then on
	static void CancelReplyCB(Widget, XtPointer, XtPointer);
	int m_pendingLists = 0;


	// typing: channel -> user -> when it expires
	std::map<Snowflake, std::map<Snowflake, time_t>> m_typing;
	XtIntervalId m_typingTimer = 0;
	time_t m_lastTypingSent = 0;
	std::string m_statusText;
};

MainWindow* GetMainWindow();
