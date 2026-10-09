#include "Demo.hpp"

#include <ctime>
#include <map>
#include <memory>
#include <set>

#include "state/MessageCache.hpp"
#include "models/ActiveStatus.hpp"

// --demo: servers, channels and messages without logging in (to try the
// client out, for screenshots, and for performance tests: #stress-test has
// two thousand messages of every kind).

namespace
{
	// The people in the demo; their snowflakes are id << 22 (which also
	// picks their default avatars).
	struct Person { Snowflake id; const char* name; int status; uint32_t color; };
	const Person PEOPLE[] = {
		{ 1001, "Ada", STATUS_ONLINE, 0xe91e63 },
		{ 1002, "Grace", STATUS_IDLE, 0x3498db },
		{ 1003, "Linus", STATUS_OFFLINE, 0 },
		{ 1004, "Bjarne", STATUS_ONLINE, 0 },
		{ 1005, "Dennis", STATUS_DND, 0 },
		{ 1006, "Margaret", STATUS_ONLINE, 0x2ecc71 },
		{ 1007, "Ken", STATUS_OFFLINE, 0 },
		{ 1008, "Barbara", STATUS_ONLINE, 0 },
		{ 1009, "Radia", STATUS_IDLE, 0x9b59b6 },
		{ 1010, "Seymour", STATUS_OFFLINE, 0xf1c40f },
	};
	enum { ADA, GRACE, LINUS, BJARNE, DENNIS, MARGARET, KEN, BARBARA, RADIA, SEYMOUR };

	struct Server { Snowflake id; const char* name; std::vector<int> members; };
	const Server SERVERS[] = {
		{ 100, "Silicon Graphics User Group", { ADA, GRACE, DENNIS, LINUS } },
		{ 101, "Vintage Computer CH", { ADA, MARGARET, BARBARA, KEN, LINUS } },
		{ 102, "Demoscene", { RADIA, BJARNE, GRACE, SEYMOUR, KEN } },
		{ 103, "IRIX Network \xe2\x9c\xa8", { DENNIS, ADA, BJARNE, RADIA, MARGARET, KEN } },
		{ 104, "Octane Owners", { BARBARA, GRACE, SEYMOUR } },
		{ 200, "Tezro Fans", { ADA, LINUS, SEYMOUR } },
	};

	enum Kind { K_TEXT, K_VOICE, K_DM };
	enum Style { S_CHAT, S_CODE, S_MARKET, S_STRESS, S_HAND };
	struct Chan
	{
		Snowflake id, guild;
		const char* category; // a header above it, or null
		const char* name;
		const char* topic;
		Kind kind;
		Style style;
		int count;            // generated messages
		bool unread;
		int mentions;
		std::vector<int> people; // a conversation's (DM)
	};
	std::vector<Chan> g_chans = {
		{ 300, 100, "INFORMATION", "rules", "Read before posting", K_TEXT, S_HAND, 0, false, 0, {} },
		{ 301, 100, nullptr, "announcements \xf0\x9f\x93\xa2", "News from the group", K_TEXT, S_HAND, 0, true, 0, {} },
		{ 302, 100, "TEXT CHANNELS", "general", "Talk about Indigo2s, Octanes and the rest of the SGI family", K_TEXT, S_HAND, 0, false, 0, {} },
		{ 303, 100, nullptr, "marketplace", "Buy, sell, swap. Prices in the post, please", K_TEXT, S_MARKET, 40, true, 2, {} },
		{ 305, 100, nullptr, "help-desk", "Stuck? Post your hinv", K_TEXT, S_CHAT, 60, false, 0, {} },
		{ 304, 100, nullptr, "Lounge", "", K_VOICE, S_CHAT, 0, false, 0, {} },
		{ 306, 100, "TESTING", "stress-test", "2,000 messages of every kind, for scrolling and drawing speed", K_TEXT, S_STRESS, 2000, false, 0, {} },
		{ 310, 101, "WELCOME", "welcome", "Gr\xc3\xbc" "ezi! Introduce yourself", K_TEXT, S_CHAT, 20, false, 0, {} },
		{ 311, 101, "PROJECTS", "restoration", "Capacitors, retrobright and patience", K_TEXT, S_CHAT, 50, true, 0, {} },
		{ 312, 101, nullptr, "swiss-meetups", "Next one in Bern", K_TEXT, S_CHAT, 25, false, 0, {} },
		{ 313, 101, nullptr, "Stammtisch", "", K_VOICE, S_CHAT, 0, false, 0, {} },
		{ 320, 102, "PRODUCTIONS", "releases", "Only finished things", K_TEXT, S_CHAT, 30, true, 3, {} },
		{ 321, 102, nullptr, "code", "Effects, tricks and MIPS assembly", K_TEXT, S_CODE, 45, false, 0, {} },
		{ 322, 102, nullptr, "party-planning", "", K_TEXT, S_CHAT, 20, false, 0, {} },
		{ 330, 103, "SUPPORT", "help", "IRIX 6.5 questions, any machine", K_TEXT, S_CODE, 70, false, 0, {} },
		{ 331, 103, nullptr, "packages", "New builds and their bugs", K_TEXT, S_CHAT, 35, true, 0, {} },
		{ 332, 103, "COMMUNITY", "off-topic", "", K_TEXT, S_CHAT, 50, false, 0, {} },
		{ 340, 104, "TEXT CHANNELS", "general", "Dual R12000 or bust", K_TEXT, S_CHAT, 30, false, 0, {} },
		{ 341, 104, nullptr, "parts-wanted", "", K_TEXT, S_MARKET, 20, false, 0, {} },
		{ 350, 200, "TEXT CHANNELS", "general", "The last MIPS workstation", K_TEXT, S_CHAT, 15, false, 0, {} },
		{ 500, 1, "DIRECT MESSAGES", "Ada", "", K_DM, S_CHAT, 14, true, 1, { ADA } },
		{ 501, 1, nullptr, "Grace", "", K_DM, S_CHAT, 8, false, 0, { GRACE } },
		{ 502, 1, nullptr, "Linus, Dennis", "", K_DM, S_CHAT, 20, false, 0, { LINUS, DENNIS } },
	};

