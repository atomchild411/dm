#pragma once

#include <set>
#include <cstdarg>
#include <nlohmann/json.h>
#include <protobuf/Protobuf.hpp>
#include "models/Snowflake.hpp"
#include "models/ScrollDir.hpp"
#include "models/Message.hpp"
#include "network/HTTPClient.hpp"
#include "utils/Util.hpp"

// Cross platform interface between the DiscordInstance and the actual front-end.
class Frontend
{
public:
	Frontend() {}
	virtual ~Frontend() {}

	// Events
	// Connect the gateway again (StartSession) after delayMs.
	virtual void OnLoginAgain(int delayMs) = 0;
	virtual void OnLoggedOut() = 0;
	virtual void OnSessionClosed(int errorCode) = 0;
	virtual void OnConnecting() = 0;
	virtual void OnConnected() = 0;
	virtual void OnAddMessage(Snowflake channelID, const Message& msg) = 0;
	virtual void OnUpdateMessage(Snowflake channelID, const Message& msg) = 0;
	virtual void OnDeleteMessage(Snowflake messageInCurrentChannel) = 0;
	virtual void OnStartTyping(Snowflake userID, Snowflake guildID, Snowflake channelID, time_t startTime) = 0;
	virtual void OnAttachmentDownloaded(bool bIsProfilePicture, const uint8_t* pData, size_t nSize, const std::string& additData) = 0;
	virtual void OnAttachmentFailed(bool bIsProfilePicture, const std::string& additData) = 0;
	virtual void OnRequestDone(NetRequest* pRequest) = 0;
	virtual void OnFailedToSendMessage(Snowflake channel, Snowflake message) = 0;
	virtual void OnNotification() = 0;

	// Error messages
	virtual void OnGenericError(const std::string& message) = 0;
	virtual void OnJsonException(const std::string& message) = 0;
	virtual void OnCantViewChannel(const std::string& channelName) = 0;
	virtual void OnGatewayConnectFailure() = 0;
	virtual void OnProtobufError(Protobuf::ErrorCode code) = 0;

	// Update requests
	virtual void UpdateSelectedGuild() = 0;
	virtual void UpdateSelectedChannel() = 0;
	virtual void UpdateChannelList() = 0;
	virtual void UpdateMemberList() = 0;
	virtual void UpdateChannelAcknowledge(Snowflake channelID, Snowflake messageID) = 0;
	virtual void UpdateUserData(Snowflake userID) = 0;
	virtual void RepaintGuildList() = 0;
	virtual void RefreshMessages(ScrollDir::eScrollDir sd, Snowflake gapCulprit) = 0;
	virtual void RefreshMembers(const std::set<Snowflake>& members) = 0;

	// Interactive requests
	virtual void LaunchURL(const std::string& url) = 0;

	// Called by WebSocketClient, dispatches to relevant places including DiscordInstance
	virtual void OnWebsocketMessage(int gatewayID, const std::string& payload) = 0;
	virtual void OnWebsocketClose(int gatewayID, int errorCode, const std::string& message) = 0;
	virtual void OnWebsocketFail(int gatewayID, int errorCode, const std::string& message, bool isTLSError, bool mayRetry) = 0;

	// Heartbeat interval
	// Call DiscordInstance::SendHeartbeat after firstMs, then every timeMs;
	// timeMs 0 stops it.
	virtual void SetHeartbeatInterval(int timeMs, int firstMs) = 0;

	// Interface with AvatarCache

	// Config
	virtual std::string LoadConfig() = 0;
	virtual bool SaveConfig(const std::string& configJson) = 0;

	// Quit
	virtual void RequestQuit() = 0;
	
	// Queries
	virtual bool IsWindowFocused() = 0;
	
	// Strings
	virtual std::string GetDirectMessagesText() = 0;
	virtual std::string GetPleaseWaitText() = 0;
	virtual std::string GetTodayAtText() = 0;
	virtual std::string GetYesterdayAtText() = 0;
	virtual std::string GetFormatTimeLongText() = 0;
	virtual std::string GetFormatTimeShorterText() = 0;
	virtual std::string GetFormatTimestampTimeShort() = 0;
	virtual std::string GetFormatTimestampTimeLong() = 0;
	virtual std::string GetFormatTimestampDateShort() = 0;
	virtual std::string GetFormatTimestampDateLong() = 0;
	virtual std::string GetFormatTimestampDateLongTimeShort() = 0;
	virtual std::string GetFormatTimestampDateLongTimeLong() = 0;

	// Debugging
#ifdef USE_DEBUG_PRINTS
	virtual void DebugPrint(const char* fmt, va_list vl) = 0;
#endif
};

// Defined in the specific platform that this application is compiled for.
Frontend* GetFrontend();
