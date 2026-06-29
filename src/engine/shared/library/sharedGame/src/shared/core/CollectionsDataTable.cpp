// ======================================================================
//
// CollectionsDataTable.cpp
// Copyright 2006 Sony Online Entertainment LLC (SOE)
// All rights reserved.
//
// ======================================================================

#include "sharedGame/FirstSharedGame.h"
#include "sharedGame/CollectionsDataTable.h"

#include "UnicodeUtils.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedUtility/DataTableManager.h"
#include "sharedUtility/DataTable.h"

#include <cstdio>
#include <map>
#ifdef _WIN64
#include <malloc.h>
#include <new>
#endif
#include <set>
#include <utility>
#include <vector>

// ======================================================================

namespace CollectionsDataTableNamespace
{
	char const * const cs_collectionsDataTableName = "datatables/collection/collection.iff";

	template <typename T>
	struct NamedListBucket
	{
		NamedListBucket(std::string const & bucketKey) :
			key(bucketKey),
			values()
		{
		}

		std::string key;
		std::vector<T> values;
	};

	//----------------------------------------------------------------------
	// slots

	// all slots by slot name. Vector-backed on x64 startup for the same reason
	// as s_allCollectionsByName: this table has thousands of entries.
	std::vector<CollectionsDataTable::CollectionInfoSlot const *> s_allSlotsByName;

	// all slots by begin slot id
	// for quick lookup of a slot by begin slot id, we use a vector because
	// slot id are guaranteed to start at 0 and be contiguous; for counter-type
	// slot, only the begin slot id index points to the slot; the other slot
	// id indices point to NULL
	std::vector<CollectionsDataTable::CollectionInfoSlot const *> s_allSlotsById;

	// all title(able) slots
	std::vector<CollectionsDataTable::CollectionInfoSlot const *> s_allTitleableSlots;

	// all slot titles
	std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoSlot const *> > s_allSlotTitles;

	//----------------------------------------------------------------------

#ifdef _WIN64
	void * s_slotPoolStorage = NULL;
	size_t s_slotPoolCapacity = 0;
	size_t s_slotPoolUsed = 0;

	void initializeSlotPool(size_t const capacity)
	{
		if (s_slotPoolStorage)
		{
			_aligned_free(s_slotPoolStorage);
			s_slotPoolStorage = NULL;
		}

		s_slotPoolCapacity = capacity;
		s_slotPoolUsed = 0;
		s_slotPoolStorage = _aligned_malloc(sizeof(CollectionsDataTable::CollectionInfoSlot) * capacity, __alignof(CollectionsDataTable::CollectionInfoSlot));
		FATAL(!s_slotPoolStorage, ("%s: failed to allocate x64 slot pool for %u entries", cs_collectionsDataTableName, static_cast<unsigned int>(capacity)));
	}

	void * allocateSlotPoolEntry()
	{
		FATAL(!s_slotPoolStorage || (s_slotPoolUsed >= s_slotPoolCapacity), ("%s: x64 slot pool exhausted", cs_collectionsDataTableName));
		char * const storage = static_cast<char *>(s_slotPoolStorage) + (sizeof(CollectionsDataTable::CollectionInfoSlot) * s_slotPoolUsed);
		++s_slotPoolUsed;
		return storage;
	}

	void destroySlotPool()
	{
		if (s_slotPoolStorage)
		{
			_aligned_free(s_slotPoolStorage);
			s_slotPoolStorage = NULL;
		}

		s_slotPoolCapacity = 0;
		s_slotPoolUsed = 0;
	}
#endif

	//----------------------------------------------------------------------

	//----------------------------------------------------------------------
	// collections

	// slots in each collection
	std::vector<NamedListBucket<CollectionsDataTable::CollectionInfoSlot const *> *> s_slotsInCollection;

	// all title(able) collections
	std::vector<CollectionsDataTable::CollectionInfoCollection const *> s_allTitleableCollections;

	// all collection titles
	std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> > s_allCollectionTitles;

	// all collections that have "server first" tracking
	std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> > s_allServerFirstCollections;

	// all collections by collection name. Kept vector-backed on x64 startup to
	// avoid fragile STLport tree growth while loading the large collection table.
	std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> > s_allCollectionsByName;

	//----------------------------------------------------------------------

	//----------------------------------------------------------------------
	// pages

	// slots in each page
	std::vector<NamedListBucket<CollectionsDataTable::CollectionInfoSlot const *> *> s_slotsInPage;

	// collections in each page
	std::vector<NamedListBucket<CollectionsDataTable::CollectionInfoCollection const *> *> s_collectionsInPage;

	// all title(able) pages
	std::vector<CollectionsDataTable::CollectionInfoPage const *> s_allTitleablePages;

	// all page titles
	std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoPage const *> > s_allPageTitles;

	// all pages by page name
	std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoPage const *> > s_allPagesByName;

	//----------------------------------------------------------------------

	//----------------------------------------------------------------------
	// books

	// slots in each book
	std::vector<NamedListBucket<CollectionsDataTable::CollectionInfoSlot const *> *> s_slotsInBook;

	// collections in each book
	std::vector<NamedListBucket<CollectionsDataTable::CollectionInfoCollection const *> *> s_collectionsInBook;

	// pages in each book
	std::vector<NamedListBucket<CollectionsDataTable::CollectionInfoPage const *> *> s_pagesInBook;

	// all books
	std::vector<CollectionsDataTable::CollectionInfoBook const *> s_allBooks;

	// all books by book name
	std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoBook const *> > s_allBooksByName;

	//----------------------------------------------------------------------

	//----------------------------------------------------------------------
	// categories

	template <typename Key>
	struct CategorySlotBucket
	{
		CategorySlotBucket(Key const & bucketKey) :
			key(bucketKey),
			slots()
		{
		}

		Key key;
		std::vector<CollectionsDataTable::CollectionInfoSlot const *> slots;
	};

	// slots in each category
	std::vector<CategorySlotBucket<std::string> *> s_slotsInCategory;

	// slots in each (collection, category)
	std::vector<CategorySlotBucket<std::pair<std::string, std::string> > *> s_slotsInCategoryByCollection;

	// slots in each (page, category)
	std::vector<CategorySlotBucket<std::pair<std::string, std::string> > *> s_slotsInCategoryByPage;

	// slots in each (book, category)
	std::vector<CategorySlotBucket<std::pair<std::string, std::string> > *> s_slotsInCategoryByBook;

	// all categories
	std::vector<std::string> s_slotCategories;

	// categories in each collection
	std::vector<std::pair<std::string, std::vector<std::string> > > s_slotCategoriesByCollection;

	// categories in each page
	std::vector<std::pair<std::string, std::vector<std::string> > > s_slotCategoriesByPage;

	// categories in each book
	std::vector<std::pair<std::string, std::vector<std::string> > > s_slotCategoriesByBook;

	//----------------------------------------------------------------------
	
	// the DB can only currently handle this many collections
	//
	// !!!!!!!!!!!!!!!!!!!!!!!!!WARNING!!!!!!!!!!!!!!!!!!!!!!!!!
	// ***DO NOT*** change this value unless you know what you
	// are doing, or you will crash the SWG DB Server!!!!!!!!!!!
	// !!!!!!!!!!!!!!!!!!!!!!!!!WARNING!!!!!!!!!!!!!!!!!!!!!!!!!
	int const COLLECTIONS_PER_INDEX = 16000;
	int const MAX_COLLECTIONS = COLLECTIONS_PER_INDEX * 2;

	void writeStartupTrace(char const *stage, int row = -1)
	{
#ifdef _WIN32
		char verbose[MAX_PATH];
		DWORD const verboseResult = GetEnvironmentVariable("SWG_VERBOSE_STARTUP_TRACE", verbose, sizeof(verbose));
		if (verboseResult == 0 || verboseResult >= sizeof(verbose))
			return;

		char path[MAX_PATH];
		DWORD const result = GetEnvironmentVariable("SWG_STARTUP_TRACE_FILE", path, sizeof(path));
		if (result == 0 || result >= sizeof(path))
			return;

		HANDLE const file = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
			return;

		char line[512];
		int const count = _snprintf_s(line, sizeof(line), _TRUNCATE, "CollectionsDataTable:%s row=%d\r\n", stage, row);
		if (count > 0)
		{
			DWORD bytesWritten = 0;
			IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
		}

		CloseHandle(file);
#else
		UNREF(stage);
		UNREF(row);
#endif
	}