	Snowflake g_guild = Demo::SELECTED_GUILD;
	Snowflake g_channel = Demo::SELECTED_CHANNEL;
	std::map<Snowflake, Snowflake> g_lastIn = { { 100, 302 } }; // server -> channel open there last
	std::set<Snowflake> g_loaded;
	std::map<Snowflake, Snowflake> g_lastRead; // an unread channel's: messages after it are new

	Chan* Find(Snowflake id)
	{
		for (auto& c : g_chans)
			if (c.id == id)
				return &c;
		return nullptr;
	}

	const Server* FindServer(Snowflake id)
	{
		for (auto& s : SERVERS)
			if (s.id == id)
				return &s;
		return nullptr;
	}

	Message Make(Snowflake id, int person, time_t when, const std::string& text)
	{
		Message m;
		m.m_snowflake = id;
		if (person < 0) {
			m.m_author_snowflake = Demo::ME;
			m.m_author = "you";
		}
		else {
			m.m_author_snowflake = PEOPLE[person].id << 22;
			m.m_author = PEOPLE[person].name;
		}
		m.m_message = text;
		m.SetTime(when);
		return m;
	}

	void React(Message& m, const char* emoji, int count, bool me)
	{
		Reaction r;
		r.m_emojiName = emoji;
		r.m_count = count;
		r.m_bMe = me;
		m.m_reactions.push_back(r);
	}

	void Picture(Message& m, bool rose)
	{
		Attachment a;
		// WebP: pictures from anywhere but Discord's servers must be
		if (rose) {
			a.m_fileName = "rose.webp";
			a.m_size = 81836;
			a.m_width = 400;
			a.m_height = 301;
			a.m_proxyUrl = a.m_actualUrl = "https://www.gstatic.com/webp/gallery3/1_webp_ll.webp";
		}
		else {
			a.m_fileName = "falls.webp";
			a.m_size = 30320;
			a.m_width = 550;
			a.m_height = 368;
			a.m_proxyUrl = a.m_actualUrl = "https://www.gstatic.com/webp/gallery/1.webp";
		}
		a.m_contentType = ContentType::WEBP;
		a.UpdatePreviewSize();
		m.m_attachments.push_back(a);
	}

