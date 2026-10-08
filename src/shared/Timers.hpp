#pragma once

#include <functional>

// One-shot timers on the UI thread, for the shared layer.  Each front end
// supplies them from its own event loop (Xt timeouts, a frame loop, ...).
namespace Timers
{
	struct Backend
	{
		// Calls fn once, ms from now, on the UI thread; returns a handle.
		std::function<void*(int ms, std::function<void()> fn)> add;
		// Cancels a timer that has not run yet.
		std::function<void(void* handle)> remove;
	};
	void SetBackend(const Backend& backend);

	// Runs fn once after ms; the id (never 0) cancels it.
	int After(int ms, std::function<void()> fn);
	// Cancels the timer, if it has not run yet (0 is ignored).
	void Cancel(int id);
}
