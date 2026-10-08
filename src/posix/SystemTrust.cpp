// Which certificates a TLS connection trusts.
//
// On macOS the system decides, as for any other program: the server's chain
// goes to the Security framework, which checks it against the keychains'
// roots (with any the user or their organisation added), its revocation and
// distrust rules, and the host name.  Elsewhere OpenSSL checks it against a
// bundle of root certificates (GetCACertFile).
//
// DM_CA_FILE names a bundle to use instead, on every system (for tests).

#include "NetworkerThread.hpp"

#include <cstdio>
#include <cstdlib>
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
#endif

std::string TrustDescription()
{
#if defined(__APPLE__)
	const char* env = getenv("DM_CA_FILE");
	if (!env || !*env)
		return "the macOS keychains (Security framework)";
#endif
	std::string file = GetCACertFile();
	return file.empty() ? "OpenSSL's default" : file;
}

void UseSystemTrust(SSL_CTX* ctx)
{
	if (!ctx)
		return;
#if defined(__APPLE__)
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