	template <typename T>
	bool hasEntries(std::map<std::string, std::vector<T> > const & container, std::string const & key)
	{
		typename std::map<std::string, std::vector<T> >::const_iterator const iter = container.find(key);
		return (iter != container.end()) && !iter->second.empty();
	}

	template <typename T>
	bool hasEntries(std::vector<NamedListBucket<T> *> const & container, std::string const & key)
	{
		for (typename std::vector<NamedListBucket<T> *>::const_iterator iter = container.begin(); iter != container.end(); ++iter)
		{
			if ((*iter)->key == key)
				return !(*iter)->values.empty();
		}

		return false;
	}

	template <typename T>
	std::vector<T> & findOrCreateNamedList(std::vector<NamedListBucket<T> *> & container, std::string const & key, int reserveCount)
	{
		for (typename std::vector<NamedListBucket<T> *>::iterator iter = container.begin(); iter != container.end(); ++iter)
		{
			if ((*iter)->key == key)
				return (*iter)->values;
		}

		NamedListBucket<T> * const bucket = new NamedListBucket<T>(key);
		int const cappedReserveCount = (reserveCount > 128) ? 128 : reserveCount;
		if (cappedReserveCount > 0)
			bucket->values.reserve(cappedReserveCount);
		container.push_back(bucket);
		return bucket->values;
	}

	template <typename T>
	std::vector<T> const * findNamedList(std::vector<NamedListBucket<T> *> const & container, std::string const & key)
	{
		for (typename std::vector<NamedListBucket<T> *>::const_iterator iter = container.begin(); iter != container.end(); ++iter)
		{
			if ((*iter)->key == key)
				return &(*iter)->values;
		}

		return NULL;
	}

	template <typename T>
	void deleteNamedListBuckets(std::vector<NamedListBucket<T> *> & container)
	{
		for (typename std::vector<NamedListBucket<T> *>::iterator iter = container.begin(); iter != container.end(); ++iter)
			delete *iter;

		container.clear();
	}

	int findNameRow(std::vector<std::pair<std::string, int> > const & names, std::string const & name)
	{
		for (std::vector<std::pair<std::string, int> >::const_iterator iter = names.begin(); iter != names.end(); ++iter)
		{
			if (iter->first == name)
				return iter->second;
		}

		return -1;
	}

	template <typename Key>
	std::vector<CollectionsDataTable::CollectionInfoSlot const *> & findOrCreateCategorySlots(std::vector<CategorySlotBucket<Key> *> & buckets, Key const & key, int reserveCount)
	{
		for (typename std::vector<CategorySlotBucket<Key> *>::iterator iter = buckets.begin(); iter != buckets.end(); ++iter)
		{
			if ((*iter)->key == key)
				return (*iter)->slots;
		}

		CategorySlotBucket<Key> * const bucket = new CategorySlotBucket<Key>(key);
		int const cappedReserveCount = (reserveCount > 128) ? 128 : reserveCount;
		if (cappedReserveCount > 0)
			bucket->slots.reserve(cappedReserveCount);
		buckets.push_back(bucket);
		return bucket->slots;
	}

	template <typename Key>
	void deleteCategorySlotBuckets(std::vector<CategorySlotBucket<Key> *> & buckets)
	{
		for (typename std::vector<CategorySlotBucket<Key> *>::iterator iter = buckets.begin(); iter != buckets.end(); ++iter)
			delete *iter;

		buckets.clear();
	}

	bool containsString(std::vector<std::string> const & values, std::string const & value)
	{
		for (std::vector<std::string>::const_iterator iter = values.begin(); iter != values.end(); ++iter)
		{
			if (*iter == value)
				return true;
		}

		return false;
	}

	bool hasSlotsInBook(std::string const & bookName)
	{
		for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
		{
			CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
			if (slot && slot->collection.page.book.name == bookName)
				return true;
		}

		return false;
	}

	bool hasCollectionsInPage(std::string const & pageName)
	{
		for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iterFind = s_allCollectionsByName.begin(); iterFind != s_allCollectionsByName.end(); ++iterFind)
		{
			CollectionsDataTable::CollectionInfoCollection const * const collection = iterFind->second;
			if (collection && collection->page.name == pageName)
				return true;
		}

		return false;
	}

	bool hasCollectionsInBook(std::string const & bookName)
	{
		for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iterFind = s_allCollectionsByName.begin(); iterFind != s_allCollectionsByName.end(); ++iterFind)
		{
			CollectionsDataTable::CollectionInfoCollection const * const collection = iterFind->second;
			if (collection && collection->page.book.name == bookName)
				return true;
		}

		return false;
	}

	void addUniqueString(std::vector<std::string> & values, std::string const & value)
	{
		if (!containsString(values, value))
			values.push_back(value);
	}

	std::vector<std::string> & findOrCreateCategoryNameList(std::vector<std::pair<std::string, std::vector<std::string> > > & categories, std::string const & key)
	{
		for (std::vector<std::pair<std::string, std::vector<std::string> > >::iterator iter = categories.begin(); iter != categories.end(); ++iter)
		{
			if (iter->first == key)
				return iter->second;
		}

		categories.push_back(std::make_pair(key, std::vector<std::string>()));
		categories.back().second.reserve(16);
		return categories.back().second;
	}

	void fillSet(std::set<std::string> & output, std::vector<std::string> const & input)
	{
		output.clear();
		for (std::vector<std::string>::const_iterator iter = input.begin(); iter != input.end(); ++iter)
			IGNORE_RETURN(output.insert(*iter));
	}
}

using namespace CollectionsDataTableNamespace;

// ======================================================================

