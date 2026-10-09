// The token in the system's own store of secrets (see SecretStore.hpp).

#include "SecretStore.hpp"

#include <cstdlib>
#include <cstring>

namespace
{
	bool g_disabled = false;

	bool Forbidden()
	{
		const char* how = getenv("DM_TOKEN_STORE");
		return g_disabled || (how && !strcmp(how, "file"));
	}
}

void SecretStore::Disable()
{
	g_disabled = true;
}

#if defined(__APPLE__)
#include <Security/Security.h>

// A generic password in the user's Keychain: service "Discord Messenger",
// account the profile's directory.
namespace
{
	CFMutableDictionaryRef Query(const std::string& profile)
	{
		CFMutableDictionaryRef q = CFDictionaryCreateMutable(nullptr, 0,
			&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFStringRef account = CFStringCreateWithCString(nullptr, profile.c_str(), kCFStringEncodingUTF8);
		CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
		CFDictionarySetValue(q, kSecAttrService, CFSTR("Discord Messenger"));
		CFDictionarySetValue(q, kSecAttrAccount, account ? account : CFSTR(""));
		if (account)
			CFRelease(account);
		return q;
	}
}

bool SecretStore::Usable() { return !Forbidden(); }
const char* SecretStore::Name() { return "the Keychain"; }

SecretStore::Result SecretStore::Load(const std::string& profile, std::string& token)
{
	CFMutableDictionaryRef q = Query(profile);
	CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
	CFTypeRef data = nullptr;
	OSStatus st = SecItemCopyMatching(q, &data);
	CFRelease(q);
	if (st == errSecItemNotFound)
		return NOT_FOUND;
	if (st != errSecSuccess || !data)
		return FAILED;
	CFDataRef d = (CFDataRef) data;
	token.assign((const char*) CFDataGetBytePtr(d), (size_t) CFDataGetLength(d));
	CFRelease(data);
	return FOUND;
}

bool SecretStore::Save(const std::string& profile, const std::string& token)
{
	CFMutableDictionaryRef q = Query(profile);
	OSStatus st;
	if (token.empty()) {
		st = SecItemDelete(q);
		CFRelease(q);
		return st == errSecSuccess || st == errSecItemNotFound;
	}
	CFDataRef data = CFDataCreate(nullptr, (const UInt8*) token.data(), (CFIndex) token.size());
	CFMutableDictionaryRef change = CFDictionaryCreateMutable(nullptr, 0,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(change, kSecValueData, data);
	st = SecItemUpdate(q, change);
	if (st == errSecItemNotFound) {
		CFDictionarySetValue(q, kSecValueData, data);
		CFDictionarySetValue(q, kSecAttrLabel, CFSTR("Discord Messenger login token"));
		st = SecItemAdd(q, nullptr);
	}
	CFRelease(change);
	CFRelease(data);
	CFRelease(q);
	return st == errSecSuccess;
}

#elif defined(_WIN32)
#include <windows.h>
#include <wincred.h>

// A generic credential: "DiscordMessenger:" and the profile's directory.
namespace
{
	std::wstring Target(const std::string& profile)
	{
		std::string t = "DiscordMessenger:" + profile;
		int n = MultiByteToWideChar(CP_UTF8, 0, t.c_str(), (int) t.size(), nullptr, 0);
		std::wstring w(n > 0 ? n : 0, L'\0');
		if (n > 0)
			MultiByteToWideChar(CP_UTF8, 0, t.c_str(), (int) t.size(), &w[0], n);
		return w;
	}
}

bool SecretStore::Usable() { return !Forbidden(); }
const char* SecretStore::Name() { return "the Credential Manager"; }

SecretStore::Result SecretStore::Load(const std::string& profile, std::string& token)
{
	PCREDENTIALW cred = nullptr;
	if (!CredReadW(Target(profile).c_str(), CRED_TYPE_GENERIC, 0, &cred))
		return GetLastError() == ERROR_NOT_FOUND ? NOT_FOUND : FAILED;
	token.assign((const char*) cred->CredentialBlob, cred->CredentialBlobSize);
	CredFree(cred);
	return FOUND;
}

bool SecretStore::Save(const std::string& profile, const std::string& token)
{
	std::wstring target = Target(profile);
	if (token.empty())
		return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
	CREDENTIALW c = {};
	c.Type = CRED_TYPE_GENERIC;
	c.TargetName = &target[0];
	c.Comment = (LPWSTR) L"Discord Messenger login token";
	c.CredentialBlobSize = (DWORD) token.size();
	c.CredentialBlob = (LPBYTE) token.data();
	c.Persist = CRED_PERSIST_LOCAL_MACHINE;
	c.UserName = (LPWSTR) L"token";
	return CredWriteW(&c, 0) != FALSE;
}

#elif defined(__linux__)
#include <dlfcn.h>
#include <sys/stat.h>

// The Secret Service through libsecret, loaded when first needed: a desktop
// without it (or a system without a session bus) keeps the token in the
// file.  libsecret's types, as its headers declare them (its ABI is stable).
namespace
{
	struct SchemaAttribute { const char* name; int type; };
	struct Schema
	{
		const char* name;
		int flags;
		SchemaAttribute attributes[32];
		int reserved;
		void* reserved1; void* reserved2; void* reserved3; void* reserved4;
		void* reserved5; void* reserved6; void* reserved7;
	};
	struct GErrorS { unsigned domain; int code; char* message; };

	typedef int (*StoreFn)(const Schema*, const char*, const char*, const char*, void*, GErrorS**, ...);
	typedef char* (*LookupFn)(const Schema*, void*, GErrorS**, ...);
	typedef int (*ClearFn)(const Schema*, void*, GErrorS**, ...);
	typedef void (*FreeFn)(char*);
	typedef void (*ErrorFreeFn)(GErrorS*);

	struct Lib
	{
		bool tried = false, ok = false;
		StoreFn store = nullptr;
		LookupFn lookup = nullptr;
		ClearFn clear = nullptr;
		FreeFn free = nullptr;
		ErrorFreeFn errorFree = nullptr;
	} g_lib;

	// "dm-profile": the settings directory
	const Schema SCHEMA = { "net.discordmessenger.Token", 0, { { "dm-profile", 0 }, { nullptr, 0 } },
		0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

	bool SessionBus()
	{
		const char* addr = getenv("DBUS_SESSION_BUS_ADDRESS");
		if (addr && *addr)
			return true;
		const char* run = getenv("XDG_RUNTIME_DIR");
		struct stat st;
		return run && *run && stat((std::string(run) + "/bus").c_str(), &st) == 0;
	}

	bool Open()
	{
		if (g_lib.tried)
			return g_lib.ok;
		g_lib.tried = true;
		if (!SessionBus())
			return false;
		void* h = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
		if (!h)
			return false;
		g_lib.store = (StoreFn) dlsym(h, "secret_password_store_sync");
		g_lib.lookup = (LookupFn) dlsym(h, "secret_password_lookup_sync");
		g_lib.clear = (ClearFn) dlsym(h, "secret_password_clear_sync");
		g_lib.free = (FreeFn) dlsym(h, "secret_password_free");
		g_lib.errorFree = (ErrorFreeFn) dlsym(h, "g_error_free"); // (glib, which it loads)
		g_lib.ok = g_lib.store && g_lib.lookup && g_lib.clear && g_lib.free && g_lib.errorFree;
		return g_lib.ok;
	}

	bool Failed(GErrorS* err)
	{
		if (!err)
			return false;
		g_lib.errorFree(err);
		return true;
	}
}

bool SecretStore::Usable() { return !Forbidden() && Open(); }
const char* SecretStore::Name() { return "the desktop's keyring"; }

SecretStore::Result SecretStore::Load(const std::string& profile, std::string& token)
{
	if (!Open())
		return FAILED;
	GErrorS* err = nullptr;
	char* pw = g_lib.lookup(&SCHEMA, nullptr, &err, "dm-profile", profile.c_str(), (char*) nullptr);
	if (Failed(err))
		return FAILED;
	if (!pw)
		return NOT_FOUND;
	token = pw;
	g_lib.free(pw);
	return FOUND;
}

bool SecretStore::Save(const std::string& profile, const std::string& token)
{
	if (!Open())
		return false;
	GErrorS* err = nullptr;
	if (token.empty()) {
		g_lib.clear(&SCHEMA, nullptr, &err, "dm-profile", profile.c_str(), (char*) nullptr);
		return !Failed(err);
	}
	int ok = g_lib.store(&SCHEMA, "default", "Discord Messenger login token", token.c_str(), nullptr, &err,
		"dm-profile", profile.c_str(), (char*) nullptr);
	return !Failed(err) && ok;
}

#else
// No store (IRIX and others): the file keeps the token.
bool SecretStore::Usable() { (void) Forbidden(); return false; }
const char* SecretStore::Name() { return "settings.json"; }
SecretStore::Result SecretStore::Load(const std::string&, std::string&) { return FAILED; }
bool SecretStore::Save(const std::string&, const std::string&) { return false; }
#endif
