// ======================================================================
//
// DataTableColumnType.cpp
//
// copyright 2002 Sony Online Entertainment
//
// ======================================================================

#include "sharedUtility/FirstSharedUtility.h"
#include "sharedUtility/DataTableColumnType.h"

#include "UnicodeUtils.h"
#include "sharedFoundation/Crc.h"
#include "sharedUtility/DataTable.h"
#include "sharedUtility/DataTableManager.h"
#include <cstdio>
#include <map>
#include <stdlib.h>
#include <string.h>

// ----------------------------------------------------------------------

static std::string chomp(std::string const &str)
{
	std::string result = str;
	while(!result.empty() && result[0] == ' ')
		result.erase(0, 1);
	while(!result.empty() && result[result.size() - 1] == ' ')
		result.erase(result.size() - 1, 1);
	return result;
}

static std::string getDelimStr(std::string const &str, char left, char right)
{
	std::string::size_type lPos = str.find(left);
	std::string::size_type rPos = str.rfind(right);

	if (lPos == std::string::npos || rPos == std::string::npos)
		return "";

	return str.substr(lPos+1, rPos-lPos-1);
}

static char *duplicateEnumLabel(std::string const &label)
{
	char * const result = static_cast<char *>(malloc(label.length() + 1));
	if (result)
		strcpy(result, label.c_str());

	return result;
}

static DataTableColumnTypeEnumMap *createEnumMap()
{
	DataTableColumnTypeEnumMap * const enumMap = new DataTableColumnTypeEnumMap;
	enumMap->entries = 0;
	enumMap->count = 0;
	enumMap->capacity = 0;
	return enumMap;
}

static void destroyEnumMap(DataTableColumnTypeEnumMap *enumMap)
{
	if (!enumMap)
		return;

	for (int i = 0; i < enumMap->count; ++i)
	{
		free(enumMap->entries[i].label);
		enumMap->entries[i].label = 0;
	}

	free(enumMap->entries);
	enumMap->entries = 0;
	enumMap->count = 0;
	enumMap->capacity = 0;
	delete enumMap;
}

static bool reserveEnumValues(DataTableColumnTypeEnumMap &enumMap, int const capacity)
{
	if (capacity <= enumMap.capacity)
		return true;

	DataTableColumnTypeEnumEntry * const entries =
		static_cast<DataTableColumnTypeEnumEntry *>(malloc(sizeof(DataTableColumnTypeEnumEntry) * capacity));
	if (!entries)
		return false;

	for (int i = 0; i < capacity; ++i)
	{
		entries[i].label = 0;
		entries[i].value = 0;
	}

	for (int i = 0; i < enumMap.count; ++i)
		entries[i] = enumMap.entries[i];

	free(enumMap.entries);
	enumMap.entries = entries;
	enumMap.capacity = capacity;
	return true;
}

static int findEnumIndex(DataTableColumnTypeEnumMap const &enumMap, std::string const &label)
{
	for (int i = 0; i < enumMap.count; ++i)
	{
		if (enumMap.entries[i].label && strcmp(enumMap.entries[i].label, label.c_str()) == 0)
			return i;
	}

	return -1;
}

static bool setEnumValue(DataTableColumnTypeEnumMap &enumMap, std::string const &label, int const value)
{
	int const existingIndex = findEnumIndex(enumMap, label);
	if (existingIndex >= 0)
	{
		enumMap.entries[existingIndex].value = value;
		return true;
	}

	if (enumMap.count >= enumMap.capacity)
	{
		int const newCapacity = enumMap.capacity > 0 ? enumMap.capacity * 2 : 8;
		if (!reserveEnumValues(enumMap, newCapacity))
			return false;
	}

	char * const labelCopy = duplicateEnumLabel(label);
	if (!labelCopy)
		return false;

	enumMap.entries[enumMap.count].label = labelCopy;
	enumMap.entries[enumMap.count].value = value;
	++enumMap.count;
	return true;
}

static DataTableColumnTypeEnumMap *cloneEnumMap(DataTableColumnTypeEnumMap const *rhs)
{
	if (!rhs)
		return 0;

	DataTableColumnTypeEnumMap * const clone = createEnumMap();
	if (!reserveEnumValues(*clone, rhs->count))
		return clone;

	for (int i = 0; i < rhs->count; ++i)
	{
		if (rhs->entries[i].label)
			IGNORE_RETURN(setEnumValue(*clone, rhs->entries[i].label, rhs->entries[i].value));
	}

	return clone;
}

