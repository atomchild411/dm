#pragma once
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

namespace CloseCode
{
	enum {
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
	};
}

#ifdef __sgi
// websocketpp gives DNS, the TCP connect, the TLS handshake and the
// WebSocket handshake 5 s each.  An R10000 doing other work can take longer
// than that over the TLS handshake alone, so they get 30 s here.
struct WSClientConfig : public websocketpp::config::asio_tls_client
{
	typedef WSClientConfig type;
	typedef websocketpp::config::asio_tls_client base;

	typedef base::concurrency_type concurrency_type;
	typedef base::request_type request_type;
	typedef base::response_type response_type;
	typedef base::message_type message_type;
	typedef base::con_msg_manager_type con_msg_manager_type;
	typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;
	typedef base::alog_type alog_type;
	typedef base::elog_type elog_type;
	typedef base::rng_type rng_type;

	struct transport_config : public base::transport_config
	{
		typedef type::concurrency_type concurrency_type;
		typedef type::alog_type alog_type;
		typedef type::elog_type elog_type;
		typedef type::request_type request_type;
		typedef type::response_type response_type;
		typedef websocketpp::transport::asio::tls_socket::endpoint socket_type;

		static const long timeout_dns_resolve = 30000;
		static const long timeout_connect = 30000;
		static const long timeout_socket_post_init = 30000; // the TLS handshake
	};

	typedef websocketpp::transport::asio::endpoint<transport_config> transport_type;

	static const long timeout_open_handshake = 30000;
};
typedef websocketpp::client<WSClientConfig> WSClient;
#else
typedef websocketpp::client<websocketpp::config::asio_tls_client> WSClient;
#endif
typedef websocketpp::lib::shared_ptr<websocketpp::lib::thread> WSThreadSharedPtr;
typedef websocketpp::lib::asio::ssl::context AsioSslContext;
typedef websocketpp::lib::shared_ptr<AsioSslContext> AsioSslContextSharedPtr;
typedef websocketpp::transport::asio::tls_socket::connection::socket_type AsioSocketType;

class WSConnectionMetadata
{
public:
	enum eStatus
	{
		CONNECTING,
		OPEN,
		FAILED,
		CLOSED,
	};

	typedef websocketpp::lib::shared_ptr<WSConnectionMetadata> Pointer;
 
	WSConnectionMetadata(int id, websocketpp::connection_hdl hdl, std::string uri)
	  : m_id(id)
	  , m_hdl(hdl)
	  , m_status(CONNECTING)
	  , m_uri(uri)
	  , m_server("N/A")
	{}

	void OnOpen(WSClient* c, websocketpp::connection_hdl hdl);
	void OnFail(WSClient* c, websocketpp::connection_hdl hdl);
	void OnClose(WSClient* c, websocketpp::connection_hdl hdl);
	void OnMessage(websocketpp::connection_hdl hdl, WSClient::message_ptr msg);

	websocketpp::connection_hdl GetHDL() const
	{
		return m_hdl;
	}

	int GetID() const
	{
		return m_id;
	}

	eStatus GetStatus() const
	{
		return m_status;
	}

private:
	int m_id;
	websocketpp::connection_hdl m_hdl;
	eStatus m_status;
	std::string m_uri;
	std::string m_server;
	std::string m_errorReason;
};

struct WebsocketMessageParm
{
	int m_gatewayId;
	std::string m_payload;
};

class WebsocketClient
{
public:
	WebsocketClient();
	~WebsocketClient();

	void Init();

	void Kill();

	// Returns a connection ID.
	int Connect(const std::string& uri);

	// Gets metadata about a connection.
	WSConnectionMetadata::Pointer GetMetadata(int ID);

	// Closes a connection by ID.
	void Close(int ID, websocketpp::close::status::value code);

	// Send a message to a connection.
	void SendMsg(int id, const std::string& msg);

private:
	typedef std::map<int, WSConnectionMetadata::Pointer> WSConnList;

	WSClient m_endpoint;
	WSThreadSharedPtr m_thread;
	WSConnList m_connList;
	int m_nextId = 0;
	bool m_bKilled = true;

	// Handle TLS initialization.
	AsioSslContextSharedPtr HandleTLSInit(websocketpp::connection_hdl hdl);

	// Handle socket initialization.
	void HandleSocketInit(websocketpp::connection_hdl hdl, AsioSocketType& socketType);
};

WebsocketClient* GetWebsocketClient();

