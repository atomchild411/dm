// --bench: a fixed workload on made-up data, timed, for comparing builds.
// No login and no network (images stay placeholders), so two builds run
// exactly the same steps.

#include "Xm.hpp"
#include "Bench.hpp"

#include <cstdio>
#include <ctime>
#include <list>
#include <string>
#include <vector>

#include "DiscordInstance.hpp"
#include "state/MessageCache.hpp"
#include "utils/Util.hpp"

#include "Fonts.hpp"
#include "IconList.hpp"
#include "ImageCache.hpp"
#include "MainWindow.hpp"
#include "MessageView.hpp"
#include "Perf.hpp"
#include "Shortcodes.hpp"
#include "Theme.hpp"

namespace
{
	const Snowflake BENCH_CHANNEL = 4242;
	const int MESSAGES = 400;
	const int MEMBERS = 300;

	std::function<void()> g_done;
	Snowflake g_nextId = 5000000;
	time_t g_now;

	const char* const g_authors[] = {
		"Ada", "Grace", "Dennis", "Linus", "Bjarne", "Margaret", "Ken", "Barbara",
		"Radia", "Frances", "Edsger", "Niklaus",
	};

	const char* const g_texts[] = {
		"Has anyone got the **Indigo2** booting from the new disk yet?",
		"Not yet, the PROM says `Unable to load bootp()` and stops.",
		"Here is what fixed it for me:\n```\nsetenv netaddr 192.168.1.20\nboot -f bootp()/unix\n```\nThen it went straight to the miniroot.",
		"> it went straight to the miniroot\nNice. *Italic*, __underlined__, ~~struck~~ and a link: https://www.sgi.com/",
		"Emoji: \xf0\x9f\x98\x80 \xf0\x9f\x8e\x89 \xe2\x9c\xa8 \xe2\x9d\xa4\xef\xb8\x8f \xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd and text \xe2\x98\x85 \xe2\x9c\x93 stays text",
		"A long line to see the wrapping: Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.",
		"ok",
		"Does the Octane's V12 do 1280x1024 at 76 Hz, or only 72?",
		"caf\xc3\xa9 na\xc3\xafve \xe2\x80\x94 r\xc3\xa9sum\xc3\xa9, and a few more words to fill the line out a bit.",
		"# Release notes\n- MIPS IV build\n- FreeType text\n- Motif UI",
	};

	Message MakeMessage(int k, time_t when)
	{
		Message m;
		g_nextId += 4; // room for messages inserted between
		m.m_snowflake = g_nextId;
		int a = (k * 7 + k / 3) % 12;
		m.m_author_snowflake = (Snowflake) (1001 + a) << 22;
		m.m_author = g_authors[a];
		m.m_message = g_texts[k % (sizeof g_texts / sizeof g_texts[0])];
		m.SetTime(when);
		if (k % 17 == 5) {
			m.m_pReferencedMessage = std::make_shared<ReferenceMessage>();
			m.m_pReferencedMessage->m_author = g_authors[(a + 1) % 12];
			m.m_pReferencedMessage->m_message = g_texts[(k + 1) % (sizeof g_texts / sizeof g_texts[0])];
		}
		return m;
	}

	std::vector<IconRow> MemberRows(int changed, int status)
	{
		std::vector<IconRow> rows;
		for (int i = 0; i < MEMBERS; i++)
		{
			if (i % 50 == 0) {
				IconRow h;
				h.type = IconRow::HEADER;
				h.text = "Role " + std::to_string(i / 50 + 1) + " \xe2\x80\x94 50";
				rows.push_back(h);
			}
			IconRow r;
			r.id = (Snowflake) (2000 + i) << 22;
			r.text = std::string(g_authors[i % 12]) + " " + std::to_string(i) + (i % 9 == 0 ? " \xe2\x9c\xa8" : "");
			r.hasImage = true;
			r.imageKind = ImageCache::DEFAULT_AVATAR;
			r.imageSf = r.id;
			r.colorSeed = r.id;
			r.status = i == changed ? status : 1 + i % 3;
			r.textColor = i % 4 == 0 ? 0xe91e63 : 0;
			rows.push_back(r);
		}
		return rows;
	}

	struct Result
	{
		const char* name;
		int ops;
		double seconds;
	};

