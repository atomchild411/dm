#include "TlsSocket.hpp"

#include <chrono>
#include <cstring>
#include <mutex>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Sock;
#define CloseSock closesocket
#define PollSocks WSAPoll
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Sock;
#define CloseSock close
#define PollSocks poll
#endif

void UseSystemTrust(SSL_CTX* ctx); // posix/SystemTrust.cpp

int64_t NowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int LastSockError()
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

std::string TlsSocket::ErrorText(int e)
{
#ifdef _WIN32
	char buf[256] = { 0 };
	DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD) e, 0, buf, sizeof buf, NULL);
	while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == '.'))
		buf[--n] = 0;
	return n ? std::string(buf) : "socket error " + std::to_string(e);
#else
	return strerror(e);
#endif
}

// The failures worth another try later (the network, not the server's
// answer or its certificate).
static bool IsRetryable(int e)
{
#ifdef _WIN32
	return e == WSAETIMEDOUT || e == WSAECONNRESET || e == WSAECONNREFUSED || e == WSAENETUNREACH ||
		e == WSAEHOSTUNREACH || e == WSAECONNABORTED;
#else
	return e == ETIMEDOUT || e == ECONNRESET || e == ECONNREFUSED || e == ENETUNREACH || e == EHOSTUNREACH;
#endif
}

static bool SetNonBlocking(Sock s)
{
#ifdef _WIN32
	u_long on = 1;
	return ioctlsocket(s, FIONBIO, &on) == 0;
#else
	int fl = fcntl(s, F_GETFL, 0);
	return fl >= 0 && fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

static std::string OpenSslErrorText()
{
	unsigned long e = ERR_get_error();
	ERR_clear_error();
	if (!e)
		return "";
	char buf[256];
	ERR_error_string_n(e, buf, sizeof buf);
	return buf;
}

// ---- SocketWaker ----------------------------------------------------------

#ifdef _WIN32
bool SocketWaker::Create()
{
	Sock s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == INVALID_SOCKET)
		return false;
	m_fds[0] = (intptr_t) s;
	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	int len = sizeof a;
	return bind(s, (sockaddr*) &a, sizeof a) == 0 && getsockname(s, (sockaddr*) &a, &len) == 0 &&
		connect(s, (sockaddr*) &a, sizeof a) == 0 && SetNonBlocking(s);
}
intptr_t SocketWaker::Fd() const { return m_fds[0]; }
void SocketWaker::Wake() { send((Sock) m_fds[0], "x", 1, 0); }
void SocketWaker::Drain() { char b[64]; while (recv((Sock) m_fds[0], b, sizeof b, 0) > 0) {} }
SocketWaker::~SocketWaker() { if (m_fds[0] != -1) CloseSock((Sock) m_fds[0]); }
#else
bool SocketWaker::Create()
{
	int fds[2];
	if (pipe(fds) != 0)
		return false;
	m_fds[0] = fds[0];
	m_fds[1] = fds[1];
	return SetNonBlocking(fds[0]) && SetNonBlocking(fds[1]);
}
intptr_t SocketWaker::Fd() const { return m_fds[0]; }
void SocketWaker::Wake() { ssize_t r = write((int) m_fds[1], "x", 1); (void) r; }
void SocketWaker::Drain() { char b[64]; while (read((int) m_fds[0], b, sizeof b) > 0) {} }
SocketWaker::~SocketWaker()
{
	if (m_fds[0] != -1) close((int) m_fds[0]);
	if (m_fds[1] != -1) close((int) m_fds[1]);
}
#endif

// ---- TlsSocket ------------------------------------------------------------

void TlsSocket::Init()
{
#ifdef _WIN32
	static WSADATA wsa;
	static bool done = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
	(void) done;
#else
	// a write to a connection the server has dropped fails, it does not
	// end the program
	signal(SIGPIPE, SIG_IGN);
#endif
}

