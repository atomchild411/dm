// WebLogin where there is no browser view yet (see WebLogin_mac.mm).

#if !defined(__APPLE__)
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

void WebLogin::SelfTest(std::function<void()> finished)
{
	if (finished)
		finished();
}
#endif
