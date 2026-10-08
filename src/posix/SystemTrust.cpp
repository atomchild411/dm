// Which certificates a TLS connection trusts.
//
// On macOS and Windows the system decides, as for any other program: the
// server's chain goes to the Security framework, or to Windows' chain engine
// (crypt32), which check it against the system's roots (with any the user or
// their organisation added), revocation and distrust rules, and the host
// name.  Elsewhere OpenSSL checks it against a bundle of root certificates
// (GetCACertFile).
//
// DM_CA_FILE names a bundle to use instead, on every system (for tests).

#include "NetworkerThread.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#if defined(__APPLE__)
#include <Security/Security.h>

namespace
{
	void AddCertificate(CFMutableArrayRef certs, X509* cert)
	{
		unsigned char* der = nullptr;
		int n = i2d_X509(cert, &der);
		if (n <= 0)
			return;
		CFDataRef data = CFDataCreate(nullptr, der, n);
		OPENSSL_free(der);
		if (!data)
			return;
		SecCertificateRef sc = SecCertificateCreateWithData(nullptr, data);
		CFRelease(data);
		if (sc) {
			CFArrayAppendValue(certs, sc);
			CFRelease(sc);
		}
	}

	// In place of OpenSSL's own chain check (SSL_CTX_set_cert_verify_callback):
	// the chain the server sent, for the name the connection asked for (SNI).
	// This runs on the network threads; the Security framework may fetch
	// intermediate certificates or revocation status, so never on the UI's.
	int VerifyWithSystem(X509_STORE_CTX* xctx, void*)
	{
		SSL* ssl = (SSL*) X509_STORE_CTX_get_ex_data(xctx, SSL_get_ex_data_X509_STORE_CTX_idx());
		const char* host = ssl ? SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name) : nullptr;
		X509* leaf = X509_STORE_CTX_get0_cert(xctx);
		if (!host || !*host || !leaf) {
			fprintf(stderr, "dm: TLS: no host name or certificate to check\n");
			X509_STORE_CTX_set_error(xctx, X509_V_ERR_UNSPECIFIED);
			return 0;
		}

		CFMutableArrayRef certs = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
		AddCertificate(certs, leaf);
		STACK_OF(X509)* chain = X509_STORE_CTX_get0_untrusted(xctx);
		for (int i = 0; chain && i < sk_X509_num(chain); i++) {
			X509* c = sk_X509_value(chain, i);
			if (X509_cmp(c, leaf) != 0)
				AddCertificate(certs, c);
		}

		CFStringRef name = CFStringCreateWithCString(nullptr, host, kCFStringEncodingUTF8);
		SecPolicyRef policy = SecPolicyCreateSSL(true, name);
		SecTrustRef trust = nullptr;
		CFErrorRef err = nullptr;
		bool ok = CFArrayGetCount(certs) > 0 && policy &&
			SecTrustCreateWithCertificates(certs, policy, &trust) == errSecSuccess &&
			SecTrustEvaluateWithError(trust, &err);

		if (!ok) {
			char why[512] = "no reason given";
			if (err) {
				CFStringRef desc = CFErrorCopyDescription(err);
				if (desc) {
					CFStringGetCString(desc, why, sizeof why, kCFStringEncodingUTF8);
					CFRelease(desc);
				}
			}
			fprintf(stderr, "dm: TLS: macOS does not trust %s: %s\n", host, why);
		}
		X509_STORE_CTX_set_error(xctx, ok ? X509_V_OK : X509_V_ERR_CERT_UNTRUSTED);

		if (err) CFRelease(err);
		if (trust) CFRelease(trust);
		if (policy) CFRelease(policy);
		if (name) CFRelease(name);
		CFRelease(certs);
		return ok ? 1 : 0;
	}
}
#elif defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>

#include <string>
#include <vector>

