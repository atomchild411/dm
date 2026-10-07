// --bench: a fixed workload on made-up data, timed, for comparing builds.
// No login and no network (images stay placeholders), so two builds run
// exactly the same steps.

#include "Xm.hpp"
#include "Bench.hpp"

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "DiscordInstance.hpp"
#include "state/MessageCache.hpp"

#include "Fonts.hpp"
#include "IconList.hpp"
#include "ImageCache.hpp"
#include "MainWindow.hpp"
#include "MessageView.hpp"
#include "Perf.hpp"

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
		m.m_snowflake = ++g_nextId;
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
