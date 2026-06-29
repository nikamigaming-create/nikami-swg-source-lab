// ======================================================================
//
// DataTableManager.cpp
// 
// copyright 2002 Sony Online Entertainment
//
// ======================================================================


#include "sharedUtility/FirstSharedUtility.h"
#include "sharedUtility/DataTableManager.h"

#include "sharedFile/Iff.h"
#include "sharedFile/TreeFile.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedUtility/DataTable.h"
#include "sharedUtility/DataTableCell.h"
#include <cstdio>

// ----------------------------------------------------------------------

DataTable * DataTableManager::m_cachedTable = 0;
std::string DataTableManager::m_cachedTableName = "";
bool DataTableManager::m_errorChecking = false;
std::vector<std::pair<std::string, DataTable *> > DataTableManager::m_tables;
bool DataTableManager::m_installed = false;
std::vector<std::pair<std::string, DataTableReloadCallback> > DataTableManager::m_reloadCallbacks;

namespace
{
	typedef std::vector<std::pair<std::string, DataTable *> > TableVector;
	typedef std::vector<std::pair<std::string, DataTableReloadCallback> > ReloadCallbackVector;

	void writeStartupTrace(char const *stage, char const *table)
	{
#ifdef _WIN32
		char path[MAX_PATH];
		DWORD const result = GetEnvironmentVariable("SWG_STARTUP_TRACE_FILE", path, sizeof(path));
		if (result == 0 || result >= sizeof(path))
			return;

		HANDLE const file = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
			return;

		char line[512];
		int const count = _snprintf_s(line, sizeof(line), _TRUNCATE, "DataTableManager:%s table=%s\r\n", stage, table ? table : "");
		if (count > 0)
		{
			DWORD bytesWritten = 0;
			IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
		}

		CloseHandle(file);
#else
		UNREF(stage);
		UNREF(table);
#endif
	}

	TableVector::iterator findTable(TableVector &tables, std::string const &table)
	{
		for (TableVector::iterator i = tables.begin(); i != tables.end(); ++i)
		{
			if (i->first == table)
				return i;
		}

		return tables.end();
	}

	TableVector::const_iterator findTable(TableVector const &tables, std::string const &table)
	{
		for (TableVector::const_iterator i = tables.begin(); i != tables.end(); ++i)
		{
			if (i->first == table)
				return i;
		}

		return tables.end();
	}

	void setTable(TableVector &tables, std::string const &table, DataTable *dataTable)
	{
		TableVector::iterator const i = findTable(tables, table);
		if (i != tables.end())
			i->second = dataTable;
		else
			tables.push_back(std::make_pair(table, dataTable));
	}
}

// ----------------------------------------------------------------------

void DataTableManager::install(bool verboseErrorChecking)
{
	FATAL(m_installed, ("DataTableManager::install: already installed."));
	m_errorChecking = verboseErrorChecking;
	ExitChain::add(DataTableManager::remove, "DataTableManager::remove");

	DataTableCell::install();

	m_installed = true;
}

// ----------------------------------------------------------------------

void DataTableManager::remove()
{
	FATAL(!m_installed, ("DataTableManager::remove: not installed."));
	m_cachedTable = 0;
	m_cachedTableName = "";

	TableVector::iterator i;
	for (i=m_tables.begin(); i != m_tables.end(); ++i)
	{
		DataTable * toDelete = i->second;
		delete toDelete;
	}

	m_tables.clear();
	m_reloadCallbacks.clear();

	DataTableCell::remove();

	m_installed = false;
}

// ----------------------------------------------------------------------

