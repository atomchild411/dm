#pragma once

#include <functional>
#include <string>

// Discord's rate limits, as its answers state them, for the requests to its
// API (discord.com/developers/docs/topics/rate-limits): a request waits
// for a limit to pass rather than run into it, a 429 says how long to wait
// before the request is made again, and a token Discord refused is not sent
// again.  Every networker thread shares this.
namespace RateLimits
{
	// How long (ms) the request must wait before it goes; 0: now.  It
	// counts as one of the bucket's remaining requests.
	int WaitBefore(const std::string& method, const std::string& url);

	// What an answer said: its rate-limit headers (header("X-RateLimit-...")
	// gives their values, "" when absent) and, for a 429, its body.  Returns
	// how long (ms) a 429 asks to wait before trying again, else -1.
	int Learn(const std::string& method, const std::string& url, int status,
		const std::function<std::string(const char*)>& header, const std::string& body);

	// Discord refused this token (401): requests with it are not sent again.
	void Refuse(const std::string& token);
	bool IsRefused(const std::string& token);

	// The route a request counts against: its method and path, with the IDs
	// replaced but the channel's, the guild's or the webhook's (the "major
	// parameters").  Exposed for the tests.
	std::string Route(const std::string& method, const std::string& url);
}
