// The windows over the client: the picture viewer, the emoji picker, the
// login, errors and the question before deleting.

#include "Ui.hpp"

#include <algorithm>
#include <cstring>

#include "App.hpp"
#include "shared/Demo.hpp"
#include "DiscordInstance.hpp"
#include "config/LocalSettings.hpp"
#include "shared/ClientConfig.hpp"
#include "shared/EmojiChoices.hpp"
#include "shared/ImageCache.hpp"
#include "shared/QrLogin.hpp"
#include "shared/Shortcodes.hpp"
#include "WebLogin.hpp"

using namespace Ui;

namespace
{
	// the image viewer: the picture, what is fetched, and whether it shows
	bool viewerOpen = false;
	PictureInfo viewPic;
	PictureFetch viewFetch;
	int screenW = 1920, screenH = 1080;

	// the emoji picker: Add Reaction (to a message) or Insert Emoji
	bool pickerOpen = false;
	bool pickerReact = false;
	Snowflake pickerMessage = 0;
	ImVec2 pickerAt;

	// the login dialog
	bool loginShown = false;
	bool tokenMode = false;
	std::string loginWhy;
	char token[256];

	// The token from discord.com's login page: the session starts with it.
	// The QR login waits meanwhile (else its captcha would come up over
	// this window), and starts again when the window is closed unused.
	void OpenWebLogin()
	{
		QrLogin::Stop();
		WebLogin::Open([](const std::string& tok) {
			loginShown = false;
			GetLocalSettings()->SetToken(tok);
			GetLocalSettings()->Save();
			StartWithToken();
		}, [] {
			if (loginShown && !tokenMode)
				Ui::ShowLogin(loginWhy);
		});
	}

	// The captcha Discord wants before a QR login completes, in its own
	// window; the login goes on with the answer.
	std::string captchaShownFor; // its rqtoken, so it opens by itself once

	bool CaptchaPending()
	{
		const QrLogin::Captcha& c = QrLogin::PendingCaptcha();
		return WebLogin::Available() && QrLogin::Failed() && !c.sitekey.empty()
			&& (c.service.empty() || c.service == "hcaptcha");
	}

	void ShowCaptcha()
	{
		const QrLogin::Captcha& c = QrLogin::PendingCaptcha();
		captchaShownFor = c.rqtoken.empty() ? c.sitekey : c.rqtoken;
		WebLogin::ShowCaptcha(c.sitekey, c.rqdata, [](const std::string& answer) {
			if (CaptchaPending())
				QrLogin::SolveCaptcha(answer);
		}, [] {});
	}