static void writeStartupTrace(char const *stage, std::string const &desc)
{
	if (desc.find("state.iff") == std::string::npos &&
		desc.find("movementstates.iff") == std::string::npos)
		return;

	char const *path = getenv("SWG_STARTUP_TRACE_FILE");
	if (!path || !*path)
		return;

	FILE *file = fopen(path, "ab");
	if (!file)
		return;

	fprintf(file, "DataTableColumnType:%s desc=%s\n", stage, desc.c_str());
	fclose(file);
}

static void writeStartupTraceValue(char const *stage, std::string const &desc, int const value)
{
	if (desc.find("state.iff") == std::string::npos &&
		desc.find("movementstates.iff") == std::string::npos)
		return;

	char const *path = getenv("SWG_STARTUP_TRACE_FILE");
	if (!path || !*path)
		return;

	FILE *file = fopen(path, "ab");
	if (!file)
		return;

	fprintf(file, "DataTableColumnType:%s value=%d desc=%s\n", stage, value, desc.c_str());
	fclose(file);
}

// ----------------------------------------------------------------------

bool consumePackedObjVarIntField(char const *&s)
{
	while (*s)
	{
		char const c = *s;
		++s;
		if (c == '|')
			return true;
		else if ( c != '-' && !isdigit(c))
			break;
	}
	return false;
}

// ----------------------------------------------------------------------

bool consumePackedObjVarStringField(char const *& s)
{
	while (*s)
	{
		char const c = *s++;
		if (c == '|')
			return true;
	}
	return false;
}

// ----------------------------------------------------------------------

DataTableColumnType::~DataTableColumnType ()
{
	destroyEnumMap(m_enumMap);
	m_enumMap = 0;

	delete m_defaultCell;
	m_defaultCell = 0;
}

// ----------------------------------------------------------------------

DataTableColumnType::DataTableColumnType(std::string const &desc) :
	m_typeSpecString(desc),
	m_type(DT_Unknown),
	m_basicType(DT_Unknown),
	m_defaultValue(),
	m_enumMap(0),
	m_defaultCell(0)
{
	writeStartupTrace("ctor-entry", desc);
	// desc may only look like:
	// {ifshbep}[def] or e(x=0,y=1,z=2,...)[def]

	// first, split into type and default value
	int type = tolower(desc[0]);
	m_defaultValue = getDelimStr(desc, '[', ']');

	if (type == 'i')
	{
		writeStartupTrace("ctor-int", desc);
		m_type = m_basicType = DT_Int;
		if (m_defaultValue.length() == 0)
			m_defaultValue = "0";
	}
	else if (type == 'f')
	{
		m_type = m_basicType = DT_Float;
		if (m_defaultValue.length() == 0)
			m_defaultValue = "0";
	}
	else if (type == 's')
	{
		m_type = m_basicType = DT_String;
	}
	else if (type == 'c')
	{
		m_type = m_basicType = DT_Comment;
	}
	else if (type == 'h')
	{
		m_type = DT_HashString;
		m_basicType = DT_Int;
	}
	else if (type == 'p')
	{
		m_type = DT_PackedObjVars;
		m_basicType = DT_String;
	}
	else if (type == 'b')
	{
		m_type = DT_Bool;
		m_basicType = DT_Int;
		if (m_defaultValue != "1")
			m_defaultValue = "0";
	}
	else if (type == 'e')
	{
		m_enumMap = createEnumMap();
		m_type = DT_Enum;
		m_basicType = DT_Int;
		// build the enumeration map
		std::string enumList = getDelimStr(desc, '(', ')');
		enumList += ",";
		// enumList looks like "foo=0,bar=1,life=42,"
		std::string::size_type eqPos;
		while ((eqPos = enumList.find('=')) != std::string::npos)
		{
			std::string::size_type endPos = enumList.find(',');
			std::string label = enumList.substr(0, eqPos);
			std::string val = enumList.substr(eqPos+1, endPos-eqPos-1);
			IGNORE_RETURN(setEnumValue(*m_enumMap, label, static_cast<int>(strtol(val.c_str(), NULL, 0))));
			enumList.erase(0, endPos+1);
		}
		// assure the default is a member of the enumeration
		if (findEnumIndex(*m_enumMap, m_defaultValue) < 0)
		{
			WARNING(true, ("Default value [%s] is not a member of enumeration", m_defaultValue.c_str()));
			m_basicType = DT_Unknown;
		}
	}
	else if (type == 'v')
	{
		m_enumMap = createEnumMap();
		m_type = DT_BitVector;
		m_basicType = DT_Int;
		// build the enumeration map
		std::string enumList = getDelimStr(desc, '(', ')');
		enumList += ",";
		// enumList looks like "foo=0,bar=1,life=42,"
		std::string::size_type eqPos;
		while ((eqPos = enumList.find('=')) != std::string::npos)
		{
			std::string::size_type endPos = enumList.find(',');
			std::string label = enumList.substr(0, eqPos);
			std::string val = enumList.substr(eqPos+1, endPos-eqPos-1);
			int bit = static_cast<int>(strtol(val.c_str(), NULL, 0));
			if((bit < 1) || (bit > 32))
			{
				WARNING(true, ("Flags value [%s] is not a whole number from 1 to 32", label.c_str()));
				m_basicType = DT_Unknown;
			}
			IGNORE_RETURN(setEnumValue(*m_enumMap, label, 1 << (bit - 1)));
			enumList.erase(0, endPos+1);
		}
		// assure the default is a member of the enumeration
		if(strcmp(m_defaultValue.c_str(), "NONE") != 0)
		{		
			if (findEnumIndex(*m_enumMap, m_defaultValue) < 0)
			{
				WARNING(true, ("Default value [%s] is not a member of enumeration", m_defaultValue.c_str()));
				m_basicType = DT_Unknown;
			}
		}
	}
	else if (type == 'z')
	{
		writeStartupTrace("ctor-z-entry", desc);
		m_enumMap = createEnumMap();
		m_type = DT_Enum;
		m_basicType = DT_Int;
		// get the filename
		std::string fileName = getDelimStr(desc, '(', ')');
		writeStartupTrace("ctor-z-after-filename", desc);

		DataTable * enumTable = DataTableManager::getTable(fileName, true);
		writeStartupTrace("ctor-z-after-get-table", desc);
		if (!enumTable)
		{
			m_basicType = DT_Unknown;
			return;
		}
		int enumCount = enumTable->getNumRows();
		writeStartupTrace("ctor-z-after-row-count", desc);
		writeStartupTraceValue("ctor-z-row-count", desc, enumCount);
		IGNORE_RETURN(reserveEnumValues(*m_enumMap, enumCount));
		writeStartupTrace("ctor-z-after-reserve", desc);
		int x;
		std::string firstKey;
		for (x=0; x<enumCount; ++x)
		{
			writeStartupTrace("ctor-z-row-before-key", desc);
			std::string key = enumTable->getStringValue(0, x);
			writeStartupTrace("ctor-z-row-after-key", desc);
			Unicode::trim(key);
			writeStartupTrace("ctor-z-row-after-trim", desc);
			int value = enumTable->getIntValue(1,x);
			writeStartupTrace("ctor-z-row-after-value", desc);
			if (x==0)
				firstKey = key;
			IGNORE_RETURN(setEnumValue(*m_enumMap, key, value));
			writeStartupTrace("ctor-z-row-after-set", desc);
		}

		// assure the default is a member of the enumeration
		writeStartupTrace("ctor-z-before-default-check", desc);
		if (findEnumIndex(*m_enumMap, m_defaultValue) < 0)
		{
			m_defaultValue = firstKey;
		}
		writeStartupTrace("ctor-z-after-default-check", desc);
	}
	else
		m_basicType = DT_Unknown;

	writeStartupTrace("ctor-before-default-cell", desc);
	createDefaultCell();
	writeStartupTrace("ctor-exit", desc);
}

