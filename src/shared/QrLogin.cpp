#include "QrLogin.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <boost/base64/base64.hpp>
#include <nlohmann/json.h>
#include <qrcodegen/qrcodegen.h>

#include "network/DiscordAPI.hpp"
#include "network/HTTPClient.hpp"
#include "network/WebsocketClient.hpp"
#include "posix/MainQueue.hpp"
#include "Timers.hpp"

using Json = nlohmann::json;

namespace
{
	const char* const GATEWAY_URL = "wss://remote-auth-gateway.discord.gg/?v=2";

	struct State
	{
		std::function<void()> changed;
		std::function<void(const std::string&)> loggedIn;

		EVP_PKEY* key = nullptr;
		std::string publicKey; // base64 SubjectPublicKeyInfo (DER)
		int heartbeat = 0;     // Timers id
		int heartbeatMs = 0;
		std::vector<uint8_t> qr; // qrcodegen's buffer; empty until a code arrives
		std::string status;
		bool waitingForPhone = false;
		bool loggingIn = false;   // the ticket is being exchanged: keep things as they are
		bool failed = false;      // an error is shown: wait for Retry
		std::string ticket;       // the phone's, to exchange for the token
		QrLogin::Captcha captcha; // what Discord wants solved first (none: no site key)
		int generation = 0;       // bumped by every new login
	};

	State* g_state;
	std::atomic<int> g_gateway(-1);
	int g_generation = 0;
	const std::string g_empty;
	const QrLogin::Captcha g_noCaptcha;

	void TicketResponse(NetRequest* req);

	// Exchanges the ticket for the token (with the captcha's answer, when
	// Discord asked for one).
	void SendTicket(const std::vector<std::pair<std::string, std::string>>& headers)
	{
		Json body;
		body["ticket"] = g_state->ticket;
		GetHTTPClient()->PerformRequest(
			true,
			NetRequest::POST_JSON,
			GetDiscordAPI() + "users/@me/remote-auth/login",
			0,
			0,
			body.dump(),
			"",
			"",
			TicketResponse,
			nullptr,
			0,
			headers
		);
	}

	void Changed()
	{
		if (g_state && g_state->changed)
			g_state->changed();
	}

	void SetStatus(const std::string& text)
	{
		if (!g_state)
			return;
		g_state->status = text;
		Changed();
	}

	std::string Base64(const uint8_t* data, size_t n, bool url)
	{
		std::string out(base64::encoded_size(n), '\0');
		out.resize(base64::encode(&out[0], data, n));
		if (url) {
			for (auto& ch : out) {
				if (ch == '+') ch = '-';
				else if (ch == '/') ch = '_';
			}
			while (!out.empty() && out.back() == '=')
				out.pop_back();
		}
		return out;
	}

	std::vector<uint8_t> Unbase64(const std::string& s)
	{
		std::vector<uint8_t> out(base64::decoded_size(s.size()) + 4);
		auto r = base64::decode(out.data(), s.c_str(), s.size());
		out.resize(r.first);
		return out;
	}

