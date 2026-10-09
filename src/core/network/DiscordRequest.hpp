#pragma once

namespace DiscordRequest
{
	enum eDiscordRequest
	{
		NOTHING,
		PROFILE,
		MESSAGES,
		GUILD,
		IMAGE,
		GATEWAY, // on init, fetches the Websocket gateway address
		TYPING,
		IMAGE_ATTACHMENT,
		DELETE_MESSAGE,
		ACK,
		MESSAGE_CREATE,
		REACTION,
	};
};