// One TLS context for the program (two: with and without the certificate
// check), set up once: the roots are read once, not for each connection.
static SSL_CTX* SharedContext(bool verify)
{
	static std::mutex lock;
	static SSL_CTX* ctx[2];
	std::lock_guard<std::mutex> g(lock);
	SSL_CTX*& c = ctx[verify ? 1 : 0];
	if (!c) {
		c = SSL_CTX_new(TLS_client_method());
		if (c) {
			SSL_CTX_set_min_proto_version(c, TLS1_2_VERSION);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
			// a server that ends the connection without TLS's goodbye has
			// ended it (HTTP's framing tells a cut-short answer)
			SSL_CTX_set_options(c, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
			if (verify) {
				SSL_CTX_set_default_verify_paths(c);
				UseSystemTrust(c);
				SSL_CTX_set_verify(c, SSL_VERIFY_PEER, nullptr);
			}
		}
	}
	return c;
}

TlsSocket::~TlsSocket()
{
	Close();
}

void TlsSocket::Close()
{
	if (m_ssl) {
		SSL_shutdown(m_ssl);
		SSL_free(m_ssl);
		ERR_clear_error();
		m_ssl = nullptr;
	}
	if (m_fd != -1) {
		CloseSock((Sock) m_fd);
		m_fd = -1;
	}
}

int TlsSocket::Wait(int what, int64_t deadline)
{
	for (;;)
	{
		if (Stopped())
			return -1;
		int timeout = -1;
		if (deadline >= 0) {
			int64_t left = deadline - NowMs();
			if (left <= 0)
				return 0;
			timeout = int(left);
		}
		pollfd p[2] = {};
		p[0].fd = (Sock) m_fd;
		p[0].events = short(((what & READABLE) ? POLLIN : 0) | ((what & WRITABLE) ? POLLOUT : 0));
		int n = 1;
		if (m_interrupt.waker) {
			p[1].fd = (Sock) m_interrupt.waker->Fd();
			p[1].events = POLLIN;
			n = 2;
		}
		int r = PollSocks(p, n, timeout);
		if (r < 0) {
#ifndef _WIN32
			if (errno == EINTR)
				continue;
#endif
			return -1;
		}
		if (p[0].revents)
			return 1;
		if (n == 2 && p[1].revents) {
			m_interrupt.waker->Drain();
			if (Stopped())
				return -1;
			if (m_interrupt.returnOnWake)
				return 2;
		}
	}
}

bool TlsSocket::ConnectTcp(const std::string& host, const std::string& port, int stepMs, Failure& why)
{
	addrinfo hints = {}, *res = nullptr;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int r = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
	if (r != 0) {
		why.message = "Could not look up " + host + ": " + gai_strerror(r);
		why.code = r;
		why.retry = false;
		return false;
	}

	int lastError = 0;
	bool timedOut = false;
	for (addrinfo* ai = res; ai && m_fd == -1; ai = ai->ai_next)
	{
		Sock s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
#ifdef _WIN32
		if (s == INVALID_SOCKET) {
#else
		if (s < 0) {
#endif
			lastError = LastSockError();
			continue;
		}
		if (!SetNonBlocking(s)) {
			lastError = LastSockError();
			CloseSock(s);
			continue;
		}
		if (connect(s, ai->ai_addr, (int) ai->ai_addrlen) != 0) {
			int e = LastSockError();
#ifdef _WIN32
			bool pending = e == WSAEWOULDBLOCK;
#else
			bool pending = e == EINPROGRESS;
#endif
			if (!pending) {
				lastError = e;
				CloseSock(s);
				continue;
			}
			m_fd = (intptr_t) s;
			int64_t deadline = NowMs() + stepMs;
			int w;
			do
				w = Wait(WRITABLE, deadline);
			while (w == 2);
			m_fd = -1;
			if (w <= 0) {
				CloseSock(s);
				if (w < 0)
					break;
				timedOut = true;
				continue;
			}
			int soError = 0;
			socklen_t len = sizeof soError;
			getsockopt(s, SOL_SOCKET, SO_ERROR, (char*) &soError, &len);
			if (soError != 0) {
				lastError = soError;
				CloseSock(s);
				continue;
			}
		}
		int on = 1;
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*) &on, sizeof on);
		m_fd = (intptr_t) s;
	}
	freeaddrinfo(res);

	if (m_fd != -1)
		return true;
	if (lastError) {
		why.message = "Could not connect to " + host + ": " + ErrorText(lastError);
		why.code = lastError;
		why.retry = IsRetryable(lastError);
	}
	else {
		why.message = "Could not connect to " + host + ": timed out";
		why.code = 0;
		why.retry = timedOut;
	}
	return false;
}