DataTable* DataTableManager::open(const std::string& table)
{
	writeStartupTrace("open-entry", table.c_str());
	FATAL(!m_installed, ("DataTableManager::open: not installed."));
	DataTable *retVal = getTable(table, false);
	if (retVal)
	{
		writeStartupTrace("open-found-existing", table.c_str());
		return retVal;
	}

	if (!TreeFile::exists(table.c_str()))
	{
		DEBUG_WARNING(true, ("Could not find treefile table for open [%s]", table.c_str()));
		writeStartupTrace("open-missing-treefile", table.c_str());
		return 0;
	}

	retVal = new DataTable;
	writeStartupTrace("open-before-iff-scope", table.c_str());
	{
		writeStartupTrace("open-before-iff", table.c_str());
		Iff iff(table.c_str(), false);
		writeStartupTrace("open-after-iff", table.c_str());
		writeStartupTrace("open-before-load", table.c_str());
		retVal->load(iff);
		writeStartupTrace("open-after-load", table.c_str());
#ifdef _WIN64
		iff.setOwnsData(false);
		writeStartupTrace("open-after-iff-detach-data-x64", table.c_str());
#endif
		writeStartupTrace("open-before-iff-destroy", table.c_str());
	}
	writeStartupTrace("open-after-iff-destroy", table.c_str());

	m_cachedTable = retVal;
	m_cachedTableName = table;
	setTable(m_tables, table, retVal);
	writeStartupTrace("open-after-cache", table.c_str());

	writeStartupTrace("open-before-return", table.c_str());
	return retVal;
}
// ----------------------------------------------------------------------


void DataTableManager::close(const std::string& table)
{
//	FATAL(!m_installed, ("DataTableManager::close: not installed."));
#ifdef _WIN64
	writeStartupTrace("close-retain-x64", table.c_str());
	UNREF(table);
	return;
#else
	TableVector::iterator i = findTable(m_tables, table);
	if (i == m_tables.end())
	{
		DEBUG_WARNING(true, ("Could not find loaded table for close [%s]", table.c_str()));
		return;
	}

	if (m_cachedTable == i->second)
	{
		DEBUG_WARNING(m_cachedTableName != table, ("Cached name got out of sync"));
		m_cachedTableName = "";
		m_cachedTable = 0;
	}

	DataTable * dt = i->second;
	m_tables.erase(i);
	delete dt;
#endif
}
// ----------------------------------------------------------------------


DataTable * DataTableManager::getTable(const std::string& table, bool openIfNotFound)
{
	writeStartupTrace("get-entry", table.c_str());
//	FATAL(!m_installed, ("DataTableManager::getTable: not installed."));
	if (m_cachedTableName == table)
	{
		writeStartupTrace("get-cache-hit", table.c_str());
		return m_cachedTable;
	}

	TableVector::iterator i = findTable(m_tables, table);
	if (i == m_tables.end())
	{
		writeStartupTrace("get-map-miss", table.c_str());
		if (openIfNotFound)
		{
			DataTable * dt = open(table);
			writeStartupTrace("get-after-open", table.c_str());
			if (!dt)
			{
				DEBUG_WARNING(true, ("Could not find table [%s]", table.c_str()));
				writeStartupTrace("get-open-failed", table.c_str());
				return NULL;
			}
			else
			{
				m_cachedTableName = table;
				m_cachedTable = dt;
				writeStartupTrace("get-open-success", table.c_str());
				return dt;
			}
		}
		else
		{
			writeStartupTrace("get-map-miss-no-open", table.c_str());
			return NULL;
		}
	}

	m_cachedTableName = table;
	m_cachedTable = i->second;
	writeStartupTrace("get-map-hit", table.c_str());
	return i->second;
}

// ----------------------------------------------------------------------

DataTable*  DataTableManager::reload(const std::string & table)
{
//	FATAL(!m_installed, ("DataTableManager::reload: not installed."));
	close(table);

	DataTable * const dataTable = open(table);

	if (dataTable != NULL)
	{
		for (ReloadCallbackVector::const_iterator i = m_reloadCallbacks.begin(); i != m_reloadCallbacks.end(); ++i)
		{
			if (i->first == table)
				(*(i->second))(*dataTable);
		}
	}

	return dataTable;
}

// ----------------------------------------------------------------------

DataTable*  DataTableManager::reloadIfOpen(const std::string & table)
{
	if(isOpen(table))
		return reload(table);
	else
		return NULL;
}

// ----------------------------------------------------------------------

void DataTableManager::addReloadCallback(const std::string& table, DataTableReloadCallback callbackFunction)
{
	m_reloadCallbacks.push_back(std::make_pair(table, callbackFunction));
}

// ----------------------------------------------------------------------

bool DataTableManager::isOpen(const std::string& table)
{
	TableVector::const_iterator const i = findTable(m_tables, table);
	return i != m_tables.end();
}

// ======================================================================

