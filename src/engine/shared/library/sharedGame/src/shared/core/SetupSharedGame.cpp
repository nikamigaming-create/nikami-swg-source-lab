// ======================================================================
//
// SetupSharedGame.cpp
// Copyright 2002-2003 Sony Online Entertainment, Inc.
// All Rights Reserved.
//
// ======================================================================

#include "sharedGame/FirstSharedGame.h"
#include "sharedGame/SetupSharedGame.h"

#include "LocalizationManager.h"
#include "sharedDebug/InstallTimer.h"
#include "sharedFile/TreeFile.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedGame/AppearanceManager.h"
#include "sharedGame/AssetCustomizationManager.h"
#include "sharedGame/AsteroidGenerationManager.h"
#include "sharedGame/CitizenRankDataTable.h"
#include "sharedGame/ClientCombatManagerSupport.h"
#include "sharedGame/CollectionsDataTable.h"
#include "sharedGame/CombatDataTable.h"
#include "sharedGame/ConfigSharedGame.h"
#include "sharedGame/CustomizationManager.h"
#include "sharedGame/GameLanguageManager.h"
#include "sharedGame/GameScheduler.h"
#include "sharedGame/GroundZoneManager.h"
#include "sharedGame/GuildRankDataTable.h"
#include "sharedGame/HyperspaceManager.h"
#include "sharedGame/LfgDataTable.h"
#include "sharedGame/MoodManager.h"
#include "sharedGame/MountValidScaleRangeTable.h"
#include "sharedGame/NebulaManager.h"
#include "sharedGame/ObjectUsabilityManager.h"
#include "sharedGame/PlayerFormationManager.h"
#include "sharedGame/SharedBuffBuilderManager.h"
#include "sharedGame/SharedBuildoutAreaManager.h"
#include "sharedGame/SharedImageDesignerManager.h"
#include "sharedGame/SharedSaddleManager.h"
#include "sharedGame/ShipChassis.h"
#include "sharedGame/ShipComponentAttachmentManager.h"
#include "sharedGame/ShipComponentDescriptor.h"
#include "sharedGame/ShipComponentWeaponManager.h"
#include "sharedGame/ShipSlotIdManager.h"
#include "sharedGame/ShipTurretManager.h"
#include "sharedGame/SpatialChatManager.h"
#include "sharedGame/TextManager.h"
#include "sharedGame/TravelManager.h"
#include "sharedGame/Waypoint.h"
#include "sharedGame/WearableAppearanceMap.h"

#include <cstdio>

// ======================================================================

namespace SetupSharedGameNamespace
{
	bool ms_installed;

	void remove ();

	void writeStartupTrace(char const *stage)
	{
#ifdef _WIN32
		char path[MAX_PATH];
		DWORD const result = GetEnvironmentVariable("SWG_STARTUP_TRACE_FILE", path, sizeof(path));
		if (result == 0 || result >= sizeof(path))
			return;

		HANDLE const file = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
			return;

		char line[256];
		int const count = _snprintf_s(line, sizeof(line), _TRUNCATE, "SetupSharedGame:%s\r\n", stage);
		if (count > 0)
		{
			DWORD bytesWritten = 0;
			IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
		}

		CloseHandle(file);
#else
		UNREF(stage);
#endif
	}
}

using namespace SetupSharedGameNamespace;

// ======================================================================
// PUBLIC SetupSharedGame::Data
// ======================================================================

SetupSharedGame::Data::Data () :
m_debugBadStringsFunc (0),
m_useGameScheduler    (false),
m_useMountValidScaleRangeTable (false),
m_useWearableAppearanceMap (true),
m_useClientCombatManagerSupport (false)
{
}

// ----------------------------------------------------------------------

void SetupSharedGame::Data::setUseMountValidScaleRangeTable (bool useIt)
{
	m_useMountValidScaleRangeTable = useIt;
}

// ----------------------------------------------------------------------

bool SetupSharedGame::Data::getUseMountValidScaleRangeTable () const
{
	return m_useMountValidScaleRangeTable;
}

// ----------------------------------------------------------------------

void SetupSharedGame::Data::setUseGameScheduler (bool useIt)
{
	m_useGameScheduler = useIt;
}

// ----------------------------------------------------------------------

bool SetupSharedGame::Data::getUseGameScheduler () const
{
	return m_useGameScheduler;
}

// ----------------------------------------------------------------------

void SetupSharedGame::Data::setUseWearableAppearanceMap (bool useIt)
{
	m_useWearableAppearanceMap = useIt;
}

// ----------------------------------------------------------------------

bool SetupSharedGame::Data::getUseWearableAppearanceMap () const
{
	return m_useWearableAppearanceMap;
}

// ----------------------------------------------------------------------

void SetupSharedGame::Data::setUseClientCombatManagerSupport(bool useIt)
{
	m_useClientCombatManagerSupport = useIt;
}

// ----------------------------------------------------------------------

bool SetupSharedGame::Data::getUseClientCombatManagerSupport() const
{
	return m_useClientCombatManagerSupport;
}

// ======================================================================
// STATIC PUBLIC SetupSharedGame
// ======================================================================

