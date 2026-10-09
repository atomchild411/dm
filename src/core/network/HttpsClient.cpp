#include "HttpsClient.hpp"
#include "TlsSocket.hpp"
#include "../config/LocalSettings.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

// How long an answer may go without a byte arriving.
#ifdef __sgi
static const int READ_IDLE_MS = 60000;
#else
static const int READ_IDLE_MS = 30000;
#endif
// A connection idle longer than this is not used again (servers end idle
// connections after a while; a fresh one is more likely to work).
static const int64_t REUSE_IDLE_MS = 60000;
// The most an answer may be: its headers, how many there are, its body.
static const size_t MAX_HEADER_BYTES = 65536;
static const size_t MAX_HEADERS = 128;
static const size_t MAX_BODY = 64u * 1024 * 1024;
static const int MAX_REDIRECTS = 5;

struct HttpsClient::Connection
{
	TlsSocket tls;
	std::string buf;    // what was read and not yet taken
	size_t pos = 0;
	int64_t lastUsed = 0;
	bool gotBytes = false;

	// More from the server: false when the connection ended or failed.
	bool Fill(std::string& error)
	{
		if (pos > 0 && pos == buf.size()) {
			buf.clear();
			pos = 0;
		}
		char tmp[16384];
		int n = tls.Read(tmp, sizeof tmp, NowMs() + READ_IDLE_MS);
		if (n > 0) {
			buf.append(tmp, size_t(n));
			gotBytes = true;
			return true;
		}
		error = n == 0 ? "the server closed the connection" : n == -2 ? "the server stopped answering" : "the connection failed";
		return false;
	}

	// A line (without its CRLF), at most max long.
	bool Line(std::string& line, size_t max, std::string& error)
	{
		for (;;)
		{
			size_t eol = buf.find('\n', pos);
			if (eol != std::string::npos) {
				size_t end = eol > pos && buf[eol - 1] == '\r' ? eol - 1 : eol;
				line.assign(buf, pos, end - pos);
				pos = eol + 1;
				return true;
			}
			if (buf.size() - pos > max) {
				error = "the server's answer had a line too long";
				return false;
			}
			if (!Fill(error))
				return false;
		}
	}

	// Exactly n bytes, added to out.
	bool Take(size_t n, std::string& out, std::string& error)
	{
		while (n > 0)
		{
			if (pos == buf.size() && !Fill(error))
				return false;
			size_t k = std::min(n, buf.size() - pos);
			out.append(buf, pos, k);
			pos += k;
			n -= k;
		}
		return true;
	}

	// Whether the idle connection is still there: nothing may have arrived
	// on it (the server's end of it, mostly) but TLS's own records.
	bool Alive()
	{
		if (!tls.IsOpen() || pos != buf.size())
			return false;
		char c;
		return tls.Read(&c, 1, NowMs()) == -2;
	}
};

static std::string Lower(std::string s)
{
	for (auto& c : s)
		c = char(tolower((unsigned char) c));
	return s;
}

std::string HttpsResponse::Header(const std::string& name) const
{
	std::string want = Lower(name);
	for (auto& h : headers)
		if (h.first == want)
			return h.second;
	return "";
}

HttpsClient::HttpsClient()
{
	TlsSocket::Init();
}

HttpsClient::~HttpsClient()
{
}

static std::string Trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
	return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// https://host[:port][/path][?query] -> its parts.
