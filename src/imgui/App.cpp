#include "App.hpp"

#include <cstdlib>
#include <cstring>

#include "imgui.h"

#include "DiscordInstance.hpp"
#include "state/MessageCache.hpp"
#include "shared/ClientConfig.hpp"
#include "shared/Demo.hpp"
#include "shared/Lists.hpp"
#include "shared/Typing.hpp"
#include "Ui.hpp"

using namespace Ui;

namespace
{
	int g_dirty = App::LISTS | App::MESSAGES; // marked since the last frame
	bool g_restoredLast = false;

	void UpdateLists()
	{
		if (demo) {
			if (frameDirty & App::LISTS) {
				guildRows = Demo::GuildRows();
				channelRows = Demo::ChannelRows();
				memberRows = Demo::MemberRows();
				selGuild = Demo::SELECTED_GUILD;
				selChannel = Demo::SELECTED_CHANNEL;
			}
			return;
		}
		DiscordInstance* pInst = GetDiscordInstance();
		if (frameDirty & App::LIST_GUILDS)
			guildRows = Lists::GuildRows();
		if (frameDirty & App::LIST_CHANNELS)
			channelRows = Lists::ChannelRows();
		if ((frameDirty & App::LIST_MEMBERS) && IsPaneShown(PANE_MEMBERS))
			memberRows = Lists::MemberRows();
		selGuild = pInst->GetCurrentGuildID();
		selChannel = pInst->GetCurrentChannelID();
	}

	// ImGui's own widgets (menus, popups, the message box) in the same dark
	// theme as the drawn parts.
	void Style()
	{
		ImGuiStyle& s = ImGui::GetStyle();
		ImGui::StyleColorsDark(&s);
		s.WindowRounding = 8;
		s.PopupRounding = 6;
		s.FrameRounding = 4;
		s.ScrollbarSize = 8;
		s.ScrollbarRounding = 4;
		s.WindowBorderSize = 0;
		s.PopupBorderSize = 0;
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
		s.Colors[ImGuiCol_PopupBg] = c(0x111214);
		s.Colors[ImGuiCol_ChildBg] = c(0, 0);
		s.Colors[ImGuiCol_Border] = c(DIVIDER);
		s.Colors[ImGuiCol_FrameBg] = c(0x1e1f22);
		s.Colors[ImGuiCol_FrameBgHovered] = c(0x1e1f22);
		s.Colors[ImGuiCol_FrameBgActive] = c(0x1e1f22);
		s.Colors[ImGuiCol_Button] = c(BLURPLE);
		s.Colors[ImGuiCol_ButtonHovered] = c(0x4752c4);
		s.Colors[ImGuiCol_ButtonActive] = c(0x3c45a5);
		s.Colors[ImGuiCol_Header] = c(BLURPLE, 0.0f);
		s.Colors[ImGuiCol_HeaderHovered] = c(BLURPLE);
		s.Colors[ImGuiCol_HeaderActive] = c(0x4752c4);
		s.Colors[ImGuiCol_Separator] = c(0x2e2f34);
		s.Colors[ImGuiCol_ScrollbarBg] = c(0, 0);
		s.Colors[ImGuiCol_ScrollbarGrab] = c(0x1a1b1e);
		s.Colors[ImGuiCol_ScrollbarGrabHovered] = c(0x1a1b1e);
		s.Colors[ImGuiCol_ScrollbarGrabActive] = c(0x1a1b1e);
		s.Colors[ImGuiCol_ModalWindowDimBg] = c(0, 0.7f);
		s.Colors[ImGuiCol_TextSelectedBg] = c(BLURPLE, 0.5f);
		s.Colors[ImGuiCol_NavHighlight] = c(BLURPLE, 0.0f);
	}
}

