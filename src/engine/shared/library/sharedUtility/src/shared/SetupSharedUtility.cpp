//===================================================================
//
// SetupSharedUtility.cpp
// asommers
//
// copyright 2002, sony online entertainment
//
//===================================================================

#include "sharedUtility/FirstSharedUtility.h"
#include "sharedUtility/SetupSharedUtility.h"

#include "sharedFile/FileManifest.h"
#include "sharedFile/TreeFile.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedUtility/CachedFileManager.h"
#include "sharedUtility/Callback.h"
#include "sharedUtility/ConfigSharedUtility.h"
#include "sharedUtility/CurrentUserOptionManager.h"
#include "sharedUtility/DataTable.h"
#include "sharedUtility/DataTableManager.h"
#include "sharedUtility/LocalMachineOptionManager.h"
#include "sharedUtility/LocationManager.h"
#include "sharedUtility/PooledString.h"
#include "sharedUtility/WorldSnapshotReaderWriter.h"
#include "sharedDebug/InstallTimer.h"

//===================================================================

namespace
{
	bool ms_installed;

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
		int const count = _snprintf_s(line, sizeof(line), _TRUNCATE, "SetupSharedUtility:%s\r\n", stage);
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

//===================================================================

SetupSharedUtility::Data::Data () :
	m_allowFileCaching(false)
{
}

//===================================================================

void SetupSharedUtility::install(SetupSharedUtility::Data const & data)
{
	InstallTimer const installTimer("SetupSharedUtility::install");
	writeStartupTrace("entry");

	DEBUG_FATAL (ms_installed, ("SetupSharedUtility::install already installed"));
	ms_installed = true;
	writeStartupTrace("after-installed-flag");

	ConfigSharedUtility::install();
	writeStartupTrace("after-config");

	CurrentUserOptionManager::install ();
	writeStartupTrace("after-current-user-options");
	LocalMachineOptionManager::install ();
	writeStartupTrace("after-local-machine-options");
	DataTableManager::install ();
	writeStartupTrace("after-data-table-manager");
	WorldSnapshotReaderWriter::Node::install ();
	writeStartupTrace("after-world-snapshot-node");
	Callback::install ();
	writeStartupTrace("after-callback");
	CachedFileManager::install(data.m_allowFileCaching);
	writeStartupTrace("after-cached-file-manager");
	PooledString::install ();
	writeStartupTrace("after-pooled-string");
	LocationManager::install ();
	writeStartupTrace("after-location-manager");
	installFileManifestEntries ();
	writeStartupTrace("after-file-manifest");

	ExitChain::add (SetupSharedUtility::remove, "SetupSharedUtility");
	writeStartupTrace("after-exit-chain");
}

//-------------------------------------------------------------------

void SetupSharedUtility::remove ()
{
	DEBUG_FATAL (!ms_installed, ("SetupSharedUtility::remove not installed"));
	ms_installed = false;
}

//-------------------------------------------------------------------

void SetupSharedUtility::setupGameData(Data & data)
{
	data.m_allowFileCaching = false;
}

//-------------------------------------------------------------------

void SetupSharedUtility::setupToolData(Data & data)
{
	data.m_allowFileCaching = false;
}

//-------------------------------------------------------------------

void SetupSharedUtility::installFileManifestEntries ()
{
	// read in the datatable entries for sharedFile/FileManifest.cpp
	std::string datatableName = FileManifest::getDatatableName();

	FATAL(!TreeFile::exists(datatableName.c_str()), ("%s could not be found. Are your paths set up correctly?", datatableName.c_str()));

	DataTable * manifestDatatable = DataTableManager::getTable(datatableName, true);

	if (manifestDatatable)
	{
		int numRows = manifestDatatable->getNumRows();

		for (int i = 0; i < numRows; ++i)
		{
			std::string fileName = manifestDatatable->getStringValue("fileName", i);
			std::string sceneId = manifestDatatable->getStringValue("sceneId", i);
			int fileSize = manifestDatatable->getIntValue("fileSize", i);

			if (!fileName.empty())
				FileManifest::addStoredManifestEntry(fileName.c_str(), sceneId.c_str(), fileSize);
			else
				DEBUG_WARNING(true, ("SetupSharedUtility::installFileManifestEntries(): found an entry with a null filename: (row %i)\n", i));
		}
	}
	else
		DEBUG_WARNING(true, ("SetupSharedUtility::installFileManifestEntries(): can't find %s\n", datatableName.c_str()));
	DataTableManager::close(datatableName);
}
//===================================================================