namespace
{
	bool AddCertificate(HCERTSTORE store, X509* cert, PCCERT_CONTEXT* added)
	{
		unsigned char* der = nullptr;
		int n = i2d_X509(cert, &der);
		if (n <= 0)
			return false;
		BOOL ok = CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, der, (DWORD) n,
			CERT_STORE_ADD_ALWAYS, added);
		OPENSSL_free(der);
		return ok != FALSE;
	}

	// In place of OpenSSL's own chain check (SSL_CTX_set_cert_verify_callback):
	// the chain the server sent, for the name the connection asked for (SNI).
	// Windows may fetch intermediates and revocation lists: network threads only.
	int VerifyWithSystem(X509_STORE_CTX* xctx, void*)
	{
		SSL* ssl = (SSL*) X509_STORE_CTX_get_ex_data(xctx, SSL_get_ex_data_X509_STORE_CTX_idx());
		const char* host = ssl ? SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name) : nullptr;
		X509* leaf = X509_STORE_CTX_get0_cert(xctx);
		if (!host || !*host || !leaf) {
			fprintf(stderr, "dm: TLS: no host name or certificate to check\n");
			X509_STORE_CTX_set_error(xctx, X509_V_ERR_UNSPECIFIED);
			return 0;
		}

		HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
		PCCERT_CONTEXT leafCtx = nullptr;
		PCCERT_CHAIN_CONTEXT chain = nullptr;
		DWORD error = 0;
		bool ok = store && AddCertificate(store, leaf, &leafCtx);
		STACK_OF(X509)* sent = X509_STORE_CTX_get0_untrusted(xctx);
		for (int i = 0; ok && sent && i < sk_X509_num(sent); i++) {
			X509* c = sk_X509_value(sent, i);
			if (X509_cmp(c, leaf) != 0)
				AddCertificate(store, c, nullptr);
		}

		if (ok) {
			LPSTR serverAuth[] = { (LPSTR) szOID_PKIX_KP_SERVER_AUTH };
			CERT_CHAIN_PARA para = {};
			para.cbSize = sizeof para;
			para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
			para.RequestedUsage.Usage.cUsageIdentifier = 1;
			para.RequestedUsage.Usage.rgpszUsageIdentifier = serverAuth;
			ok = CertGetCertificateChain(nullptr, leafCtx, nullptr, store, &para,
				CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT, nullptr, &chain) != FALSE;
			if (!ok)
				error = GetLastError();
		}
		if (ok) {
			int n = MultiByteToWideChar(CP_UTF8, 0, host, -1, nullptr, 0);
			std::vector<wchar_t> wide(n > 0 ? n : 1, L'\0');
			if (n > 0)
				MultiByteToWideChar(CP_UTF8, 0, host, -1, wide.data(), n);

			SSL_EXTRA_CERT_CHAIN_POLICY_PARA sslPara = {};
			sslPara.cbSize = sizeof sslPara;
			sslPara.dwAuthType = AUTHTYPE_SERVER;
			sslPara.pwszServerName = wide.data();
			CERT_CHAIN_POLICY_PARA policy = {};
			policy.cbSize = sizeof policy;
			policy.pvExtraPolicyPara = &sslPara;
			// revocation that cannot be checked (offline) does not fail it,
			// as in the browsers; a revoked certificate does
			policy.dwFlags = CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS;
			CERT_CHAIN_POLICY_STATUS status = {};
			status.cbSize = sizeof status;
			ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status) &&
				status.dwError == 0;
			if (!ok)
				error = status.dwError ? status.dwError : GetLastError();
		}

		if (!ok) {
			char why[512] = "";
			if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
					why, sizeof why, nullptr))
				snprintf(why, sizeof why, "error 0x%08lx", (unsigned long) error);
			for (char* e = why + strlen(why); e > why && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' '); )
				*--e = 0;
			fprintf(stderr, "dm: TLS: Windows does not trust %s: %s (0x%08lx)\n", host, why, (unsigned long) error);
		}
		X509_STORE_CTX_set_error(xctx, ok ? X509_V_OK : X509_V_ERR_CERT_UNTRUSTED);

		if (chain) CertFreeCertificateChain(chain);
		if (leafCtx) CertFreeCertificateContext(leafCtx);
		if (store) CertCloseStore(store, 0);
		return ok ? 1 : 0;
	}
}
#endif

std::string TrustDescription()
{
#if defined(__APPLE__) || defined(_WIN32)
	const char* env = getenv("DM_CA_FILE");
	if (!env || !*env)
#if defined(__APPLE__)
		return "the macOS keychains (Security framework)";
#else
		return "the Windows certificate stores (crypt32)";
#endif
#endif
	std::string file = GetCACertFile();
	return file.empty() ? "OpenSSL's default" : file;
}

void UseSystemTrust(SSL_CTX* ctx)
{
	if (!ctx)
		return;
#if defined(__APPLE__) || defined(_WIN32)
	const char* env = getenv("DM_CA_FILE");
	if (!env || !*env) {
		SSL_CTX_set_cert_verify_callback(ctx, VerifyWithSystem, nullptr);
		return;
	}
#endif
	std::string file = GetCACertFile();
	if (!file.empty())
		SSL_CTX_load_verify_locations(ctx, file.c_str(), nullptr);
}
