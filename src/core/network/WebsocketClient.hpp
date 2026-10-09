#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace CloseCode
{
	enum {
		NORMAL = 1000,
		GOING_AWAY = 1001,
		PROTOCOL_ERROR = 1002,
		NO_STATUS = 1005,       // (a close frame without a code)
		ABNORMAL = 1006,        // (no close frame: the connection dropped)
		INVALID_PAYLOAD = 1007,
		MESSAGE_TOO_BIG = 1009,

		UNKNOWN_ERROR = 4000,
		UNKNOWN_OPCODE,
		DECODE_ERROR,
		NOT_AUTHENTICATED,
		AUTHENTICATION_FAILED,
		ALREADY_AUTHENTICATED,
		INVALID_SEQ = 4007,
		RATE_LIMITED,
		SESSION_TIMED_OUT,
		INVALID_SHARD,
		SHARDING_REQUIRED,
		INVALID_API_VERSION,
		INVALID_INTENT,
		DISALLOWED_INTENT,

		LOG_ON_AGAIN = 5000,
		// (ours: the client stopped reconnecting by itself, DiscordInstance)
		TOO_MANY_LOGINS = 5001,
	};
}

// WebSocket connections (RFC 6455) over TLS, one thread each.  What arrives
// goes to the front end, from that thread: OnWebsocketMessage for each text
// message, then OnWebsocketClose once an open connection has ended (also one
// this side closed), or OnWebsocketFail when it never opened.
class WebsocketClient
{
public:
	void Init();

	// Closes what is still open (1001, "going away") and waits a little for
	// the close handshakes, those of connections closed just before too.
	void Kill();

	// Starts connecting to a wss:// URL.  Returns the connection's ID, or -1
	// for a URL it cannot use.
	int Connect(const std::string& uri);

	// Closes a connection with that code.  One still connecting is given up.
	void Close(int id, int code);

	// Sends a text message on an open connection (else it is dropped).
	void SendMsg(int id, const std::string& msg);

	struct Connection;

private:
	std::mutex m_mutex;
	std::map<int, std::shared_ptr<Connection>> m_conns;
	int m_nextId = 0;
	bool m_bKilled = true;
};

WebsocketClient* GetWebsocketClient();