void CollectionsDataTable::install()
{
	writeStartupTrace("install-entry");
	DataTable * table = DataTableManager::getTable(cs_collectionsDataTableName, true);
	writeStartupTrace("after-get-table");
	if (table)
	{
		int const columnBookName = table->findColumnNumber("bookName");
		int const columnPageName = table->findColumnNumber("pageName");
		int const columnCollectionName = table->findColumnNumber("collectionName");
		int const columnSlotName = table->findColumnNumber("slotName");
		int const columnBeginSlotId = table->findColumnNumber("beginSlotId");
		int const columnEndSlotId = table->findColumnNumber("endSlotId");
		int const columnMaxSlotValue = table->findColumnNumber("maxSlotValue");
		int const columnIcon = table->findColumnNumber("icon");
		int const columnMusic = table->findColumnNumber("music");
		int const columnShowIfNotYetEarned = table->findColumnNumber("showIfNotYetEarned");
		int const columnHidden = table->findColumnNumber("hidden");
		int const columnTitle = table->findColumnNumber("title");
		int const columnNoReward = table->findColumnNumber("noReward");
		int const columnTrackServerFirst = table->findColumnNumber("trackServerFirst");
		writeStartupTrace("after-required-columns");

		// the can be a variable number of "category" columns, as long as the columns
		// are named category1, category2, category3, category4, category5, and so on
		std::vector<int> columnCategory;
		char buffer[128];
		int columnNumber;
		for (int i = 1; i <= 1000000000; ++i)
		{
			snprintf(buffer, sizeof(buffer)-1, "category%d", i);
			buffer[sizeof(buffer)-1] = '\0';

			columnNumber = table->findColumnNumber(buffer);
			if (columnNumber < 0)
				break;

			columnCategory.push_back(columnNumber);
		}
		writeStartupTrace("after-category-columns");

		// the can be a variable number of "prereq" columns, as long as the columns
		// are named prereqSlotName1, prereqSlotName2, prereqSlotName3, prereqSlotName4,
		// prereqSlotName5, and so on
		std::vector<int> columnPrereq;
		for (int i = 1; i <= 1000000000; ++i)
		{
			snprintf(buffer, sizeof(buffer)-1, "prereqSlotName%d", i);
			buffer[sizeof(buffer)-1] = '\0';

			columnNumber = table->findColumnNumber(buffer);
			if (columnNumber < 0)
				break;

			columnPrereq.push_back(columnNumber);
		}
		writeStartupTrace("after-prereq-columns");

		// the can be a variable number of "alternate title" columns, as long as the columns
		// are named alternateTitle1, alternateTitle2, alternateTitle3, alternateTitle4,
		// alternateTitle5, and so on
		std::vector<int> columnAlternateTitle;
		for (int i = 1; i <= 1000000000; ++i)
		{
			snprintf(buffer, sizeof(buffer)-1, "alternateTitle%d", i);
			buffer[sizeof(buffer)-1] = '\0';

			columnNumber = table->findColumnNumber(buffer);
			if (columnNumber < 0)
				break;

			columnAlternateTitle.push_back(columnNumber);
		}
		writeStartupTrace("after-alternate-title-columns");

		FATAL((columnBookName < 0), ("column \"bookName\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnPageName < 0), ("column \"pageName\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnCollectionName < 0), ("column \"collectionName\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnSlotName < 0), ("column \"slotName\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnBeginSlotId < 0), ("column \"beginSlotId\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnEndSlotId < 0), ("column \"endSlotId\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnMaxSlotValue < 0), ("column \"maxSlotValue\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnIcon < 0), ("column \"icon\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnMusic < 0), ("column \"music\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnShowIfNotYetEarned < 0), ("column \"showIfNotYetEarned\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnHidden < 0), ("column \"hidden\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnTitle < 0), ("column \"title\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnNoReward < 0), ("column \"noReward\" not found in %s", cs_collectionsDataTableName));
		FATAL((columnTrackServerFirst < 0), ("column \"trackServerFirst\" not found in %s", cs_collectionsDataTableName));

		CollectionsDataTable::CollectionInfoBook const * currentBook = NULL;
		CollectionsDataTable::CollectionInfoPage const * currentPage = NULL;
		CollectionsDataTable::CollectionInfoCollection const * currentCollection = NULL;
		CollectionsDataTable::CollectionInfoSlot const * currentSlot = NULL;

		int const numRows = table->getNumRows();
		writeStartupTrace("after-row-count", numRows);
		int const startupReserveRows = (numRows > 512) ? 512 : numRows;
		s_allSlotsByName.reserve(numRows);
#ifdef _WIN64
		initializeSlotPool(static_cast<size_t>(numRows));
#endif
		s_allCollectionsByName.reserve(startupReserveRows);
		s_slotsInCollection.reserve(startupReserveRows);
		s_slotsInPage.reserve(startupReserveRows);
		s_collectionsInPage.reserve(startupReserveRows);
		s_slotsInBook.reserve(64);
		s_collectionsInBook.reserve(64);
		s_pagesInBook.reserve(64);
		s_slotCategories.reserve(startupReserveRows);
		s_slotCategoriesByCollection.reserve(startupReserveRows);
		s_slotCategoriesByPage.reserve(64);
		s_slotCategoriesByBook.reserve(64);
		writeStartupTrace("after-reserves", numRows);
		std::string bookName, pageName, collectionName, slotName, category, prereq, alternateTitle, icon, music;
		std::vector<std::string> categories;
		std::vector<std::string> prereqs;
		std::vector<std::string> titles;
		int beginSlotId, endSlotId, tempBeginSlotId, tempEndSlotId, maxSlotValue;
		unsigned long maxValueForNumBits;
		ShowIfNotYetEarnedType showIfNotYetEarned;
		bool hidden, notifyScriptOnModify, title, noReward, trackServerFirst;
		int tempCount;
		std::vector<std::pair<std::string, int> > names;
#ifndef _WIN64
		names.reserve(numRows);
#endif
		std::vector<std::pair<std::string, int> > allPrereqs;
#ifndef _WIN64
		allPrereqs.reserve(numRows);
#endif
		std::vector<CollectionsDataTable::CollectionInfoSlot const *> allSlotsById;
#ifndef _WIN64
		allSlotsById.resize(MAX_COLLECTIONS, NULL);
#endif
		std::vector<int> allSlotRowsById;
#ifndef _WIN64
		allSlotRowsById.resize(MAX_COLLECTIONS, -1);
#endif
		std::vector<std::string> allSlotNamesById;
#ifndef _WIN64
		allSlotNamesById.resize(MAX_COLLECTIONS);
#endif
		int highestSlotId = -1;
		for (int i = 0; i < numRows; ++i)
		{
			writeStartupTrace("row-start", i);
			bookName = table->getStringValue(columnBookName, i);
			pageName = table->getStringValue(columnPageName, i);
			collectionName = table->getStringValue(columnCollectionName, i);
			slotName = table->getStringValue(columnSlotName, i);
			writeStartupTrace("row-after-names", i);

			if (bookName.empty() && pageName.empty() && collectionName.empty() && slotName.empty())
				continue;

			// only one of bookName, pageName, collectionName, or slotName can be specified
			// and bookName, pageName, collectionName, and slotName must be unique
			tempCount = 0;
#ifdef _WIN64
			if (!bookName.empty())
				++tempCount;
			if (!pageName.empty())
				++tempCount;
			if (!collectionName.empty())
				++tempCount;
			if (!slotName.empty())
				++tempCount;
#else
			if (!bookName.empty())
			{
				int const duplicateRow = findNameRow(names, bookName);
				FATAL((duplicateRow >= 0), ("%s, row %d: book name %s already used at row %d (either as a book, page, collection, slot, or alternate title)", cs_collectionsDataTableName, (i+3), bookName.c_str(), duplicateRow));
				names.push_back(std::make_pair(bookName, (i+3)));
				++tempCount;
			}
			if (!pageName.empty())
			{
				int const duplicateRow = findNameRow(names, pageName);
				FATAL((duplicateRow >= 0), ("%s, row %d: page name %s already used at row %d (either as a book, page, collection, slot, or alternate title)", cs_collectionsDataTableName, (i+3), pageName.c_str(), duplicateRow));
				names.push_back(std::make_pair(pageName, (i+3)));
				++tempCount;
			}
			if (!collectionName.empty())
			{
				int const duplicateRow = findNameRow(names, collectionName);
				FATAL((duplicateRow >= 0), ("%s, row %d: collection name %s already used at row %d (either as a book, page, collection, slot, or alternate title)", cs_collectionsDataTableName, (i+3), collectionName.c_str(), duplicateRow));
				names.push_back(std::make_pair(collectionName, (i+3)));
				++tempCount;
			}
			if (!slotName.empty())
			{
				int const duplicateRow = findNameRow(names, slotName);
				FATAL((duplicateRow >= 0), ("%s, row %d: slot name %s already used at row %d (either as a book, page, collection, slot, or alternate title)", cs_collectionsDataTableName, (i+3), slotName.c_str(), duplicateRow));
				names.push_back(std::make_pair(slotName, (i+3)));
				++tempCount;
			}
#endif

			FATAL((tempCount != 1), ("%s, row %d: only one of bookName, pageName, collectionName, or slotName can be specified", cs_collectionsDataTableName, (i+3)));

			beginSlotId = table->getIntValue(columnBeginSlotId, i);
			endSlotId = table->getIntValue(columnEndSlotId, i);
			maxSlotValue = table->getIntValue(columnMaxSlotValue, i);
			icon = table->getStringValue(columnIcon, i);
			music = table->getStringValue(columnMusic, i);
			showIfNotYetEarned = static_cast<ShowIfNotYetEarnedType>(table->getIntValue(columnShowIfNotYetEarned, i));
			hidden = (table->getIntValue(columnHidden, i) != 0);
			title = (table->getIntValue(columnTitle, i) != 0);
			noReward = (table->getIntValue(columnNoReward, i) != 0);
			trackServerFirst = (table->getIntValue(columnTrackServerFirst, i) != 0);
			writeStartupTrace("row-after-scalars", i);

			// read all alternate titles
			titles.clear();
			if (title)
			{
				if (!pageName.empty())
					titles.push_back(pageName);
				else if (!collectionName.empty())
					titles.push_back(collectionName);
				else if (!slotName.empty())
					titles.push_back(slotName);
			}

			for (std::vector<int>::const_iterator iterColumnAlternateTitle = columnAlternateTitle.begin(); iterColumnAlternateTitle != columnAlternateTitle.end(); ++iterColumnAlternateTitle)
			{
				alternateTitle = table->getStringValue(*iterColumnAlternateTitle, i);

				if (!alternateTitle.empty())
				{
#ifndef _WIN64
					int const duplicateRow = findNameRow(names, alternateTitle);
					FATAL((duplicateRow >= 0), ("%s, row %d: alternate title %s already used at row %d (either as a book, page, collection, slot, or alternate title)", cs_collectionsDataTableName, (i+3), alternateTitle.c_str(), duplicateRow));
					names.push_back(std::make_pair(alternateTitle, (i+3)));
#endif

					titles.push_back(alternateTitle);
				}
			}
			writeStartupTrace("row-after-titles", i);

			// start of new book
			if (!bookName.empty())
			{
				// verify previous book, unless it's the first book
				if (currentBook)
				{
					// make sure previous book is not empty
					FATAL(!hasEntries(s_pagesInBook, currentBook->name), ("%s: book %s has no pages", cs_collectionsDataTableName, currentBook->name.c_str()));
					FATAL(!currentPage, ("%s: book %s has no pages", cs_collectionsDataTableName, currentBook->name.c_str()));
					FATAL(!hasSlotsInBook(currentBook->name), ("%s: book %s has no slots", cs_collectionsDataTableName, currentBook->name.c_str()));

					// make sure last page of previous book is not empty
					FATAL(!hasCollectionsInPage(currentPage->name), ("%s: page %s has no collections", cs_collectionsDataTableName, currentPage->name.c_str()));
					FATAL(!currentCollection, ("%s: page %s has no collections", cs_collectionsDataTableName, currentPage->name.c_str()));
					FATAL(!hasEntries(s_slotsInPage, currentPage->name), ("%s: page %s has no slots", cs_collectionsDataTableName, currentPage->name.c_str()));

					// make sure last collection of last page of previous book is not empty
					FATAL(!hasEntries(s_slotsInCollection, currentCollection->name), ("%s: page %s has empty collection %s", cs_collectionsDataTableName, currentPage->name.c_str(), currentCollection->name.c_str()));
				}

				// start new book
				FATAL(title, ("%s: book %s cannot be \"titleable\")", cs_collectionsDataTableName, bookName.c_str()));
				FATAL(!titles.empty(), ("%s: book %s cannot have any alternate titles (books are not \"titleable\")", cs_collectionsDataTableName, bookName.c_str()));
				currentBook = new CollectionInfoBook(bookName, icon, showIfNotYetEarned, hidden);
				currentPage = NULL;
				currentCollection = NULL;
				IGNORE_RETURN(findOrCreateNamedList(s_pagesInBook, bookName, 64));
				IGNORE_RETURN(findOrCreateNamedList(s_slotsInBook, bookName, numRows));
				writeStartupTrace("book-after-collections-list", i);
				s_allBooks.push_back(currentBook);
				s_allBooksByName.push_back(std::make_pair(bookName, currentBook));
			}
			// start of new page
			else if (!pageName.empty())
			{
				writeStartupTrace("page-entry", i);
				// cannot start page without a book
				FATAL(!currentBook, ("%s, row %d: page %s must be in a book", cs_collectionsDataTableName, (i+3), pageName.c_str()));
				writeStartupTrace("page-after-context", i);

				// verify previous page, unless it's the first page
				if (currentPage)
				{
					writeStartupTrace("page-before-previous-verify", i);
					// make sure previous page is not empty
					FATAL(!hasCollectionsInPage(currentPage->name), ("%s: page %s has no collections", cs_collectionsDataTableName, currentPage->name.c_str()));
					writeStartupTrace("page-after-previous-collections", i);
					FATAL(!currentCollection, ("%s: page %s has no collections", cs_collectionsDataTableName, currentPage->name.c_str()));
					writeStartupTrace("page-after-current-collection", i);
					FATAL(!hasEntries(s_slotsInPage, currentPage->name), ("%s: page %s has no slots", cs_collectionsDataTableName, currentPage->name.c_str()));
					writeStartupTrace("page-after-previous-slots", i);

					// make sure last collection of previous page is not empty
					FATAL(!hasEntries(s_slotsInCollection, currentCollection->name), ("%s: page %s has empty collection %s", cs_collectionsDataTableName, currentPage->name.c_str(), currentCollection->name.c_str()));
					writeStartupTrace("page-after-previous-collection-slots", i);
				}

				// start new page
				FATAL((!titles.empty() && !title), ("%s: page %s cannot have any alternate titles unless it is defined as \"titleable\")", cs_collectionsDataTableName, pageName.c_str()));
				writeStartupTrace("page-before-new", i);
				currentPage = new CollectionInfoPage(pageName, icon, showIfNotYetEarned, hidden, titles, *currentBook);
				writeStartupTrace("page-after-new", i);
				currentCollection = NULL;
				writeStartupTrace("page-after-collections-list", i);
				IGNORE_RETURN(findOrCreateNamedList(s_slotsInPage, pageName, 128));
				writeStartupTrace("page-after-slots-list", i);
				std::vector<CollectionsDataTable::CollectionInfoPage const *> & pagesInBook = findOrCreateNamedList(s_pagesInBook, currentBook->name, 64);
				writeStartupTrace("page-after-book-list", i);
				pagesInBook.push_back(currentPage);
				writeStartupTrace("page-after-book-push", i);
				s_allPagesByName.push_back(std::make_pair(pageName, currentPage));
				writeStartupTrace("page-after-global-insert", i);

				if (!currentPage->titles.empty())
				{
					s_allTitleablePages.push_back(currentPage);

					for (std::vector<std::string>::const_iterator iterTitles = currentPage->titles.begin(); iterTitles != currentPage->titles.end(); ++iterTitles)
						s_allPageTitles.push_back(std::make_pair(*iterTitles, currentPage));
				}
			}
			else if (!collectionName.empty())
			{
				writeStartupTrace("collection-entry", i);
				// cannot start collection without a page or a book
				FATAL(!currentBook, ("%s, row %d: collection %s must be in a book", cs_collectionsDataTableName, (i+3), collectionName.c_str()));
				FATAL(!currentPage, ("%s, row %d: collection %s must be in a page", cs_collectionsDataTableName, (i+3), collectionName.c_str()));
				writeStartupTrace("collection-after-context", i);

				// verify previous collection, unless it's the first collection in the page
				if (currentCollection)
				{
					// make sure previous collection is not empty
					FATAL(!hasEntries(s_slotsInCollection, currentCollection->name), ("%s: page %s has empty collection %s", cs_collectionsDataTableName, currentPage->name.c_str(), currentCollection->name.c_str()));
				}
				writeStartupTrace("collection-after-previous", i);

				// read all category for collection
				for (std::vector<int>::const_iterator iterColumnCategory = columnCategory.begin(); iterColumnCategory != columnCategory.end(); ++iterColumnCategory)
				{
					category = table->getStringValue(*iterColumnCategory, i);

					if (!category.empty())
					{
						categories.push_back(category);
					}
				}
				writeStartupTrace("collection-after-categories", i);

				// start new collection
				FATAL((!titles.empty() && !title), ("%s: collection %s cannot have any alternate titles unless it is defined as \"titleable\")", cs_collectionsDataTableName, collectionName.c_str()));
				currentCollection = new CollectionInfoCollection(collectionName, icon, showIfNotYetEarned, hidden, categories, titles, noReward, trackServerFirst, *currentPage);
				writeStartupTrace("collection-after-new", i);
				IGNORE_RETURN(findOrCreateNamedList(s_slotsInCollection, collectionName, 128));
				writeStartupTrace("collection-after-page-insert", i);
				writeStartupTrace("collection-after-book-insert", i);
				s_allCollectionsByName.push_back(std::make_pair(collectionName, currentCollection));
				writeStartupTrace("collection-after-insert", i);

				if (!currentCollection->titles.empty())
				{
					s_allTitleableCollections.push_back(currentCollection);

					for (std::vector<std::string>::const_iterator iterTitles = currentCollection->titles.begin(); iterTitles != currentCollection->titles.end(); ++iterTitles)
						s_allCollectionTitles.push_back(std::make_pair(*iterTitles, currentCollection));
				}

				if (currentCollection->trackServerFirst)
					s_allServerFirstCollections.push_back(std::make_pair(collectionName, currentCollection));
				writeStartupTrace("collection-after-title-insert", i);

				categories.clear();
			}
			else
			{
				writeStartupTrace("slot-entry", i);
				// cannot have a slot without a collection or a page or a book
				FATAL(!currentBook, ("%s, row %d: slot %s must be in a book", cs_collectionsDataTableName, (i+3), slotName.c_str()));
				FATAL(!currentPage, ("%s, row %d: slot %s must be in a page", cs_collectionsDataTableName, (i+3), slotName.c_str()));
				FATAL(!currentCollection, ("%s, row %d: slot %s must be in a collection", cs_collectionsDataTableName, (i+3), slotName.c_str()));
				writeStartupTrace("slot-after-context", i);

				// check for valid slot id
				FATAL(((beginSlotId < 0) || (beginSlotId >= MAX_COLLECTIONS)), ("%s, row %d: begin slot id %d must be 0-%d", cs_collectionsDataTableName, (i+3), beginSlotId, (MAX_COLLECTIONS-1)));
				FATAL((((endSlotId < 0) && (endSlotId != -1)) || (endSlotId >= MAX_COLLECTIONS)), ("%s, row %d: end slot id %d must be 0-%d", cs_collectionsDataTableName, (i+3), endSlotId, (MAX_COLLECTIONS-1)));

				// check for valid beginSlotId/endSlotId combination
				FATAL(((endSlotId != -1) && (beginSlotId >= endSlotId)), ("%s, row %d: begin slot id %d must be < end slot id %d", cs_collectionsDataTableName, (i+3), beginSlotId, endSlotId));
				FATAL(((endSlotId != -1) && ((beginSlotId / COLLECTIONS_PER_INDEX) != (endSlotId / COLLECTIONS_PER_INDEX))), ("%s, row %d: counter-type slot cannot span across the 15999-16000 / 31999-32000 / 47999-48000 / 63999-64000 / etc slot id boundary", cs_collectionsDataTableName, (i+3)));

				// max number of bits for a counter-type slot is 32 bits
				FATAL(((endSlotId != -1) && ((endSlotId - beginSlotId) > 31)), ("%s, row %d: counter-type slot uses %d bits which exceeds the limit of 32 bits for counter-type slot", cs_collectionsDataTableName, (i+3), (endSlotId - beginSlotId + 1)));

				// maxSlotValue is only valid if both beginSlotId and endSlotId specified
				FATAL(((endSlotId == -1) && (maxSlotValue != -1)), ("%s, row %d: max slot value %d cannot be specified for a non counter-type slot", cs_collectionsDataTableName, (i+3), maxSlotValue));

				// check for valid maxSlotValue
				FATAL(((maxSlotValue <= 1) && (maxSlotValue != -1)), ("%s, row %d: max slot value %d must be > 1", cs_collectionsDataTableName, (i+3), maxSlotValue));
				writeStartupTrace("slot-after-id-validate", i);

				// if maxSlotValue specified, make sure there are enough bits allocated to be able to store the value
				maxValueForNumBits = 1;
				if (endSlotId != -1)
				{
					unsigned long const numBits = static_cast<unsigned long>(endSlotId - beginSlotId + 1);
					maxValueForNumBits = (0xffffffff >> (32 - numBits));

					if (maxSlotValue != -1)
						FATAL((maxValueForNumBits < static_cast<unsigned long>(maxSlotValue)), ("%s, row %d: counter-type slot uses %lu bits, which can only hold a max value of %lu, which is less than the specified max value of %d", cs_collectionsDataTableName, (i+3), numBits, maxValueForNumBits, maxSlotValue));
				}
				writeStartupTrace("slot-after-bits", i);

				// read all category for slot
				notifyScriptOnModify = true;
				for (std::vector<int>::const_iterator iterColumnCategory = columnCategory.begin(); iterColumnCategory != columnCategory.end(); ++iterColumnCategory)
				{
					category = table->getStringValue(*iterColumnCategory, i);
				
					if (!category.empty())
					{
						categories.push_back(category);

						if (category == "noScriptNotifyOnModify")
							notifyScriptOnModify = false;
					}
				}
				writeStartupTrace("slot-after-categories", i);

				// read all prereq for slot
				for (std::vector<int>::const_iterator iterColumnPrereq = columnPrereq.begin(); iterColumnPrereq != columnPrereq.end(); ++iterColumnPrereq)
				{
					prereq = table->getStringValue(*iterColumnPrereq, i);

					if (!prereq.empty())
					{
						FATAL((prereq == slotName), ("%s, row %d: slot %s cannot have itself as a prereq", cs_collectionsDataTableName, (i+3), slotName.c_str()));

						prereqs.push_back(prereq);

#ifndef _WIN64
						if (findNameRow(allPrereqs, prereq) < 0)
							allPrereqs.push_back(std::make_pair(prereq, (i+3)));
#endif
					}
				}
				writeStartupTrace("slot-after-prereqs", i);

				// new slot
				FATAL((!titles.empty() && !title), ("%s: slot %s cannot have any alternate titles unless it is defined as \"titleable\")", cs_collectionsDataTableName, slotName.c_str()));
#ifdef _WIN64
				static std::vector<std::string> const emptySlotStrings;
				std::vector<std::string> const & slotCategoriesForInfo = emptySlotStrings;
				std::vector<std::string> const & slotPrereqsForInfo = emptySlotStrings;
				std::vector<std::string> const & slotTitlesForInfo = emptySlotStrings;
#else
				std::vector<std::string> const & slotCategoriesForInfo = categories;
				std::vector<std::string> const & slotPrereqsForInfo = prereqs;
				std::vector<std::string> const & slotTitlesForInfo = titles;
#endif
#ifdef _WIN64
				currentSlot = new (allocateSlotPoolEntry()) CollectionInfoSlot(slotName, icon, showIfNotYetEarned, hidden, notifyScriptOnModify, (beginSlotId / COLLECTIONS_PER_INDEX), (beginSlotId % COLLECTIONS_PER_INDEX), beginSlotId, ((endSlotId < 0) ? -1 : (endSlotId % COLLECTIONS_PER_INDEX)), ((endSlotId < 0) ? -1 : endSlotId), (((endSlotId < 0) || (maxSlotValue <= 1)) ? 0 : static_cast<unsigned long>(maxSlotValue)), maxValueForNumBits, slotCategoriesForInfo, slotPrereqsForInfo, music, slotTitlesForInfo, *currentCollection);
#else
				currentSlot = new CollectionInfoSlot(slotName, icon, showIfNotYetEarned, hidden, notifyScriptOnModify, (beginSlotId / COLLECTIONS_PER_INDEX), (beginSlotId % COLLECTIONS_PER_INDEX), beginSlotId, ((endSlotId < 0) ? -1 : (endSlotId % COLLECTIONS_PER_INDEX)), ((endSlotId < 0) ? -1 : endSlotId), (((endSlotId < 0) || (maxSlotValue <= 1)) ? 0 : static_cast<unsigned long>(maxSlotValue)), maxValueForNumBits, slotCategoriesForInfo, slotPrereqsForInfo, music, slotTitlesForInfo, *currentCollection);
#endif
				writeStartupTrace("slot-after-new", i);

#ifndef _WIN64
				// check for duplicate slot id across all collections
				if (endSlotId == -1)
				{
					FATAL((allSlotRowsById[beginSlotId] >= 0), ("%s, row %d: slot id %d already used by slot %s at row %d", cs_collectionsDataTableName, (i+3), beginSlotId, allSlotNamesById[beginSlotId].c_str(), allSlotRowsById[beginSlotId]));
				}
				else
				{
					for (int j = beginSlotId; j <= endSlotId; ++j)
						FATAL((allSlotRowsById[j] >= 0), ("%s, row %d: slot id %d already used by slot %s at row %d", cs_collectionsDataTableName, (i+3), j, allSlotNamesById[j].c_str(), allSlotRowsById[j]));
				}
#endif
				writeStartupTrace("slot-after-duplicate-id", i);

				writeStartupTrace("slot-before-collection-list", i);
				std::vector<CollectionsDataTable::CollectionInfoSlot const *> & slotsInCollection = findOrCreateNamedList(s_slotsInCollection, currentCollection->name, 128);
				writeStartupTrace("slot-after-collection-list", i);
				slotsInCollection.push_back(currentSlot);
				writeStartupTrace("slot-after-collection-push", i);
				std::vector<CollectionsDataTable::CollectionInfoSlot const *> & slotsInPage = findOrCreateNamedList(s_slotsInPage, currentPage->name, 128);
				writeStartupTrace("slot-after-page-list", i);
				slotsInPage.push_back(currentSlot);
				writeStartupTrace("slot-after-page-push", i);
				std::vector<CollectionsDataTable::CollectionInfoSlot const *> & slotsInBook = findOrCreateNamedList(s_slotsInBook, currentBook->name, numRows);
				writeStartupTrace("slot-after-book-list", i);
				UNREF(slotsInBook);
				writeStartupTrace("slot-after-book-push", i);
				s_allSlotsByName.push_back(currentSlot);
				writeStartupTrace("slot-after-global-insert", i);

#ifdef _WIN64
				if (endSlotId > highestSlotId)
					highestSlotId = endSlotId;
				if (beginSlotId > highestSlotId)
					highestSlotId = beginSlotId;
#else
				if (endSlotId == -1)
				{
					allSlotsById[beginSlotId] = currentSlot;
					allSlotRowsById[beginSlotId] = i + 3;
					allSlotNamesById[beginSlotId] = currentSlot->name;
					if (beginSlotId > highestSlotId)
						highestSlotId = beginSlotId;
				}
				else
				{
					for (int j = beginSlotId; j <= endSlotId; ++j)
					{
						allSlotsById[j] = currentSlot;
						allSlotRowsById[j] = i + 3;
						allSlotNamesById[j] = currentSlot->name;
						if (j > highestSlotId)
							highestSlotId = j;
					}
				}
#endif
				writeStartupTrace("slot-after-id-insert", i);

				if (!currentSlot->titles.empty())
				{
					s_allTitleableSlots.push_back(currentSlot);

					for (std::vector<std::string>::const_iterator iterTitles = currentSlot->titles.begin(); iterTitles != currentSlot->titles.end(); ++iterTitles)
						s_allSlotTitles.push_back(std::make_pair(*iterTitles, currentSlot));
				}
				writeStartupTrace("slot-after-title-insert", i);

				for (std::vector<std::string>::const_iterator iterCategories = categories.begin(); iterCategories != categories.end(); ++iterCategories)
				{
					writeStartupTrace("slot-category-begin", i);
					writeStartupTrace("slot-category-after-global-name", i);
					writeStartupTrace("slot-category-after-book-name", i);
					writeStartupTrace("slot-category-after-page-name", i);
					writeStartupTrace("slot-category-after-collection-name", i);
				}
				writeStartupTrace("slot-after-category-insert", i);

				currentSlot = NULL;
				categories.clear();
				prereqs.clear();
			}
			writeStartupTrace("row-after", i);
		}

		writeStartupTrace("after-rows");
		DataTableManager::close(cs_collectionsDataTableName);
		writeStartupTrace("after-close");

		// verify last book
		if (currentBook)
		{
			// make sure last book is not empty
			FATAL(!hasEntries(s_pagesInBook, currentBook->name), ("%s: book %s has no pages", cs_collectionsDataTableName, currentBook->name.c_str()));
			FATAL(!currentPage, ("%s: book %s has no pages", cs_collectionsDataTableName, currentBook->name.c_str()));
			FATAL(!hasSlotsInBook(currentBook->name), ("%s: book %s has no slots", cs_collectionsDataTableName, currentBook->name.c_str()));

			// make sure last page of last book is not empty
			FATAL(!hasCollectionsInPage(currentPage->name), ("%s: page %s has no collections", cs_collectionsDataTableName, currentPage->name.c_str()));
			FATAL(!currentCollection, ("%s: page %s has no collections", cs_collectionsDataTableName, currentPage->name.c_str()));
			FATAL(!hasEntries(s_slotsInPage, currentPage->name), ("%s: page %s has no slots", cs_collectionsDataTableName, currentPage->name.c_str()));

			// make sure last collection of last page of last book is not empty
			FATAL(!hasEntries(s_slotsInCollection, currentCollection->name), ("%s: page %s has empty collection %s", cs_collectionsDataTableName, currentPage->name.c_str(), currentCollection->name.c_str()));
		}
		writeStartupTrace("after-verify-last-book");

		// save off all slots ordered by slot ids
		s_allSlotsById.resize(highestSlotId + 1, NULL);
		writeStartupTrace("after-resize-all-slots", highestSlotId + 1);
#ifdef _WIN64
		for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterSlot = s_allSlotsByName.begin(); iterSlot != s_allSlotsByName.end(); ++iterSlot)
		{
			CollectionsDataTable::CollectionInfoSlot const * const slot = *iterSlot;
			if (slot && slot->absoluteBeginSlotId >= 0 && slot->absoluteBeginSlotId < static_cast<int>(s_allSlotsById.size()))
				s_allSlotsById[slot->absoluteBeginSlotId] = slot;
		}
#else
		beginSlotId = -1;
		for (int slotId = 0; slotId <= highestSlotId; ++slotId)
		{
			CollectionsDataTable::CollectionInfoSlot const * const slot = allSlotsById[slotId];

			if (slotId == 0)
				FATAL((slot == NULL), ("%s: slot id must start at 0", cs_collectionsDataTableName));
			else
				FATAL((slot == NULL), ("%s: slot id must be contiguous (there is a \"hole\" between %d and %d)", cs_collectionsDataTableName, beginSlotId, slotId));

			tempBeginSlotId = ((slot->beginSlotId == -1) ? slot->beginSlotId : ((slot->slotIdIndex * COLLECTIONS_PER_INDEX) + slot->beginSlotId));
			FATAL((tempBeginSlotId != slot->absoluteBeginSlotId), ("%s: beginSlotId/absoluteBeginSlotId mismatch for slot %s (%d, %d, %d)", cs_collectionsDataTableName, slot->name.c_str(), slot->slotIdIndex, slot->beginSlotId, slot->absoluteBeginSlotId));

			tempEndSlotId = ((slot->endSlotId == -1) ? slot->endSlotId : ((slot->slotIdIndex * COLLECTIONS_PER_INDEX) + slot->endSlotId));
			FATAL((tempEndSlotId != slot->absoluteEndSlotId), ("%s: endSlotId/absoluteEndSlotId mismatch for slot %s (%d, %d, %d)", cs_collectionsDataTableName, slot->name.c_str(), slot->slotIdIndex, slot->endSlotId, slot->absoluteEndSlotId));

			if (tempEndSlotId == -1)
			{
				FATAL((slotId != tempBeginSlotId), ("%s: begin slot id mismatch for slot %s (%d, %d)", cs_collectionsDataTableName, slot->name.c_str(), slotId, tempBeginSlotId));
			}
			else
			{
				FATAL(((slotId < tempBeginSlotId) || (slotId > tempEndSlotId)), ("%s: slot id mismatch for slot %s (%d, %d, %d)", cs_collectionsDataTableName, slot->name.c_str(), slotId, tempBeginSlotId, tempEndSlotId));
			}

			beginSlotId = slotId;

			if (slotId == tempBeginSlotId)
			{
				s_allSlotsById[slotId] = slot;
			}
			else
			{
				s_allSlotsById[slotId] = NULL;
			}
		}
#endif
		writeStartupTrace("after-slot-id-map");

		// make sure that all collection slot name specified as prereqs actually exists
		for (std::vector<std::pair<std::string, int> >::const_iterator iterAllPrereqs = allPrereqs.begin(); iterAllPrereqs != allPrereqs.end(); ++iterAllPrereqs)
		{
			FATAL((!getSlotByName(iterAllPrereqs->first)), ("%s, row %d: prereq slot name %s does not exist", cs_collectionsDataTableName, iterAllPrereqs->second, iterAllPrereqs->first.c_str()));
		}
		writeStartupTrace("after-prereq-validate");

		// set up collection slot prereqs as pointers to the actual prereq slot, for faster access
		for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterAllSlotsByName = s_allSlotsByName.begin(); iterAllSlotsByName != s_allSlotsByName.end(); ++iterAllSlotsByName)
		{
			CollectionsDataTable::CollectionInfoSlot * slot = const_cast<CollectionsDataTable::CollectionInfoSlot *>(*iterAllSlotsByName);
			for (std::vector<std::string>::const_iterator iterPrereq = slot->prereqs.begin(); iterPrereq != slot->prereqs.end(); ++iterPrereq)
			{
				CollectionsDataTable::CollectionInfoSlot const * prereqSlot = getSlotByName(*iterPrereq);
				FATAL((!prereqSlot), ("%s: prereq slot name %s does not exist", cs_collectionsDataTableName, iterPrereq->c_str()));
				(const_cast<std::vector<CollectionInfoSlot const *> *>(&slot->prereqsPtr))->push_back(prereqSlot);
			}
		}
		writeStartupTrace("install-exit");
	}
	else
	{
		FATAL(true, ("collection datatable %s not found", cs_collectionsDataTableName));
	}

	ExitChain::add(remove, "CollectionsDataTable::remove");
}