	// RSA-OAEP with SHA-256, as the remote-auth gateway encrypts.
	bool Decrypt(EVP_PKEY* key, const std::string& b64, std::vector<uint8_t>& out)
	{
		std::vector<uint8_t> in = Unbase64(b64);
		EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(key, NULL);
		bool ok = ctx && EVP_PKEY_decrypt_init(ctx) > 0 &&
			EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) > 0 &&
			EVP_PKEY_CTX_set_rsa_oaep_md(ctx, EVP_sha256()) > 0 &&
			EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, EVP_sha256()) > 0;
		size_t n = 0;
		ok = ok && EVP_PKEY_decrypt(ctx, NULL, &n, in.data(), in.size()) > 0;
		if (ok) {
			out.resize(n);
			ok = EVP_PKEY_decrypt(ctx, out.data(), &n, in.data(), in.size()) > 0;
			out.resize(ok ? n : 0);
		}
		EVP_PKEY_CTX_free(ctx);
		return ok;
	}

	// A 2048-bit RSA key and its public half as base64 DER.  Slow on an old
	// CPU: runs on its own thread.
	EVP_PKEY* MakeKey(std::string& publicKey)
	{
		EVP_PKEY* key = nullptr;
		EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
		if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0 ||
			EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0 ||
			EVP_PKEY_keygen(ctx, &key) <= 0)
			key = nullptr;
		EVP_PKEY_CTX_free(ctx);
		if (!key)
			return nullptr;

		unsigned char* der = nullptr;
		int len = i2d_PUBKEY(key, &der);
		if (len <= 0) {
			EVP_PKEY_free(key);
			return nullptr;
		}
		publicKey = Base64(der, (size_t) len, false);
		OPENSSL_free(der);
		return key;
	}

	void Send(const Json& j)
	{
		int id = g_gateway;
		if (id >= 0)
			GetWebsocketClient()->SendMsg(id, j.dump());
	}

	void StopHeartbeat()
	{
		if (g_state && g_state->heartbeat) {
			Timers::Cancel(g_state->heartbeat);
			g_state->heartbeat = 0;
		}
	}

	void Heartbeat()
	{
		if (!g_state)
			return;
		g_state->heartbeat = Timers::After(g_state->heartbeatMs, Heartbeat);
		Json j;
		j["op"] = "heartbeat";
		Send(j);
	}

	void CloseGateway()
	{
		StopHeartbeat();
		int id = g_gateway.exchange(-1);
		if (id >= 0)
			GetWebsocketClient()->Close(id, websocketpp::close::status::normal);
	}

	void Connect()
	{
		if (!g_state)
			return;
		g_state->qr.clear();
		g_state->waitingForPhone = false;
		g_state->loggingIn = false;
		g_state->failed = false;
		g_state->ticket.clear();
		g_state->captcha = QrLogin::Captcha();
		SetStatus("Connecting to Discord\xe2\x80\xa6");
		int id = GetWebsocketClient()->Connect(GATEWAY_URL);
		g_gateway = id;
		if (id < 0)
			SetStatus("Could not reach Discord's login service.  Check the network, then try again.");
	}

	void ReconnectAfter(int ms)
	{
		int gen = g_state->generation;
		Timers::After(ms, [gen] {
			if (g_state && g_state->generation == gen)
				Connect();
		});
	}

	// Shows why the login failed and waits for Retry.
	void Fail(const std::string& why, const std::string& detail)
	{
		if (!g_state)
			return;
		fprintf(stderr, "dm: QR login failed: %s%s%s\n", why.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
		g_state->failed = true;
		g_state->loggingIn = false;
		CloseGateway();
		g_state->qr.clear();
		SetStatus(why);
	}

	// Ends the login: the connection closed, the key freed, the state gone.
	State* Detach()
	{
		State* s = g_state;
		if (!s)
			return nullptr;
		g_state = nullptr;
		int id = g_gateway.exchange(-1);
		if (id >= 0)
			GetWebsocketClient()->Close(id, websocketpp::close::status::normal);
		if (s->heartbeat)
			Timers::Cancel(s->heartbeat);
		if (s->key)
			EVP_PKEY_free(s->key);
		return s;
	}

	// The exchange of the ticket for the token comes back here (from the
	// HTTP thread, through the UI thread).
	void TicketResponse(NetRequest* req)
	{
		int result = req->result;
		std::string response = req->response;
		MainQueue::Post([result, response] {
			if (!g_state)
				return;
			if (result == 200) {
				try {
					Json j = Json::parse(response);
					std::vector<uint8_t> token;
					if (j.contains("encrypted_token") && Decrypt(g_state->key, j["encrypted_token"], token)) {
						State* s = Detach();
						auto loggedIn = s->loggedIn;
						delete s;
						loggedIn(std::string(token.begin(), token.end()));
						return;
					}
				}
				catch (...) {}
				Fail("Discord's answer could not be read.\nTry again, or log in with a token.", "unreadable 200 response");
				return;
			}
			// the error's code and message (never anything secret)
			std::string detail = "HTTP " + std::to_string(result);
			try {
				Json j = Json::parse(response);
				if (j.contains("code"))
					detail += " code " + j["code"].dump();
				if (j.contains("message"))
					detail += " message " + j["message"].dump();
				if (j.contains("captcha_key"))
					detail += " captcha " + j["captcha_key"].dump() + " service " + j.value("captcha_service", std::string());
			}
			catch (...) {
				detail += " body " + response.substr(0, 200);
			}
			try {
				Json j = Json::parse(response);
				if (j.contains("captcha_sitekey")) {
					QrLogin::Captcha& c = g_state->captcha;
					c.service = j.value("captcha_service", std::string());
					c.sitekey = j.value("captcha_sitekey", std::string());
					c.rqdata = j.value("captcha_rqdata", std::string());
					c.rqtoken = j.value("captcha_rqtoken", std::string());
				}
			}
			catch (...) {}
			if (response.find("captcha") != std::string::npos)
				Fail("Discord wants a captcha for this login, which Discord\nMessenger cannot show.  Log in with a token instead.", detail);
			else
				Fail("Discord refused the login (" + std::to_string(result) + ").\nTry again, or log in with a token.", detail);
		});
	}
}