	void Login()
	{
		if (!loginShown)
			return;
		ImGui::OpenPopup("Log in to Discord");
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		if (!ImGui::BeginPopupModal("Log in to Discord", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		if (!loginWhy.empty())
			ImGui::TextWrapped("%s", loginWhy.c_str());
		if (!tokenMode && !QrLogin::Enabled()) {
			// discord.com's own login page: email and password, with any
			// captcha Discord asks for; or a token
			ImGui::Text("Log in to Discord");
			if (ImGui::Button("Log In on discord.com", ImVec2(300, 0)))
				OpenWebLogin();
			ImGui::TextDisabled("With your email and password, in a window of its own.");
			ImGui::Separator();
			if (ImGui::Button("Use a Token Instead"))
				tokenMode = true;
			ImGui::SameLine();
			if (ImGui::Button("Quit"))
				quit = true;
		}
		else if (!tokenMode) {
			ImGui::Text("Log in with a QR code");
			if (!QrLogin::Notice().empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Col(RED)));
				ImGui::TextWrapped("%s", QrLogin::Notice().c_str());
				ImGui::PopStyleColor();
			}
			const float area = 300;
			ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(area, area));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(pos, ImVec2(pos.x + area, pos.y + area), Col(0xffffff));
			int n = QrLogin::CodeSize();
			if (n > 0) {
				int scale = std::max(1, (int) area / (n + 8));
				float x0 = pos.x + (area - n * scale) / 2, y0 = pos.y + (area - n * scale) / 2;
				for (int y = 0; y < n; y++)
					for (int x = 0; x < n; x++)
						if (QrLogin::CodeModule(x, y))
							dl->AddRectFilled(ImVec2(x0 + x * scale, y0 + y * scale), ImVec2(x0 + (x + 1) * scale, y0 + (y + 1) * scale), Col(0));
				if (QrLogin::Scanned())
					dl->AddRectFilled(pos, ImVec2(pos.x + area, pos.y + area), IM_COL32(255, 255, 255, 170));
			}
			else {
				const char* text = QrLogin::Failed() ? "No code: see below" : "Preparing a code\xe2\x80\xa6";
				int tw = Gfx::Measure(text, FS_ITALIC, 14);
				TextAt(dl, pos.x + (area - tw) / 2, pos.y + area / 2, text, FS_ITALIC, 14, 0x606060);
			}
			bool captcha = CaptchaPending();
			if (captcha) {
				const QrLogin::Captcha& c = QrLogin::PendingCaptcha();
				if (captchaShownFor != (c.rqtoken.empty() ? c.sitekey : c.rqtoken))
					ShowCaptcha();
				ImGui::TextWrapped("Discord wants a captcha before it finishes this login:\n"
					"solve it in its window and the login goes on.");
				if (ImGui::Button("Show the Captcha", ImVec2(300, 0)))
					ShowCaptcha();
			}
			else
				ImGui::TextWrapped("%s", QrLogin::StatusText().c_str());
			ImGui::Separator();
			if (WebLogin::Available()) {
				// discord.com's own login page: email and password, and the
				// captcha Discord may ask for (which the QR login cannot show)
				if (ImGui::Button("Log In on discord.com", ImVec2(300, 0)))
					OpenWebLogin();
				ImGui::TextDisabled("With your email and password; any captcha shows there.");
				ImGui::Separator();
			}
			if (ImGui::Button("Use a Token Instead")) {
				QrLogin::Stop();
				tokenMode = true;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!QrLogin::Failed());
			if (ImGui::Button("Try Again"))
				QrLogin::Retry();
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Quit"))
				quit = true;
		}
		else {
			ImGui::TextWrapped("The token is the \"authorization\" header of any request a logged-in\n"
				"discord.com page makes (the browser's developer tools, Network).");
			ImGui::SetNextItemWidth(420);
			bool enter = ImGui::InputText("Token", token, sizeof token, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
			if ((ImGui::Button("Log In") || enter) && token[0]) {
				GetLocalSettings()->SetToken(token);
				GetLocalSettings()->Save();
				memset(token, 0, sizeof token);
				loginShown = false;
				ImGui::CloseCurrentPopup();
				StartWithToken();
			}
			ImGui::SameLine();
			// back to discord.com's page (or the QR code)
			if (WebLogin::Available() || QrLogin::Enabled()) {
				if (ImGui::Button("Back")) {
					memset(token, 0, sizeof token);
					Ui::ShowLogin(loginWhy);
				}
				ImGui::SameLine();
			}
			if (ImGui::Button("Quit"))
				quit = true;
		}
		ImGui::EndPopup();
	}