	// The message history on disk: a channel's newest messages fetched and
	// saved; read back by a new session; then the newest fetched over them,
	// one message deleted and one edited on the server meanwhile.  "" when
	// all is as it should be.
	std::string HistoryCheck()
	{
		using nlohmann::json;
		const Snowflake ch = 777;
		MessageCache* mc = GetMessageCache();
		auto message = [](Snowflake id, const std::string& text) {
			json m;
			m["id"] = std::to_string(id);
			m["channel_id"] = "777";
			m["type"] = 0;
			m["content"] = text;
			m["timestamp"] = "2026-10-07T12:00:00.000000+00:00";
			m["author"]["id"] = "4242";
			m["author"]["username"] = "bench";
			return m;
		};
		// newest first, as Discord sends them
		auto page = [&](Snowflake from, Snowflake to, Snowflake deleted, Snowflake edited) {
			json j = json::array();
			for (Snowflake id = to; id >= from; id--)
				if (id != deleted)
					j.push_back(message(id, id == edited ? "edited" : "message " + std::to_string(id)));
			return j;
		};
		auto loaded = [&](std::vector<Snowflake>& ids, std::vector<Snowflake>& gaps) {
			std::list<MessagePtr> l;
			mc->GetLoadedMessages(ch, 0, l);
			ids.clear();
			gaps.clear();
			for (auto& m : l)
				(m->IsLoadGap() ? gaps : ids).push_back(m->m_snowflake);
		};
		std::vector<Snowflake> ids, gaps;

		json first = page(1010, 1059, 0, 0);
		mc->ProcessRequest(ch, ScrollDir::BEFORE, 0, first, "#bench");
		mc->SaveDirty();

		mc->ClearAllChannels();
		mc->LoadCachedChannel(ch, 0);
		loaded(ids, gaps);
		if (ids.size() != 50 || ids.front() != 1010 || ids.back() != 1059)
			return "the saved messages did not come back (" + std::to_string(ids.size()) + ")";
		if (gaps.size() != 2 || gaps.back() != 1060)
			return "the gaps around the saved messages are wrong";

		json newest = page(1030, 1080, 1040, 1050);
		mc->ProcessRequest(ch, ScrollDir::BEFORE, 1060, newest, "#bench");
		loaded(ids, gaps);
		if (ids.size() != 70 || ids.front() != 1010 || ids.back() != 1080)
			return "after the newest were fetched: " + std::to_string(ids.size()) + " messages";
		for (Snowflake id : ids)
			if (id == 1040)
				return "a message deleted meanwhile is still shown";
		MessagePtr e = mc->GetLoadedMessage(ch, 1050);
		if (!e || e->m_message != "edited")
			return "a message edited meanwhile is not";
		if (gaps.size() != 1 || gaps.front() != 1009)
			return "there is a gap between the saved and the fetched messages";

		remove((GetCachePath() + "/messages/777.json").c_str());
		return "";
	}

	// Emoji written as shortcodes in the message box: text with emoji the
	// box cannot show goes in and comes out unchanged, and typed shortcodes
	// become emoji.  "" when all is well.
	std::string ShortcodeCheck()
	{
		const char* samples[] = {
			"plain text, caf\xc3\xa9 na\xc3\xafve, at 10:30:45 and a:b:c",
			"\xf0\x9f\x98\x82 and \xe2\x9d\xa4\xef\xb8\x8f and \xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd",
			"flag \xf0\x9f\x87\xa8\xf0\x9f\x87\xad family \xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7 \xe2\x80\x94 \xe2\x9c\x93",
			"a server emoji <:blobwave:123456789012345678> stays as it is",
		};
		for (const char* s : samples) {
			std::string box = Shortcodes::ToEditor(s);
			if (Utf8ToLatin1(box) != box && Latin1ToUtf8(Utf8ToLatin1(box)) != box)
				return std::string("the box would lose characters of: ") + s;
			std::string back = Shortcodes::FromEditor(Latin1ToUtf8(Utf8ToLatin1(box)), 0);
			if (back != s)
				return std::string("did not come back unchanged: ") + s + " -> " + box + " -> " + back;
		}
		std::string typed = Shortcodes::FromEditor(":joy: :+1: :heart: :nosuchname: :U+1F600:", 0);
		if (typed != "\xf0\x9f\x98\x82 \xf0\x9f\x91\x8d \xe2\x9d\xa4\xef\xb8\x8f :nosuchname: \xf0\x9f\x98\x80")
			return "typed shortcodes came out as: " + typed;
		return "";
	}

	void Sync()
	{
		XSync(XtDisplay(GetMainWindow()->GetShell()), False);
	}

