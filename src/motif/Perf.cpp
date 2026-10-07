#include "Perf.hpp"

#include <cstdlib>
#include <sys/time.h>

namespace
{
	struct Stat
	{
		long count = 0;
		double total = 0, max = 0;
	};

	Stat g_stats[Perf::COUNTERS];

	const char* const g_names[Perf::COUNTERS] = {
		"gateway message",
		"messages: refresh",
		"messages: layout",
		"messages: paint",
		"list: set rows",
		"list: paint",
		"present to X",
		"member rows",
		"channel rows",
		"guild rows",
	};
}

bool Perf::Enabled()
{
	static int enabled = -1;
	if (enabled < 0)
		enabled = getenv("DM_PERF") != nullptr;
	return enabled != 0;
}

double Perf::Now()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

void Perf::Add(Counter c, double seconds)
{
	Stat& s = g_stats[c];
	s.count++;
	s.total += seconds;
	if (seconds > s.max)
		s.max = seconds;
}

void Perf::Report(FILE* f, const char* title)
{
	fprintf(f, "dm perf: %s\n", title);
	fprintf(f, "dm perf:   %-20s %8s %10s %9s %9s\n", "", "count", "total ms", "avg ms", "max ms");
	for (int i = 0; i < COUNTERS; i++) {
		const Stat& s = g_stats[i];
		if (!s.count)
			continue;
		fprintf(f, "dm perf:   %-20s %8ld %10.1f %9.2f %9.2f\n", g_names[i], s.count,
			s.total * 1e3, s.total * 1e3 / s.count, s.max * 1e3);
	}
	fflush(f);
}

void Perf::Reset()
{
	for (auto& s : g_stats)
		s = Stat();
}
