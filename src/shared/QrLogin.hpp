#pragma once

#include <functional>
#include <string>

// Logging in by QR code, as discord.com's login page offers: Discord's
// remote-auth gateway gives a code that the phone app scans; once the
// login is confirmed on the phone, the account's token arrives encrypted
// to a key made for this login only.
//
// This is the protocol and its state; a front end shows StatusText() and the
// code, and offers Retry() once Failed().  Everything runs on the UI
// thread except where noted.
namespace QrLogin
{
	// Starts a login: makes the key (on a thread of its own), connects and
	// asks for a code.  changed() runs whenever the status, the code or the
	// failed state changes; loggedIn(token) once the phone confirmed.
	// Does nothing while a login is running.
	void Start(std::function<void()> changed, std::function<void(const std::string& token)> loggedIn);

	// Asks for a new code after a failure.
	void Retry();

	// Ends the login (the user quit, or chose a token instead): closes the
	// connection and forgets the key.  loggedIn is not called.
	void Stop();

	// What to tell the user (may hold line breaks).  (Not "Status": Xlib defines that name.)
	const std::string& StatusText();

	// The code to show: CodeSize() modules square (0 while there is none),
	// CodeModule(x, y) true for a dark one.
	int CodeSize();
	bool CodeModule(int x, int y);

	// The code was scanned and waits for the phone's confirmation (show it
	// greyed out).
	bool Scanned();

	// An error is shown, waiting for Retry().
	bool Failed();

	// When Discord wants a captcha solved before it hands over the token
	// (Failed() then, with a site key here): what the front end shows the
	// user (an hCaptcha widget with this site key and request data).
	struct Captcha
	{
		std::string service;   // "hcaptcha"
		std::string sitekey;
		std::string rqdata;
		std::string rqtoken;
	};
	const Captcha& PendingCaptcha();
	// The user's answer: the login is tried again with it.
	void SolveCaptcha(const std::string& answer);

	// The remote-auth gateway's connection, -1 when none: the frontend hands
	// its traffic here (any thread).
	int GatewayId();
	void OnGatewayMessage(const std::string& payload); // UI thread
	void OnGatewayClosed(int code, const std::string& reason); // UI thread
}
