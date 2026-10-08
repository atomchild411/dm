#pragma once

#include <functional>
#include <string>

#include "Xm.hpp"
#include "Canvas.hpp"

// The QR login dialog (shared/QrLogin does the talking to Discord): the
// code, what is happening, and Use a Token Instead, Try Again and Quit.
namespace QrLoginDialog
{
	// Shows the dialog.  done(token) when the phone confirmed; done("")
	// when the user quits.  useToken() when they would rather paste a token.
	void Show(Widget parent, const PixelFormat& fmt, const std::string& message,
		std::function<void(const std::string&)> done, std::function<void()> useToken);
}