//----------------------------------------------------------------------

void CollectionsDataTable::remove()
{
	// free slots
	{
		for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterSlot = s_allSlotsByName.begin(); iterSlot != s_allSlotsByName.end(); ++iterSlot)
		{
#ifdef _WIN64
			const_cast<CollectionsDataTable::CollectionInfoSlot *>(*iterSlot)->~CollectionInfoSlot();
#else
			delete *iterSlot;
#endif
		}
#ifdef _WIN64
		destroySlotPool();
#endif
	}

	// free collections
	{
		for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iterCollection = s_allCollectionsByName.begin(); iterCollection != s_allCollectionsByName.end(); ++iterCollection)
		{
			delete iterCollection->second;
		}

	}

	// free pages
	{
		for (std::vector<NamedListBucket<CollectionsDataTable::CollectionInfoPage const *> *>::const_iterator iterBook = s_pagesInBook.begin(); iterBook != s_pagesInBook.end(); ++iterBook)
		{
			for (std::vector<CollectionsDataTable::CollectionInfoPage const *>::const_iterator iterPage = (*iterBook)->values.begin(); iterPage != (*iterBook)->values.end(); ++iterPage)
			{
				delete *iterPage;
			}
		}
	}

	// free books
	{
		for (std::vector<CollectionsDataTable::CollectionInfoBook const *>::const_iterator iterBook = s_allBooks.begin(); iterBook != s_allBooks.end(); ++iterBook)
		{
			delete *iterBook;
		}
	}

	// slots
	s_allSlotsByName.clear();
	s_allSlotsById.clear();
	s_allTitleableSlots.clear();
	s_allSlotTitles.clear();

	// collections
	deleteNamedListBuckets(s_slotsInCollection);
	s_allTitleableCollections.clear();
	s_allCollectionTitles.clear();
	s_allServerFirstCollections.clear();
	s_allCollectionsByName.clear();

	// pages
	deleteNamedListBuckets(s_slotsInPage);
	deleteNamedListBuckets(s_collectionsInPage);
	s_allTitleablePages.clear();
	s_allPageTitles.clear();
	s_allPagesByName.clear();

	// books
	deleteNamedListBuckets(s_slotsInBook);
	deleteNamedListBuckets(s_collectionsInBook);
	deleteNamedListBuckets(s_pagesInBook);
	s_allBooks.clear();
	s_allBooksByName.clear();

	// categories
	deleteCategorySlotBuckets(s_slotsInCategory);
	deleteCategorySlotBuckets(s_slotsInCategoryByCollection);
	deleteCategorySlotBuckets(s_slotsInCategoryByPage);
	deleteCategorySlotBuckets(s_slotsInCategoryByBook);
	s_slotCategories.clear();
	s_slotCategoriesByCollection.clear();
	s_slotCategoriesByPage.clear();
	s_slotCategoriesByBook.clear();
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