// ----------------------------------------------------------------------

void DataTableColumnType::createDefaultCell()
{
	writeStartupTrace("default-cell-entry", m_typeSpecString);
	std::string value;

	IS_NULL(m_defaultCell);

	// If mangleValue does not update the input value string,
	//   then the default cell will have a value of 0 for floats and ints
	//     and a value of empty string for strings.
	IGNORE_RETURN(mangleValue(value));
	writeStartupTrace("default-cell-after-mangle", m_typeSpecString);

	switch(m_basicType)
	{
	case DT_Int:
		writeStartupTrace("default-cell-int", m_typeSpecString);
		m_defaultCell = new DataTableCell(static_cast<int>(strtol(value.c_str(), NULL, 0)));
		break;
	case DT_Float:
		writeStartupTrace("default-cell-float", m_typeSpecString);
		m_defaultCell = new DataTableCell(static_cast<float>(atof(value.c_str())));
		break;
	case DT_String:
	case DT_Comment:
	case DT_Unknown:
	default:
		writeStartupTrace("default-cell-string", m_typeSpecString);
		m_defaultCell = new DataTableCell(value.c_str());
		break;
	}
	writeStartupTrace("default-cell-exit", m_typeSpecString);
}

// ----------------------------------------------------------------------

DataTableColumnType::DataTableColumnType(DataTableColumnType const &rhs) :
	m_typeSpecString(rhs.m_typeSpecString),
	m_type(rhs.m_type),
	m_basicType(rhs.m_basicType),
	m_defaultValue(rhs.m_defaultValue),
	m_enumMap(cloneEnumMap(rhs.m_enumMap)),
	m_defaultCell(0)
{
	createDefaultCell();
}

// ----------------------------------------------------------------------

