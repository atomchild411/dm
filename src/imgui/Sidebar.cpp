// The channel sidebar (the server's name, its channels by category, the
// user's own panel with the settings), and the member list.

#include "Ui.hpp"

#include "App.hpp"
#include "DiscordInstance.hpp"
#include "models/Profile.hpp"
#include "shared/ClientConfig.hpp"
#include "shared/Demo.hpp"

namespace
{
	// The server's name over the channels (Direct Messages at home).
	void Header(float width)
	{
		using namespace Ui;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(width, HEADER_H));
		std::string name = "Direct Messages";
		if (demo)
			name = "Silicon Graphics User Group";
		else if (selGuild) {
			Guild* g = GetDiscordInstance()->GetGuild(selGuild);
			name = g ? g->m_name : "";
		}
		int px = 15;
		TextMid(dl, pos.x + 16, pos.y + HEADER_H / 2.0f, Gfx::Elide(name, FS_BOLD, px, (int) width - 48), FS_BOLD, px, TEXT_BRIGHT);
		dl->AddLine(ImVec2(pos.x, pos.y + HEADER_H - 1), ImVec2(pos.x + width, pos.y + HEADER_H - 1), Col(DIVIDER), 1.5f);
	}

	// The channels (or, at home, the conversations).
	void Channels(float width, float height)
	{
		using namespace Ui;
		ImGui::BeginChild("channels", ImVec2(width, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		int px = GetTextSize();
		ImGui::Dummy(ImVec2(0, 8));
		for (size_t i = 0; i < channelRows.size(); i++)
		{
			const ListRow& r = channelRows[i];
			ImVec2 pos = ImGui::GetCursorScreenPos();
			if (r.type == ListRow::SPACE) {
				ImGui::Dummy(ImVec2(width, 8));
				continue;
			}
			if (r.type == ListRow::HEADER) {
				// a category: small capitals, with its chevron
				ImGui::Dummy(ImVec2(width, 34));
				float cy = pos.y + 24;
				dl->AddTriangleFilled(ImVec2(pos.x + 10, cy - 2), ImVec2(pos.x + 16, cy - 2), ImVec2(pos.x + 13, cy + 2), Col(MUTED));
				TextMid(dl, pos.x + 20, cy, Gfx::Elide(r.text, FS_BOLD, 12, (int) width - 32), FS_BOLD, 12, MUTED);
				continue;
			}

			bool person = r.hasImage || r.initials;
			float h = person ? 44 : 34;
			ImGui::PushID((int) i);
			bool clicked = ImGui::InvisibleButton("ch", ImVec2(width, h));
			bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();
			if (!ImGui::IsItemVisible())
				continue;
			bool sel = r.id == selChannel && r.selectable;
			if (clicked && r.selectable && !demo)
				GetDiscordInstance()->OnSelectChannel(r.id);

			float x = pos.x + 8, w = width - 16;
			if (sel || (hovered && r.selectable))
				Fill(dl, x, pos.y + 1, w, h - 2, sel ? SELECTED : HOVER, 4);
			// an unread channel: the white pill on the edge
			if (r.unread && !sel)
				dl->AddRectFilled(ImVec2(pos.x - 4, pos.y + h / 2 - 4), ImVec2(pos.x + 4, pos.y + h / 2 + 4), Col(TEXT_STRONG), 4);

			uint32_t tc = sel ? TEXT_STRONG : r.unread ? TEXT_BRIGHT : (hovered && r.selectable) ? TEXT : (r.dim ? FAINT : MUTED);
			FontStyle st = r.unread && !sel ? FS_BOLD : FS_REGULAR;
			float tx;
			if (person) {
				Avatar(dl, x + 8, pos.y + (h - 32) / 2, 32, r, sel ? SELECTED : hovered ? HOVER : SIDEBAR_BG);
				tx = x + 8 + 32 + 12;
			}
			else {
				std::string glyph = r.glyph.empty() ? "#" : r.glyph;
				int gpx = px + 4;
				int gw = Gfx::Measure(glyph, FS_REGULAR, gpx);
				TextMid(dl, x + 8 + (18 - gw) / 2.0f, pos.y + h / 2, glyph, FS_REGULAR, gpx, FAINT);
				tx = x + 8 + 18 + 8;
			}
			float right = x + w - 8;
			int bw = 0;
			if (r.mentions > 0) {
				bw = 26;
				Badge(dl, right - 10, pos.y + h / 2, r.mentions, sel ? SELECTED : hovered ? HOVER : SIDEBAR_BG);
			}
			TextMid(dl, tx, pos.y + h / 2, Gfx::Elide(r.text, st, px, (int) (right - tx - bw)), st, px, tc);
		}
		ImGui::Dummy(ImVec2(0, 8));
		ImGui::EndChild();
	}

	// The user: avatar, name and status, and the settings.
	void UserPanel(float width)
	{
		using namespace Ui;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 pos = ImGui::GetCursorScreenPos();
		Fill(dl, pos.x, pos.y, width, PANEL_H, PANEL_BG);

		std::string name = "you", line = "Online";
		Snowflake id = 0;
		std::string avatar;
		int presence = 1;
		if (!demo && GetDiscordInstance()) {
			if (Profile* p = GetDiscordInstance()->GetProfile()) {
				name = p->m_globalName.empty() ? p->m_name : p->m_globalName;
				id = p->m_snowflake;
				avatar = p->m_avatarlnk;
				presence = (int) p->m_activeStatus;
				const char* names[] = { "Offline", "Online", "Idle", "Do Not Disturb" };
				line = !p->m_status.empty() ? p->m_status : (presence >= 0 && presence < 4 ? names[presence] : "");
			}
		}
		UserAvatar(dl, pos.x + 8, pos.y + (PANEL_H - 32) / 2, 32, id, avatar, name, presence, PANEL_BG);
		float tx = pos.x + 8 + 32 + 8, right = pos.x + width - 44;
		TextAt(dl, tx, pos.y + 23, Gfx::Elide(name, FS_BOLD, 14, (int) (right - tx)), FS_BOLD, 14, TEXT_BRIGHT);
		TextAt(dl, tx, pos.y + 40, Gfx::Elide(line, FS_REGULAR, 12, (int) (right - tx)), FS_REGULAR, 12, MUTED);

		// the gear: the settings menu
		ImGui::SetCursorScreenPos(ImVec2(pos.x + width - 40, pos.y + (PANEL_H - 32) / 2));
		bool gear = ImGui::InvisibleButton("settings", ImVec2(32, 32));
		bool hovered = ImGui::IsItemHovered();
		ImVec2 g = ImGui::GetItemRectMin();
		if (hovered)
			Fill(dl, g.x, g.y, 32, 32, HOVER, 4);
		const char* cog = "\xe2\x9a\x99"; // U+2699
		int cw = Gfx::Measure(cog, FS_REGULAR, 20);
		TextMid(dl, g.x + (32 - cw) / 2.0f, g.y + 16, cog, FS_REGULAR, 20, hovered ? TEXT : MUTED);
		if (hovered)
			ImGui::SetTooltip("User Settings");
		if (gear)
			ImGui::OpenPopup("settings");
		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y));
		ImGui::Dummy(ImVec2(width, PANEL_H));

		if (ImGui::BeginPopup("settings")) {
			bool members = IsPaneShown(PANE_MEMBERS);
			if (ImGui::MenuItem("Show Member List", nullptr, &members)) {
				SetPaneShown(PANE_MEMBERS, members);
				SaveClientConfig();
				App::MarkDirty(App::LIST_MEMBERS);
			}
			if (ImGui::MenuItem("Larger Text")) {
				SetTextSize(GetTextSize() + 1);
				SaveClientConfig();
				list->ForgetLayout();
			}
			if (ImGui::MenuItem("Smaller Text")) {
				SetTextSize(GetTextSize() - 1);
				SaveClientConfig();
				list->ForgetLayout();
			}
			if (ImGui::BeginMenu("Theme")) {
				const char* const names[] = { "Sync with System", "Dark", "Light" };
				for (int i = 0; i < 3; i++)
					if (ImGui::MenuItem(names[i], nullptr, GetColorScheme() == i)) {
						SetColorScheme((ColorScheme) i);
						SaveClientConfig();
						App::ApplyTheme();
					}
				ImGui::EndMenu();
			}
			bool sound = IsNotifyOn(NOTIFY_SOUND);
			if (ImGui::MenuItem("Notification Sound", nullptr, &sound)) {
				SetNotifyOn(NOTIFY_SOUND, sound);
				SaveClientConfig();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Reconnect", nullptr, false, !demo))
				RequestReconnect();
			if (ImGui::MenuItem("Log Out", nullptr, false, !demo))
				RequestLogout();
			ImGui::Separator();
			if (ImGui::MenuItem("Quit"))
				quit = true;
			ImGui::EndPopup();
		}
	}
}

