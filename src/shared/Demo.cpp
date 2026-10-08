#include "Demo.hpp"

#include <ctime>
#include <memory>

#include "state/MessageCache.hpp"
#include "models/Message.hpp"

// --demo: sample messages and lists, without logging in (to see how
// messages are drawn, and for screenshots).

void Demo::LoadMessages()
{
	const Snowflake chan = CHANNEL;
	time_t now = time(NULL);
	struct Sample { Snowflake author; const char* name; int minutesAgo; const char* text; MessageType::eType type; };
	const Sample samples[] = {
		{ 1001, "Ada", 26 * 60, "Good morning! Has anyone got the **Indigo2** booting from the new disk yet?", MessageType::DEFAULT },
		{ 1002, "Grace", 26 * 60 - 3, "Not yet, the PROM says `Unable to load bootp()` and stops.", MessageType::DEFAULT },
		{ 1002, "Grace", 26 * 60 - 2, "I'll try `setenv netaddr` again after lunch.", MessageType::DEFAULT },
		{ 1003, "Linus", 90, "", MessageType::USER_JOIN },
		{ 1001, "Ada", 45, "Here is what fixed it for me:\n```\nsetenv netaddr 192.168.1.20\nboot -f bootp()/unix\n```\nThen it went straight to the miniroot.", MessageType::DEFAULT },
		{ 1004, "Bjarne", 30, "> it went straight to the miniroot\nNice. *Italic*, __underlined__, ~~struck~~ and a link: https://www.sgi.com/ and caf\xc3\xa9 na\xc3\xafve \xe2\x80\x94 \xe2\x9c\x93 \xe2\x98\x85 \xf0\x9f\x98\x80", MessageType::DEFAULT },
		{ 1002, "Grace", 12, "# Release notes\n- MIPS IV build\n- FreeType text\n- Motif UI\n-# small print: tested on an emulated R10000", MessageType::DEFAULT },
		{ 1003, "Linus", 6, "Emoji: \xf0\x9f\x98\x80 \xf0\x9f\x8e\x89 \xe2\x9c\xa8 \xe2\x9d\xa4\xef\xb8\x8f \xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd \xf0\x9f\x87\xa8\xf0\x9f\x87\xad \xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7 1\xef\xb8\x8f\xe2\x83\xa3 \xf0\x9f\x96\xa5\xef\xb8\x8f and text \xe2\x98\x85 \xe2\x9c\x93 stays text", MessageType::DEFAULT },
		{ 1005, "Dennis", 2, "A long line to see the wrapping: Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.", MessageType::DEFAULT },
	};
	Snowflake id = 1000000;
	// enough older messages to need scrolling
	for (int k = 0; k < 30; k++) {
		Message m;
		m.m_snowflake = ++id;
		m.m_author_snowflake = (Snowflake) (1001 + k % 3) << 22;
		m.m_author = k % 3 == 0 ? "Ada" : k % 3 == 1 ? "Grace" : "Dennis";
		m.m_message = "Older message number " + std::to_string(k + 1) + ", to scroll back to.";
		m.SetTime(now - (3 * 24 * 60 - k * 20) * 60);
		GetMessageCache()->AddMessage(chan, m);
	}
	for (auto& sm : samples)
	{
		Message m;
		m.m_snowflake = ++id;
		m.m_author_snowflake = sm.author << 22;
		m.m_author = sm.name;
		m.m_message = sm.text;
		m.m_type = sm.type;
		m.SetTime(now - sm.minutesAgo * 60);
		if (sm.author == 1001 && sm.minutesAgo == 45) {
			Attachment a;
			a.m_fileName = "bootp-setup.txt";
			a.m_size = 1834;
			a.m_actualUrl = "https://example.com/bootp-setup.txt";
			m.m_attachments.push_back(a);
		}
		if (sm.author == 1002 && sm.minutesAgo == 12) {
			Attachment a;
			// WebP: pictures from anywhere but Discord's servers must be
			a.m_fileName = "rose.webp";
			a.m_size = 81836;
			a.m_width = 400;
			a.m_height = 301;
			a.m_contentType = ContentType::WEBP;
			a.m_proxyUrl = a.m_actualUrl = "https://www.gstatic.com/webp/gallery3/1_webp_ll.webp";
			a.UpdatePreviewSize();
			m.m_attachments.push_back(a);
		}
		if (sm.author == 1004) {
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
		if (sm.author == 1005 || sm.author == 1003) {
			const char* emoji[] = { "\xf0\x9f\x98\x82", "\xf0\x9f\x91\x8d", "\xe2\x9d\xa4\xef\xb8\x8f" };
			for (int k = 0; k < (sm.author == 1005 ? 3 : 1); k++) {
				Reaction r;
				r.m_emojiName = emoji[k];
				r.m_count = k + 1;
				r.m_bMe = k == 1;
				m.m_reactions.push_back(r);
			}
		}
		GetMessageCache()->AddMessage(chan, m);
	}
}

std::vector<ListRow> Demo::GuildRows()
{
	auto item = [](Snowflake id, const char* text) { ListRow r; r.id = id; r.text = text; return r; };
	auto header = [](const char* text) { ListRow r; r.type = ListRow::HEADER; r.text = text; return r; };
	std::vector<ListRow> g;
	ListRow dm = item(1, "Direct Messages"); dm.glyph = "@"; g.push_back(dm);
	ListRow sp; sp.type = ListRow::SPACE; g.push_back(sp);
	const char* names[] = { "Silicon Graphics User Group", "Vintage Computer CH", "Demoscene", "IRIX Network \xe2\x9c\xa8", "Octane Owners" };
	for (int i = 0; i < 5; i++) {
		ListRow r = item(100 + i, names[i]);
		r.initials = true;
		r.colorSeed = (Snowflake) (i * 3 + 1) << 22;
		r.unread = i == 1;
		r.mentions = i == 2 ? 3 : 0;
		if (i == 0) {
			r.hasImage = true;
			r.imageKind = ImageCache::DEFAULT_AVATAR;
			r.imageSf = (Snowflake) 2 << 22;
		}
		g.push_back(r);
	}
	g.push_back(header("Folder"));
	ListRow f = item(200, "Tezro Fans"); f.initials = true; f.indent = 8; g.push_back(f);
	return g;
}

std::vector<ListRow> Demo::ChannelRows()
{
	auto item = [](Snowflake id, const char* text) { ListRow r; r.id = id; r.text = text; return r; };
	auto header = [](const char* text) { ListRow r; r.type = ListRow::HEADER; r.text = text; return r; };
	std::vector<ListRow> c;
	c.push_back(header("INFORMATION"));
	ListRow c1 = item(300, "rules"); c1.glyph = "#"; c.push_back(c1);
	ListRow c2 = item(301, "announcements \xf0\x9f\x93\xa2"); c2.glyph = "#"; c2.unread = true; c.push_back(c2);
	c.push_back(header("TEXT CHANNELS"));
	ListRow c3 = item(302, "general"); c3.glyph = "#"; c.push_back(c3);
	ListRow c4 = item(303, "marketplace"); c4.glyph = "#"; c4.mentions = 2; c4.unread = true; c.push_back(c4);
	ListRow c5 = item(304, "Lounge"); c5.glyph = "\xe2\x99\xaa"; c5.selectable = false; c5.dim = true; c.push_back(c5);
	return c;
}

std::vector<ListRow> Demo::MemberRows()
{
	auto item = [](Snowflake id, const char* text) { ListRow r; r.id = id; r.text = text; return r; };
	auto header = [](const char* text) { ListRow r; r.type = ListRow::HEADER; r.text = text; return r; };
	std::vector<ListRow> m;
	m.push_back(header("Online \xe2\x80\x94 3"));
	const char* who[] = { "Ada", "Grace", "Dennis" };
	uint32_t colors[] = { 0xe91e63, 0x3498db, 0 };
	for (int i = 0; i < 3; i++) {
		ListRow r = item(400 + i, who[i]);
		r.hasImage = true;
		r.imageKind = ImageCache::DEFAULT_AVATAR;
		r.imageSf = (Snowflake) (i + 3) << 22;
		r.status = i + 1;
		r.textColor = colors[i];
		m.push_back(r);
	}
	m.push_back(header("Offline \xe2\x80\x94 1"));
	ListRow off = item(410, "Linus");
	off.hasImage = true; off.imageKind = ImageCache::DEFAULT_AVATAR; off.imageSf = (Snowflake) 7 << 22;
	off.status = 0; off.dim = true;
	m.push_back(off);
	return m;
}