	// #general: the hand-written messages every screenshot shows.
	void LoadGeneral(Snowflake chan)
	{
		time_t now = time(NULL);
		struct Sample { int person; int minutesAgo; const char* text; MessageType::eType type; };
		const Sample samples[] = {
			{ ADA, 26 * 60, "Good morning! Has anyone got the **Indigo2** booting from the new disk yet?", MessageType::DEFAULT },
			{ GRACE, 26 * 60 - 3, "Not yet, the PROM says `Unable to load bootp()` and stops.", MessageType::DEFAULT },
			{ GRACE, 26 * 60 - 2, "I'll try `setenv netaddr` again after lunch.", MessageType::DEFAULT },
			{ LINUS, 90, "", MessageType::USER_JOIN },
			{ ADA, 45, "Here is what fixed it for me:\n```\nsetenv netaddr 192.168.1.20\nboot -f bootp()/unix\n```\nThen it went straight to the miniroot.", MessageType::DEFAULT },
			{ BJARNE, 30, "> it went straight to the miniroot\nNice. *Italic*, __underlined__, ~~struck~~ and a link: https://www.sgi.com/ and caf\xc3\xa9 na\xc3\xafve \xe2\x80\x94 \xe2\x9c\x93 \xe2\x98\x85 \xf0\x9f\x98\x80", MessageType::DEFAULT },
			{ GRACE, 12, "# Release notes\n- MIPS IV build\n- FreeType text\n- Motif UI\n-# small print: tested on an emulated R10000", MessageType::DEFAULT },
			{ LINUS, 6, "Emoji: \xf0\x9f\x98\x80 \xf0\x9f\x8e\x89 \xe2\x9c\xa8 \xe2\x9d\xa4\xef\xb8\x8f \xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd \xf0\x9f\x87\xa8\xf0\x9f\x87\xad \xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7 1\xef\xb8\x8f\xe2\x83\xa3 \xf0\x9f\x96\xa5\xef\xb8\x8f and text \xe2\x98\x85 \xe2\x9c\x93 stays text", MessageType::DEFAULT },
			{ DENNIS, 2, "A long line to see the wrapping: Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.", MessageType::DEFAULT },
		};
		Snowflake id = 1000000;
		// enough older messages to need scrolling
		for (int k = 0; k < 30; k++) {
			int who = k % 3 == 0 ? ADA : k % 3 == 1 ? GRACE : DENNIS;
			GetMessageCache()->AddMessage(chan, Make(++id, who, now - (3 * 24 * 60 - k * 20) * 60,
				"Older message number " + std::to_string(k + 1) + ", to scroll back to."));
		}
		for (auto& sm : samples)
		{
			Message m = Make(++id, sm.person, now - sm.minutesAgo * 60, sm.text);
			m.m_type = sm.type;
			if (sm.person == ADA && sm.minutesAgo == 45) {
				Attachment a;
				a.m_fileName = "bootp-setup.txt";
				a.m_size = 1834;
				a.m_actualUrl = "https://example.com/bootp-setup.txt";
				m.m_attachments.push_back(a);
			}
			if (sm.person == GRACE && sm.minutesAgo == 12)
				Picture(m, true);
			if (sm.person == BJARNE) {
				m.m_pReferencedMessage = std::make_shared<ReferenceMessage>();
				m.m_pReferencedMessage->m_author = "Ada";
				m.m_pReferencedMessage->m_message = "Then it went straight to the miniroot.";
				RichEmbed e;
				e.m_color = 0x2d8f9e;
				e.m_providerName = "sgi.com";
				e.m_title = "Silicon Graphics";
				e.m_url = "https://www.sgi.com/";
				e.m_description = "High-performance computing and *visualization* since 1982.";
				e.m_footerText = "Embed footer";
				e.m_bHasImage = true;
				e.m_imageUrl = e.m_imageProxiedUrl = "https://www.gstatic.com/webp/gallery/1.webp";
				e.m_imageWidth = 550;
				e.m_imageHeight = 368;
				m.m_embeds.push_back(e);
			}
			// reactions on a few, one of them the user's
			if (sm.person == DENNIS || sm.person == LINUS) {
				const char* emoji[] = { "\xf0\x9f\x98\x82", "\xf0\x9f\x91\x8d", "\xe2\x9d\xa4\xef\xb8\x8f" };
				for (int k = 0; k < (sm.person == DENNIS ? 3 : 1); k++)
					React(m, emoji[k], k + 1, k == 1);
			}
			GetMessageCache()->AddMessage(chan, m);
		}
	}

