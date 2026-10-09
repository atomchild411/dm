#pragma once

#include <unordered_map>
#include <algorithm>
#include <string>
#include <list>
#include <deque>
#include <ctime>
#include <set>
#include <unordered_set>
#include <nlohmann/json.h>
#include "network/DiscordAPI.hpp"
#include "models/Snowflake.hpp"
#include "config/SettingsManager.hpp"
#include "models/Guild.hpp"
#include "network/DiscordRequest.hpp"
#include "state/MessageCache.hpp"
#include "state/ProfileCache.hpp"
#include "models/ScrollDir.hpp"
#include "models/Message.hpp"
#include "models/Relationship.hpp"
#include "state/NotificationManager.hpp"
#include "state/UserGuildSettings.hpp"
#include "models/GuildListItem.hpp"
#include "text/FormattedText.hpp"

struct NetRequest;

// Used by GetGuildIDsOrdered
#define BIT_FOLDER 0x8000000000000000

namespace GatewayOp
{
	enum eOpcode
	{
		DISPATCH,
		HEARTBEAT,
		IDENTIFY,
		PRESENCE_UPDATE,
		VOICE_STATE_UPDATE,
		RESUME = 6,
		RECONNECT,
		REQUEST_GUILD_MEMBERS,
		INVALID_SESSION,
		HELLO,
		HEARTBACK,

		SUBSCRIBE_DM = 13,
		SUBSCRIBE_GUILD,

		UPDATE_SUBSCRIPTIONS = 37, // guess - update channels subscribed to
		// payload:
		// example payload: {'d': {'subscriptions': {'GUILDIDHERE': {'channels': {'CHANNELID1': [[0, 99]], 'CHANNELID2': [[0, 99]]}}}}, 'op': 37}
		// note: dropped in favour of opcode 14
	};
};

class DiscordInstance
{
private:
	std::string m_token;

public:
	Snowflake m_mySnowflake = 0;
	Snowflake m_CurrentGuild   = 0;
	Snowflake m_CurrentChannel = 0;

	// Guild DB
	std::list<Guild> m_guilds;

	Guild m_dmGuild;

	// Requests in progress
	// (channel, gap) of the message requests on their way
	std::set<std::pair<Snowflake, Snowflake>> m_messageRequestsInProgress;

	// Gateway url and connection ID
	std::string m_gatewayUrl = "";
	int m_gatewayConnId = -1;
	int m_heartbeatSequenceId = -1;

	// Things we get on ready
	std::string m_gatewayResumeUrl = "";
	std::string m_sessionId = ""; // for resume

	// The gateway session, as Discord asks clients to keep it: resumed
	// after a drop (not logged in afresh), heartbeats acknowledged (or the
	// connection is dead), reconnects spaced out more each time, and fresh
	// logins (IDENTIFY) limited, so nothing can make the client hammer Discord.
	bool m_resuming = false;          // this connection resumes the session
	bool m_heartbeatAcked = true;     // the last heartbeat was acknowledged
	int m_reconnectAttempts = 0;      // since the session was last up for a while
	time_t m_connectedAt = 0;         // when READY or RESUMED came (0: not up)
	std::deque<time_t> m_identifies;  // the fresh logins of the last hour

	// Last time we sent a typing indicator
	uint64_t m_lastTypingSent = 0;

	// Sequence number of ack state
	int m_ackVersion = 0;

	// Relationships
	std::list<Relationship> m_relationships;

	// Blocked Users
	std::unordered_set<Snowflake> m_blockedUsers;

	// User guild settings
	UserGuildSettings m_userGuildSettings;

	// Notification manager
	NotificationManager m_notificationManager;

	// List of channels user cannot view because an HTTPS request related
	// to them returned a 403.  Frankly this shouldn't be usable, but oh well.
	std::set<Snowflake> m_channelDenyList;

	// List of guilds and guild folders.
	GuildItemList m_guildItemList;

public:
	Profile* GetProfile() {
		return GetProfileCache()->LookupProfile(m_mySnowflake, "", "", "", false);
	}

	Snowflake GetUserID() const {
		return m_mySnowflake;
	}

	const std::string& GetToken() const {
		return m_token;
	}

	void SetToken(const std::string& token){
		m_token = token;
	}

	int GetGatewayID() const {
		return m_gatewayConnId;
	}

	// TODO optimize this stuff
	Guild* GetGuild(Snowflake sf)
	{
		assert(sf != 1);
		
		if (!sf)
			return &m_dmGuild;

		for (auto& g : m_guilds)
		{
			if (g.m_snowflake == sf)
				return &g;
		}
		return nullptr;
	}

	std::string GetGuildFolderName(Snowflake sf)
	{
		auto items = m_guildItemList.GetItems();
		for (auto& item : *items)
		{
			if (item->GetID() != sf)
				continue;

			auto name = item->GetName();
			if (!name.empty())
				return name;

			if (!item->IsFolder() || item->GetItems()->empty())
				return "Empty Folder";

			name = "";
			auto subitems = item->GetItems();
			for (auto& subitem : *subitems) {
				if (!name.empty())
					name += ", ";
				name += subitem->GetName();
			}

			if (name.size() > 100)
				name = name.substr(0, 97) + "...";
			return name;
		}

		return "Unknown";
	}

