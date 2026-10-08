#pragma once

#include <functional>
#include <string>

#include "models/Snowflake.hpp"

// Who is typing where: Discord says when someone starts (it lasts ten
// seconds unless repeated) and their message ends it.  UI thread.
namespace Typing
{
	// Runs when the set of people typing changes (and once a second while
	// anyone is, as their time runs out).
	void SetChangedCallback(std::function<void()> fn);

	void Started(Snowflake user, Snowflake channel);
	void Stopped(Snowflake channel, Snowflake user);

	// "Ada is typing...", "Ada and Grace are typing...", or "" when nobody is.
	std::string Text(Snowflake channel);
}
