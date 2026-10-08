#pragma once

#include <string>

#include "models/Snowflake.hpp"

// The Dear ImGui client's window: the server, channel and member lists,
// the messages (shared/MessageList, drawn here) and the message box, with
// the login, error and confirmation dialogs.  Built afresh every frame.
namespace App
{
	void Init(bool demo);

	// Builds this frame's interface over the whole window.
	void Frame(bool windowFocused);

	// What changed since the last frame (from the frontend's hooks).
	enum { LIST_GUILDS = 1, LIST_CHANNELS = 2, LIST_MEMBERS = 4, LISTS = 7, MESSAGES = 8 };
	void MarkDirty(int what);

	// The open channel changed (the core selected another).
	void OnChannelChanged();
	// The session is up: where the user was last time.
	void RestoreLastChannel();

	void SetStatus(const std::string& text);
	void ShowError(const std::string& text);
	// The login dialog (QR code, or a token), with why it shows.
	void ShowLogin(const std::string& why);

	bool QuitRequested();
}

// Main.cpp: a new session with the token in the settings, and logging out.
void StartWithToken();
void RequestLogout();
void RequestReconnect();
