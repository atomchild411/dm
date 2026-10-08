// WebLogin where there is no browser view yet (see WebLogin_mac.mm and
// WebLogin_win.cpp).

#if !defined(__APPLE__) && !defined(_WIN32)
#include "WebLogin.hpp"

bool WebLogin::Available()
{
	return false;
}

void WebLogin::Open(std::function<void(const std::string&)>, std::function<void()> cancelled)
{
	if (cancelled)
		cancelled();
}

void WebLogin::ShowCaptcha(const std::string&, const std::string&,
	std::function<void(const std::string&)>, std::function<void()> cancelled)
{
	if (cancelled)
		cancelled();
}

void WebLogin::SelfTest(std::function<void()> finished)
{
	if (finished)
		finished();
}
#endif
