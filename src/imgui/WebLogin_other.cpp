// WebLogin where there is no browser view (IRIX; see WebLogin_mac.mm,
// WebLogin_win.cpp and WebLogin_linux.cpp).

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__linux__)
#include "WebLogin.hpp"

bool WebLogin::Available()
{
	return false;
}

bool WebLogin::Busy() { return false; }
void WebLogin::Pump() {}

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