CollectionsDataTable::CollectionInfoSlot const * CollectionsDataTable::getSlotByName(std::string const & slotName)
{
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot && slot->name == slotName)
			return slot;
	}

	return NULL;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

CollectionsDataTable::CollectionInfoSlot const * CollectionsDataTable::getSlotByBeginSlotId(int slotId)
{
	if ((slotId < 0) || (slotId >= static_cast<int>(s_allSlotsById.size())))
		return NULL;

	return s_allSlotsById[slotId];
}

//----------------------------------------------------------------------

std::vector<CollectionsDataTable::CollectionInfoSlot const *> const & CollectionsDataTable::getAllTitleableSlots()
{
	return s_allTitleableSlots;
}

//----------------------------------------------------------------------

CollectionsDataTable::CollectionInfoSlot const * CollectionsDataTable::isASlotTitle(std::string const & titleName)
{
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoSlot const *> >::const_iterator iterFind = s_allSlotTitles.begin(); iterFind != s_allSlotTitles.end(); ++iterFind)
		if (iterFind->first == titleName)
			return iterFind->second;

	return NULL;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoSlot const *> const & CollectionsDataTable::getSlotsInCollection(std::string const & collectionName)
{
	static std::vector<CollectionsDataTable::CollectionInfoSlot const *> empty;

	std::vector<CollectionsDataTable::CollectionInfoSlot const *> const * const result = findNamedList(s_slotsInCollection, collectionName);
	if (result)
		return *result;

	return empty;
}

