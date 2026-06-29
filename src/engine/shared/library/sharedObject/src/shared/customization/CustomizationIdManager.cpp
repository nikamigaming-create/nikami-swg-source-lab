// ======================================================================
//
// CustomizationIdManager.cpp
// Copyright 2003 Sony Online Entertainment, Inc.
// All Rights Reserved.
//
// ======================================================================

#include "sharedObject/FirstSharedObject.h"
#include "sharedObject/CustomizationIdManager.h"

#include "sharedFile/Iff.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedFoundation/LessPointerComparator.h"
#include "sharedFoundation/PersistentCrcString.h"
#include "sharedFoundation/PointerDeleter.h"
#include "sharedFoundation/TemporaryCrcString.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// ======================================================================

namespace CustomizationIdManagerNamespace
{
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	void  remove();

	void  load(Iff &iff);
	void  load_0001(Iff &iff);
	void  writeStartupTrace(char const *stage);
	
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	Tag const TAG_CIDM = TAG(C,I,D,M);
	Tag const TAG_DATA = TAG(D,A,T,A);

	int const cs_firstAssignedId = 1;
	
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	typedef std::vector<CrcString*>                                 StringVector;
	typedef std::vector<std::pair<CrcString const*, int> >          StringIntVector;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	StringVector  s_idToVariableName;
	StringIntVector  s_variableNameToId;
	
	bool  s_installed;
}

using namespace CustomizationIdManagerNamespace;

// ======================================================================
// namespace CustomizationIdManagerNamespace
// ======================================================================

void CustomizationIdManagerNamespace::remove()
{
	DEBUG_FATAL(!s_installed, ("CustomizationIdManager not installed."));
	s_installed = false;
	
	//-- s_idToVariableName owns the strings.  Delete from here, then clean up vector.
	std::for_each(s_idToVariableName.begin(), s_idToVariableName.end(), PointerDeleter());
	StringVector().swap(s_idToVariableName);

	//-- Clear out s_variableNameToId map.
	s_variableNameToId.clear();
}

// ----------------------------------------------------------------------

void CustomizationIdManagerNamespace::writeStartupTrace(char const *stage)
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
	int const count = _snprintf_s(line, sizeof(line), _TRUNCATE, "CustomizationIdManager:%s\r\n", stage ? stage : "unknown");
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

// ----------------------------------------------------------------------

namespace
{
	StringIntVector::iterator findVariableNameId(CrcString const &variableName)
	{
		for (StringIntVector::iterator i = s_variableNameToId.begin(); i != s_variableNameToId.end(); ++i)
		{
			if (i->first && *i->first == variableName)
				return i;
		}

		return s_variableNameToId.end();
	}
}

// ----------------------------------------------------------------------

void CustomizationIdManagerNamespace::load(Iff &iff)
{
	writeStartupTrace("load-entry");
	iff.enterForm(TAG_CIDM);
	writeStartupTrace("load-after-enter-cidm");
	{
		Tag const version = iff.getCurrentName();
		switch (version)
		{
			case TAG_0001:
				writeStartupTrace("load-before-0001");
				load_0001(iff);
				writeStartupTrace("load-after-0001");
				break;

			default:
			{
				char name[5];
				ConvertTagToString(version, name);
				FATAL(true, ("CustomizationIdManager: unsupported data file version [%s].", name));
			}
		}
	}
	iff.exitForm(TAG_CIDM);
	writeStartupTrace("load-exit");
}

// ----------------------------------------------------------------------