	// A picture, as large as fits the window it is in (resizable); Escape,
	// a click on it, or Close shuts it.
	void ViewerWindow()
	{
		if (!viewerOpen)
			return;
		const char* id = "Picture##viewer";
		ImGui::OpenPopup(id);
		ImGuiViewport* vp = ImGui::GetMainViewport();
		// the picture's size (in the main window), the buttons below it
		float maxW = vp->WorkSize.x * 0.9f, maxH = vp->WorkSize.y * 0.9f - 60;
		float w = (float) viewFetch.w, h = (float) viewFetch.h;
		float k = std::min(1.0f, std::min(maxW / w, maxH / h));
		ImGui::SetNextWindowSize(ImVec2(std::max(320.0f, w * k + 16), h * k + 60), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(Col(0x18191c)));
		if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar))
		{
			ImVec2 avail = ImGui::GetContentRegionAvail();
			avail.y -= ImGui::GetFrameHeightWithSpacing() + 4;
			ImVec2 pos = ImGui::GetCursorScreenPos();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const Image* img = ImageCache::Get(ImageCache::URL, viewFetch.url, 0, viewFetch.w, viewFetch.h);
			bool close = false;
			if (img) {
				// scaled to the window, its shape kept; the GPU smooths it
				float s = std::min(avail.x / img->w, avail.y / img->h);
				float dw = img->w * s, dh = img->h * s;
				ImVec2 p0(pos.x + (avail.x - dw) / 2, pos.y + (avail.y - dh) / 2);
				Gfx::AddImage(dl, *img, p0, ImVec2(p0.x + dw, p0.y + dh));
			}
			else {
				bool failed = ImageCache::Failed(ImageCache::URL, viewFetch.url, 0, viewFetch.w, viewFetch.h);
				const char* text = failed ? "The picture could not be loaded." : "Loading\xe2\x80\xa6";
				int tw = Gfx::Measure(text, FS_ITALIC, 15);
				TextAt(dl, pos.x + (avail.x - tw) / 2, pos.y + avail.y / 2, text, FS_ITALIC, 15, 0xb0b0b0);
			}
			if (ImGui::InvisibleButton("##picture", ImVec2(std::max(1.0f, avail.x), std::max(1.0f, avail.y))))
				close = true;
			ImGui::Dummy(ImVec2(0, 4));
			ImGui::TextDisabled("%s", viewPic.title.c_str());
			ImGui::SameLine(ImGui::GetWindowWidth() - 70);
			if (ImGui::Button("Close", ImVec2(60, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
				close = true;
			if (close) {
				viewerOpen = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();
	}

	// The common emoji, then the server's own; the one clicked reacts to the
	// message, or goes into the message box as its shortcode.
	void PickerWindow()
	{
		const char* id = "##emojipicker";
		if (pickerOpen) {
			ImGui::OpenPopup(id);
			pickerOpen = false;
		}
		const int COLUMNS = 8, CELL = 38, VISIBLE_ROWS = 7;
		float width = COLUMNS * CELL + 2 * ImGui::GetStyle().WindowPadding.x + ImGui::GetStyle().ScrollbarSize;
		ImVec2 at(pickerAt.x - width / 2, pickerAt.y - (VISIBLE_ROWS * CELL + 70));
		ImGuiViewport* vp = ImGui::GetMainViewport();
		at.x = std::max(vp->WorkPos.x, std::min(at.x, vp->WorkPos.x + vp->WorkSize.x - width));
		at.y = std::max(vp->WorkPos.y, at.y);
		ImGui::SetNextWindowPos(at, ImGuiCond_Appearing);
		if (!ImGui::BeginPopup(id))
			return;
		ImGui::TextDisabled(pickerReact ? "Add Reaction" : "Insert Emoji");
		Snowflake guild = list->GetGuild();
		std::vector<EmojiChoices::Section> sections = EmojiChoices::For(demo ? 0 : guild);
		static std::string hoverName;
		std::string hovered;
		// as tall as the emoji need, up to VISIBLE_ROWS rows (then it scrolls)
		float contentH = 0;
		for (auto& section : sections)
			contentH += (section.title.empty() ? 0 : 24) + (float) ((section.emoji.size() + COLUMNS - 1) / COLUMNS) * CELL;
		float gridH = std::min(contentH + 4, (float) (VISIBLE_ROWS * CELL + 24));
		ImGui::BeginChild("grid", ImVec2(COLUMNS * CELL + ImGui::GetStyle().ScrollbarSize, gridH));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const Palette& p = GetPalette();
		int n = 0;
		for (auto& section : sections) {
			if (!section.title.empty()) {
				ImGui::Dummy(ImVec2(0, 4));
				ImVec2 tp = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(COLUMNS * CELL, 20));
				TextAt(dl, tp.x + 6, tp.y + 15, Gfx::Elide(section.title, FS_BOLD, GetTextSize() - 2, COLUMNS * CELL - 12), FS_BOLD, GetTextSize() - 2, p.msgMuted);
			}
			for (size_t i = 0; i < section.emoji.size(); i++) {
				const Reaction& e = section.emoji[i];
				if (i % COLUMNS)
					ImGui::SameLine(0, 0);
				ImVec2 cp = ImGui::GetCursorScreenPos();
				ImGui::PushID(n++);
				bool clicked = ImGui::InvisibleButton("e", ImVec2(CELL, CELL));
				bool hover = ImGui::IsItemHovered();
				ImGui::PopID();
				if (hover) {
					dl->AddRectFilled(ImVec2(cp.x + 1, cp.y + 1), ImVec2(cp.x + CELL - 1, cp.y + CELL - 1), Col(p.selBg), 6.0f);
					hovered = e.m_emojiId ? ":" + e.m_emojiName + ":" : Shortcodes::For(e);
				}
				if (ImGui::IsItemVisible()) {
					if (e.m_emojiId) {
						int s = CELL - 10;
						const Image* im = ImageCache::Get(ImageCache::EMOJI, "", e.m_emojiId, s, s);
						if (im)
							Gfx::DrawImage(dl, *im, cp.x + (CELL - im->w) / 2.0f, cp.y + (CELL - im->h) / 2.0f);
						else
							dl->AddRectFilled(ImVec2(cp.x + 5, cp.y + 5), ImVec2(cp.x + 5 + s, cp.y + 5 + s), Col(Mix(p.msgBg, p.msgMuted, 1, 4)), 4.0f);
					}
					else {
						int px = CELL * 2 / 3; // the colour emoji come out a fifth larger
						int w = Gfx::Measure(e.m_emojiName, FS_REGULAR, px);
						Gfx::Text(dl, ImVec2(cp.x + (CELL - w) / 2.0f, cp.y + (CELL - Gfx::LineHeight(FS_REGULAR, px)) / 2.0f), e.m_emojiName, FS_REGULAR, px, p.msgFg);
					}
				}
				if (clicked) {
					if (pickerReact && pickerMessage && demo) {
						if (const Message* m = FindListed(pickerMessage)) {
							Demo::React(list->GetChannel(), *m, e, true);
							App::MarkDirty(App::MESSAGES);
						}
					}
					else if (pickerReact && pickerMessage)
						GetDiscordInstance()->RequestReaction(list->GetChannel(), pickerMessage, e, true);
					else {
						std::string code = Shortcodes::For(e);
						size_t len = strlen(input);
						if (len && input[len - 1] != ' ' && len + 1 < sizeof input)
							strcat(input, " ");
						if (strlen(input) + code.size() + 1 < sizeof input)
							strcat(input, code.c_str());
						focusInput = true;
					}
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::EndChild();
		if (!hovered.empty())
			hoverName = hovered;
		ImGui::TextDisabled("%s", hoverName.empty() ? " " : hoverName.c_str());
		ImGui::EndPopup();
	}

	void Dialogs()
	{
		if (!errors.empty()) {
			// (its own ID: the main window is "Discord Messenger" too, and a
			// popup by that name would be that window)
			ImGui::OpenPopup("Discord Messenger##error");
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopupModal("Discord Messenger##error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				// (an auto-sized window has no width to wrap to: this is it)
				ImGui::PushTextWrapPos(440.0f);
				ImGui::TextUnformatted(errors.front().c_str());
				ImGui::PopTextWrapPos();
				if (ImGui::Button("OK")) {
					errors.erase(errors.begin());
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		if (deleteMessage) {
			ImGui::OpenPopup("Delete Message");
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopupModal("Delete Message", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::PushTextWrapPos(440.0f);
				ImGui::TextUnformatted(MessageList::DeleteQuestion(*deleteMessage).c_str());
				ImGui::PopTextWrapPos();
				if (ImGui::Button("Delete")) {
					GetDiscordInstance()->RequestDeleteMessage(list->GetChannel(), deleteMessage->m_snowflake);
					deleteMessage.reset();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					deleteMessage.reset();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		ViewerWindow();
		PickerWindow();
		Login();
	}

}

void Ui::OpenViewer(const PictureInfo& pic)
{
	viewPic = pic;
	// as large as the screen leaves room for
	viewFetch = FetchFor(pic, screenW * 9 / 10, screenH * 9 / 10 - 80);
	viewerOpen = true;
}

void Ui::OpenPicker(bool react, Snowflake message, ImVec2 at)
{
	pickerReact = react;
	pickerMessage = message;
	pickerAt = at;
	pickerOpen = true;
}

void Ui::SetScreenSize(int w, int h)
{
	if (w > 0 && h > 0) {
		screenW = w;
		screenH = h;
	}
}

void Ui::ShowLogin(const std::string& why)
{
	loginWhy = why;
	loginShown = true;
	// discord.com's page where there is a browser view, else a token (the
	// QR code only with DM_QR_LOGIN=1, see QrLogin::Enabled)
	tokenMode = !QrLogin::Enabled() && !WebLogin::Available();
	if (!QrLogin::Enabled())
		return;
	QrLogin::Start([] {}, [](const std::string& tok) {
		loginShown = false;
		GetLocalSettings()->SetToken(tok);
		GetLocalSettings()->Save();
		StartWithToken();
	});
}

void Ui::Popups()
{
	Dialogs();
}