bool TlsSocket::Open(const std::string& host, const std::string& port, bool verify, int stepMs, Failure& why)
{
	Close();
	if (!ConnectTcp(host, port, stepMs, why))
		return false;

	SSL_CTX* ctx = SharedContext(verify);
	m_ssl = ctx ? SSL_new(ctx) : nullptr;
	if (!m_ssl || !SSL_set_fd(m_ssl, (int) m_fd) ||
		!SSL_set_tlsext_host_name(m_ssl, host.c_str()) ||
		(verify && !SSL_set1_host(m_ssl, host.c_str()))) {
		why.message = "Could not set up TLS for " + host + " " + OpenSslErrorText();
		why.retry = false;
		Close();
		return false;
	}
	SSL_set_mode(m_ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

	int64_t deadline = NowMs() + stepMs;
	for (;;)
	{
		int r = SSL_connect(m_ssl);
		if (r == 1)
			return true;
		int e = SSL_get_error(m_ssl, r);
		if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
			int w = Wait(e == SSL_ERROR_WANT_READ ? READABLE : WRITABLE, deadline);
			if (w < 0) {
				Close();
				return false;
			}
			if (w == 0) {
				why.message = "The TLS handshake with " + host + " timed out";
				why.retry = true;
				Close();
				return false;
			}
			continue;
		}
		long vr = SSL_get_verify_result(m_ssl);
		if (verify && vr != X509_V_OK) {
			why.message = "The certificate of " + host + " was not accepted: " + X509_verify_cert_error_string(vr);
			why.tlsError = true;
			why.retry = false;
		}
		else {
			std::string text = OpenSslErrorText();
			if (text.empty() && e == SSL_ERROR_SYSCALL)
				text = ErrorText(LastSockError());
			why.message = "The TLS handshake with " + host + " failed" + (text.empty() ? "" : ": " + text);
			why.retry = true;
		}
		// (no close_notify to a server whose handshake failed)
		SSL_free(m_ssl);
		m_ssl = nullptr;
		Close();
		return false;
	}
}

int TlsSocket::Read(char* buf, int size, int64_t deadline)
{
	for (;;)
	{
		int n = SSL_read(m_ssl, buf, size);
		if (n > 0)
			return n;
		int e = SSL_get_error(m_ssl, n);
		if (e == SSL_ERROR_ZERO_RETURN)
			return 0;
		if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
			ERR_clear_error();
			// (a server that just drops the connection: its end too)
			return e == SSL_ERROR_SYSCALL && n == 0 ? 0 : -1;
		}
		int w = Wait(e == SSL_ERROR_WANT_READ ? READABLE : WRITABLE, deadline);
		if (w == 0)
			return -2;
		if (w < 0)
			return -3;
	}
}

bool TlsSocket::Write(const char* data, size_t size, int64_t deadline, bool& timedOut)
{
	timedOut = false;
	size_t done = 0;
	while (done < size)
	{
		int n = SSL_write(m_ssl, data + done, int(size - done));
		if (n > 0) {
			done += size_t(n);
			continue;
		}
		int e = SSL_get_error(m_ssl, n);
		if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
			ERR_clear_error();
			return false;
		}
		int w = Wait(e == SSL_ERROR_WANT_READ ? READABLE : WRITABLE, deadline);
		if (w <= 0) {
			timedOut = w == 0;
			return false;
		}
	}
	return true;
}