	void GetGuildIDsOrdered(std::vector<Snowflake>& sf, bool bUI = false)
	{
		if (bUI) {
			sf.push_back(0); // @me
			sf.push_back(1); // gap
		}

		auto items = m_guildItemList.GetItems();
		for (auto& item : *items)
		{
			// If there is a folder, OR the snowflake with BIT_FOLDER
			Snowflake id = item->GetID() & ~BIT_FOLDER;

			if (item->IsFolder())
			{
				if (bUI)
					sf.push_back(BIT_FOLDER | item->GetID());

				auto subitems = item->GetItems();
				for (auto& subitem : *subitems)
				{
					sf.push_back(subitem->GetID());
				}

				// add an empty item to terminate this folder
				if (bUI)
					sf.push_back(BIT_FOLDER);
			}
			else
			{
				sf.push_back(item->GetID());
			}
		}
	}

	Guild* GetCurrentGuild()
	{
		return GetGuild(m_CurrentGuild);
	}

	Snowflake GetCurrentGuildID() const {
		return m_CurrentGuild;
	}

	Channel* GetChannel(Snowflake sf)
	{
		Channel* pChan;
		Guild* pGuild = GetCurrentGuild();
		if (pGuild) {
			pChan = pGuild->GetChannel(sf);
			if (pChan) return pChan;
		}

		for (auto& gld : m_guilds) {
			pChan = gld.GetChannel(sf);
			if (pChan)
				return pChan;
		}

		return m_dmGuild.GetChannel(sf);
	}

	Channel* GetCurrentChannel()
	{
		return GetChannel(m_CurrentChannel);
	}

	Snowflake GetCurrentChannelID() const {
		return m_CurrentChannel;
	}

	Channel* GetChannelGlobally(Snowflake sf) {
		return GetChannel(sf);
	}

	void HandledChannelSwitch() {
		m_messageRequestsInProgress.erase(std::make_pair(m_CurrentChannel, (Snowflake) 0));
	}

	bool IsUserBlocked(Snowflake sf) const {
		return m_blockedUsers.find(sf) != m_blockedUsers.end();
	}

	bool IsChannelMuted(Snowflake guildID, Snowflake channelID) const;

	// Lookup
	std::string LookupChannelNameGlobally(Snowflake sf);
	std::string LookupRoleName(Snowflake sf, Snowflake guildID);
	std::string LookupUserNameGlobally(Snowflake sf, Snowflake gld);

	// Select a guild.
	void OnSelectGuild(Snowflake sf, Snowflake chan = 0);

	// Select a channel in the current guild.
	void OnSelectChannel(Snowflake sf, bool bSendSubscriptionUpdate = true);

	// Fetch messages in specified channel.
	void RequestMessages(Snowflake sf, ScrollDir::eScrollDir dir = ScrollDir::BEFORE, Snowflake source = 0, Snowflake gapper = 0);

	// Send a refresh to the message list.
	void OnFetchedMessages(Snowflake gap = 0, ScrollDir::eScrollDir dir = ScrollDir::BEFORE);

	// Handle the case where a channel list was fetched from a guild.
	void OnFetchedChannels(Guild* pGld, const std::string& content);

	// Handle the case where the gateway is closed.
	void GatewayClosed(int errorCode);

	// Start a new gateway session.
	void StartGatewaySession();

	// Transform user, channel, or emoji mentions ("@usernamehere") into snowflake mentions (<@12347689436274>).
	std::string ResolveMentions(const std::string& str, Snowflake guild, Snowflake channel);

	// Send a message to the current channel.
	bool SendMessageToCurrentChannel(const std::string& msg, Snowflake& tempSf, Snowflake reply = 0, bool mentionReplied = true);
	// The same to any channel (a conversation in a window of its own).
	bool SendMessageToChannel(Snowflake guild, Snowflake channel, const std::string& msg, Snowflake& tempSf, Snowflake reply = 0, bool mentionReplied = true);

	// Set current activity status.
	void SetActivityStatus(eActiveStatus status);

	// Close the current gateway session.
	void CloseGatewaySession();

	// Inform the Discord backend that we are typing.
	void Typing();
	void Typing(Snowflake channel);

	// Inform the Discord backend that we have acknowledged a message.
	void RequestAcknowledgeChannel(Snowflake channel);

	// Request a message deletion.
	void RequestDeleteMessage(Snowflake chan, Snowflake msg);

	// Replaces the text of one of the user's messages.
	void RequestEditMessage(Snowflake chan, Snowflake msg, const std::string& text);
	// Adds the user's reaction to a message, or takes it away.
	void RequestReaction(Snowflake chan, Snowflake msg, const Reaction& emoji, bool add);

	// Update channels that we are subscribed to.
	void UpdateSubscriptions(Snowflake guild, Snowflake channel, bool typing, bool activities, bool threads, int rangeMembers = 99);

	// Check if we did the initial API_URL/gateway request.
	bool HasGatewayURL() const { return !m_gatewayUrl.empty(); }

