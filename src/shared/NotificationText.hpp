#pragma once

#include <string>

struct Notification;

// What a notification says, for a popup or a desktop notification.
namespace NotificationText
{
	// "#channel, Server", or "direct message".
	std::string Where(const Notification& n);
	// The message on one line, or "(an attachment)".
	std::string OneLine(const Notification& n);
}
