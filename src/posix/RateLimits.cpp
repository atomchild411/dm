#include "RateLimits.hpp"

#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include <nlohmann/json.h>

#include "utils/Util.hpp"

namespace
{
	struct Limit
	{
		int remaining = 1;    // requests left before resetAt
		long long resetAt = 0;
	};

	std::mutex g_lock;
	std::map<std::string, std::string> g_bucketOf; // route -> Discord's bucket hash
	std::map<std::string, Limit> g_limits;         // bucket (or route) and major parameter -> its limit
	long long g_globalUntil = 0;
	std::set<std::string> g_refused;

	long long NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	std::vector<std::string> PathParts(const std::string& url, std::string& major)
	{
		// the path after /api/vN/ (or after the host)
		size_t p = url.find("/api/");
		if (p != std::string::npos) {
			p = url.find('/', p + 5);
			p = p == std::string::npos ? url.size() : p + 1;
		}
		else {
			p = url.find("://");
			p = url.find('/', p == std::string::npos ? 0 : p + 3);
			p = p == std::string::npos ? url.size() : p + 1;
		}
		std::string path = url.substr(p);
		size_t q = path.find('?');
		if (q != std::string::npos)
			path.resize(q);

		std::vector<std::string> parts;
		size_t start = 0;
		while (start <= path.size()) {
			size_t slash = path.find('/', start);
			if (slash == std::string::npos)
				slash = path.size();
			parts.push_back(path.substr(start, slash - start));
			start = slash + 1;
		}
		major.clear();
		for (size_t i = 0; i < parts.size(); i++) {
			bool id = !parts[i].empty() && parts[i].find_first_not_of("0123456789") == std::string::npos;
			const std::string prev = i ? parts[i - 1] : "";
			if (i && prev == "reactions")
				parts[i] = ":emoji";
			else if (id && (prev == "channels" || prev == "guilds" || prev == "webhooks") && major.empty())
				major = prev + "/" + parts[i];
			else if (id)
				parts[i] = ":id";
		}
		return parts;
	}

	// the key of the limit a request counts against
	std::string KeyFor(const std::string& method, const std::string& url)
	{
		std::string route = RateLimits::Route(method, url), major;
		PathParts(url, major);
		auto b = g_bucketOf.find(route);
		return (b != g_bucketOf.end() ? b->second : route) + " " + major;
	}
}

std::string RateLimits::Route(const std::string& method, const std::string& url)
{
	std::string major;
	std::vector<std::string> parts = PathParts(url, major);
	std::string route = method;
	for (size_t i = 0; i < parts.size(); i++)
		route += (i ? "/" : " ") + parts[i];
	return route;
}

int RateLimits::WaitBefore(const std::string& method, const std::string& url)
{
	std::lock_guard<std::mutex> lk(g_lock);
	long long now = NowMs(), until = g_globalUntil;
	Limit& lim = g_limits[KeyFor(method, url)];
	if (lim.resetAt <= now) {
		// (what is known of it is over: the next answer says again)
		lim.remaining = 1;
		lim.resetAt = 0;
	}
	if (lim.remaining <= 0 && lim.resetAt > until)
		until = lim.resetAt;
	if (lim.remaining > 0)
		lim.remaining--; // this one: others in the meantime wait for the reset
	return until > now ? (int) (until - now) : 0;
}

int RateLimits::Learn(const std::string& method, const std::string& url, int status,
	const std::function<std::string(const char*)>& header, const std::string& body)
{
	std::lock_guard<std::mutex> lk(g_lock);
	long long now = NowMs();
	std::string route = Route(method, url), bucket = header("X-RateLimit-Bucket");
	if (!bucket.empty())
		g_bucketOf[route] = bucket;
	Limit& lim = g_limits[KeyFor(method, url)];

	std::string remaining = header("X-RateLimit-Remaining"), resetAfter = header("X-RateLimit-Reset-After");
	if (!remaining.empty() && !resetAfter.empty()) {
		lim.remaining = atoi(remaining.c_str());
		lim.resetAt = now + (long long) (atof(resetAfter.c_str()) * 1000) + 50;
	}

	if (status != 429)
		return -1;

	// how long: the body's retry_after (seconds), else the Retry-After
	// header; Cloudflare's own 429 (no JSON) blocks the whole address
	double after = -1;
	bool global = header("X-RateLimit-Global") == "true" || header("X-RateLimit-Scope") == "global";
	try {
		nlohmann::json j = nlohmann::json::parse(body);
		if (j.contains("retry_after") && j["retry_after"].is_number())
			after = j["retry_after"].get<double>();
		if (j.contains("global") && j["global"].is_boolean() && j["global"].get<bool>())
			global = true;
	}
	catch (...) {
		global = true;
	}
	if (after < 0) {
		std::string ra = header("Retry-After");
		after = ra.empty() ? 60 : atof(ra.c_str());
	}
	int wait = (int) (after * 1000) + 100;
	if (global)
		g_globalUntil = std::max(g_globalUntil, now + wait);
	else {
		lim.remaining = 0;
		lim.resetAt = std::max(lim.resetAt, now + wait);
	}
	DbgPrintF("429 on %s: wait %d ms (%s)", route.c_str(), wait, global ? "global" : "this route");
	return wait;
}

void RateLimits::Refuse(const std::string& token)
{
	std::lock_guard<std::mutex> lk(g_lock);
	if (!token.empty())
		g_refused.insert(token);
}

bool RateLimits::IsRefused(const std::string& token)
{
	std::lock_guard<std::mutex> lk(g_lock);
	return !token.empty() && g_refused.count(token);
}
