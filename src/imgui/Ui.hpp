#pragma once

// The ImGui client's interface, inside: its state and drawing helpers, used
// by the parts of the window (Rail, Sidebar, Chat, Popups) and App.cpp.

#include <string>
#include <vector>

#include "imgui.h"

#include "models/Message.hpp"
#include "shared/Composer.hpp"
#include "shared/ListRow.hpp"
#include "shared/MessageList.hpp"
#include "shared/PictureInfo.hpp"
#include "shared/TextStyle.hpp"
#include "DrawingContext.hpp"
#include "Gfx.hpp"

namespace Ui
{
	// ---- the look (Discord's dark or light theme) ------------------------------

	const int RAIL_W = 72, SIDEBAR_W = 240, MEMBERS_W = 240, HEADER_H = 48, PANEL_H = 52;

	// The theme's colours, set by SetDark: the backgrounds of the rail, the
	// lists, the messages and the user's panel; the text, from the most to
	// the least prominent; what marks hovered and selected rows; the lines,
	// surfaces (code, embeds, reactions, bars) and the ImGui widgets' own.
	extern uint32_t RAIL_BG, SIDEBAR_BG, CHAT_BG, PANEL_BG;
	extern uint32_t TEXT_STRONG, TEXT_BRIGHT, TEXT, MUTED, FAINT, PLACEHOLDER;
	extern uint32_t HOVER, SELECTED, MSG_HOVER, ICON_BG, COMPOSER_BG;
	extern uint32_t DIVIDER, LINE, RAIL_SEP, MENU_SEP, SURFACE, SURFACE_EDGE, QUOTE_BAR;
	extern uint32_t REACT_ME_BG, REACT_ME_FG, EMOJI_BUTTON, POPUP_BG, FRAME_BG, SCROLL_GRAB;
	extern bool dark;
	// Discord's accents, the same in both; what is drawn on them is white.
	const uint32_t BLURPLE = 0x5865f2, BLURPLE_HOVER = 0x4752c4, BLURPLE_ACTIVE = 0x3c45a5;
	const uint32_t RED = 0xf23f42, GREEN = 0x23a55a, ON_ACCENT = 0xffffff;

	// The dark theme or the light one: the colours above, the message
	// palette (GetPalette) and ImGui's style.
	void SetDark(bool dark);

	// ---- state ----------------------------------------------------------------

	extern bool demo;
	extern bool quit;
	extern int frameDirty;         // what this frame brings up to date (App::LIST_*, MESSAGES)
	extern bool focused;           // the window has the keyboard

	extern std::vector<ListRow> guildRows, channelRows, memberRows;
	extern Snowflake selGuild, selChannel;

	extern MessageList* list;
	extern DrawingContext ctx;
	extern Composer composer;
	extern char input[4000];
	extern std::string bar;        // "Replying to ...", "Editing your message"
	extern bool stick;             // the view goes to the newest messages next frame
	extern bool justOpened;
	extern Snowflake unreadAfter;  // messages after this are new (the NEW line)
	extern bool focusInput;        // the message box takes the keyboard next frame
	extern std::string status;

	extern std::vector<std::string> errors;
	extern MessagePtr menuMessage, deleteMessage;

	// ---- helpers --------------------------------------------------------------

	uint32_t Mix(uint32_t a, uint32_t b, int num, int den);
	uint32_t AvatarColor(Snowflake sf);
	std::string Initials(const std::string& text);
	// Text with its baseline at y.
	int TextAt(ImDrawList* dl, float x, float y, const std::string& s, FontStyle st, int px, uint32_t color);
	// Text vertically centred on cy.
	int TextMid(ImDrawList* dl, float x, float cy, const std::string& s, FontStyle st, int px, uint32_t color);
	// A filled rectangle (ImGui wants corners, not sizes).
	void Fill(ImDrawList* dl, float x, float y, float w, float h, uint32_t color, float rounding = 0);
	// A round avatar (or icon) from the image cache, or a coloured disc with
	// initials while it loads; with a presence dot when status >= 0.
	void Avatar(ImDrawList* dl, float x, float y, int size, const ListRow& r, uint32_t bg);
	void UserAvatar(ImDrawList* dl, float x, float y, int size, Snowflake user, const std::string& avatar,
		const std::string& name, int status, uint32_t bg);
	void PresenceDot(ImDrawList* dl, float cx, float cy, int status, uint32_t bg, float r = 5.0f);
	// A red count badge centred on (cx, cy).
	void Badge(ImDrawList* dl, float cx, float cy, int count, uint32_t border);
	// The app's icon, as a texture.
	const Image& AppIconImage();

	// ---- the parts --------------------------------------------------------------

	void Rail(float height);
	void Sidebar(float height);
	void Members(float height);
	void Chat(float height);
	void Popups();

	void OpenViewer(const PictureInfo& pic);
	void OpenPicker(bool react, Snowflake message, ImVec2 at);
	void SetScreenSize(int w, int h);

	// The login dialog (QR code, or a token).
	void ShowLogin(const std::string& why);
}