void QrLogin::Start(std::function<void()> changed, std::function<void(const std::string&)> loggedIn)
{
	if (g_state)
		return;
	State* s = new State;
	g_state = s;
	s->changed = changed;
	s->loggedIn = loggedIn;
	s->generation = ++g_generation;
	SetStatus("Making a key for this login\xe2\x80\xa6\n ");

	// the key takes a while on an old CPU: off the UI thread
	int gen = s->generation;
	std::thread([gen] {
		std::string pub;
		EVP_PKEY* key = MakeKey(pub);
		MainQueue::Post([gen, key, pub] {
			if (!g_state || g_state->generation != gen) {
				if (key) EVP_PKEY_free(key);
				return;
			}
			if (!key) {
				SetStatus("Could not make a key for the login (OpenSSL).");
				return;
			}
			g_state->key = key;
			g_state->publicKey = pub;
			Connect();
		});
	}).detach();
}

void QrLogin::Retry()
{
	Connect();
}

void QrLogin::Stop()
{
	delete Detach();
}

bool QrLogin::Active()
{
	return g_state != nullptr;
}

const std::string& QrLogin::StatusText()
{
	return g_state ? g_state->status : g_empty;
}

int QrLogin::CodeSize()
{
	return g_state && !g_state->qr.empty() ? qrcodegen_getSize(g_state->qr.data()) : 0;
}

bool QrLogin::CodeModule(int x, int y)
{
	return g_state && !g_state->qr.empty() && qrcodegen_getModule(g_state->qr.data(), x, y);
}

bool QrLogin::Scanned()
{
	return g_state && g_state->waitingForPhone;
}

const QrLogin::Captcha& QrLogin::PendingCaptcha()
{
	return g_state && g_state->failed ? g_state->captcha : g_noCaptcha;
}

void QrLogin::SolveCaptcha(const std::string& answer)
{
	State* s = g_state;
	if (!s || s->ticket.empty() || answer.empty())
		return;
	std::vector<std::pair<std::string, std::string>> headers;
	headers.push_back(std::make_pair(std::string("X-Captcha-Key"), answer));
	if (!s->captcha.rqtoken.empty())
		headers.push_back(std::make_pair(std::string("X-Captcha-Rqtoken"), s->captcha.rqtoken));
	s->captcha = QrLogin::Captcha();
	s->failed = false;
	s->loggingIn = true;
	SetStatus("Logging in\xe2\x80\xa6");
	SendTicket(headers);
}

bool QrLogin::Failed()
{
	return g_state && g_state->failed;
}

int QrLogin::GatewayId()
{
	return g_gateway;
}

