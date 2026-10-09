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
#include "SystemTheme.hpp"
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
				selGuild = Demo::Guild();
				selChannel = Demo::Channel();
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
	SetDark(true); // the theme the system wants follows (App::UpdateTheme)
	if (demo) {
		status = "Demo: sample messages, not connected.";
		// DM_DEMO_CHANNEL=name: that channel at the start (stress-test, for
		// --bench on two thousand messages)
		const char* start = getenv("DM_DEMO_CHANNEL");
		Snowflake ch = start ? Demo::FindChannel(start) : 0;
		OpenDemoChannel(ch ? ch : Demo::CHANNEL);
		// DM_TEST_OPEN=viewer, picker or error: opened at once (for screenshots)
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
			else if (!strcmp(t, "error"))
				ShowError("Could not verify the identity of https://example.com/.\n\n"
					"The server's certificate was not accepted (a test of this dialog).");
		}
	}
	Typing::SetChangedCallback([] {});
}

void App::SelectDemoGuild(Snowflake guild)
{
	if (Snowflake ch = Demo::SelectGuild(guild))
		OpenDemoChannel(ch);
}

void App::OpenDemoChannel(Snowflake channel)
{
	if (channel == Demo::Channel() && list->GetChannel() == channel)
		return;
	if (composer.Cancel())
		input[0] = 0;
	bar.clear();
	unreadAfter = Demo::OpenChannel(channel);
	list->SetChannel(0, channel);
	stick = true;
	justOpened = true;
	MarkDirty(LISTS | MESSAGES);
}

bool App::Pending()
{
	return g_dirty != 0;
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

	// DM_TEST_SWITCH=name (demo): that channel opened 30 frames in, as a
	// click would, during the frame (for screenshots of a switch)
	static int frames = 0;
	if (demo && ++frames == 30)
		if (const char* sw = getenv("DM_TEST_SWITCH"))
			if (Snowflake ch = Demo::FindChannel(sw))
				OpenDemoChannel(ch);

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

namespace
{
	bool g_systemDark = true;
	int g_shownDark = -1, g_shownScheme = -1;
	std::function<void(bool, bool)> g_frameHook;
}

void App::ApplyTheme()
{
	ColorScheme scheme = GetColorScheme();
	if (const char* e = getenv("DM_THEME"))
		scheme = !strcmp(e, "light") ? SCHEME_LIGHT : !strcmp(e, "dark") ? SCHEME_DARK : scheme;
	bool d = scheme == SCHEME_SYSTEM ? g_systemDark : scheme == SCHEME_DARK;
	if ((int) d == g_shownDark && (int) scheme == g_shownScheme)
		return;
	g_shownDark = d;
	g_shownScheme = scheme;
	SetDark(d);
	if (g_frameHook)
		g_frameHook(d, scheme == SCHEME_SYSTEM);
}

void App::CheckSystemTheme()
{
	SystemTheme::Query([](bool d) {
		g_systemDark = d;
		ApplyTheme();
	});
}

void App::SetFrameHook(std::function<void(bool, bool)> hook)
{
	g_frameHook = hook;
}