	void LoadHand(Chan& c)
	{
		time_t now = time(NULL);
		Snowflake id = c.id << 24;
		if (c.id == 302)
			LoadGeneral(c.id);
		else if (c.id == 300) {
			GetMessageCache()->AddMessage(c.id, Make(++id, GRACE, now - 40 * 86400,
				"# Rules\n1. Be kind; everyone was new once.\n2. Post your `hinv` when asking for help.\n"
				"3. No piracy: SGI software comes from your own media or SGI's own downloads.\n"
				"4. Prices in **marketplace** posts, please.\n-# Questions about these? Ask in #general."));
		}
		else if (c.id == 301) {
			Message m = Make(++id, GRACE, now - 9 * 86400, "The **spring meetup** is on 12 April in Z\xc3\xbcrich. Bring a machine!");
			React(m, "\xf0\x9f\x8e\x89", 14, true);
			GetMessageCache()->AddMessage(c.id, m);
			GetMessageCache()->AddMessage(c.id, Make(++id, ADA, now - 3 * 86400,
				"Photos from the meetup are up: https://example.com/meetup-photos"));
			c.count = 3; // all three, for the NEW line
			m = Make(++id, GRACE, now - 50 * 60, "New channel: **#stress-test**, two thousand messages for anyone measuring how fast their client draws.");
			React(m, "\xf0\x9f\x91\x80", 3, false);
			GetMessageCache()->AddMessage(c.id, m);
		}
	}

	// A small, repeatable random number generator (the same messages every
	// run, so performance tests compare like with like).
	struct Rand
	{
		uint32_t s;
		explicit Rand(uint32_t seed) : s(seed * 2654435761u + 1) {}
		uint32_t Next() { s = s * 1664525u + 1013904223u; return s >> 8; }
		int Below(int n) { return (int) (Next() % (uint32_t) n); }
		template <class T, size_t N> const T& Pick(const T (&a)[N]) { return a[Below((int) N)]; }
	};

	const char* MACHINES[] = { "Indy", "Indigo2", "Octane", "O2", "Fuel", "Tezro", "Onyx", "Crimson", "Personal IRIS", "Octane2" };
	const char* THINGS[] = { "IRIX 6.5.22", "MIPSpro 7.4", "Netscape 4.8", "the Elan board", "a new PSU", "a SCSI2SD",
		"the PROM battery", "an R10000 module", "the Solid IMPACT board", "Maya 6.5", "the 13W3 adapter", "nekoware" };
	const char* PRICES[] = { "CHF 80", "CHF 150", "50 EUR", "a crate of beer", "$120", "free to a good home" };
	const char* SHORT[] = {
		"agreed", "same here", "nice!", "ha, yes", "which PROM version?", "\xf0\x9f\x91\x8d", "that worked, thanks",
		"pics or it didn't happen", "brb, reflowing", "oh no", "\xf0\x9f\x98\x82", "yes, exactly that",
	};
	const char* CODE_SNIPPETS[] = {
		"```\nhinv -c processor\nCPU: MIPS R10000 Processor Chip Revision: 3.4\n```",
		"```c\nint main(void)\n{\n\tprintf(\"hello, IRIX\\n\");\n\treturn 0;\n}\n```",
		"```\nversions -b | grep eoe\n```",
		"```\nsetenv console d\nboot -f dksc(0,1,8)sashARCS\n```",
		"```asm\n\tlui\t$t0, 0xbfc0\n\tjr\t$t0\n\tnop\n```",
		"```\ninst -f /CDROM/dist\nInst> install standard\nInst> go\n```",
	};

	std::string Fill(std::string s, Rand& r)
	{
		size_t p;
		while ((p = s.find("{M}")) != std::string::npos)
			s.replace(p, 3, r.Pick(MACHINES));
		while ((p = s.find("{T}")) != std::string::npos)
			s.replace(p, 3, r.Pick(THINGS));
		while ((p = s.find("{P}")) != std::string::npos)
			s.replace(p, 3, r.Pick(PRICES));
		return s;
	}

