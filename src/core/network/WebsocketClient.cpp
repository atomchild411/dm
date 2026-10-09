#include "WebsocketClient.hpp"
#include "TlsSocket.hpp"
#include "../config/DiscordClientConfig.hpp"
#include "../config/LocalSettings.hpp"
#include "../Frontend.hpp"
#include "../utils/Base64.hpp"
#include "../utils/Util.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <set>
#include <thread>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

// how long a closing connection waits for the server's side of the close
static const int CLOSE_TIMEOUT_MS = 5000;
// the largest message taken (Discord's READY for a big account is a few MB)
static const size_t MAX_MESSAGE = 32000000;

namespace Opcode
{
	enum { CONTINUATION = 0, TEXT = 1, BINARY = 2, CLOSE = 8, PING = 9, PONG = 10 };
}

static WebsocketClient g_WSCSingleton;

WebsocketClient* GetWebsocketClient()
{
	return &g_WSCSingleton;
}

static bool ValidUtf8(const std::string& s)
{
	const unsigned char* p = (const unsigned char*) s.data();
	size_t n = s.size(), i = 0;
	while (i < n)
	{
		unsigned c = p[i];
		size_t len;
		uint32_t cp;
		if (c < 0x80) { i++; continue; }
		else if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
		else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
		else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
		else return false;
		if (i + len > n)
			return false;
		for (size_t k = 1; k < len; k++) {
			if ((p[i + k] & 0xC0) != 0x80)
				return false;
			cp = (cp << 6) | (p[i + k] & 0x3F);
		}
		// no overlong forms, surrogates or code points past U+10FFFF
		if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
			(cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
			return false;
		i += len;
	}
	return true;
}

// A frame from this side: masked, as RFC 6455 has clients send them.
static std::string Frame(int opcode, const char* data, size_t size)
{
	std::string f;
	f += char(0x80 | opcode);
	if (size < 126)
		f += char(0x80 | size);
	else if (size < 65536) {
		f += char(0x80 | 126);
		f += char(size >> 8);
		f += char(size);
	}
	else {
		f += char(0x80 | 127);
		for (int i = 7; i >= 0; i--)
			f += char(uint64_t(size) >> (8 * i));
	}
	unsigned char mask[4];
	RAND_bytes(mask, sizeof mask);
	f.append((const char*) mask, 4);
	size_t at = f.size();
	f.append(data, size);
	for (size_t i = 0; i < size; i++)
		f[at + i] ^= mask[i & 3];
	return f;
}

static std::string CloseFrame(int code)
{
	char c[2] = { char(code >> 8), char(code) };
	return Frame(Opcode::CLOSE, c, 2);
}

struct WebsocketClient::Connection
{
	int id = -1;
	std::string host, port, path, hostHeader, userAgent;
	bool verify = true;
	SocketWaker waker;
	TlsSocket tls;

	// asked for by the other threads
	std::mutex lock;
	std::string queued;              // frames to send, whole
	int closeCode = 0;               // a close asked for (0: none)
	std::atomic<bool> open{ false };
	std::atomic<bool> quiet{ false };  // tell the front end nothing more (Kill)

	// the connection's own thread's
	std::string rbuf;
	size_t rpos = 0;
	std::string wbuf;
	std::string message;             // a fragmented message so far
	int messageOpcode = 0;
	bool closeSent = false, closeReceived = false;
	int64_t closeDeadline = -1;
	int remoteCode = CloseCode::ABNORMAL;
	std::string remoteReason, server;

	void RequestClose(int code)
	{
		{
			std::lock_guard<std::mutex> g(lock);
			if (closeCode)
				return;
			closeCode = code;
		}
		waker.Wake();
	}

	bool CloseRequested()
	{
		std::lock_guard<std::mutex> g(lock);
		return closeCode != 0;
	}

	void Run();
	bool Handshake(std::string& err, int& code, bool& retry);
	void Loop();
	bool Flush(bool& wantWrite);
	bool ReadAvailable();
	int ParseFrame();
	void SendClose(int code);
};

// The connections' threads, each until it has finished, also after its
// connection was closed: Kill() waits for them.  (Never freed: a thread
// can outlive the client at exit.)
struct LiveConnections
{
	std::mutex m;
	std::condition_variable cv;
	std::set<WebsocketClient::Connection*> conns;
};

static LiveConnections& Live()
{
	static LiveConnections* live = new LiveConnections;
	return *live;
}

// The WebSocket handshake: an HTTP request to switch protocols, and the
// server's 101 answer with the key's proof.
bool WebsocketClient::Connection::Handshake(std::string& err, int& code, bool& retry)
{
	unsigned char nonce[16];
	RAND_bytes(nonce, sizeof nonce);
	std::string key = Base64Encode(nonce, sizeof nonce);

	std::string req =
		"GET " + path + " HTTP/1.1\r\n"
		"Host: " + hostHeader + "\r\n"
		"User-Agent: " + userAgent + "\r\n"
		"Origin: https://discord.com\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: " + key + "\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n";

	int64_t deadline = NowMs() + TLS_STEP_TIMEOUT_MS;
	bool timedOut = false;
	if (!tls.Write(req.data(), req.size(), deadline, timedOut)) {
		err = timedOut ? "The WebSocket handshake timed out" : "The connection failed during the WebSocket handshake";
		retry = true;
		return false;
	}

	size_t end;
	char buf[4096];
	while ((end = rbuf.find("\r\n\r\n")) == std::string::npos)
	{
		if (rbuf.size() > 16384) {
			err = "The server's answer to the WebSocket handshake was too long";
			retry = false;
			return false;
		}
		int n = tls.Read(buf, sizeof buf, deadline);
		if (n > 0) {
			rbuf.append(buf, size_t(n));
			continue;
		}
		// (-3: closed meanwhile; nobody waits to hear)
		err = n == -2 ? "The WebSocket handshake timed out" : "The connection closed during the WebSocket handshake";
		retry = n != -3;
		return false;
	}

	std::string head = rbuf.substr(0, end);
	rpos = end + 4;

	// the status line, then the headers (names in lower case)
	size_t eol = head.find("\r\n");
	std::string status = head.substr(0, eol);
	std::vector<std::pair<std::string, std::string>> headers;
	for (size_t at = eol; at != std::string::npos && at < head.size(); )
	{
		at += 2;
		size_t next = head.find("\r\n", at);
		std::string line = head.substr(at, next == std::string::npos ? std::string::npos : next - at);
		size_t colon = line.find(':');
		if (colon != std::string::npos) {
			std::string name = line.substr(0, colon), value = line.substr(colon + 1);
			for (auto& ch : name) ch = char(tolower((unsigned char) ch));
			size_t a = value.find_first_not_of(" \t"), b = value.find_last_not_of(" \t");
			headers.emplace_back(name, a == std::string::npos ? "" : value.substr(a, b - a + 1));
		}
		at = next;
	}
	auto header = [&headers](const char* name) {
		for (auto& h : headers)
			if (h.first == name)
				return h.second;
		return std::string();
	};
	auto lower = [](std::string s) {
		for (auto& ch : s) ch = char(tolower((unsigned char) ch));
		return s;
	};
	server = header("server");

	int httpStatus = 0;
	size_t sp = status.find(' ');
	if (status.compare(0, 5, "HTTP/") == 0 && sp != std::string::npos)
		httpStatus = atoi(status.c_str() + sp + 1);
	if (httpStatus != 101) {
		std::string what = sp != std::string::npos ? status.substr(sp + 1) : status;
		err = "The server refused the WebSocket connection: " + (what.empty() ? std::string("no answer") : what);
		code = httpStatus;
		retry = false;
		return false;
	}

	// the key's proof: base64(SHA-1(key + the protocol's GUID))
	std::string proof = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	unsigned char sha[EVP_MAX_MD_SIZE];
	unsigned int shaLen = 0;
	EVP_Digest(proof.data(), proof.size(), sha, &shaLen, EVP_sha1(), NULL);

	std::string connection = "," + lower(header("connection")) + ",";
	for (auto& ch : connection)
		if (ch == ' ' || ch == '\t') ch = ',';
	const char* bad = nullptr;
	if (lower(header("upgrade")) != "websocket")
		bad = "Upgrade";
	else if (connection.find(",upgrade,") == std::string::npos)
		bad = "Connection";
	else if (header("sec-websocket-accept") != Base64Encode(sha, shaLen))
		bad = "Sec-WebSocket-Accept";
	else if (!header("sec-websocket-extensions").empty())
		bad = "Sec-WebSocket-Extensions";
	else if (!header("sec-websocket-protocol").empty())
		bad = "Sec-WebSocket-Protocol";
	if (bad) {
		err = std::string("The server's WebSocket handshake was not valid (") + bad + ")";
		retry = false;
		return false;
	}
	return true;
}

void WebsocketClient::Connection::SendClose(int code)
{
	if (closeSent)
		return;
	wbuf += CloseFrame(code);
	closeSent = true;
	closeDeadline = NowMs() + CLOSE_TIMEOUT_MS;
}

// Takes one frame from what was read: 1, or 0 when it has not all arrived,
// or -1 when the server broke the protocol (a close is on its way then).
int WebsocketClient::Connection::ParseFrame()
{
	size_t avail = rbuf.size() - rpos;
	if (avail < 2)
		return 0;
	const unsigned char* p = (const unsigned char*) rbuf.data() + rpos;
	bool fin = (p[0] & 0x80) != 0;
	int opcode = p[0] & 0x0F;
	size_t len = p[1] & 0x7F, hdr = 2;

	// no extensions were agreed, and servers do not mask
	if ((p[0] & 0x70) || (p[1] & 0x80)) {
		SendClose(CloseCode::PROTOCOL_ERROR);
		return -1;
	}
	if (len == 126) {
		if (avail < 4)
			return 0;
		len = (size_t(p[2]) << 8) | p[3];
		hdr = 4;
	}
	else if (len == 127) {
		if (avail < 10)
			return 0;
		uint64_t l = 0;
		for (int i = 0; i < 8; i++)
			l = (l << 8) | p[2 + i];
		if (l > MAX_MESSAGE) {
			SendClose(CloseCode::MESSAGE_TOO_BIG);
			return -1;
		}
		len = size_t(l);
		hdr = 10;
	}

	bool control = (opcode & 8) != 0;
	if (control) {
		if (!fin || len > 125 || opcode > Opcode::PONG) {
			SendClose(CloseCode::PROTOCOL_ERROR);
			return -1;
		}
	}
	else if (opcode > Opcode::BINARY ||
		(opcode == Opcode::CONTINUATION) != (messageOpcode != 0)) {
		SendClose(CloseCode::PROTOCOL_ERROR);
		return -1;
	}
	else if (message.size() + len > MAX_MESSAGE) {
		SendClose(CloseCode::MESSAGE_TOO_BIG);
		return -1;
	}

	if (avail < hdr + len)
		return 0;
	const char* payload = rbuf.data() + rpos + hdr;
	rpos += hdr + len;

	switch (opcode)
	{
		case Opcode::CLOSE:
			if (len == 1) {
				SendClose(CloseCode::PROTOCOL_ERROR);
				return -1;
			}
			closeReceived = true;
			if (len >= 2) {
				remoteCode = (int((unsigned char) payload[0]) << 8) | (unsigned char) payload[1];
				remoteReason.assign(payload + 2, len - 2);
			}
			else {
				remoteCode = CloseCode::NO_STATUS;
			}
			// the close echoed (without a code when it came without one)
			if (!closeSent) {
				wbuf += len >= 2 ? CloseFrame(remoteCode) : Frame(Opcode::CLOSE, "", 0);
				closeSent = true;
				closeDeadline = NowMs() + CLOSE_TIMEOUT_MS;
			}
			break;

		case Opcode::PING:
			if (!closeSent)
				wbuf += Frame(Opcode::PONG, payload, len);
			break;

		case Opcode::PONG:
			break;

		default:
			if (opcode != Opcode::CONTINUATION)
				messageOpcode = opcode;
			message.append(payload, len);
			if (!fin)
				break;
			if (messageOpcode == Opcode::TEXT) {
				if (!ValidUtf8(message)) {
					SendClose(CloseCode::INVALID_PAYLOAD);
					return -1;
				}
				// (nothing more once this side is closing)
				if (!closeSent && !quiet)
					GetFrontend()->OnWebsocketMessage(id, message);
			}
			else {
				DbgPrintF("WebsocketClient: ignored a binary message of %u bytes", (unsigned) message.size());
			}
			message.clear();
			messageOpcode = 0;
			break;
	}
	return 1;
}

// Sends what it can of wbuf.  False when the connection failed.
bool WebsocketClient::Connection::Flush(bool& wantWrite)
{
	wantWrite = false;
	while (!wbuf.empty())
	{
		int n = SSL_write(tls.Ssl(), wbuf.data(), int(wbuf.size()));
		if (n > 0) {
			wbuf.erase(0, size_t(n));
			continue;
		}
		int e = SSL_get_error(tls.Ssl(), n);
		if (e == SSL_ERROR_WANT_WRITE) {
			wantWrite = true;
			return true;
		}
		// (it wants to read first: the loop polls for input anyway)
		return e == SSL_ERROR_WANT_READ;
	}
	return true;
}

// Reads what has arrived.  False when the connection has ended.
bool WebsocketClient::Connection::ReadAvailable()
{
	if (rpos > 65536 && rpos * 2 > rbuf.size()) {
		rbuf.erase(0, rpos);
		rpos = 0;
	}
	char buf[16384];
	// (at most a megabyte at a time, so frames are taken as they come)
	for (int i = 0; i < 64; i++)
	{
		int n = SSL_read(tls.Ssl(), buf, sizeof buf);
		if (n > 0) {
			rbuf.append(buf, size_t(n));
			continue;
		}
		int e = SSL_get_error(tls.Ssl(), n);
		return e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE;
	}
	return true;
}

void WebsocketClient::Connection::Loop()
{
	bool failed = false, ended = false;
	for (;;)
	{
		{
			std::lock_guard<std::mutex> g(lock);
			if (!closeSent)
				wbuf += queued;
			queued.clear();
			if (closeCode)
				SendClose(closeCode);
		}

		while (!failed && !closeReceived)
		{
			int r = ParseFrame();
			if (r < 0)
				failed = true;
			if (r <= 0)
				break;
		}
		if (failed) {
			rbuf.clear();
			rpos = 0;
		}
		// (what came with the end of the connection, its close frame
		// often, was taken just above)
		if (ended)
			break;

		bool wantWrite;
		if (!Flush(wantWrite))
			break;

		if (closeDeadline >= 0 && NowMs() >= closeDeadline)
			break;

		// TLS may hold decrypted bytes already: no waiting for those
		if (SSL_pending(tls.Ssl()) == 0)
		{
			int w = tls.Wait(TlsSocket::READABLE | (wantWrite ? TlsSocket::WRITABLE : 0), closeDeadline);
			if (w < 0)
				break;
			if (w != 1)
				continue;
		}

		// (the server ends the TCP connection after the close handshake)
		if (!ReadAvailable())
			ended = true;
	}
}

void WebsocketClient::Connection::Run()
{
	std::string err;
	int code = 0;
	bool tlsError = false, retry = false;
	TlsSocket::Failure why;
	TlsSocket::Interrupt in;
	in.waker = &waker;
	in.stop = [this] { return CloseRequested(); };
	tls.SetInterrupt(in);
	if (!waker.Create()) {
		err = "Could not set up the connection";
	}
	else if (!tls.Open(host, port, verify, TLS_STEP_TIMEOUT_MS, why)) {
		err = why.message;
		code = why.code;
		tlsError = why.tlsError;
		retry = why.retry;
	}
	else if (Handshake(err, code, retry)) {
		// open: a wake is for frames to send, or the close
		in.stop = nullptr;
		in.returnOnWake = true;
		tls.SetInterrupt(in);
		open = true;
		Loop();
		open = false;
		tls.Close();
		if (!quiet) {
			std::string text = "Close code: " + std::to_string(remoteCode);
			if (!remoteReason.empty())
				text += ", Close reason: " + remoteReason;
			DbgPrintF("WebsocketClient: connection %d closed. %s", id, text.c_str());
			GetFrontend()->OnWebsocketClose(id, remoteCode, text);
		}
		return;
	}

	// (closed while connecting: nobody waits to hear)
	if (CloseRequested() || quiet)
		return;
	if (!server.empty())
		err += " (server: " + server + ")";
	DbgPrintF("WebsocketClient: connection %d failed: %s", id, err.c_str());
	GetFrontend()->OnWebsocketFail(id, code, err, tlsError, retry);
}

void WebsocketClient::Init()
{
	TlsSocket::Init();
	std::lock_guard<std::mutex> g(m_mutex);
	m_bKilled = false;
}

void WebsocketClient::Kill()
{
	{
		std::lock_guard<std::mutex> g(m_mutex);
		if (m_bKilled)
			return;
		m_bKilled = true;
		m_conns.clear();
	}

	LiveConnections& live = Live();
	std::unique_lock<std::mutex> g(live.m);
	for (auto c : live.conns) {
		c->quiet = true;
		c->RequestClose(CloseCode::GOING_AWAY);
	}
	live.cv.wait_for(g, std::chrono::seconds(3), [&live] { return live.conns.empty(); });
}

int WebsocketClient::Connect(const std::string& uri)
{
	// wss://host[:port][/path]
	const std::string scheme = "wss://";
	if (uri.compare(0, scheme.size(), scheme) != 0) {
		DbgPrintF("WebsocketClient: not a wss:// URL: %s", uri.c_str());
		return -1;
	}
	size_t hostAt = scheme.size();
	size_t pathAt = uri.find_first_of("/?", hostAt);
	std::string authority = uri.substr(hostAt, pathAt == std::string::npos ? std::string::npos : pathAt - hostAt);
	std::string path = pathAt == std::string::npos ? "/" : uri.substr(pathAt);
	if (path[0] == '?')
		path = "/" + path;

	auto c = std::make_shared<Connection>();
	size_t colon = authority.rfind(':');
	if (colon != std::string::npos && authority.find(']', colon) == std::string::npos) {
		c->host = authority.substr(0, colon);
		c->port = authority.substr(colon + 1);
	}
	else {
		c->host = authority;
		c->port = "443";
	}
	if (c->host.size() > 2 && c->host.front() == '[' && c->host.back() == ']')
		c->host = c->host.substr(1, c->host.size() - 2);
	if (c->host.empty() || c->port.empty() || c->port.find_first_not_of("0123456789") != std::string::npos) {
		DbgPrintF("WebsocketClient: cannot use the URL %s", uri.c_str());
		return -1;
	}
	c->path = path;
	c->hostHeader = c->port == "443" ? authority.substr(0, colon == std::string::npos ? std::string::npos : colon) : authority;
	c->userAgent = GetClientConfig()->GetUserAgent();
	c->verify = GetLocalSettings()->EnableTLSVerification();

	{
		std::lock_guard<std::mutex> g(m_mutex);
		if (m_bKilled)
			return -1;
		c->id = m_nextId++;
		m_conns[c->id] = c;
	}
	{
		LiveConnections& live = Live();
		std::lock_guard<std::mutex> g(live.m);
		live.conns.insert(c.get());
	}
	DbgPrintF("WebsocketClient: connection %d to %s", c->id, uri.c_str());

	std::thread([c] {
		c->Run();
		LiveConnections& live = Live();
		{
			std::lock_guard<std::mutex> g(live.m);
			live.conns.erase(c.get());
		}
		live.cv.notify_all();
	}).detach();
	return c->id;
}

void WebsocketClient::Close(int id, int code)
{
	std::shared_ptr<Connection> c;
	{
		std::lock_guard<std::mutex> g(m_mutex);
		auto it = m_conns.find(id);
		if (it == m_conns.end())
			return;
		c = it->second;
		m_conns.erase(it);
	}
	c->RequestClose(code);
}

void WebsocketClient::SendMsg(int id, const std::string& msg)
{
	std::shared_ptr<Connection> c;
	{
		std::lock_guard<std::mutex> g(m_mutex);
		auto it = m_conns.find(id);
		if (it == m_conns.end())
			return;
		c = it->second;
	}
	if (!c->open)
		return;
	std::string frame = Frame(Opcode::TEXT, msg.data(), msg.size());
	{
		std::lock_guard<std::mutex> g(c->lock);
		c->queued += frame;
	}
	c->waker.Wake();
}