void CustomizationIdManagerNamespace::load_0001(Iff &iff)
{
	writeStartupTrace("0001-entry");
	iff.enterForm(TAG_0001);
	writeStartupTrace("0001-after-enter-form");
	{
		iff.enterChunk(TAG_DATA);
		writeStartupTrace("0001-after-enter-data");
		{
			for (int expectedId = 1; iff.getChunkLengthLeft() > 0; ++expectedId)
			{
				writeStartupTrace("0001-row-before-read");
				//-- Read id mapping.
				char variableName[MAX_PATH];
				
				int const id = static_cast<int>(iff.read_int16());
				iff.read_string(variableName, sizeof(variableName) - 1);
				writeStartupTrace("0001-row-after-read");

				//-- Add to id->variableName vector.
				FATAL(id != expectedId, ("CustomizationIdManager load error: non-consecutive ids listed: expecting [%d], found [%d] for [%s].", expectedId, id, variableName));
				FATAL(id >= 128, ("CustomizationIdManager load error: exceeded limit of supported customization variable IDs [%d] for variable [%s].", id, variableName));

				CrcString *crcVariableName = 0;
				s_idToVariableName.push_back(crcVariableName = new PersistentCrcString(variableName, true));
				writeStartupTrace("0001-row-after-string-push");

				//-- Add to variableName->id map.
				StringIntVector::iterator const duplicate = findVariableNameId(*crcVariableName);
				FATAL(duplicate != s_variableNameToId.end(), ("CustomizationIdManager: failed to insert variable name [%s], id [%d] into map. Most likely duplicate entries for variabe name.", variableName, id));
				s_variableNameToId.push_back(std::make_pair(crcVariableName, id));
				writeStartupTrace("0001-row-after-id-push");
			}
		}
		iff.exitChunk(TAG_DATA);
		writeStartupTrace("0001-after-exit-data");
	}
	iff.exitForm(TAG_0001);
	writeStartupTrace("0001-exit");
}

// ======================================================================
// class CustomizationIdManager
// ======================================================================

void CustomizationIdManager::install(char const *dataFilename)
{
	writeStartupTrace("install-entry");
	DEBUG_FATAL(s_installed, ("CustomizationIdManager already installed."));

	//-- Load up data.
	Iff iff;
	writeStartupTrace("install-after-iff-ctor");
	
	bool const openSuccess = iff.open(dataFilename, true);
	writeStartupTrace("install-after-open");
	FATAL(!openSuccess, ("CustomizationIdManager: could not load initialization data file [%s], are TreeFile paths set correctly?", dataFilename));

	writeStartupTrace("install-before-load");
	load(iff);
	writeStartupTrace("install-after-load");
#ifdef _WIN64
	iff.setOwnsData(false);
	writeStartupTrace("install-after-iff-detach-data-x64");
#endif
	
	s_installed = true;
	writeStartupTrace("install-after-installed-flag");
	ExitChain::add(remove, "CustomizationIdManager");
	writeStartupTrace("install-exit");
}

// ----------------------------------------------------------------------

bool CustomizationIdManager::mapIdToString(int id, std::string &variableName)
{
	DEBUG_FATAL(!s_installed, ("CustomizationIdManager not installed."));

	//-- Adjust id for first-assigned id offset.
	id -= cs_firstAssignedId;

	//-- Handle out-of-range id.
	if ((id < 0) || (id >= static_cast<int>(s_idToVariableName.size())))
	{
		// Not found.
		return false;
	}

	//-- Set string value.
	variableName = NON_NULL(s_idToVariableName[static_cast<StringVector::size_type>(id)])->getString();
	return true;
}

// ----------------------------------------------------------------------

bool CustomizationIdManager::mapIdToString(int id, char *variableName, int bufferLength)
{
	DEBUG_FATAL(!s_installed, ("CustomizationIdManager not installed."));

	//-- Adjust id for first-assigned id offset.
	id -= cs_firstAssignedId;

	//-- Handle out-of-range id.
	if ((id < 0) || (id >= static_cast<int>(s_idToVariableName.size())))
	{
		// Not found.
		return false;
	}

	//-- Set string value.
	NOT_NULL(variableName);
	DEBUG_FATAL(bufferLength < 1, ("CustomizationIdManager: bufferLength of [%d] too small to hold anything.", bufferLength));

	//-- Copy variable name to user buffer, ensure it gets NULL terminated.
	strncpy(variableName, NON_NULL(s_idToVariableName[static_cast<StringVector::size_type>(id)])->getString(), static_cast<size_t>(bufferLength - 1));
	variableName[bufferLength - 1] = '\0';
	
	return true;
}

// ----------------------------------------------------------------------

bool CustomizationIdManager::mapStringToId(char const *variableName, int &id)
{
	DEBUG_FATAL(!s_installed, ("CustomizationIdManager not installed."));

	//-- Lookup variable name in map.
	TemporaryCrcString const crcVariableName(variableName, true);
	StringIntVector::iterator const findIt = findVariableNameId(crcVariableName);
	
	if (findIt == s_variableNameToId.end())
	{
		// Not found.
		return false;
	}
	else
	{
		// Found it.
		id = findIt->second;
		return true;
	}
}

// ======================================================================