	std::string Text(Style style, Rand& r)
	{
		static const char* CHATTY[] = {
			"Has anyone tried {T} on an {M}?",
			"I picked up an {M} at a flea market today. It **boots**!",
			"Does {T} need more than 64 MB? Mine swaps like mad.",
			"The {M} in the corner finally has {T}.",
			"Quick question: is {T} worth it on an {M}, or should I just leave it?",
			"Reminder that the fans in an {M} are *loud* until the PROM is done.",
			"Found the manual for {T}: https://techpubs.sgi.com/ (well, a mirror of it)",
			"> is {T} worth it?\nAbsolutely, it was the best thing I did for the {M}.",
			"My {M} has been up for 212 days now. Not touching it.",
			"Spent the evening on {T} again. ~~Never again.~~ Probably again.",
			"Lorem ipsum, but about the {M}: the case is purple, the keyboard is grey, the monitor weighs as much as a small car, and it still draws windows faster than some things made this decade.",
			"\xe2\x9c\xa8 {T} \xe2\x9c\xa8",
			"Has anyone got a spare {T}? Will pay in __Swiss chocolate__.",
		};
		static const char* MARKETS[] = {
			"**For sale:** {M}, working, with {T}. {P}.",
			"WTB: {T} for an {M}. Paying {P}.",
			"Still available: {M} (no disk). {P}, pickup in Bern.",
			"Sold, thanks everyone!",
			"Is the {M} still for sale?",
		};
		switch (style) {
		case S_MARKET:
			return Fill(r.Below(4) ? r.Pick(MARKETS) : r.Pick(SHORT), r);
		case S_CODE:
			if (r.Below(3) == 0)
				return Fill(std::string("Try this on the {M}:\n") + r.Pick(CODE_SNIPPETS), r);
			break;
		case S_STRESS: {
			int k = r.Below(10);
			if (k == 0)
				return Fill(std::string("From the {M}:\n") + r.Pick(CODE_SNIPPETS), r);
			if (k == 1)
				return Fill(std::string(r.Pick(MARKETS)), r);
			if (k == 2) {
				std::string s;
				for (int i = r.Below(4) + 2; i > 0; i--)
					s += Fill(std::string(r.Pick(CHATTY)), r) + (i > 1 ? "\n" : "");
				return s;
			}
			break;
		}
		default:
			break;
		}
		return Fill(r.Below(3) ? r.Pick(CHATTY) : r.Pick(SHORT), r);
	}

	// A channel's generated messages, oldest first, ending a few minutes ago.
	void Generate(Chan& c)
	{
		Rand r((uint32_t) c.id);
		time_t now = time(NULL);
		std::vector<int> who = c.people;
		if (who.empty())
			if (const Server* s = FindServer(c.guild))
				who = s->members;
		int gapMax = c.style == S_STRESS ? 12 : 90; // minutes
		std::vector<int> gaps;
		long total = 0;
		for (int i = 0; i < c.count; i++) {
			gaps.push_back(1 + r.Below(gapMax));
			total += gaps.back();
		}
		time_t t = now - (total + 3) * 60;
		Snowflake id = c.id << 24;
		const char* emoji[] = { "\xf0\x9f\x91\x8d", "\xf0\x9f\x98\x82", "\xe2\x9d\xa4\xef\xb8\x8f", "\xf0\x9f\x8e\x89", "\xf0\x9f\x91\x80", "\xf0\x9f\x96\xa5\xef\xb8\x8f" };
		Message prev;
		for (int i = 0; i < c.count; i++) {
			t += gaps[i] * 60;
			// in a conversation the user answers about half the time
			int person = c.kind == K_DM ? (r.Below(2) ? who[r.Below((int) who.size())] : -1)
				: (r.Below(10) == 0 ? -1 : who[r.Below((int) who.size())]);
			Message m = Make(++id, person, t, Text(c.style, r));
			if (r.Below(8) == 0)
				for (int k = r.Below(3); k >= 0; k--)
					React(m, emoji[(k + i) % 6], 1 + r.Below(5), r.Below(3) == 0);
			if (i > 0 && r.Below(16) == 0) {
				m.m_pReferencedMessage = std::make_shared<ReferenceMessage>();
				m.m_pReferencedMessage->m_snowflake = prev.m_snowflake;
				m.m_pReferencedMessage->m_author = prev.m_author;
				m.m_pReferencedMessage->m_author_snowflake = prev.m_author_snowflake;
				m.m_pReferencedMessage->m_message = prev.m_message;
			}
			if ((c.style == S_STRESS && i % 150 == 75) || (c.style == S_MARKET && i % 17 == 8))
				Picture(m, (i / 150) % 2 == 0);
			GetMessageCache()->AddMessage(c.id, m);
			prev = m;
		}
	}

