#pragma once

#include <cstdio>

// Timing of the UI's costly steps, to compare builds.  With DM_PERF set,
// a table of counts and times goes to stderr every minute and at exit.
namespace Perf
{
	enum Counter
	{
		GATEWAY,        // one gateway message handled
		MV_REFRESH,     // the message view rebuilt from the cache
		MV_LAYOUT,      // its messages laid out
		MV_PAINT,       // drawn
		LIST_SETROWS,   // an icon list given new rows
		LIST_PAINT,     // an icon list drawn
		PRESENT,        // pixels sent to the X server
		MEMBERS,        // the member list's rows made
		CHANNELS,       // the channel list's rows made
		GUILDS,         // the guild list's rows made
		COUNTERS
	};

	bool Enabled();
	double Now(); // seconds
	void Add(Counter c, double seconds);
	void Report(FILE* f, const char* title);
	void Reset();

	struct Scope
	{
		explicit Scope(Counter c) : m_c(c), m_t0(Enabled() ? Now() : -1) {}
		~Scope() { if (m_t0 >= 0) Add(m_c, Now() - m_t0); }
		Counter m_c;
		double m_t0;
	};
}