void Ui::Sidebar(float height)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Col(SIDEBAR_BG)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("sidebar", ImVec2(SIDEBAR_W, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	Header(SIDEBAR_W);
	Channels(SIDEBAR_W, height - HEADER_H - PANEL_H);
	UserPanel(SIDEBAR_W);
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void Ui::Members(float height)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Col(SIDEBAR_BG)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("members", ImVec2(MEMBERS_W, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	float width = MEMBERS_W;
	int px = GetTextSize();
	ImGui::Dummy(ImVec2(0, 8));
	for (size_t i = 0; i < memberRows.size(); i++)
	{
		const ListRow& r = memberRows[i];
		ImVec2 pos = ImGui::GetCursorScreenPos();
		if (r.type == ListRow::HEADER) {
			ImGui::Dummy(ImVec2(width, 36));
			std::string t = r.text;
			for (auto& c : t)
				if (c >= 'a' && c <= 'z') c -= 32;
			TextMid(dl, pos.x + 16, pos.y + 26, Gfx::Elide(t, FS_BOLD, 12, (int) width - 32), FS_BOLD, 12, MUTED);
			continue;
		}
		if (r.type == ListRow::SPACE) {
			ImGui::Dummy(ImVec2(width, 8));
			continue;
		}
		float h = 44;
		ImGui::PushID((int) i);
		ImGui::InvisibleButton("m", ImVec2(width, h));
		bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		if (!ImGui::IsItemVisible())
			continue;
		float x = pos.x + 8, w = width - 16;
		if (hovered)
			Fill(dl, x, pos.y + 1, w, h - 2, HOVER, 4);
		Avatar(dl, x + 8, pos.y + (h - 32) / 2, 32, r, hovered ? HOVER : SIDEBAR_BG);
		float tx = x + 8 + 32 + 12, right = x + w - 8;
		uint32_t nameColor = r.textColor ? r.textColor : (r.dim ? FAINT : MUTED);
		if (r.dim && r.textColor)
			nameColor = Mix(r.textColor, SIDEBAR_BG, 1, 2);
		if (r.subtext.empty())
			TextMid(dl, tx, pos.y + h / 2, Gfx::Elide(r.text, FS_REGULAR, px, (int) (right - tx)), FS_REGULAR, px, hovered && !r.textColor ? TEXT : nameColor);
		else {
			TextAt(dl, tx, pos.y + 20, Gfx::Elide(r.text, FS_REGULAR, px, (int) (right - tx)), FS_REGULAR, px, hovered && !r.textColor ? TEXT : nameColor);
			TextAt(dl, tx, pos.y + 36, Gfx::Elide(r.subtext, FS_REGULAR, 12, (int) (right - tx)), FS_REGULAR, 12, MUTED);
		}
	}
	ImGui::Dummy(ImVec2(0, 8));
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}