	void Load(Chan& c)
	{
		if (!g_loaded.insert(c.id).second)
			return;
		if (c.style == S_HAND)
			LoadHand(c);
		else
			Generate(c);
		// the whole history is here: no gap to fetch more, the channel's start
		// above its first message (as a fetch that reached it leaves them)
		GetMessageCache()->DeleteMessage(c.id, 0);
		Message start;
		start.m_type = MessageType::CHANNEL_HEADER;
		start.m_snowflake = 1;
		start.m_author = c.name;
		GetMessageCache()->AddMessage(c.id, start);
		// unread: its last messages are new (one per mention, at least one)
		if (c.unread) {
			int n = c.mentions > 0 ? c.mentions : 1;
			if (c.count > n)
				g_lastRead[c.id] = (c.id << 24) + (Snowflake) (c.count - n);
		}
	}

	Snowflake NextId()
	{
		// after every sample message (they number from small values)
		static Snowflake next = ((Snowflake) time(NULL) * 1000 - 1420070400000ULL) << 22;
		return ++next;
	}

	ListRow Item(Snowflake id, const std::string& text)
	{
		ListRow r;
		r.id = id;
		r.text = text;
		return r;
	}

	ListRow Header(const std::string& text)
	{
		ListRow r;
		r.type = ListRow::HEADER;
		r.text = text;
		return r;
	}
}

void Demo::LoadMessages()
{
	Load(*Find(CHANNEL));
}

std::vector<ListRow> Demo::GuildRows()
{
	auto badges = [](Snowflake guild, ListRow& r) {
		for (auto& c : g_chans)
			if (c.guild == guild && c.id != g_channel) {
				r.unread = r.unread || c.unread;
				r.mentions += c.mentions;
			}
	};
	std::vector<ListRow> g;
	ListRow dm = Item(DIRECT_MESSAGES, "Direct Messages");
	dm.glyph = "@";
	badges(DIRECT_MESSAGES, dm);
	dm.unread = false; // home shows mentions only
	g.push_back(dm);
	ListRow sp;
	sp.type = ListRow::SPACE;
	g.push_back(sp);
	for (int i = 0; i < 5; i++) {
		ListRow r = Item(SERVERS[i].id, SERVERS[i].name);
		r.initials = true;
		r.colorSeed = (Snowflake) (i * 3 + 1) << 22;
		if (i == 0) {
			r.hasImage = true;
			r.imageKind = ImageCache::DEFAULT_AVATAR;
			r.imageSf = (Snowflake) 2 << 22;
		}
		badges(r.id, r);
		g.push_back(r);
	}
	g.push_back(Header("Folder"));
	ListRow f = Item(SERVERS[5].id, SERVERS[5].name);
	f.initials = true;
	f.indent = 8;
	badges(f.id, f);
	g.push_back(f);
	return g;
}

std::vector<ListRow> Demo::ChannelRows()
{
	std::vector<ListRow> rows;
	for (auto& c : g_chans) {
		if (c.guild != g_guild)
			continue;
		if (c.category)
			rows.push_back(Header(c.category));
		ListRow r = Item(c.id, c.name);
		if (c.kind == K_DM) {
			if (c.people.size() == 1) {
				r.hasImage = true;
				r.imageKind = ImageCache::DEFAULT_AVATAR;
				r.imageSf = PEOPLE[c.people[0]].id << 22;
				r.status = PEOPLE[c.people[0]].status;
			}
			else {
				r.initials = true;
				r.colorSeed = c.id << 22;
				r.subtext = std::to_string(c.people.size() + 1) + " Members";
			}
		}
		else
			r.glyph = c.kind == K_VOICE ? "\xe2\x99\xaa" : "#";
		if (c.kind == K_VOICE) {
			r.selectable = false;
			r.dim = true;
		}
		r.unread = c.unread;
		r.mentions = c.mentions;
		rows.push_back(r);
	}
	return rows;
}

