#pragma once

#include <functional>

#include <string>
#include "Frontend.hpp"

// What every Unix frontend shares: requests and gateway traffic handed to
// the UI thread (MainQueue), the session start-up, settings files, texts and
// date formats.  A frontend derives from this and overrides what it shows.
//
// Calls that arrive on network threads (requests done, gateway messages,
// websocket failures, errors, progress) are moved to the UI thread here or
// in the hooks' documentation below; everything else is called by the
// DiscordInstance, which runs on the UI thread.
class Frontend_Posix : public Frontend
{
public:
	// Starts (or restarts) the session: fetches the gateway URL if needed,
	// then connects.  UI thread.
	void StartSession();
	// A reconnect scheduled before this does not happen (logging out).
	void CancelReconnect() { m_sessionGen++; }

	void OnLoginAgain(int delayMs) override;
	void OnLoggedOut() override {}
	void OnSessionClosed(int errorCode) override {}
	void OnConnecting() override {}
	void OnConnected() override {}
	void OnAddMessage(Snowflake channelID, const Message& msg) override;
	void OnUpdateMessage(Snowflake channelID, const Message& msg) override;
	void OnDeleteMessage(Snowflake messageInCurrentChannel) override {}
	void OnStartTyping(Snowflake userID, Snowflake guildID, Snowflake channelID, time_t startTime) override {}
	void OnAttachmentDownloaded(bool bIsProfilePicture, const uint8_t* pData, size_t nSize, const std::string& additData) override {}
	void OnAttachmentFailed(bool bIsProfilePicture, const std::string& additData) override {}
	void OnRequestDone(NetRequest* pRequest) override;
	void OnFailedToSendMessage(Snowflake channel, Snowflake message) override {}
	void OnNotification() override {}
	void OnGenericError(const std::string& message) override;
	void OnJsonException(const std::string& message) override;
	void OnCantViewChannel(const std::string& channelName) override;
	void OnGatewayConnectFailure() override;
	void OnProtobufError(Protobuf::ErrorCode code) override;
	void UpdateSelectedGuild() override {}
	void UpdateSelectedChannel() override {}
	void UpdateChannelList() override {}
	void UpdateMemberList() override {}
	void UpdateChannelAcknowledge(Snowflake channelID, Snowflake messageID) override {}
	void UpdateUserData(Snowflake userID) override {}
	void RepaintGuildList() override {}
	void RefreshMessages(ScrollDir::eScrollDir sd, Snowflake gapCulprit) override {}
	void RefreshMembers(const std::set<Snowflake>& members) override {}
	void LaunchURL(const std::string& url) override;
	void OnWebsocketMessage(int gatewayID, const std::string& payload) override;
	void OnWebsocketClose(int gatewayID, int errorCode, const std::string& message) override;
	void OnWebsocketFail(int gatewayID, int errorCode, const std::string& message, bool isTLSError, bool mayRetry) override;
	std::string LoadConfig() override;
	bool SaveConfig(const std::string& configJson) override;
	bool IsWindowFocused() override { return true; }
	std::string GetDirectMessagesText() override;
	std::string GetPleaseWaitText() override;
	std::string GetTodayAtText() override;
	std::string GetYesterdayAtText() override;
	std::string GetFormatTimeLongText() override;
	std::string GetFormatTimeShorterText() override;
	std::string GetFormatTimestampTimeShort() override;
	std::string GetFormatTimestampTimeLong() override;
	std::string GetFormatTimestampDateShort() override;
	std::string GetFormatTimestampDateLong() override;
	std::string GetFormatTimestampDateLongTimeShort() override;
	std::string GetFormatTimestampDateLongTimeLong() override;
#ifdef USE_DEBUG_PRINTS
	void DebugPrint(const char* fmt, va_list vl) override;
#endif

protected:
	// Shows an error or a notice to the user.  UI thread.
	virtual void ShowError(const std::string& message) = 0;

	// The gateway connection failed; mayRetry says whether trying again can
	// help.  The default reconnects later (DiscordInstance::ReconnectLater,
	// which spaces the attempts out).  UI thread.
	virtual void OnConnectFailed(const std::string& message, bool isTLSError, bool mayRetry);

	// Calls fn after ms milliseconds.  UI thread.
	virtual void ScheduleReconnect(int ms, std::function<void()> fn) = 0;

	// StartSession's count: a reconnect scheduled before another session
	// started does not start one more.
	int m_sessionGen = 0;
};

// Sets up the settings directory ($HOME/.discordmessenger, or DM_HOME) and
// its cache directory.  Call before loading settings.
void SetupPosixPaths();
