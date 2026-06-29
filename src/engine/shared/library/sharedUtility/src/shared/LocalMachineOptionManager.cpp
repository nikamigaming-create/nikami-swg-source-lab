// ======================================================================
//
// LocalMachineOptionManager.cpp
// asommers
//
// copyright 2003, sony online entertainment
//
// ======================================================================

#include "sharedUtility/FirstSharedUtility.h"
#include "sharedUtility/LocalMachineOptionManager.h"

#include "sharedFoundation/ExitChain.h"
#include "sharedUtility/OptionManager.h"

#include <cstdio>

// ======================================================================
// LocalMachineOptionManagerNamespace
// ======================================================================

namespace LocalMachineOptionManagerNamespace
{
	char const * const cms_fileName = "local_machine_options.iff";

	bool               ms_installed;
	OptionManager *    ms_optionManager;

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
		int const count = _snprintf_s(line, sizeof(line), _TRUNCATE, "LocalMachineOptionManager:%s\r\n", stage);
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

	bool loadOptionsFile(OptionManager * optionManager)
	{
#ifdef _WIN32
		__try
		{
			optionManager->load(cms_fileName);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
#else
		optionManager->load(cms_fileName);
		return true;
#endif
	}
}

using namespace LocalMachineOptionManagerNamespace;

// ======================================================================
// STATIC PUBLIC LocalMachineOptionManager
// ======================================================================

void LocalMachineOptionManager::install ()
{
	writeStartupTrace("entry");
	DEBUG_FATAL (ms_installed, ("LocalMachineOptionManager::install: already installed"));
	ms_installed = true;
	writeStartupTrace("after-installed-flag");

	ms_optionManager = new OptionManager;
	writeStartupTrace("after-new-option-manager");
	if (!loadOptionsFile(ms_optionManager))
	{
		DEBUG_REPORT_LOG(true, ("LocalMachineOptionManager::install: ignoring failed load of %s and using defaults\n", cms_fileName));
		writeStartupTrace("load-exception-using-defaults");
	}
	writeStartupTrace("after-load");

	ExitChain::add (remove, "LocalMachineOptionManager::remove");
	writeStartupTrace("after-exit-chain");
}

// ----------------------------------------------------------------------

void LocalMachineOptionManager::remove ()
{
	DEBUG_FATAL (!ms_installed, ("LocalMachineOptionManager::remove: not installed"));
	ms_installed = false;

	delete ms_optionManager;
	ms_optionManager = 0;
}

// ----------------------------------------------------------------------

void LocalMachineOptionManager::save ()
{
	DEBUG_FATAL (!ms_installed, ("LocalMachineOptionManager::save: not installed"));
	ms_optionManager->save (cms_fileName);
}

// ----------------------------------------------------------------------

void LocalMachineOptionManager::registerOption (bool & variable, char const * const section, char const * const name, const int version)
{
	DEBUG_FATAL (!ms_installed, ("LocalMachineOptionManager::registerOption: not installed"));
	ms_optionManager->registerOption (variable, section, name, version);
}

// ----------------------------------------------------------------------

void LocalMachineOptionManager::registerOption (float & variable, char const * const section, char const * const name, const int version)
{
	DEBUG_FATAL (!ms_installed, ("LocalMachineOptionManager::registerOption: not installed"));
	ms_optionManager->registerOption (variable, section, name, version);
}

// ----------------------------------------------------------------------

void LocalMachineOptionManager::registerOption (int & variable, char const * const section, char const * const name, const int version)
{
	DEBUG_FATAL (!ms_installed, ("LocalMachineOptionManager::registerOption: not installed"));
	ms_optionManager->registerOption (variable, section, name, version);
}

// ----------------------------------------------------------------------

void LocalMachineOptionManager::registerOption (std::string & variable, char const * const section, char const * const name, const int version)
{
	DEBUG_FATAL (!ms_installed, ("LocalMachineOptionManager::registerOption: not installed"));
	ms_optionManager->registerOption (variable, section, name, version);
}

// ----------------------------------------------------------------------

void LocalMachineOptionManager::registerOption (Unicode::String & variable, char const * const section, char const * const name, const int version)
{
	DEBUG_FATAL (!ms_installed, ("LocalMachineOptionManager::registerOption: not installed"));
	ms_optionManager->registerOption (variable, section, name, version);
}

// ----------------------------------------------------------------------

float LocalMachineOptionManager::findFloat(char const * const section, char const * const name, float const defaultValue)
{
	DEBUG_FATAL (!ms_installed, ("LocalMachineOptionManager::findFloat: not installed"));
	return ms_optionManager->findFloat (section, name, defaultValue);
}

// ======================================================================