std::vector<ListRow> Demo::MemberRows()
{
	std::vector<int> who;
	if (g_guild == DIRECT_MESSAGES) {
		if (Chan* c = Find(g_channel))
			who = c->people;
	}
	else if (const Server* s = FindServer(g_guild))
		who = s->members;
	std::vector<ListRow> on, off;
	for (int p : who) {
		ListRow r = Item(PEOPLE[p].id, PEOPLE[p].name);
		r.hasImage = true;
		r.imageKind = ImageCache::DEFAULT_AVATAR;
		r.imageSf = PEOPLE[p].id << 22;
		r.status = PEOPLE[p].status;
		r.textColor = PEOPLE[p].color;
		if (PEOPLE[p].status == STATUS_OFFLINE) {
			r.dim = true;
			off.push_back(r);
		}
		else
			on.push_back(r);
	}
	std::vector<ListRow> m;
	m.push_back(Header("Online \xe2\x80\x94 " + std::to_string(on.size())));
	m.insert(m.end(), on.begin(), on.end());
	if (!off.empty()) {
		m.push_back(Header("Offline \xe2\x80\x94 " + std::to_string(off.size())));
		m.insert(m.end(), off.begin(), off.end());
	}
	return m;
}

Snowflake Demo::Guild()
{
	return g_guild;
}

Snowflake Demo::Channel()
{
	return g_channel;
}

Snowflake Demo::SelectGuild(Snowflake guild)
{
	auto it = g_lastIn.find(guild);
	if (it != g_lastIn.end())
		return it->second;
	for (auto& c : g_chans)
		if (c.guild == guild && c.kind != K_VOICE)
			return c.id;
	return 0;
}

Snowflake Demo::OpenChannel(Snowflake channel)
{
	Chan* c = Find(channel);
	if (!c || c->kind == K_VOICE)
		return 0;
	Load(*c);
	g_guild = c->guild;
	g_channel = channel;
	g_lastIn[c->guild] = channel;
	Snowflake read = 0;
	if (c->unread) {
		auto it = g_lastRead.find(channel);
		read = it == g_lastRead.end() ? 0 : it->second;
		c->unread = false;
		c->mentions = 0;
	}
	return read;
}

Snowflake Demo::FindChannel(const std::string& name)
{
	for (auto& c : g_chans)
		if (c.kind != K_VOICE && name == c.name)
			return c.id;
	return 0;
}

std::string Demo::GuildName()
{
	const Server* s = FindServer(g_guild);
	return s ? s->name : "Direct Messages";
}

std::string Demo::ChannelName(Snowflake channel)
{
	Chan* c = Find(channel);
	return c ? c->name : "";
}

std::string Demo::ChannelTopic(Snowflake channel)
{
	Chan* c = Find(channel);
	return c ? c->topic : "";
}

bool Demo::IsDirect(Snowflake channel)
{
	Chan* c = Find(channel);
	return c && c->kind == K_DM;
}

bool Demo::Send(Snowflake channel, const std::string& text, const Message* replyTo)
{
	if (text.find_first_not_of(" \t\r\n") == std::string::npos)
		return false;
	Message m = Make(NextId(), -1, time(NULL), text);
	if (replyTo) {
		m.m_pReferencedMessage = std::make_shared<ReferenceMessage>();
		m.m_pReferencedMessage->m_snowflake = replyTo->m_snowflake;
		m.m_pReferencedMessage->m_author = replyTo->m_author;
		m.m_pReferencedMessage->m_author_snowflake = replyTo->m_author_snowflake;
		m.m_pReferencedMessage->m_message = replyTo->m_message;
	}
	GetMessageCache()->AddMessage(channel, m);
	return true;
}

void Demo::React(Snowflake channel, const Message& message, const Reaction& emoji, bool add)
{
	Message m = message;
	auto& rs = m.m_reactions;
	size_t i = 0;
	while (i < rs.size() && !rs[i].SameEmoji(emoji))
		i++;
	if (add) {
		if (i == rs.size()) {
			Reaction r = emoji;
			r.m_count = 0;
			r.m_bMe = false;
			rs.push_back(r);
		}
		if (rs[i].m_bMe)
			return;
		rs[i].m_bMe = true;
		rs[i].m_count++;
	}
	else {
		if (i == rs.size() || !rs[i].m_bMe)
			return;
		rs[i].m_bMe = false;
		if (--rs[i].m_count <= 0)
			rs.erase(rs.begin() + (long) i);
	}
	GetMessageCache()->EditMessage(channel, m);
}
