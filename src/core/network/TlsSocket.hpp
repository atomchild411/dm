#pragma once

#include <cstdint>
#include <functional>
#include <string>

typedef struct ssl_st SSL;

// How long each step of opening a connection may take (the TCP connect, the
// TLS handshake, the first answer).  An R10000 doing other work can take
// longer than 5 s over the TLS handshake alone, so IRIX gives 30 s.
#ifdef __sgi
static const int TLS_STEP_TIMEOUT_MS = 30000;
#else
static const int TLS_STEP_TIMEOUT_MS = 5000;
#endif

int64_t NowMs();

// Wakes a thread waiting in TlsSocket::Wait from another thread: a pipe, or
// on Windows (whose poll takes only sockets) a UDP socket that sends to
// itself.
class SocketWaker
{
public:
	~SocketWaker();
	bool Create();
	intptr_t Fd() const;
	void Wake();
	void Drain();

private:
	intptr_t m_fds[2] = { -1, -1 };
};

// One TCP connection with TLS over it, the server's certificate checked
// (for the host name too) against the system's trust or the bundled roots.
// Used by one thread at a time.
class TlsSocket
{
public:
	// Why a connection could not be opened.
	struct Failure
	{
		std::string message;
		int code = 0;           // errno, or getaddrinfo's code
		bool tlsError = false;  // the certificate was not accepted
		bool retry = false;     // the network's doing: worth another try later
	};

	// Waits can be woken (waker) and given up (stop() says so); a wake that
	// is no stop ends the wait too when returnOnWake.
	struct Interrupt
	{
		SocketWaker* waker = nullptr;
		std::function<bool()> stop;
		bool returnOnWake = false;
	};

	enum { READABLE = 1, WRITABLE = 2 };

	~TlsSocket();

	// Once, before the first connection: sockets on Windows, SIGPIPE
	// ignored elsewhere.
	static void Init();

	void SetInterrupt(const Interrupt& in) { m_interrupt = in; }

	// TCP, then TLS (SNI and the host name for host); each step may take
	// stepMs.  False with why, or quietly when interrupted.
	bool Open(const std::string& host, const std::string& port, bool verify, int stepMs, Failure& why);
	void Close();
	bool IsOpen() const { return m_ssl != nullptr; }

	// Until the socket is READABLE and/or WRITABLE: 1; the deadline (-1:
	// none) passed: 0; stopped or failed: -1; woken (returnOnWake): 2.
	int Wait(int what, int64_t deadline);

	// Some bytes: their count; the connection ended: 0; failed: -1; the
	// deadline passed: -2; interrupted: -3.
	int Read(char* buf, int size, int64_t deadline);

	// All of it, or false (timedOut says whether the deadline passed).
	bool Write(const char* data, size_t size, int64_t deadline, bool& timedOut);

	SSL* Ssl() const { return m_ssl; }
	intptr_t Fd() const { return m_fd; }

	// The last socket error, as text.
	static std::string ErrorText(int error);

private:
	bool ConnectTcp(const std::string& host, const std::string& port, int stepMs, Failure& why);
	bool Stopped() const { return m_interrupt.stop && m_interrupt.stop(); }

	intptr_t m_fd = -1;
	SSL* m_ssl = nullptr;
	Interrupt m_interrupt;
};