static bool ParseUrl(const std::string& url, std::string& host, std::string& port, std::string& authority, std::string& target)
{
	if (url.size() < 8 || Lower(url.substr(0, 8)) != "https://")
		return false;
	size_t at = url.find_first_of("/?#", 8);
	authority = url.substr(8, at == std::string::npos ? std::string::npos : at - 8);
	target = at == std::string::npos ? "/" : url.substr(at);
	size_t hash = target.find('#');
	if (hash != std::string::npos)
		target.erase(hash);
	if (target.empty() || target[0] == '?')
		target = "/" + target;
	if (authority.empty() || authority.find('@') != std::string::npos)
		return false;
	size_t colon = authority.rfind(':');
	if (colon != std::string::npos && authority.find(']', colon) == std::string::npos) {
		host = authority.substr(0, colon);
		port = authority.substr(colon + 1);
	}
	else {
		host = authority;
		port = "443";
	}
	if (host.size() > 2 && host.front() == '[' && host.back() == ']')
		host = host.substr(1, host.size() - 2);
	if (host.empty() || port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
		return false;
	if (port == "443")
		authority = authority.substr(0, colon == std::string::npos ? std::string::npos : colon);
	for (char c : url)
		if ((unsigned char) c <= ' ' || c == 0x7f)
			return false;
	return true;
}

static bool HasLineBreak(const std::string& s)
{
	return s.find_first_of("\r\n", 0) != std::string::npos || s.find('\0') != std::string::npos;
}

// Reads an answer's status line and headers; skips 1xx answers.
static bool ReadHead(HttpsClient::Connection& c, HttpsResponse& r, bool& http11, std::string& error)
{
	for (;;)
	{
		std::string line;
		if (!c.Line(line, MAX_HEADER_BYTES, error))
			return false;
		// "HTTP/1.1 200 OK"
		if (line.size() < 12 || line.compare(0, 7, "HTTP/1.") || line[8] != ' ' ||
			!isdigit((unsigned char) line[9]) || !isdigit((unsigned char) line[10]) || !isdigit((unsigned char) line[11]) ||
			(line.size() > 12 && line[12] != ' ')) {
			error = "the server's answer was not HTTP";
			return false;
		}
		http11 = line[7] == '1';
		r.status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + (line[11] - '0');
		if (r.status < 100) {
			error = "the server's answer was not HTTP";
			return false;
		}

		r.headers.clear();
		size_t bytes = line.size();
		for (;;)
		{
			if (!c.Line(line, MAX_HEADER_BYTES, error))
				return false;
			if (line.empty())
				break;
			bytes += line.size();
			if (bytes > MAX_HEADER_BYTES || r.headers.size() >= MAX_HEADERS) {
				error = "the server's answer had too many headers";
				return false;
			}
			size_t colon = line.find(':');
			if (line[0] == ' ' || line[0] == '\t' || colon == std::string::npos || colon == 0 ||
				line.find_first_of(" \t") < colon) {
				error = "the server's answer had a broken header";
				return false;
			}
			r.headers.emplace_back(Lower(line.substr(0, colon)), Trim(line.substr(colon + 1)));
		}
		if (r.status >= 200)
			return true;
	}
}

// A chunked body (RFC 9112 7.1), its trailers skipped.
static bool ReadChunked(HttpsClient::Connection& c, std::string& body, std::string& error)
{
	for (;;)
	{
		std::string line;
		if (!c.Line(line, 1024, error))
			return false;
		size_t n = 0, digits = 0;
		for (char ch : line) {
			int v = isdigit((unsigned char) ch) ? ch - '0' :
				(ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
			if (v < 0)
				break;
			if (++digits > 8) {
				error = "the server's answer was too big";
				return false;
			}
			n = n * 16 + size_t(v);
		}
		if (!digits || (digits < line.size() && line[digits] != ';' && line[digits] != ' ' && line[digits] != '\t')) {
			error = "the server's answer had a broken chunk";
			return false;
		}
		if (n == 0)
			break;
		if (body.size() + n > MAX_BODY) {
			error = "the server's answer was too big";
			return false;
		}
		if (!c.Take(n, body, error))
			return false;
		if (!c.Line(line, 2, error))
			return false;
		if (!line.empty()) {
			error = "the server's answer had a broken chunk";
			return false;
		}
	}
	for (size_t trailers = 0;; trailers++)
	{
		std::string line;
		if (!c.Line(line, MAX_HEADER_BYTES, error))
			return false;
		if (line.empty())
			return true;
		if (trailers >= MAX_HEADERS) {
			error = "the server's answer had too many headers";
			return false;
		}
	}
}

bool HttpsClient::Request(const std::string& method, const std::string& url, const HttpHeaders& headers,
	const std::string& body, bool followRedirects, HttpsResponse& response, HttpsFailure& why)
{
	std::string where = url;
	for (int hop = 0;; hop++)
	{
		if (!Once(method, where, headers, body, response, why))
			return false;

		bool redirect = response.status == 301 || response.status == 302 || response.status == 303 ||
			response.status == 307 || response.status == 308;
		if (!redirect || !followRedirects || method != "GET" || hop >= MAX_REDIRECTS)
			return true;

		std::string location = response.Header("location");
		if (location.compare(0, 2, "//") == 0)
			location = "https:" + location;
		else if (!location.empty() && location[0] == '/') {
			size_t end = where.find_first_of("/?#", 8);
			location = where.substr(0, end) + location;
		}
		// only to another https:// address
		std::string h, p, a, t;
		if (!ParseUrl(location, h, p, a, t))
			return true;
		where = location;
	}
}

bool HttpsClient::Once(const std::string& method, const std::string& url, const HttpHeaders& headers,
	const std::string& body, HttpsResponse& response, HttpsFailure& why)
{
	std::string host, port, authority, target;
	if (!ParseUrl(url, host, port, authority, target)) {
		why.message = "Not an https:// address: " + url;
		return false;
	}
	for (auto& h : headers) {
		if (h.first.empty() || h.first.find_first_of(" \t:") != std::string::npos || HasLineBreak(h.first) || HasLineBreak(h.second)) {
			why.message = "A request header was not valid: " + h.first;
			return false;
		}
	}

	std::string req = method + " " + target + " HTTP/1.1\r\nHost: " + authority + "\r\n";
	for (auto& h : headers)
		req += h.first + ": " + h.second + "\r\n";
	if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH")
		req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	req += "\r\n";
	req += body;

	const bool verify = GetLocalSettings()->EnableTLSVerification();
	const std::string key = host + ":" + port + (verify ? "" : ":noverify");

	// a connection kept from before, if it is still there (else, once, a
	// new one: the server may have ended the old one just as it was used)
	for (int attempt = 0; attempt < 2; attempt++)
	{
		std::unique_ptr<Connection>& c = m_connections[key];
		bool reused = c && NowMs() - c->lastUsed < REUSE_IDLE_MS && c->Alive();
		if (!reused) {
			c.reset(new Connection);
			TlsSocket::Failure f;
			if (!c->tls.Open(host, port, verify, TLS_STEP_TIMEOUT_MS, f)) {
				m_connections.erase(key);
				why.message = f.message;
				why.tlsError = f.tlsError;
				return false;
			}
		}
		c->gotBytes = false;

		std::string error;
		bool timedOut = false, http11 = false;
		response = HttpsResponse();
		bool ok = c->tls.Write(req.data(), req.size(), NowMs() + READ_IDLE_MS, timedOut);
		if (!ok)
			error = timedOut ? "the server stopped taking the request" : "the connection failed";
		else
			ok = ReadHead(*c, response, http11, error);

		bool keep = false;
		if (ok)
		{
			// how the body is framed: none, chunked, by its length, or by
			// the end of the connection (which then cannot be used again)
			std::string te = Lower(response.Header("transfer-encoding"));
			std::string cl = response.Header("content-length");
			size_t chunked = te.rfind("chunked");
			if (method == "HEAD" || response.status == 204 || response.status == 304) {
				keep = true;
			}
			else if (!te.empty() && chunked != std::string::npos && Trim(te.substr(chunked + 7)).empty()) {
				ok = ReadChunked(*c, response.body, error);
				keep = cl.empty();
			}
			else if (te.empty() && !cl.empty()) {
				if (cl.size() > 12 || cl.find_first_not_of("0123456789") != std::string::npos) {
					ok = false;
					error = "the server's answer had a broken length";
				}
				else if ((size_t) std::stoull(cl) > MAX_BODY) {
					ok = false;
					error = "the server's answer was too big";
				}
				else {
					ok = c->Take((size_t) std::stoull(cl), response.body, error);
					keep = true;
				}
			}
			else {
				std::string ended;
				do {
					response.body.append(c->buf, c->pos, std::string::npos);
					c->pos = c->buf.size();
					if (response.body.size() > MAX_BODY) {
						ok = false;
						error = "the server's answer was too big";
						break;
					}
				} while (c->Fill(ended));
			}

			bool connClose = false;
			std::string conn = Lower(response.Header("connection"));
			for (size_t at = 0; at <= conn.size(); ) {
				size_t comma = conn.find(',', at);
				if (Trim(conn.substr(at, comma == std::string::npos ? std::string::npos : comma - at)) == "close")
					connClose = true;
				if (comma == std::string::npos)
					break;
				at = comma + 1;
			}
			keep = keep && ok && http11 && !connClose && c->pos == c->buf.size();
		}

		if (ok) {
			if (keep)
				c->lastUsed = NowMs();
			else
				m_connections.erase(key);
			return true;
		}

		bool stale = reused && !c->gotBytes;
		m_connections.erase(key);
		if (!stale) {
			why.message = "No answer from " + host + ": " + error;
			return false;
		}
	}
	why.message = "No answer from " + host;
	return false;
}

const char* HttpsClient::StatusText(int status)
{
	switch (status)
	{
		case 200: return "OK";
		case 201: return "Created";
		case 202: return "Accepted";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 304: return "Not Modified";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 409: return "Conflict";
		case 413: return "Content Too Large";
		case 415: return "Unsupported Media Type";
		case 429: return "Too Many Requests";
		case 500: return "Internal Server Error";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Timeout";
		default:  return "Unknown";
	}
}
