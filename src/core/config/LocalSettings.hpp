#pragma once

#include <string>

// The settings kept in ~/.discordmessenger/settings.json.  (The Motif client
// keeps its own window and view state in motif.conf.)
class LocalSettings
{
public:
	LocalSettings();

	bool Load();
	bool Save();
	std::string GetToken() const {
		return m_token;
	}
	void SetToken(const std::string& str) {
		m_token = str;
	}
	const std::string& GetDiscordAPI() const {
		return m_discordApi;
	}
	void SetDiscordAPI(const std::string& str) {
		m_discordApi = str;
	}
	const std::string& GetDiscordCDN() const {
		return m_discordCdn;
	}
	void SetDiscordCDN(const std::string& str) {
		m_discordCdn = str;
	}
	bool EnableTLSVerification() const {
		return m_bEnableTLSVerification;
	}
	void SetEnableTLSVerification(bool b) {
		m_bEnableTLSVerification = b;
	}
	bool AddExtraHeaders() const {
		return m_bAddExtraHeaders;
	}
	void SetAddExtraHeaders(bool b) {
		m_bAddExtraHeaders = b;
	}
	bool DisableFormatting() const {
		return m_bDisableFormatting;
	}
	void SetDisableFormatting(bool b) {
		m_bDisableFormatting = b;
	}
	bool Use12HourTime() const {
		return m_bUse12HourTime;
	}
	void SetUse12HourTime(bool b) {
		m_bUse12HourTime = b;
	}

private:
	std::string m_token;
	std::string m_discordApi;
	std::string m_discordCdn;
	bool m_bEnableTLSVerification = true;
	bool m_bDisableFormatting = false;
	bool m_bAddExtraHeaders = true;
	bool m_bUse12HourTime = false;
};

LocalSettings* GetLocalSettings();
