#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include "DiscordRequest.hpp"

enum eHttpResponseCodes
{
	HTTP_OOPS = -1,
	HTTP_OK           = 200,
	HTTP_CREATED      = 201,
	HTTP_ACCEPTED     = 202,
	HTTP_NONAUTHINFO  = 203,
	HTTP_NOCONTENT    = 204,
	HTTP_RESETCONTENT = 204,
	HTTP_PARTIALCNTNT = 204,

	HTTP_BADREQUEST   = 400,
	HTTP_UNAUTHORIZED = 401,
	HTTP_FORBIDDEN    = 403,
	HTTP_NOTFOUND     = 404,
	HTTP_UNSUPPMEDIA  = 415,
	HTTP_TOOMANYREQS  = 429,

	HTTP_BADGATEWAY   = 502,

	HTTP_CANCELED     = 998,
};

namespace httplib {
	class Result;
}

struct NetRequest
{
	typedef void(*NetworkResponseFunc)(NetRequest* pReq);
	enum eType
	{
		NOTHING_MAN,
		QUIT,
		GET,
		POST,
		PUT,
		PATCH,
		POST_JSON,
		DELETE_, // WinNT defines "DELETE" as a macro... bruh
		PUT_JSON,
	};
	int result = 0;
	int itype  = 0;
	uint64_t key = 0;
	eType type;
	NetworkResponseFunc pFunc;
	std::string url;
	std::string response = "";
	std::string params = "";
	std::string authorization = "";
	std::string additional_data = "";
	std::vector<std::pair<std::string, std::string>> extra_headers; // sent as they are
	int Priority() const;

	bool operator<(const NetRequest& other) const {
		return Priority() < other.Priority();
	}

	bool IsMediaRequest() const {
		return itype == DiscordRequest::IMAGE || itype == DiscordRequest::IMAGE_ATTACHMENT;
	}

	bool IsOk() const {
		return result == HTTP_OK || result == HTTP_NOCONTENT;
	}

	NetRequest(
		int _result = 0,
		int _itype = 0,
		uint64_t _key = 0,
		eType _type = NOTHING_MAN,
		const std::string& _url = "",
		const std::string& _response = "",
		const std::string& _params = "",
		const std::string& _authorization = "",
		const std::string& _additional_data = "",
		NetworkResponseFunc _func = nullptr
	);

	std::string ErrorMessage() const;
};

class HTTPClient
{
public:
	HTTPClient() {}
	virtual ~HTTPClient() {}

	virtual void Init() = 0;
	virtual void Kill() = 0;
	virtual void StopAllRequests() = 0;
	virtual void PrepareQuit() = 0;
	virtual std::string ErrorMessage(int code) const = 0;

	// Sends a request via this HTTP client.  If interactive, is prioritized.
	virtual void PerformRequest(
		bool interactive,
		NetRequest::eType type,
		const std::string& url,
		int itype,
		uint64_t requestKey,
		std::string params = "",
		std::string authorization = "",
		std::string additional_data = "",
		NetRequest::NetworkResponseFunc pRespFunc = nullptr,
		const std::vector<std::pair<std::string, std::string>>& extra_headers = {}
	) = 0;

	static void DefaultRequestHandler(NetRequest* pRequest);
};

HTTPClient* GetHTTPClient();