//----------------------------------------------------------------------

std::vector<CollectionsDataTable::CollectionInfoCollection const *> const & CollectionsDataTable::getAllTitleableCollections()
{
	return s_allTitleableCollections;
}

//----------------------------------------------------------------------

CollectionsDataTable::CollectionInfoCollection const * CollectionsDataTable::isACollectionTitle(std::string const & titleName)
{
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iterFind = s_allCollectionTitles.begin(); iterFind != s_allCollectionTitles.end(); ++iterFind)
		if (iterFind->first == titleName)
			return iterFind->second;

	return NULL;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::map<std::string, CollectionsDataTable::CollectionInfoCollection const *> const & CollectionsDataTable::getAllServerFirstCollections()
{
	static std::map<std::string, CollectionsDataTable::CollectionInfoCollection const *> result;

	result.clear();
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iter = s_allServerFirstCollections.begin(); iter != s_allServerFirstCollections.end(); ++iter)
		IGNORE_RETURN(result.insert(std::make_pair(iter->first, iter->second)));

	return result;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

CollectionsDataTable::CollectionInfoCollection const * CollectionsDataTable::getCollectionByName(std::string const & collectionName)
{
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iterFind = s_allCollectionsByName.begin(); iterFind != s_allCollectionsByName.end(); ++iterFind)
	{
		if (iterFind->first == collectionName)
			return iterFind->second;
	}

	return NULL;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoSlot const *> const & CollectionsDataTable::getSlotsInPage(std::string const & pageName)
{
	static std::vector<CollectionsDataTable::CollectionInfoSlot const *> empty;

	std::vector<CollectionsDataTable::CollectionInfoSlot const *> const * const result = findNamedList(s_slotsInPage, pageName);
	if (result)
		return *result;

	return empty;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoCollection const *> const & CollectionsDataTable::getCollectionsInPage(std::string const & pageName)
{
	static std::vector<CollectionsDataTable::CollectionInfoCollection const *> result;

	result.clear();
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iterFind = s_allCollectionsByName.begin(); iterFind != s_allCollectionsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoCollection const * const collection = iterFind->second;
		if (collection && collection->page.name == pageName)
			result.push_back(collection);
	}

	return result;
}

