#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

typedef std::vector<std::pair<std::string, std::string>> HttpHeaders;

struct HttpsResponse
{
	int status = 0;
	HttpHeaders headers;  // names in lower case
	std::string body;

	// A header's value (the name in any case), or "".
	std::string Header(const std::string& name) const;
};

struct HttpsFailure
{
	std::string message;
	bool tlsError = false;  // the server's certificate was not accepted
};

// HTTP/1.1 over TLS, for one thread at a time: a connection to each host is
// kept open after a request and used for the next one there.
class HttpsClient
{
public:
	HttpsClient();
	~HttpsClient();

	// One request to an https:// URL.  Host and Content-Length are added to
	// the headers.  Redirects are followed for GET when followRedirects
	// (https:// ones only, five at most); never set it on a request that
	// carries a login token.  True with the answer, whatever its status;
	// false with why there is none.
	bool Request(const std::string& method, const std::string& url, const HttpHeaders& headers,
		const std::string& body, bool followRedirects, HttpsResponse& response, HttpsFailure& why);

	// The reason phrase for a status ("Not Found").
	static const char* StatusText(int status);

	struct Connection;

private:
	bool Once(const std::string& method, const std::string& url, const HttpHeaders& headers,
		const std::string& body, HttpsResponse& response, HttpsFailure& why);

	std::map<std::string, std::unique_ptr<Connection>> m_connections;  // by host:port
};