DataTableColumnType& DataTableColumnType::operator=(DataTableColumnType const &rhs)
{
	if (&rhs != this)
	{
		m_typeSpecString = rhs.m_typeSpecString;
		m_type = rhs.m_type;
		m_basicType = rhs.m_basicType;
		m_defaultValue = rhs.m_defaultValue;

		if(rhs.m_enumMap)
		{
			if(!m_enumMap)
				m_enumMap = createEnumMap();

			destroyEnumMap(m_enumMap);
			m_enumMap = cloneEnumMap(rhs.m_enumMap);
		}
		else
		{
			destroyEnumMap(m_enumMap);
			m_enumMap = 0;
		}

		delete m_defaultCell;
		m_defaultCell = 0;

		createDefaultCell();
	}
	return *this;
}

// ----------------------------------------------------------------------

std::string const &DataTableColumnType::getTypeSpecString() const
{
	return m_typeSpecString;
}

// ----------------------------------------------------------------------

DataTableColumnType::DataType DataTableColumnType::getType() const
{
	return m_type;
}

// ----------------------------------------------------------------------

DataTableColumnType::DataType DataTableColumnType::getBasicType() const
{
	return m_basicType;
}

// ----------------------------------------------------------------------

bool DataTableColumnType::lookupEnum(std::string const &label, int &result) const
{
	NOT_NULL(m_enumMap);

	std::string localLabel = chomp(label);

	int const index = findEnumIndex(*m_enumMap, localLabel);
	if (index >= 0)
	{
		result = m_enumMap->entries[index].value;
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------

bool DataTableColumnType::lookupBitVector(std::string const &label, int &result) const
{
	std::string localLabel = label;
	if(strcmp(label.c_str(), "NONE") == 0)
	{
		result = 0;
		return true;
	}
	bool foundAny = false;
	localLabel += ",";
	// enumList looks like "foo,bar,life,"
	std::string::size_type eqPos;	
	while ((eqPos = localLabel.find(',')) != std::string::npos)
	{
		std::string::size_type endPos = localLabel.find(',');
		std::string subLabel = chomp(localLabel.substr(0, eqPos));
		int subResult;
		if(lookupEnum(subLabel, subResult))
		{
			foundAny = true;
			result |= subResult;
		}
		else
		{
			DEBUG_WARNING(true, ("DataTableColumnType::lookupEnumExtended found invalid enum value '%s'", subLabel.c_str()));
		}
		localLabel.erase(0, endPos+1);
	}
				
	return foundAny;
}

// ----------------------------------------------------------------------

bool DataTableColumnType::mangleValue(std::string &value) const
{
	// if the value passed in is empty, we mangle to the default value
	if (value.length() == 0)
	{
		if (m_defaultValue == "required" || m_defaultValue == "unique")
			return false;
		else
			value = m_defaultValue;
	}

	// special validation code for packed objvars
	if (m_type == DT_PackedObjVars)
	{
		// packed objvars are of the form:
		// nameString|typeInt|valueString|nameString|typeInt|valueString|$|
		// where || may be used in the string fields to represent a |
		char const *s = value.c_str();
		while (*s)
		{
			// name
			if (s[0] == '$' && s[1] == '|' && s[2] == '\0')
				break;
			if (!consumePackedObjVarStringField(s))
				return false;
			// type
			if (!consumePackedObjVarIntField(s))
				return false;
			// value
			if (!consumePackedObjVarStringField(s))
				return false;
		}
	}

	// only basic type DT_Int are complex types that use value mangling other
	// than default values
	if (m_basicType != DT_Int || m_type == DT_Int)
		return true;
	// complex type which needs mangling
	switch (m_type)
	{
	case DT_Bool:
		{
			if (value == "0" || value == "1")
				return true;
		}
		break;
	case DT_HashString:
		{
			// conversion to integer crc
			int val;
			if (value.length())
				val = Crc::normalizeAndCalculate(value.c_str());
			else
				val = Crc::crcNull;
			char buf[16];
			sprintf(buf, "%d", val);
			value = buf;
			return true;
		}
		break;
	case DT_Enum:
		{
			// enumeration lookup
			int val = 0;
			if (lookupEnum(value, val))
			{
				char buf[16];
				sprintf(buf, "%d", val);
				value = buf;
				return true;
			}
		}
		break;
	case DT_BitVector:
		{
			// enumeration lookup
			int val = 0;
			if (lookupBitVector(value, val))
			{
				char buf[16];
				sprintf(buf, "%d", val);
				value = buf;
				return true;
			}
		}
		break;
	default:
		break;
	}
	return false;
}

// ----------------------------------------------------------------------

bool DataTableColumnType::areUniqueCellsRequired() const
{
	return m_defaultValue == "unique";
}

// ======================================================================