//----------------------------------------------------------------------

std::vector<CollectionsDataTable::CollectionInfoPage const *> const & CollectionsDataTable::getAllTitleablePages()
{
	return s_allTitleablePages;
}

//----------------------------------------------------------------------

CollectionsDataTable::CollectionInfoPage const * CollectionsDataTable::isAPageTitle(std::string const & titleName)
{
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoPage const *> >::const_iterator iterFind = s_allPageTitles.begin(); iterFind != s_allPageTitles.end(); ++iterFind)
		if (iterFind->first == titleName)
			return iterFind->second;

	return NULL;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

CollectionsDataTable::CollectionInfoPage const * CollectionsDataTable::getPageByName(std::string const & pageName)
{
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoPage const *> >::const_iterator iterFind = s_allPagesByName.begin(); iterFind != s_allPagesByName.end(); ++iterFind)
		if (iterFind->first == pageName)
			return iterFind->second;

	return NULL;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoSlot const *> const & CollectionsDataTable::getSlotsInBook(std::string const & bookName)
{
	static std::vector<CollectionsDataTable::CollectionInfoSlot const *> result;

	result.clear();
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot && slot->collection.page.book.name == bookName)
			result.push_back(slot);
	}

	return result;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoCollection const *> const & CollectionsDataTable::getCollectionsInBook(std::string const & bookName)
{
	static std::vector<CollectionsDataTable::CollectionInfoCollection const *> result;

	result.clear();
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iterFind = s_allCollectionsByName.begin(); iterFind != s_allCollectionsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoCollection const * const collection = iterFind->second;
		if (collection && collection->page.book.name == bookName)
			result.push_back(collection);
	}

	return result;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoPage const *> const & CollectionsDataTable::getPagesInBook(std::string const & bookName)
{
	static std::vector<CollectionsDataTable::CollectionInfoPage const *> empty;

	std::vector<CollectionsDataTable::CollectionInfoPage const *> const * const result = findNamedList(s_pagesInBook, bookName);
	if (result)
		return *result;

	return empty;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoBook const *> const & CollectionsDataTable::getAllBooks()
{
	return s_allBooks;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

CollectionsDataTable::CollectionInfoBook const * CollectionsDataTable::getBookByName(std::string const & bookName)
{
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoBook const *> >::const_iterator iterFind = s_allBooksByName.begin(); iterFind != s_allBooksByName.end(); ++iterFind)
		if (iterFind->first == bookName)
			return iterFind->second;

	return NULL;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoSlot const *> const & CollectionsDataTable::getSlotsInCategory(std::string const & categoryName)
{
	static std::vector<CollectionsDataTable::CollectionInfoSlot const *> result;

	result.clear();
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot && containsString(slot->categories, categoryName))
			result.push_back(slot);
	}

	return result;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoSlot const *> const & CollectionsDataTable::getSlotsInCategoryByCollection(std::string const & collectionName, std::string const & categoryName)
{
	static std::vector<CollectionsDataTable::CollectionInfoSlot const *> result;

	result.clear();
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot && slot->collection.name == collectionName && containsString(slot->categories, categoryName))
			result.push_back(slot);
	}

	return result;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoSlot const *> const & CollectionsDataTable::getSlotsInCategoryByPage(std::string const & pageName, std::string const & categoryName)
{
	static std::vector<CollectionsDataTable::CollectionInfoSlot const *> result;

	result.clear();
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot && slot->collection.page.name == pageName && containsString(slot->categories, categoryName))
			result.push_back(slot);
	}

	return result;
}

