// Transceiver.cpp
// Copyright 2000-01, Sony Online Entertainment Inc., all rights reserved. 
// Author: Justin Randall

//-----------------------------------------------------------------------

#include "sharedMessageDispatch/FirstSharedMessageDispatch.h"
#include "sharedMessageDispatch/Transceiver.h"

#include <map>

namespace MessageDispatch {

//-----------------------------------------------------------------------

TransceiverBase::GlobalReceiverInfo::GlobalReceiverInfo() :
receivers(),
pendingAdds(),
pendingRemoves(),
locked(false)
{
}

//-----------------------------------------------------------------------

TransceiverBase::TransceiverBase() :
locked(false)
{
}

//-----------------------------------------------------------------------

TransceiverBase::~TransceiverBase()
{
}

//-----------------------------------------------------------------------

TransceiverBase::GlobalReceiverInfo & TransceiverBase::getGlobalReceiverInfo(const type_info & typeId)
{
#ifndef _WIN64
	static std::map<const char * const, GlobalReceiverInfo> receiverSets;
	GlobalReceiverInfo & result = receiverSets[typeId.name()];
	return result;
#else
	struct ReceiverSetEntry
	{
		const char * name;
		GlobalReceiverInfo * info;
	};

	static ReceiverSetEntry * receiverSets = 0;
	static size_t receiverSetCount = 0;
	static size_t receiverSetCapacity = 0;

	const char * const name = typeId.name();
	size_t i;
	for(i = 0; i < receiverSetCount; ++i)
	{
		if(receiverSets[i].name == name)
			return *receiverSets[i].info;
	}

	if(receiverSetCount == receiverSetCapacity)
	{
		const size_t newCapacity = receiverSetCapacity == 0 ? 64 : receiverSetCapacity * 2;
		ReceiverSetEntry * const newReceiverSets = new ReceiverSetEntry[newCapacity];
		for(i = 0; i < receiverSetCount; ++i)
			newReceiverSets[i] = receiverSets[i];

		delete [] receiverSets;
		receiverSets = newReceiverSets;
		receiverSetCapacity = newCapacity;
	}

	receiverSets[receiverSetCount].name = name;
	receiverSets[receiverSetCount].info = new GlobalReceiverInfo();
	return *receiverSets[receiverSetCount++].info;
#endif
}

//-----------------------------------------------------------------------

Callback::Callback() :
receivers()
{
}

//-----------------------------------------------------------------------

Callback::~Callback()
{
	TransceiverBase::BasePointerList::iterator i;
	TransceiverBase * t;
	for(i = receivers.begin(); i != receivers.end(); ++i)
	{
		t = (*i);
		delete t;
	}
}

//-----------------------------------------------------------------------

}//namespace MessageDispatch