void SetupSharedGame::install (const SetupSharedGame::Data& data)
{
	InstallTimer const installTimer("SetupSharedGame::install");
	writeStartupTrace("entry");

	DEBUG_FATAL (ms_installed, ("SetupSharedGame::install already installed"));
	ms_installed = true;
	writeStartupTrace("after-installed-flag");

	ConfigSharedGame::install();
	writeStartupTrace("after-config");

	//-- TODO: add to ExitChain in install functions
	SpatialChatManager::install();
	ExitChain::add(SpatialChatManager::remove, "SpatialChatManager::remove");
	writeStartupTrace("after-spatial-chat");

	MoodManager::install();
	ExitChain::add(MoodManager::remove, "MoodManager::remove");
	writeStartupTrace("after-mood");

	const bool displayBadStringIds   = ConfigSharedGame::getDisplayBadStringIds ();
	const bool debugStringIds        = ConfigSharedGame::getDebugStringIds      ();
	Unicode::NarrowString defaultLocale(ConfigSharedGame::getDefaultLocale ());
	Unicode::UnicodeNarrowStringVector localeVector;
	localeVector.push_back(defaultLocale);

	LocalizationManager::install (new TreeFile::TreeFileFactory, localeVector, debugStringIds, data.m_debugBadStringsFunc, displayBadStringIds);
	ExitChain::add(LocalizationManager::remove, "LocalizationManager::remove");
	writeStartupTrace("after-localization");

	TravelManager::install ();
	writeStartupTrace("after-travel");

	if (data.getUseGameScheduler ())
		GameScheduler::install ();
	writeStartupTrace("after-game-scheduler");

	writeStartupTrace("before-appearance-manager");
	AppearanceManager::install();
	writeStartupTrace("after-appearance-manager");
	writeStartupTrace("before-customization-manager");
	CustomizationManager::install();
	writeStartupTrace("after-customization-manager");
	writeStartupTrace("before-buff-builder");
	SharedBuffBuilderManager::install();
	writeStartupTrace("after-buff-builder");
	writeStartupTrace("before-image-designer");
	SharedImageDesignerManager::install();
	writeStartupTrace("after-image-designer");
	writeStartupTrace("before-text-manager");
	TextManager::install();
	writeStartupTrace("after-text-manager");
	writeStartupTrace("before-language-manager");
	GameLanguageManager::install();
	writeStartupTrace("after-language-manager");
	writeStartupTrace("before-ship-chassis");
	ShipChassis::install ();
	writeStartupTrace("after-ship-chassis");
	writeStartupTrace("before-ship-component-descriptor");
	ShipComponentDescriptor::install ();
	writeStartupTrace("after-ship-component-descriptor");
	writeStartupTrace("before-ship-component-weapon");
	ShipComponentWeaponManager::install ();
	writeStartupTrace("after-ship-component-weapon");
	writeStartupTrace("before-ship-component-attachment");
	ShipComponentAttachmentManager::install ();
	writeStartupTrace("after-ship-component-attachment");
	writeStartupTrace("before-ship-slot");
	ShipSlotIdManager::install ();
	writeStartupTrace("after-ship-slot");
	writeStartupTrace("before-ship-turret");
	ShipTurretManager::install ();
	writeStartupTrace("after-ship-turret");
	writeStartupTrace("before-object-usability");
	ObjectUsabilityManager::install();
	writeStartupTrace("after-object-usability");

	writeStartupTrace("before-asset-customization");
	AssetCustomizationManager::install ("customization/asset_customization_manager.iff");
	writeStartupTrace("after-asset-customization");

	if (data.getUseMountValidScaleRangeTable ())
		MountValidScaleRangeTable::install ("datatables/mount/valid_scale_range.iff");

	if (data.getUseWearableAppearanceMap ())
		WearableAppearanceMap::install ("datatables/appearance/wearable_appearance_map.iff");

	if (data.getUseClientCombatManagerSupport ())
		ClientCombatManagerSupport::install ("combat/combat_manager.iff");

	writeStartupTrace("before-asteroid-generation");
	AsteroidGenerationManager::install();
	writeStartupTrace("after-asteroid-generation");
	writeStartupTrace("before-nebula");
	NebulaManager::install();
	writeStartupTrace("after-nebula");
	writeStartupTrace("before-hyperspace");
	HyperspaceManager::install();
	writeStartupTrace("after-hyperspace");
	writeStartupTrace("before-player-formation");
	PlayerFormationManager::install();
	writeStartupTrace("after-player-formation");
	writeStartupTrace("before-collections");
	CollectionsDataTable::install();
	writeStartupTrace("after-collections");
	writeStartupTrace("before-lfg");
	LfgDataTable::install();
	writeStartupTrace("after-lfg");
	writeStartupTrace("before-guild-rank");
	GuildRankDataTable::install();
	writeStartupTrace("after-guild-rank");
	writeStartupTrace("before-citizen-rank");
	CitizenRankDataTable::install();
	writeStartupTrace("after-citizen-rank");
	writeStartupTrace("before-shared-buildout-area");
	SharedBuildoutAreaManager::install();
	writeStartupTrace("after-shared-buildout-area");
	writeStartupTrace("before-ground-zone");
	GroundZoneManager::install();
	writeStartupTrace("after-ground-zone");

	writeStartupTrace("before-waypoint");
	Waypoint::install();
	writeStartupTrace("after-waypoint");

	writeStartupTrace("before-shared-saddle");
	SharedSaddleManager::install();
	writeStartupTrace("after-shared-saddle");
	writeStartupTrace("before-combat-data");
	CombatDataTable::install();
	writeStartupTrace("after-combat-data");
	ExitChain::add (SetupSharedGameNamespace::remove, "SetupSharedGameNamespace::remove");
}

// ======================================================================
// STATIC PRIVATE SetupSharedGame
// ======================================================================

void SetupSharedGameNamespace::remove ()
{
	DEBUG_FATAL (!ms_installed, ("SetupSharedGameNamespace::remove not installed"));
	ms_installed = false;
}

// ======================================================================
