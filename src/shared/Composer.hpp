#pragma once

#include <ctime>
#include <string>

#include "models/Snowflake.hpp"

// What a message box sends: a new message, a reply, or an edit of the
// user's own message, with emoji shortcodes made into emoji; and the
// "typing" notice while text is in it.  One per message box: the main
// window's follows the open channel, a conversation window's has its own.
class Composer
{
public:
	// channel 0: the channel open in the main window.
	explicit Composer(Snowflake channel = 0) : m_channel(channel) {}

	// The next message replies to that one.
	void BeginReply(Snowflake message);
	// The next send replaces that message's text.
	void BeginEdit(Snowflake message);
	// Back to a plain message.  True when an edit was dropped (its text is
	// still in the box).
	bool Cancel();
	Snowflake Editing() const { return m_editing; }

	enum Result
	{
		NOTHING,   // no text, or no channel
		SENT,      // a new message (or reply) is on its way
		EDITED,    // the edit is on its way
		FAILED,    // the core would not send it
	};
	// Sends the box's text (UTF-8).  After SENT or EDITED the box empties
	// and the reply or edit is over.
	Result Send(const std::string& text);

	// The box's text changed: tells Discord the user is typing (at most
	// every 8 seconds while there is text).
	void TextChanged(bool empty);

private:
	Snowflake ChannelId() const;

	Snowflake m_channel;
	Snowflake m_replyTo = 0;
	Snowflake m_editing = 0;
	time_t m_lastTypingSent = 0;
};
