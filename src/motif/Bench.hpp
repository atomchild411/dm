#pragma once

#include <functional>

#include "Xm.hpp"

namespace Bench
{
	// Loads the benchmark's messages and lists, runs it once the window is
	// up, prints the times on stdout, then calls done.
	void Start(XtAppContext app, std::function<void()> done);
}