	// Resolves links automatically in a formatted message.
	void ResolveLinks(FormattedText* message, std::vector<InteractableItem>& interactables, Snowflake guildID = 0);

public:
	DiscordInstance(std::string token) : m_token(token), m_notificationManager(this) {
		InitDispatchFunctions();
	}

	void HandleRequest(NetRequest* pReq);

	void HandleGatewayMessage(const std::string& payload);

	// The heartbeat timer's: sends one, or (the last one was never
	// acknowledged) drops the dead connection and reconnects.
	void SendHeartbeat();

	// After the gateway closed or could not be reached: reconnects later,
	// a random half to whole of 1, 2, 4 ... 60 s (at least minimumMs), unless
	// the client logged in afresh too often this hour.
	void ReconnectLater(int minimumMs = 0);

	// The user asked to reconnect: at once, the backoff forgotten.
	void ReconnectNow();

private:
	void SendIdentify();
	void SendResume();
	void SendHeartbeatPayload();
	// Closes the connection and keeps the session resumable (4000, where
	// 1000 would end the session).
	void DropConnection();
	void ForgetSession();
	bool CanResume() const;
public:

	void LoadUserSettings(const std::string& userSettings, bool partial = false);

	bool ResortChannels(Snowflake guild);

public:
	// returns user's id. The user parameter is used only if j["user"] doesn't exist
	Snowflake ParseGuildMember(Snowflake guild, nlohmann::json& j, Snowflake user = 0);
	Snowflake ParseGuildMemberOrGroup(Snowflake guild, nlohmann::json& j);

private:
	void InitDispatchFunctions();
	void UpdateSettingsInfo();
	bool SortGuilds();
	void ParseChannel(Channel& c, nlohmann::json& j, int& num);
	void ParseAndAddGuild(nlohmann::json& j);
	void ParsePermissionOverwrites(Channel& c, nlohmann::json& j);
	void ParseReadStateObject(nlohmann::json& j, bool bAlternate);
	void RefreshRelationships();
	std::string ResolveTimestamp(const std::string& timestampCode);
	std::string TransformMention(const std::string& source, Snowflake guild, Snowflake channel);

	// handle functions
	void HandleREADY(nlohmann::json& j);
	void HandleRESUMED(nlohmann::json& j);
	void HandleREADY_SUPPLEMENTAL(nlohmann::json& j);
	void HandleMESSAGE_CREATE(nlohmann::json& j);
	void HandleMESSAGE_DELETE(nlohmann::json& j);
	void HandleMESSAGE_REACTION_ADD(nlohmann::json& j);
	void HandleMESSAGE_REACTION_REMOVE(nlohmann::json& j);
	void HandleMESSAGE_REACTION_REMOVE_ALL(nlohmann::json& j);
	void HandleMESSAGE_REACTION_REMOVE_EMOJI(nlohmann::json& j);
	void ChangeReactions(nlohmann::json& data, int delta, bool all, bool wholeEmoji);
	void HandleMESSAGE_UPDATE(nlohmann::json& j);
	void HandleMESSAGE_ACK(nlohmann::json& j);
	void HandleUSER_GUILD_SETTINGS_UPDATE(nlohmann::json& j);
	void HandleUSER_SETTINGS_PROTO_UPDATE(nlohmann::json& j);
	void HandleGUILD_CREATE(nlohmann::json& j);
	void HandleGUILD_DELETE(nlohmann::json& j);
	void HandleCHANNEL_CREATE(nlohmann::json& j);
	void HandleCHANNEL_DELETE(nlohmann::json& j);
	void HandleCHANNEL_UPDATE(nlohmann::json& j);
	void HandleGUILD_MEMBER_LIST_UPDATE(nlohmann::json& j);
	void HandleTYPING_START(nlohmann::json& j);
	void HandlePRESENCE_UPDATE(nlohmann::json& j);
	void HandlePASSIVE_UPDATE_V1(nlohmann::json& j);

private:
	void HandleGuildMemberListUpdate_Sync(Snowflake guild, nlohmann::json& j);
	void HandleGuildMemberListUpdate_Insert(Snowflake guild, nlohmann::json& j);
	void HandleGuildMemberListUpdate_Delete(Snowflake guild, nlohmann::json& j);
	void HandleGuildMemberListUpdate_Update(Snowflake guild, nlohmann::json& j);
	void HandleMessageInsertOrUpdate(nlohmann::json& j, bool bIsUpdate);
};

DiscordInstance* GetDiscordInstance();

int64_t GetIntFromString(const std::string& str);

Snowflake GetSnowflake(const nlohmann::json& j, const std::string& key);

// Fetches a key
std::string GetFieldSafe(const nlohmann::json& j, const std::string& key);

int GetFieldSafeInt(const nlohmann::json& j, const std::string& key);

#define TYPING_INTERVAL 10000 // 10 sec
// Fresh logins (IDENTIFY) in an hour after which the client stops
// reconnecting by itself (Discord allows 1000 a day; a person needs a few).
#define MAX_IDENTIFIES_PER_HOUR 10
