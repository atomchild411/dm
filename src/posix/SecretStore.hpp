#pragma once

#include <string>

// Where the login token is kept, when the system has a place for secrets:
// the macOS Keychain, Windows' Credential Manager, or a Linux desktop's
// Secret Service (GNOME Keyring, KWallet; libsecret, loaded at run time so
// it is not needed to run).  Elsewhere (IRIX, Linux without a desktop
// session) the token stays in settings.json, readable only by the user.
//
// A token is kept per settings directory (the account it is filed under
// is that directory's path), so DM_HOME profiles never share one.
namespace SecretStore
{
	// Whether this system has a store and it may be used: not with
	// DM_TOKEN_STORE=file, nor after Disable().
	bool Usable();
	// No store for this run (the demo, benchmarks and snapshots: they have
	// no token, and must not touch the user's).
	void Disable();

	enum Result { FOUND, NOT_FOUND, FAILED };
	// The token kept for that profile.
	Result Load(const std::string& profile, std::string& token);
	// Keeps the token for that profile (an empty one: forgets it).  False
	// when the store refused.
	bool Save(const std::string& profile, const std::string& token);
	// What the store is called, for messages ("the Keychain").
	const char* Name();
}
