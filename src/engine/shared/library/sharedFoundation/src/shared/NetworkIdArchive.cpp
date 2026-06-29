// ======================================================================
//
// NetworkIdArchive.cpp
// Copyright 2001 Sony Online Entertainment Inc.
// All Rights Reserved.
//
// ======================================================================

#include "sharedFoundation/FirstSharedFoundation.h"
#include "sharedFoundation/NetworkIdArchive.h"

#include "Archive/Archive.h"
#include "Archive/ByteStream.h"
#include "sharedFoundation/NetworkId.h"

// ======================================================================

namespace Archive
{
	//-----------------------------------------------------------------------

	void get(ReadIterator & source, NetworkId & target)
	{
		int64 id;         // Using int64 instead of NetworkIdType so that we get a compile error if we change it
		//source.get(&id,8); // because this line hard-codes the size
		get(source, id);
		target = NetworkId(id);
	}

	//-----------------------------------------------------------------------

	void put(ByteStream & target, const NetworkId & source)
	{
		//uint64 tmp = source.getValue();
		put(target, source.getValue());
		//target.put(&tmp,8);
	}

	//-----------------------------------------------------------------------
}

// ======================================================================
