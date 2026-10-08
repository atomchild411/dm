// The server rail: Direct Messages (the home button), then the servers as
// round icons, with the white pill on the left (unread, hovered, selected)
// and the red mention badges.

#include "Ui.hpp"

#include "DiscordInstance.hpp"
#include "shared/ImageCache.hpp"

void Ui::Rail(float height)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Col(RAIL_BG)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("rail", ImVec2(RAIL_W, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float ICON = 48;
	ImGui::Dummy(ImVec2(0, 12));

	for (size_t i = 0; i < guildRows.size(); i++)
	{
		const ListRow& r = guildRows[i];
		if (r.type == ListRow::HEADER)
			continue; // a folder: its servers follow
		ImVec2 pos = ImGui::GetCursorScreenPos();
		if (r.type == ListRow::SPACE) {
			ImGui::Dummy(ImVec2(RAIL_W, 10));
			dl->AddRectFilled(ImVec2(pos.x + (RAIL_W - 32) / 2, pos.y + 4), ImVec2(pos.x + (RAIL_W + 32) / 2, pos.y + 6), Col(RAIL_SEP), 1);
			continue;
		}

		ImGui::PushID((int) i);
		ImGui::SetCursorScreenPos(ImVec2(pos.x + (RAIL_W - ICON) / 2, pos.y));
		bool clicked = ImGui::InvisibleButton("guild", ImVec2(ICON, ICON));
		bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		ImGui::Dummy(ImVec2(0, 8));
		if (hovered)
			ImGui::SetTooltip("%s", r.text.c_str());
		if (clicked && !demo)
			GetDiscordInstance()->OnSelectGuild(r.id);

		bool sel = r.id == selGuild;
		float x = pos.x + (RAIL_W - ICON) / 2, y = pos.y;
		// round, and a rounded square when picked or pointed at
		float rounding = sel || hovered ? 16.0f : ICON / 2;

		if (r.id == 0 || r.glyph == "@") {
			// home: the app's icon on blurple when it is where the user is
			Fill(dl, x, y, ICON, ICON, sel || hovered ? BLURPLE : ICON_BG, rounding);
			const Image& icon = AppIconImage();
			float s = 30;
			dl->AddImage(Gfx::Texture(icon), ImVec2(x + (ICON - s) / 2, y + (ICON - s) / 2), ImVec2(x + (ICON + s) / 2, y + (ICON + s) / 2));
		}
		else {
			const Image* img = r.hasImage ? ImageCache::Get(r.imageKind, r.imagePlace, r.imageSf, (int) ICON, (int) ICON) : nullptr;
			if (img)
				dl->AddImageRounded(Gfx::Texture(*img), ImVec2(x, y), ImVec2(x + ICON, y + ICON), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
			else {
				Fill(dl, x, y, ICON, ICON, sel || hovered ? BLURPLE : ICON_BG, rounding);
				std::string ini = Initials(r.text);
				int px = 16, tw = Gfx::Measure(ini, FS_BOLD, px);
				while (tw > ICON - 10 && px > 9)
					tw = Gfx::Measure(ini, FS_BOLD, --px);
				TextMid(dl, x + (ICON - tw) / 2, y + ICON / 2, ini, FS_BOLD, px, sel || hovered ? ON_ACCENT : TEXT);
			}
		}

		// the pill: tall for the open one, half for the pointed at, a dot
		// for unread
		float pill = sel ? 40 : hovered ? 20 : r.unread ? 8 : 0;
		if (pill > 0)
			dl->AddRectFilled(ImVec2(pos.x - 4, y + (ICON - pill) / 2), ImVec2(pos.x + 4, y + (ICON + pill) / 2), Col(TEXT_STRONG), 4.0f);
		if (r.mentions > 0)
			Badge(dl, x + ICON - 6, y + ICON - 6, r.mentions, RAIL_BG);
	}
	ImGui::Dummy(ImVec2(0, 8));
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}
