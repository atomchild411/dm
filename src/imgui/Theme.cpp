// Discord's two themes, dark and light: the drawn parts' colours (Ui.hpp),
// the message palette, and ImGui's own widgets (menus, popups, the message
// box) to match.

#include "Ui.hpp"

namespace Ui
{
	uint32_t RAIL_BG, SIDEBAR_BG, CHAT_BG, PANEL_BG;
	uint32_t TEXT_STRONG, TEXT_BRIGHT, TEXT, MUTED, FAINT, PLACEHOLDER;
	uint32_t HOVER, SELECTED, MSG_HOVER, ICON_BG, COMPOSER_BG;
	uint32_t DIVIDER, LINE, RAIL_SEP, MENU_SEP, SURFACE, SURFACE_EDGE, QUOTE_BAR;
	uint32_t REACT_ME_BG, REACT_ME_FG, EMOJI_BUTTON, POPUP_BG, FRAME_BG, SCROLL_GRAB;
	bool dark = true;
}

namespace
{
	void Colors(bool dark)
	{
		using namespace Ui;
		if (dark) {
			RAIL_BG = 0x1e1f22; SIDEBAR_BG = 0x2b2d31; CHAT_BG = 0x313338; PANEL_BG = 0x232428;
			TEXT_STRONG = 0xffffff; TEXT_BRIGHT = 0xf2f3f5; TEXT = 0xdbdee1; MUTED = 0x949ba4; FAINT = 0x80848e;
			PLACEHOLDER = 0x6d6f78;
			HOVER = 0x35373c; SELECTED = 0x404249; MSG_HOVER = 0x2e3035; ICON_BG = 0x313338; COMPOSER_BG = 0x383a40;
			DIVIDER = 0x1f2023; LINE = 0x3f4147; RAIL_SEP = 0x35363c; MENU_SEP = 0x2e2f34;
			SURFACE = 0x2b2d31; SURFACE_EDGE = 0x1e1f22; QUOTE_BAR = 0x4e5058;
			REACT_ME_BG = 0x373a54; REACT_ME_FG = 0xc9cdfb; EMOJI_BUTTON = 0xb5bac1;
			POPUP_BG = 0x111214; FRAME_BG = 0x1e1f22; SCROLL_GRAB = 0x1a1b1e;
		}
		else {
			RAIL_BG = 0xe3e5e8; SIDEBAR_BG = 0xf2f3f5; CHAT_BG = 0xffffff; PANEL_BG = 0xebedef;
			TEXT_STRONG = 0x060607; TEXT_BRIGHT = 0x060607; TEXT = 0x313338; MUTED = 0x5c5e66; FAINT = 0x8a8e94;
			PLACEHOLDER = 0x80848e;
			HOVER = 0xe0e1e5; SELECTED = 0xd4d7dc; MSG_HOVER = 0xf7f7f8; ICON_BG = 0xffffff; COMPOSER_BG = 0xebedef;
			DIVIDER = 0xe1e2e4; LINE = 0xe1e2e4; RAIL_SEP = 0xc4c9ce; MENU_SEP = 0xe1e2e4;
			SURFACE = 0xf2f3f5; SURFACE_EDGE = 0xe3e5e8; QUOTE_BAR = 0xc4c9ce;
			REACT_ME_BG = 0xe7e9fd; REACT_ME_FG = 0x4752c4; EMOJI_BUTTON = 0x4e5058;
			POPUP_BG = 0xffffff; FRAME_BG = 0xe3e5e8; SCROLL_GRAB = 0xc4c9ce;
		}
	}

	// ImGui's own widgets in the same theme as the drawn parts.
	void Style(bool dark)
	{
		using namespace Ui;
		ImGuiStyle& s = ImGui::GetStyle();
		if (dark)
			ImGui::StyleColorsDark(&s);
		else
			ImGui::StyleColorsLight(&s);
		s.WindowRounding = 8;
		s.PopupRounding = 6;
		s.FrameRounding = 4;
		s.ScrollbarSize = 8;
		s.ScrollbarRounding = 4;
		s.WindowBorderSize = 0;
		s.PopupBorderSize = dark ? 0 : 1;
		s.ItemSpacing = ImVec2(8, 6);
		s.WindowPadding = ImVec2(10, 10);
		auto c = [](uint32_t rgb, float a = 1.0f) {
			ImVec4 v = ImGui::ColorConvertU32ToFloat4(Col(rgb));
			v.w = a;
			return v;
		};
		s.Colors[ImGuiCol_Text] = c(TEXT);
		s.Colors[ImGuiCol_TextDisabled] = c(MUTED);
		s.Colors[ImGuiCol_WindowBg] = c(CHAT_BG);
		s.Colors[ImGuiCol_PopupBg] = c(POPUP_BG);
		s.Colors[ImGuiCol_ChildBg] = c(0, 0);
		s.Colors[ImGuiCol_Border] = c(DIVIDER);
		s.Colors[ImGuiCol_FrameBg] = c(FRAME_BG);
		s.Colors[ImGuiCol_FrameBgHovered] = c(FRAME_BG);
		s.Colors[ImGuiCol_FrameBgActive] = c(FRAME_BG);
		s.Colors[ImGuiCol_Button] = c(BLURPLE);
		s.Colors[ImGuiCol_ButtonHovered] = c(BLURPLE_HOVER);
		s.Colors[ImGuiCol_ButtonActive] = c(BLURPLE_ACTIVE);
		s.Colors[ImGuiCol_Header] = c(BLURPLE, 0.0f);
		s.Colors[ImGuiCol_HeaderHovered] = c(BLURPLE);
		s.Colors[ImGuiCol_HeaderActive] = c(BLURPLE_HOVER);
		s.Colors[ImGuiCol_Separator] = c(MENU_SEP);
		s.Colors[ImGuiCol_ScrollbarBg] = c(0, 0);
		s.Colors[ImGuiCol_ScrollbarGrab] = c(SCROLL_GRAB);
		s.Colors[ImGuiCol_ScrollbarGrabHovered] = c(SCROLL_GRAB);
		s.Colors[ImGuiCol_ScrollbarGrabActive] = c(SCROLL_GRAB);
		s.Colors[ImGuiCol_CheckMark] = c(BLURPLE);
		s.Colors[ImGuiCol_ModalWindowDimBg] = c(0, 0.7f);
		s.Colors[ImGuiCol_TextSelectedBg] = c(BLURPLE, 0.5f);
		s.Colors[ImGuiCol_NavHighlight] = c(BLURPLE, 0.0f);
	}
}

void Ui::SetDark(bool d)
{
	dark = d;
	Colors(d);
	Style(d);
	Gfx::SetPaletteDark(d);
}
