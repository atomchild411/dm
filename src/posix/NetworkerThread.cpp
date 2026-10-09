#include "NetworkerThread.hpp"
#include "network/DiscordRequest.hpp"
#include "config/LocalSettings.hpp"
#include "config/DiscordClientConfig.hpp"
#include "Frontend.hpp"
#include "utils/Util.hpp"

#include <cassert>
#include <cstdlib>
#include <functional>
#include <sys/stat.h>
#include <unistd.h>

#include <httplib/httplib.h>

#include "RateLimits.hpp"
#include "network/DiscordAPI.hpp"
#include <thread>

#ifndef DM_DATADIR
#define DM_DATADIR "/usr/local/share/discord-messenger"
#endif

constexpr size_t REPORT_PROGRESS_EVERY_BYTES = 15360; // arbitrary

// Connection failures are retried this many times, a second apart and then
// two, before the request fails.
constexpr int MAX_ATTEMPTS = 3;


static bool FileExists(const std::string& path)
{
	struct stat st;
	return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string GetCACertFile()
{
	static std::string s_file;
	static bool s_looked = false;
	if (s_looked)
		return s_file;
	s_looked = true;

	const char* env = getenv("DM_CA_FILE");
	const char* candidates[] = {
		env,
		DM_DATADIR "/cacert.pem",
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/ssl/cert.pem",
		"/etc/pki/tls/certs/ca-bundle.crt",
	};
	for (const char* c : candidates) {
		if (c && FileExists(c)) {
			s_file = c;
			break;
		}
	}
	return s_file;
}

int NetRequest::Priority() const
{
	int prio = 0;

	switch (type)
	{
		case QUIT:
			prio = 200;
			break;
		case PUT:
		case POST:
		case POST_JSON:
		case PATCH:
		case PUT_JSON:
		case DELETE_:
			prio = 100;
			break;
		case GET:
			prio = 90;
			break;
		default:
			assert(!"huh?");
	}

	switch (itype) {
		using namespace DiscordRequest;
		default:
			prio += 9;
			break;

		case IMAGE_ATTACHMENT:
		case MESSAGES:
		case GUILD:
			prio += 8;
			break;

		case IMAGE:
			prio += 1;
			break;
	}

	return prio;
}

static const char* MethodName(NetRequest::eType t)
{
	switch (t) {
		case NetRequest::POST: case NetRequest::POST_JSON: return "POST";
		case NetRequest::PUT: case NetRequest::PUT_JSON: return "PUT";
		case NetRequest::PATCH: return "PATCH";
		case NetRequest::DELETE_: return "DELETE";
		default: return "GET";
	}
}

bool NetworkerThread::ProcessResult(NetRequest& req, const httplib::Result& res, int& attempt, bool api, int& limited)
{
	using namespace httplib;

	// Discord's rate limits: what the answer says of them; a 429 is made
	// again (twice at most) once the time it asks for has passed; a refused
	// token is not sent again
	if (api && res) {
		int wait = RateLimits::Learn(MethodName(req.type), req.url, res->status,
			[&res](const char* h) { return res->get_header_value(h); }, res->body);
		if (res->status == 401 && !req.authorization.empty())
			RateLimits::Refuse(req.authorization);
		if (wait >= 0 && ++limited <= 2) {
			DbgPrintF("Request to %s was rate limited; trying again in %d ms", req.url.c_str(), wait);
			return true;
		}
	}

	if (!res || res.error() == Error::SSLServerVerification)
	{
		bool isSSLError = res.error() == Error::SSLServerVerification;
		std::string errorstr = to_string(res.error());

		if (!isSSLError && ++attempt < MAX_ATTEMPTS) {
			DbgPrintF("Request to %s failed (%s), retrying", req.url.c_str(), errorstr.c_str());
			sleep(attempt);
			return true;
		}

		req.result = -1;
		req.response = errorstr;
		if (isSSLError)
			GetFrontend()->OnGenericError("Could not verify the identity of " + req.url +
				".\n\nThe server's certificate was not accepted (" + errorstr + ").");
	}
	else if (res.error() == Error::Canceled)
	{
		req.result = HTTP_CANCELED;
		req.response = "Operation cancelled by user";
	}
	else
	{
		// Discord's front end sometimes cannot reach the service behind it
		// (502, 503, 504): a request that is safe to make twice is made
		// again (sending a message is not)
		bool repeatable = req.type == NetRequest::GET || req.type == NetRequest::PUT ||
			req.type == NetRequest::PATCH || req.type == NetRequest::DELETE_;
		int status = res->status;
		if (repeatable && (status == 502 || status == 503 || status == 504) && ++attempt < MAX_ATTEMPTS) {
			DbgPrintF("Request to %s got %d, retrying", req.url.c_str(), status);
			sleep(attempt);
			return true;
		}
		req.result = res->status;
		req.response = res->body;
	}

	// N.B.  Don't return unless you're absolutely done with the request!
	req.pFunc(&req);
	return false;
}

std::string NetworkerThreadManager::ErrorMessage(int code) const
{
	if (code < 0) return "Client Error";
	return std::string(httplib::detail::status_message(code));
}

void NetworkerThread::FulfillRequest(NetRequest& req)
{
	std::string& url = req.url;
	DbgPrintF("Accessing URL: %s", url.c_str());

	// split the URL into its host name and path
	std::string hostName = "", path = "";
	auto pos = url.find("://"), pos2 = pos;
	if (pos != std::string::npos)
		pos2 = url.find("/", pos + 4);
	else
		pos2 = url.find("/");

	if (pos2 != std::string::npos)
	{
		hostName = url.substr(0, pos2);
		path = url.substr(pos2);
	}

	using namespace httplib;
	Client client(hostName);

	client.enable_server_certificate_verification(GetLocalSettings()->EnableTLSVerification());
#if defined(__APPLE__) || defined(_WIN32)
	if (GetLocalSettings()->EnableTLSVerification())
		UseSystemTrust(client.ssl_context());
#else
	std::string caFile = GetCACertFile();
	if (!caFile.empty())
		client.set_ca_cert_path(caFile.c_str());
#endif

	// Follow redirects (CDN links), but never with the login token: httplib
	// sends the same headers to wherever a redirect points, http:// included.
	client.set_follow_location(req.authorization.empty());

	Headers headers;
	headers.insert(std::make_pair("User-Agent", GetClientConfig()->GetUserAgent()));

	if (GetLocalSettings()->AddExtraHeaders())
	{
		headers.insert(std::make_pair("X-Super-Properties", GetClientConfig()->GetSerializedBase64Blob()));
		headers.insert(std::make_pair("X-Discord-Timezone", GetClientConfig()->GetTimezone()));
		headers.insert(std::make_pair("X-Discord-Locale", GetClientConfig()->GetLocale()));
		headers.insert(std::make_pair("Sec-Ch-Ua", GetClientConfig()->GetSecChUa()));
		headers.insert(std::make_pair("Sec-Ch-Ua-Mobile", "?0"));
		headers.insert(std::make_pair("Sec-Ch-Ua-Platform", GetClientConfig()->GetOS()));
	}

	if (req.authorization.size())
	{
		assert(req.url.find("images") == std::string::npos);
		assert(req.url.find("cdn") == std::string::npos);
		assert(req.url.find("discord") != std::string::npos);

		headers.insert(std::make_pair("Authorization", req.authorization));
	}
	for (auto& h : req.extra_headers)
		headers.insert(h);

	// a token Discord refused is not sent again (each refusal counts
	// against this address with Cloudflare)
	if (!req.authorization.empty() && RateLimits::IsRefused(req.authorization)) {
		DbgPrintF("Not sending %s: Discord refused the token", req.url.c_str());
		return;
	}
	const bool api = req.url.compare(0, GetDiscordAPI().size(), GetDiscordAPI()) == 0;

	int attempt = 0, limited = 0;
	bool retry = false;
	do
	{
		// wait for the route's (or every route's) limit to pass, rather than
		// meet it; a wait of minutes is given up
		if (api) {
			int wait = RateLimits::WaitBefore(MethodName(req.type), req.url);
			if (wait > 120000) {
				req.result = HTTP_TOOMANYREQS;
				req.response = "Discord asked to wait " + std::to_string(wait / 1000) + " seconds before more requests.";
				req.pFunc(&req);
				return;
			}
			if (wait > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(wait));
		}

		switch (req.type)
		{
			case NetRequest::POST:
				retry = ProcessResult(req, client.Post(path, headers, req.params, "application/x-www-form-urlencoded"), attempt, api, limited);
				break;
			case NetRequest::POST_JSON:
				retry = ProcessResult(req, client.Post(path, headers, req.params, "application/json"), attempt, api, limited);
				break;
			case NetRequest::PUT:
				retry = ProcessResult(req, client.Put(path, headers, req.params, "application/x-www-form-urlencoded"), attempt, api, limited);
				break;
			case NetRequest::PUT_JSON:
				retry = ProcessResult(req, client.Put(path, headers, req.params, "application/json"), attempt, api, limited);
				break;
			case NetRequest::GET:
				retry = ProcessResult(req, client.Get(path, headers), attempt, api, limited);
				break;
			case NetRequest::PATCH:
				retry = ProcessResult(req, client.Patch(path, headers, req.params, "application/json"), attempt, api, limited);
				break;
			case NetRequest::DELETE_:
				// no body, no content type (deleting a message or a reaction)
				if (req.params.empty())
					retry = ProcessResult(req, client.Delete(path, headers), attempt, api, limited);
				else
					retry = ProcessResult(req, client.Delete(path, headers, req.params, "application/json"), attempt, api, limited);
				break;
			default:
				assert(!"Don't know how to handle that type of request!");
				break;
		}
	}
	while (retry);
}

void NetworkerThread::Run()
{
	for (;;)
	{
		NetRequest request;
		{
			std::unique_lock<std::mutex> lk(m_requestLock);
			m_requestCond.wait(lk, [this] { return !m_requests.empty(); });
			request = m_requests.top();
			m_requests.pop();
		}

		if (request.type == NetRequest::QUIT)
			break;

		FulfillRequest(request);
	}
}

void NetworkerThread::AddRequest(
	NetRequest::eType type,
	const std::string& url,
	int itype,
	uint64_t requestKey,
	std::string params,
	std::string authorization,
	std::string additional_data,
	NetRequest::NetworkResponseFunc pRespFunc,
	const std::vector<std::pair<std::string, std::string>>& extra_headers)
{
	NetRequest rq(0, itype, requestKey, type, url, "", params, authorization, additional_data, pRespFunc);
	rq.extra_headers = extra_headers;

	std::lock_guard<std::mutex> lk(m_requestLock);
	m_requests.push(rq);
	m_requestCond.notify_one();
}

void NetworkerThread::StopAllRequests()
{
	std::lock_guard<std::mutex> lk(m_requestLock);
	while (!m_requests.empty())
		m_requests.pop();
}

void NetworkerThread::PrepareQuit()
{
	std::lock_guard<std::mutex> lk(m_requestLock);
	while (!m_requests.empty())
		m_requests.pop();
	m_requests.push(NetRequest(0, 0, 0, NetRequest::QUIT));
	m_requestCond.notify_one();
}

void NetworkerThread::Join()
{
	if (m_thread.joinable())
		m_thread.join();
}

NetworkerThread::NetworkerThread()
{
	m_thread = std::thread(&NetworkerThread::Run, this);
}

NetworkerThread::~NetworkerThread()
{
	PrepareQuit();
	Join();
}

NetworkerThreadManager::~NetworkerThreadManager()
{
	Kill();
}

void NetworkerThreadManager::Init()
{
	m_bKilled = false;
	for (int i = 0; i < C_AMT_NETWORKER_THREADS; i++)
		m_pNetworkThreads[i] = new NetworkerThread();
}

void NetworkerThreadManager::StopAllRequests()
{
	for (int i = 0; i < C_AMT_NETWORKER_THREADS; i++) {
		if (m_pNetworkThreads[i])
			m_pNetworkThreads[i]->StopAllRequests();
	}
}

void NetworkerThreadManager::PrepareQuit()
{
	for (int i = 0; i < C_AMT_NETWORKER_THREADS; i++) {
		if (m_pNetworkThreads[i])
			m_pNetworkThreads[i]->PrepareQuit();
	}
}

void NetworkerThreadManager::Kill()
{
	if (m_bKilled)
		return;

	PrepareQuit();
	for (int i = 0; i < C_AMT_NETWORKER_THREADS; i++)
	{
		delete m_pNetworkThreads[i];
		m_pNetworkThreads[i] = nullptr;
	}

	m_bKilled = true;
}

void NetworkerThreadManager::PerformRequest(
	bool interactive,
	NetRequest::eType type,
	const std::string& url,
	int itype,
	uint64_t requestKey,
	std::string params,
	std::string authorization,
	std::string additional_data,
	NetRequest::NetworkResponseFunc pRespFunc,
	const std::vector<std::pair<std::string, std::string>>& extra_headers)
{
	int idx;
	if (interactive) {
		m_nextInteractiveId = (m_nextInteractiveId + 1) % C_INTERACTIVE_NETWORKER_THREADS;
		idx = m_nextInteractiveId;
	}
	else {
		m_nextBackgroundId = C_INTERACTIVE_NETWORKER_THREADS + (m_nextBackgroundId + 1) % (C_AMT_NETWORKER_THREADS - C_INTERACTIVE_NETWORKER_THREADS);
		idx = m_nextBackgroundId;
	}

	if (m_pNetworkThreads[idx])
		m_pNetworkThreads[idx]->AddRequest(type, url, itype, requestKey, params, authorization, additional_data, pRespFunc, extra_headers);
}