void App::Init(bool isDemo)
{
	demo = isDemo;
	// roomier than the Motif client's, as Discord lays messages out
	MessageList::Geometry geo;
	geo.margin = 16;
	geo.avatar = 40;
	geo.textX = 72;
	geo.groupGap = 17;
	geo.lineGap = 2;
	geo.dateSep = 36;
	geo.pictureMax = 400;
	list = new MessageList(Gfx::Metrics(), geo);
	Style();
	if (demo) {
		Demo::LoadMessages();
		list->SetChannel(0, Demo::CHANNEL);
		status = "Demo: sample messages, not connected.";
		unreadAfter = 0;
		// DM_TEST_OPEN=viewer or picker: opened at once (for screenshots)
		if (const char* t = getenv("DM_TEST_OPEN")) {
			if (!strcmp(t, "viewer")) {
				PictureInfo pic;
				pic.url = "https://www.gstatic.com/webp/gallery3/1_webp_ll.webp";
				pic.width = 400;
				pic.height = 301;
				pic.title = "rose.webp";
				OpenViewer(pic);
			}
			else if (!strcmp(t, "picker"))
				OpenPicker(false, 0, ImVec2(800, 760));
		}
	}
	Typing::SetChangedCallback([] {});
}

void App::MarkDirty(int what)
{
	g_dirty |= what;
}

void App::OnChannelChanged()
{
	DiscordInstance* pInst = GetDiscordInstance();
	if (composer.Cancel())
		input[0] = 0;
	bar.clear();
	// remembered for the next start (not before the last one was opened again)
	if (g_restoredLast && pInst->GetCurrentChannelID()) {
		Snowflake g = 0, c = 0;
		GetLastChannel(g, c);
		if (g != pInst->GetCurrentGuildID() || c != pInst->GetCurrentChannelID()) {
			SetLastChannel(pInst->GetCurrentGuildID(), pInst->GetCurrentChannelID());
			SaveClientConfig();
		}
	}
	// what was new when the channel was opened: the NEW line goes above it
	Channel* ch = pInst->GetCurrentChannel();
	unreadAfter = ch && ch->HasUnreadMessages() ? ch->m_lastViewedMsg : 0;

	GetMessageCache()->LoadCachedChannel(pInst->GetCurrentChannelID(), pInst->GetCurrentGuildID());
	list->SetChannel(pInst->GetCurrentGuildID(), pInst->GetCurrentChannelID());
	if (pInst->GetCurrentChannel())
		pInst->HandledChannelSwitch();
	stick = true;
	justOpened = true;
	MarkDirty(LISTS | MESSAGES);
}

void App::RestoreLastChannel()
{
	if (g_restoredLast)
		return;
	g_restoredLast = true;
	DiscordInstance* pInst = GetDiscordInstance();
	Snowflake guild = 0, channel = 0;
	GetLastChannel(guild, channel);
	Guild* pGuild = pInst->GetGuild(guild);
	if (!channel || !pGuild || !pGuild->GetChannel(channel))
		return;
	pInst->OnSelectGuild(guild, channel);
}

void App::SetScreenSize(int w, int h)
{
	Ui::SetScreenSize(w, h);
}

void App::SetStatus(const std::string& text)
{
	status = text;
}

void App::ShowError(const std::string& text)
{
	errors.push_back(text);
}

void App::ShowLogin(const std::string& why)
{
	Ui::ShowLogin(why);
}

bool App::QuitRequested()
{
	return quit;
}

void App::Frame(bool windowFocused)
{
	focused = windowFocused;
	frameDirty = g_dirty;
	g_dirty = 0;
	UpdateLists();

	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::Begin("Discord Messenger", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

	float h = ImGui::GetContentRegionAvail().y;
	Rail(h);
	ImGui::SameLine();
	Sidebar(h);
	ImGui::SameLine();
	Chat(h);
	// the members of a server (not at home, with the conversations)
	if (IsPaneShown(PANE_MEMBERS) && selGuild) {
		ImGui::SameLine();
		Members(h);
	}
	ImGui::PopStyleVar(3);

	Popups();
	ImGui::End();
	Gfx::CollectTextures();
}