	void RunCB(XtPointer, XtIntervalId*)
	{
		MainWindow* mw = GetMainWindow();
		MessageView* mv = mw->GetMessageView();
		std::vector<Result> results;
		double t;

		Sync();
		Perf::Reset();

		// switching to the channel: everything laid out and drawn
		t = Perf::Now();
		for (int i = 0; i < 10; i++) {
			mv->SetChannel(0, BENCH_CHANNEL);
			Sync();
		}
		results.push_back({ "switch to the channel", 10, Perf::Now() - t });

		// a message arrives
		t = Perf::Now();
		for (int i = 0; i < 100; i++) {
			GetMessageCache()->AddMessage(BENCH_CHANNEL, MakeMessage(MESSAGES + i, g_now + i));
			mv->Refresh();
			Sync();
		}
		results.push_back({ "a message arrives", 100, Perf::Now() - t });

		// scrolling up a page's worth in wheel steps, then back down
		t = Perf::Now();
		int y0 = mv->GetScroll();
		for (int i = 1; i <= 100; i++) {
			mv->ScrollTo(y0 - 40 * i);
			Sync();
		}
		for (int i = 99; i >= 0; i--) {
			mv->ScrollTo(y0 - 40 * i);
			Sync();
		}
		results.push_back({ "scroll one wheel step", 200, Perf::Now() - t });

		// presence: a member on screen changes status, then one far down
		IconList* members = mw->GetMemberList();
		t = Perf::Now();
		for (int i = 0; i < 100; i++) {
			members->SetRows(MemberRows(i % 10, i % 4), 0);
			Sync();
		}
		results.push_back({ "member on screen changes", 100, Perf::Now() - t });

		t = Perf::Now();
		for (int i = 0; i < 100; i++) {
			members->SetRows(MemberRows(280, i % 4), 0);
			Sync();
		}
		results.push_back({ "member off screen changes", 100, Perf::Now() - t });

		// images arrived: the lists drawn again
		t = Perf::Now();
		for (int i = 0; i < 50; i++) {
			mw->GetGuildList()->Repaint();
			mw->GetChannelList()->Repaint();
			members->Repaint();
			Sync();
		}
		results.push_back({ "repaint the three lists", 50, Perf::Now() - t });

		// edits and deletions (and a message from the past) here and there;
		// then the items, as updated one change at a time, must match ones
		// made afresh
		{
			std::list<MessagePtr> msgs;
			GetMessageCache()->GetLoadedMessages(BENCH_CHANNEL, 0, msgs);
			std::vector<Snowflake> ids;
			for (auto& m : msgs)
				ids.push_back(m->m_snowflake);
			t = Perf::Now();
			for (int i = 0; i < 60; i++) {
				Snowflake id = ids[(i * 97) % ids.size()];
				if (i % 3 == 0) {
					GetMessageCache()->DeleteMessage(BENCH_CHANNEL, id);
				}
				else if (i % 3 == 1) {
					for (auto& m : msgs) {
						if (m->m_snowflake == id) {
							Message e = *m;
							e.m_message = std::string(g_texts[(i + 3) % (sizeof g_texts / sizeof g_texts[0])]) + " (edited)";
							GetMessageCache()->EditMessage(BENCH_CHANNEL, e);
							break;
						}
					}
				}
				else {
					Message old = MakeMessage(i, g_now - MESSAGES * 150 + i * 40);
					old.m_snowflake = id - 1; // between two others
					GetMessageCache()->AddMessage(BENCH_CHANNEL, old);
				}
				mv->Refresh();
				Sync();
			}
			results.push_back({ "edit/delete/insert earlier", 60, Perf::Now() - t });
			std::string updated = mv->LayoutSignature();
			mv->SetChannel(0, BENCH_CHANNEL);
			Sync();
			printf("dm bench: items after the changes match a fresh layout: %s\n",
				updated == mv->LayoutSignature() ? "yes" : "NO");
		}

		{
			std::string err = ShortcodeCheck();
			printf("dm bench: emoji as shortcodes in the message box, and back: %s\n",
				err.empty() ? "yes" : ("NO: " + err).c_str());
		}
		{
			std::string err = HistoryCheck();
			printf("dm bench: message history saved, read back and brought up to date: %s\n",
				err.empty() ? "yes" : ("NO: " + err).c_str());
		}

		// text alone: measuring and eliding names
		t = Perf::Now();
		int sum = 0;
		for (int i = 0; i < 2000; i++) {
			std::string s = std::string(g_authors[i % 12]) + " the " + std::to_string(i) + "th, of somewhere far away";
			sum += Fonts::Measure(s, FS_REGULAR, 13);
			sum += (int) Fonts::Elide(s, FS_BOLD, 13, 90).size();
		}
		results.push_back({ "measure + elide a name", 2000, Perf::Now() - t });

		printf("dm bench: %-28s %6s %10s %10s\n", "", "ops", "total ms", "ms/op");
		double all = 0;
		for (auto& r : results) {
			printf("dm bench: %-28s %6d %10.1f %10.3f\n", r.name, r.ops, r.seconds * 1e3, r.seconds * 1e3 / r.ops);
			all += r.seconds;
		}
		printf("dm bench: %-28s %6s %10.1f   (check %d)\n", "total", "", all * 1e3, sum);
		fflush(stdout);
		if (Perf::Enabled())
			Perf::Report(stderr, "during the benchmark");
		if (g_done)
			g_done();
	}
}

void Bench::Start(XtAppContext app, std::function<void()> done)
{
	g_done = done;
	ImageCache::SetOffline(true);
	g_now = time(NULL);
	for (int k = 0; k < MESSAGES; k++)
		GetMessageCache()->AddMessage(BENCH_CHANNEL, MakeMessage(k, g_now - (MESSAGES - k) * 150));

	MainWindow* mw = GetMainWindow();
	mw->ShowDemoLists();
	mw->GetMemberList()->SetRows(MemberRows(-1, 0), 0);
	mw->GetMessageView()->SetChannel(0, BENCH_CHANNEL);
	mw->SetStatus("Benchmark running; it quits by itself.");

	// once the window is up
	XtAppAddTimeOut(app, 3000, RunCB, NULL);
}
