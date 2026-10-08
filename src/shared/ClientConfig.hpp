#pragma once

#include <string>

#include "models/Snowflake.hpp"

// The client's own preferences, kept in ~/.discordmessenger/<fileName>
// (each front end its file: the Motif client's is motif.conf): the text
// size (DM_TEXT_SIZE overrides it for one run), which panes show, the
// notification switches and the channel last open.
void LoadClientConfig(const std::string& fileName);
void SaveClientConfig();
int GetTextSize();
void SetTextSize(int px);
enum Pane { PANE_GUILDS, PANE_CHANNELS, PANE_MEMBERS, PANE_COUNT };
bool IsPaneShown(Pane p);
void SetPaneShown(Pane p, bool shown);
// Notifications for mentions and direct messages: a sound, and a popup
// when the window is not in front.
enum Notify { NOTIFY_SOUND, NOTIFY_POPUP, NOTIFY_COUNT };
bool IsNotifyOn(Notify n);
void SetNotifyOn(Notify n, bool on);
// The server and channel last open, to open again at the next start.
void GetLastChannel(Snowflake& guild, Snowflake& channel);
void SetLastChannel(Snowflake guild, Snowflake channel);
