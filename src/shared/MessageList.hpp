#pragma once

#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "models/Message.hpp"
#include "models/RectAndPoint.hpp"
#include "text/FormattedText.hpp"
#include "PictureInfo.hpp"
#include "TextStyle.hpp"

// A channel's messages as a list to draw: kept in step with the message
// cache, grouped the way Discord groups them (with day separators and
// one-line system messages), and laid out -- the author line, the text,
// the reply above it, attachments, embeds and reaction pills -- in pixels,
// with the front end's font measurements.  The front end scrolls and
// draws it, and asks it what a click hit.
class MessageList
{
public:
	// Geometry, in pixels
	static const int MARGIN = 16;        // left and right
	static const int AVATAR = 36;
	static const int TEXT_X = MARGIN + AVATAR + 12;
	static const int GROUP_GAP = 14;     // above a message with a header
	static const int LINE_GAP = 2;       // above a grouped message
	static const int DATE_SEP = 30;
	static const int GROUP_SECONDS = 7 * 60;
	static const int PILL_PAD = 7;       // a reaction pill's padding
	static const int EMBED_WIDTH = 520;  // embeds are at most this wide
	static const int THUMB = 80;         // an embed's thumbnail box

	struct Link
	{
		Rect rect; // item coordinates
		std::string url;
	};

	// Per-item layout of everything but the text.
	struct ItemExtra
	{
		std::string dateSep;   // non-empty: a day separator above the item
		std::vector<Link> links;
		std::vector<std::unique_ptr<FormattedText>> embedTexts;
		std::vector<int> embedTops;
		std::vector<int> embedHeights;
		// images: where they go (item coordinates), what to fetch, and what a
		// click shows in the image viewer (no url there: a click opens the link)
		struct Pic { Rect rect; std::string url; PictureInfo view; };
		std::vector<Pic> attachPics;       // one per attachment; empty url: a file line
		std::vector<Pic> embedThumbs;      // one per embed; empty url: none
		std::vector<Pic> embedImages;      // one per embed; empty url: none
		std::vector<Rect> reactionRects;   // one pill per reaction
		int attachTop = 0;
		int replyTop = 0;
		int headerTop = 0;
	};

	struct Item
	{
		MessagePtr msg;
		bool grouped = false;   // follows a message by the same author
		bool systemLine = false;
		std::string systemText;
		FormattedText text;
		std::vector<InteractableItem> interactables;
		std::unique_ptr<FormattedText> reply;
		int y = 0;              // top, in content coordinates
		int height = 0;
		int textTop = 0;        // offsets inside the item
		int day = -1;           // DayNumber of the message
		// what the layout was made for: it is kept while these hold
		int laidOutWidth = -1;
		int laidOutPx = 0;
		bool laidOutGrouped = false;
		bool laidOutDateSep = false;
		std::shared_ptr<ItemExtra> extra;
	};

	explicit MessageList(TextMetrics& metrics) : m_metrics(metrics) {}

	// Shows another channel: forgets the items and the history asked for.
	void SetChannel(Snowflake guild, Snowflake channel);
	Snowflake GetGuild() const { return m_guild; }
	Snowflake GetChannel() const { return m_channel; }

	// Brings the items up to date with the cache; true when the only change
	// is messages added after the last one.
	bool Rebuild();

	// Lays out what changed for this width and text size (ctx is the
	// front end's, for the formatted text) and places every item.
	void Layout(DrawingContext* ctx, int px, int width);
	// The next Layout lays everything out again (the text size or the
	// fonts changed).
	void ForgetLayout();

	std::list<Item>& Items() { return m_items; }
	const std::list<Item>& Items() const { return m_items; }
	int ContentHeight() const { return m_contentHeight; }

	// Each item's message, place and height, to compare layouts (--bench).
	std::string LayoutSignature() const;

	int ReactionHeight(int px);
	int ReactionWidth(const Reaction& r, int px);

	// The item at content y, or null.
	Item* ItemAt(int contentY);

	// What a click at (x, content y) is on, in the order a click takes them:
	// a link in the text, a reaction, a picture, then any other link (a
	// file, an embed's title).
	struct Hit
	{
		enum Kind { NONE, LINK, REACTION, PICTURE };
		Kind kind = NONE;
		std::string url;          // LINK
		const Message* message = nullptr; // REACTION
		size_t reaction = 0;      // REACTION
		PictureInfo picture;      // PICTURE
	};
	Hit HitTest(int x, int contentY);

	// Asks Discord for the history behind the load gaps between content y
	// top and bottom (each gap once).
	void RequestGaps(int top, int bottom);

	// Whether the newest messages are loaded and at the bottom of a view
	// scrolled to the end (atBottom): the channel may be marked read.
	bool NewestShown(bool atBottom) const;
	// Tells Discord the channel is read, when it has unread messages, and
	// drops its counts at once.  True when it asked.
	bool AcknowledgeIfUnread();

	// A message a menu can act on (not a gap, a header or one being sent).
	static bool IsActionable(const Message& m);
	// The user's own messages can be edited; deleted too, or anyone's where
	// the user may manage messages.
	bool CanEdit(const Message& m) const;
	bool CanDelete(const Message& m) const;
	// The question before deleting: whose, the start of the text, and that
	// it cannot be undone.
	static std::string DeleteQuestion(const Message& m);

	static std::string FormatSize(int bytes);
	// A media proxy URL asking for the w x h preview, when that differs from
	// the original size.
	static std::string PreviewURL(const std::string& base, int w, int h, int origW, int origH);
	// w x h scaled to fit in maxW x maxH (never enlarged).
	static void FitBox(int& w, int& h, int maxW, int maxH);

private:
	void LayoutItem(Item& item, DrawingContext* ctx, int px, int width);

	TextMetrics& m_metrics;
	Snowflake m_guild = 0, m_channel = 0;
	std::list<Item> m_items;
	std::set<Snowflake> m_requestedGaps;
	int m_contentHeight = 0;
	Snowflake m_ackSent = 0;     // the last message acknowledged
};