void QrLogin::OnGatewayMessage(const std::string& payload)
{
	State* s = g_state;
	if (!s)
		return;

	Json j;
	try {
		j = Json::parse(payload);
	}
	catch (...) {
		return;
	}
	std::string op = j.value("op", "");
	if (op != "heartbeat_ack")
		fprintf(stderr, "dm: QR login: %s\n", op.c_str());

	if (op == "hello")
	{
		s->heartbeatMs = j.value("heartbeat_interval", 41250);
		StopHeartbeat();
		s->heartbeat = Timers::After(s->heartbeatMs, Heartbeat);
		Json init;
		init["op"] = "init";
		init["encoded_public_key"] = s->publicKey;
		Send(init);
	}
	else if (op == "nonce_proof")
	{
		// prove we hold the key: the SHA-256 of the decrypted nonce
		std::vector<uint8_t> nonce;
		if (!Decrypt(s->key, j.value("encrypted_nonce", ""), nonce)) {
			SetStatus("The login service's challenge could not be decrypted.");
			return;
		}
		unsigned char digest[32];
		unsigned int dlen = 0;
		EVP_Digest(nonce.data(), nonce.size(), digest, &dlen, EVP_sha256(), NULL);
		Json proof;
		proof["op"] = "nonce_proof";
		proof["proof"] = Base64(digest, dlen, true);
		Send(proof);
	}
	else if (op == "pending_remote_init")
	{
		// The code must be for our key (its SHA-256): a code for another key
		// would log the phone in to whoever holds that key.
		std::string fingerprint = j.value("fingerprint", "");
		std::vector<uint8_t> der = Unbase64(s->publicKey);
		unsigned char digest[32];
		unsigned int dlen = 0;
		EVP_Digest(der.data(), der.size(), digest, &dlen, EVP_sha256(), NULL);
		if (fingerprint != Base64(digest, dlen, true)) {
			Fail("The login service sent a code for another key.", "");
			return;
		}
		std::string url = "https://discord.com/ra/" + fingerprint;
		std::vector<uint8_t> temp(qrcodegen_BUFFER_LEN_MAX);
		s->qr.assign(qrcodegen_BUFFER_LEN_MAX, 0);
		if (!qrcodegen_encodeText(url.c_str(), temp.data(), s->qr.data(), qrcodegen_Ecc_LOW,
				qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
			s->qr.clear();
		s->waitingForPhone = false;
		SetStatus("Scan this code with the Discord app on your phone:\n"
			"tap your avatar, then Scan QR Code.");
	}
	else if (op == "pending_ticket")
	{
		// "id:discriminator:avatar:username"
		std::vector<uint8_t> user;
		std::string name = "your phone";
		if (Decrypt(s->key, j.value("encrypted_user_payload", ""), user)) {
			std::string u(user.begin(), user.end());
			size_t p = u.find(':');
			p = p == std::string::npos ? p : u.find(':', p + 1);
			p = p == std::string::npos ? p : u.find(':', p + 1);
			if (p != std::string::npos)
				name = u.substr(p + 1);
		}
		s->waitingForPhone = true;
		SetStatus("Scanned by " + name + ".\nConfirm the login on your phone.");
	}
	else if (op == "pending_login")
	{
		s->loggingIn = true;
		SetStatus("Logging in\xe2\x80\xa6");
		s->ticket = j.value("ticket", "");
		SendTicket({});
	}
	else if (op == "cancel")
	{
		SetStatus("The login was cancelled on the phone.  Getting a new code\xe2\x80\xa6");
		CloseGateway();
		ReconnectAfter(1500);
	}
}

void QrLogin::OnGatewayClosed(int code, const std::string& reason)
{
	if (!g_state)
		return;
	g_gateway = -1;
	StopHeartbeat();
	// the gateway closes once it handed over the ticket; and an error
	// stays on screen until Retry
	if (g_state->loggingIn || g_state->failed)
		return;
	fprintf(stderr, "dm: QR login gateway closed: %d %s\n", code, reason.c_str());
	// codes last a couple of minutes: get a new one
	SetStatus("The code expired.  Getting a new one\xe2\x80\xa6");
	ReconnectAfter(1500);
}
