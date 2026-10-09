#include "SettingsManager.hpp"
#include "../Frontend.hpp" 
#include "../DiscordInstance.hpp"

static SettingsManager g_SMSingleton;
SettingsManager* GetSettingsManager()
{
	return &g_SMSingleton;
}

SettingsManager::SettingsManager()
{
	m_pSettingsMessage = new Protobuf::ObjectBaseMessage;
}

SettingsManager::~SettingsManager()
{
	delete m_pSettingsMessage;
}

void SettingsManager::LoadData(const uint8_t* data, size_t sz)
{
	using namespace Protobuf;
	ObjectBaseMessage* pMsg = new ObjectBaseMessage;
	pMsg->SetDecodingHint(CreateHint()); // gonna get deleted below

	try
	{
		DecodeBlock(data, sz, pMsg);
		m_pSettingsMessage->MergeWith(pMsg);
		m_pSettingsMessage->MarkClean();

		delete pMsg;
	}
	catch (Protobuf::ErrorCode code)
	{
		delete pMsg;
		GetFrontend()->OnProtobufError(code);
	}
}

eActiveStatus SettingsManager::GetOnlineIndicator()
{
	return GetStatusFromString(
		m_pSettingsMessage
		->GetFieldObjectDefault<Protobuf::ObjectMessage>(Settings::FIELD_ACTIVITY)
		->GetFieldObjectDefault<Protobuf::ObjectMessage>(Settings::Activity::FIELD_INDICATOR)
		->GetFieldObjectDefault<Protobuf::ObjectString>(Settings::Activity::Indicator::FIELD_STATE)
		->GetContent()
	);
}

std::string SettingsManager::GetCustomStatusText()
{
	Protobuf::ObjectBase* pObject = m_pSettingsMessage
		->GetFieldObjectDefault<Protobuf::ObjectMessage>(Settings::FIELD_ACTIVITY)
		->GetFieldObjectDefault<Protobuf::ObjectMessage>(Settings::Activity::FIELD_CUSTOM_STATUS)
		->GetFieldObject(Settings::Activity::CustomStatus::FIELD_TEXT);

	if (!pObject)
		return "";

	if (!pObject->IsStringObject())
		return ""; // failed

	return dynamic_cast<Protobuf::ObjectString*>(pObject)->GetContent();
}

void SettingsManager::GetGuildFoldersEx(std::map<Snowflake, std::string>& folders, std::vector<std::pair<Snowflake, Snowflake>>& guilds)
{
	folders.clear();
	guilds.clear();

	auto pgfRoot = m_pSettingsMessage->GetFieldObjectDefault<Protobuf::ObjectMessage>(Settings::FIELD_GUILD_FOLDERS);
	if (!pgfRoot) {
		DbgPrintF("No guild folder root");
		return;
	}
	auto items = pgfRoot->GetFieldObjects(Settings::GuildFolders::FIELD_ITEMS);
	if (!items) {
		DbgPrintF("Failed to fetch guild folder items.");
		return;
	}

	for (auto& item : *items)
	{
		// note: I don't understand why this isn't just the fixed64/varint
		Snowflake folderId = 0;
		std::string folderName = "";
		auto pFolderIdPar = item->GetFieldObject(Settings::GuildFolders::Item::FIELD_ID);
		
		// note: if pFolderIdPar isn't present, that means that this is likely
		// just a folderless single guild or list thereof
		if (pFolderIdPar)
		{
			// Fetch its ID. I don't know why this is the encapsulation. Discord....
			auto pFolderId = pFolderIdPar->GetFieldObjectDefault<Protobuf::ObjectVarInt>(1);

			if (pFolderId) {
				folderId = pFolderId->GetValue();
			}
		}
		
		// Also check if its name exists.
		auto pFolderNamePar = item->GetFieldObject(Settings::GuildFolders::Item::FIELD_NAME);
		if (pFolderNamePar)
		{
			auto pFolderName = pFolderNamePar->GetFieldObjectDefault<Protobuf::ObjectString>(1);

			if (pFolderName) {
				folderName = pFolderName->GetContent();
			}
		}

		if (folderId) {
			folders[folderId] = folderName;
		}

		auto pBytesBase = item->GetFieldObject(Settings::GuildFolders::Item::FIELD_GUILD_IDS);

		if (!pBytesBase) {
			DbgPrintF("Guild folders: No guilds!");
			continue;
		}
		if (!pBytesBase->IsByteArrayObject()) {
			DbgPrintF("Guild folders: This ain't a byte array!");
			continue;
		}

		auto pBytes = reinterpret_cast<Protobuf::ObjectBytes*>(pBytesBase);
		std::vector<uint8_t> content = pBytes->GetContent();
		if (content.size() % 8 != 0) {
			DbgPrintF("Guild folders: This ain't a list of 64-bit integers!");
			continue;
		}

		for (size_t i = 0; i < content.size(); i += 8)
		{
			Snowflake sf = *reinterpret_cast<Snowflake*>(content.data() + i);
			
			guilds.push_back(std::make_pair(folderId, sf));
		}
	}
}

Protobuf::DecodeHint* SettingsManager::CreateHint()
{
	// Create a decode hint and use it
	using Protobuf::DecodeHint;
	DecodeHint* pHint = new DecodeHint(DecodeHint::O_MESSAGE); // root

	DecodeHint* pFolderHint = pHint
		->AddChild(Settings::FIELD_GUILD_FOLDERS, DecodeHint::O_MESSAGE)
		->AddChild(Settings::GuildFolders::FIELD_ITEMS, DecodeHint::O_MESSAGE);

	pFolderHint->AddChild(Settings::GuildFolders::Item::FIELD_GUILD_IDS, DecodeHint::O_BYTES);
	pFolderHint->AddChild(Settings::GuildFolders::Item::FIELD_NAME, DecodeHint::O_MESSAGE);
	
	return pHint;
}

#include "../utils/Base64.hpp"

void SettingsManager::LoadDataBase64(const std::string& userSettings)
{
	std::vector<uint8_t> data = Base64Decode(userSettings);
	GetSettingsManager()->LoadData(data.data(), data.size());
}