//----------------------------------------------------------------------
// *****NOTE***** the pointers returned by these functions *****ARE NOT!!!*****
// owned by the caller, they are owned/managed/freed by this class

std::vector<CollectionsDataTable::CollectionInfoSlot const *> const & CollectionsDataTable::getSlotsInCategoryByBook(std::string const & bookName, std::string const & categoryName)
{
	static std::vector<CollectionsDataTable::CollectionInfoSlot const *> result;

	result.clear();
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot && slot->collection.page.book.name == bookName && containsString(slot->categories, categoryName))
			result.push_back(slot);
	}

	return result;
}

//----------------------------------------------------------------------

std::set<std::string> const & CollectionsDataTable::getAllSlotCategoriesInCollection(std::string const & collectionName)
{
	static std::set<std::string> result;

	result.clear();
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot && slot->collection.name == collectionName)
			fillSet(result, slot->categories);
	}

	return result;
}

//----------------------------------------------------------------------

std::set<std::string> const & CollectionsDataTable::getAllSlotCategoriesInPage(std::string const & pageName)
{
	static std::set<std::string> result;

	result.clear();
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot && slot->collection.page.name == pageName)
			fillSet(result, slot->categories);
	}

	return result;
}

//----------------------------------------------------------------------

std::set<std::string> const & CollectionsDataTable::getAllSlotCategoriesInBook(std::string const & bookName)
{
	static std::set<std::string> result;

	result.clear();
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot && slot->collection.page.book.name == bookName)
			fillSet(result, slot->categories);
	}

	return result;
}

//----------------------------------------------------------------------

std::set<std::string> const & CollectionsDataTable::getAllSlotCategories()
{
	static std::set<std::string> result;

	result.clear();
	for (std::vector<CollectionsDataTable::CollectionInfoSlot const *>::const_iterator iterFind = s_allSlotsByName.begin(); iterFind != s_allSlotsByName.end(); ++iterFind)
	{
		CollectionsDataTable::CollectionInfoSlot const * const slot = *iterFind;
		if (slot)
			fillSet(result, slot->categories);
	}

	return result;
}

//----------------------------------------------------------------------

Unicode::String CollectionsDataTable::localizeCollectionName(std::string const & name)
{
	return StringId("collection_n", name).localize();
}

//----------------------------------------------------------------------

Unicode::String CollectionsDataTable::localizeCollectionDescription(std::string const & name)
{
	return StringId("collection_d", name).localize();
}

//----------------------------------------------------------------------

Unicode::String CollectionsDataTable::localizeCollectionTitle(std::string const & name)
{
	return StringId("collection_title", name).localize();
}

//----------------------------------------------------------------------

void CollectionsDataTable::setServerFirstData(std::set<std::pair<std::pair<int32, std::string>, std::pair<NetworkId, Unicode::String> > > const & collectionServerFirst)
{
	// clear all "server first" info
	for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iter = s_allServerFirstCollections.begin(); iter != s_allServerFirstCollections.end(); ++iter)
	{
		time_t * serverFirstClaimTime = const_cast<time_t *>(&(iter->second->serverFirstClaimTime));
		NetworkId * serverFirstClaimantId = const_cast<NetworkId *>(&(iter->second->serverFirstClaimantId));
		Unicode::String * serverFirstClaimantName = const_cast<Unicode::String *>(&(iter->second->serverFirstClaimantName));

		*serverFirstClaimTime = 0;
		*serverFirstClaimantId = NetworkId::cms_invalid;
		serverFirstClaimantName->clear();
	}

	// set all "server first" info
	for (std::set<std::pair<std::pair<int32, std::string>, std::pair<NetworkId, Unicode::String> > >::const_iterator iter2 = collectionServerFirst.begin(); iter2 != collectionServerFirst.end(); ++iter2)
	{
		for (std::vector<std::pair<std::string, CollectionsDataTable::CollectionInfoCollection const *> >::const_iterator iterFind = s_allServerFirstCollections.begin(); iterFind != s_allServerFirstCollections.end(); ++iterFind)
		{
			if (iterFind->first != iter2->first.second)
				continue;

			time_t * serverFirstClaimTime = const_cast<time_t *>(&(iterFind->second->serverFirstClaimTime));
			NetworkId * serverFirstClaimantId = const_cast<NetworkId *>(&(iterFind->second->serverFirstClaimantId));
			Unicode::String * serverFirstClaimantName = const_cast<Unicode::String *>(&(iterFind->second->serverFirstClaimantName));

			*serverFirstClaimTime = static_cast<time_t>(iter2->first.first);
			*serverFirstClaimantId = iter2->second.first;
			*serverFirstClaimantName = iter2->second.second;
			break;
		}
	}
}

//----------------------------------------------------------------------

char const * CollectionsDataTable::getShowIfNotYetEarnedTypeString(ShowIfNotYetEarnedType const showIfNotYetEarned)
{
	if (showIfNotYetEarned == SE_gray)
		return "gray";
	else if (showIfNotYetEarned == SE_unknown)
		return "unknown";

	return "none";
}

// ======================================================================
