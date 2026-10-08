#pragma once

#include <functional>
#include <string>

// Logging in on discord.com's own login page, in a browser view inside
// the app (WebKit on macOS): the user signs in there (a captcha, two-factor
// codes and all), and the account's token is taken from the page's own
// requests.  The view keeps nothing: its cookies and storage go with it.
namespace WebLogin
{
	// Whether this platform has it.
	bool Available();

	// Opens the login window.  done(token) once the user is in; cancelled()
	// when they close the window first.  UI thread.
	void Open(std::function<void(const std::string&)> done, std::function<void()> cancelled);

	// DM_TEST_WEBLOGIN: loads the page in a hidden window, prints its title
	// and whether the token watcher is in place, and calls finished().
	void SelfTest(std::function<void()> finished);
}
