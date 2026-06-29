#include "sharedMessageDispatch/FirstSharedMessageDispatch.h"

#include "sharedDebug/Profiler.h"
#include "sharedMessageDispatch/Emitter.h"
#include "sharedMessageDispatch/Message.h"
#include "sharedMessageDispatch/MessageManager.h"
#include "sharedMessageDispatch/Receiver.h"

#include <hash_map>
#include <vector>

namespace MessageDispatch {

MessageManager MessageManager::ms_instance;

namespace
{
	typedef void (*StaticCallback)(const Emitter &, const MessageBase &);

	template <typename T>
	bool containsValue(const std::vector<T> & values, const T & value)
	{
		for(typename std::vector<T>::const_iterator i = values.begin(); i != values.end(); ++i)
		{
			if(*i == value)
			{
				return true;
			}
		}
		return false;
	}
}

struct MessageManager::Data
{
	std::hash_map<unsigned long int, std::vector<Receiver *> > receivers;
	std::hash_map<unsigned long int, std::vector<StaticCallback> > staticCallbacks;
};

//---------------------------------------------------------------------
/**
	@brief Do NOT construct Singleton objects directly!
*/
MessageManager::MessageManager()
: data (new Data)
{
}

//---------------------------------------------------------------------
/**
	@brief Do NOT destroy Singleton objects directly!
*/
MessageManager::~MessageManager()
{
	delete data;
	data = 0;
}

//---------------------------------------------------------------------
/**
	@brief add a Receiver object to a map of Receiver objects that will
	receive all messages of a certain type.

	Some Receiver objects may wish to receive a message of a certain 
	type regardless of the Emitter object that originates the message.

	@param target          The Receiver object that will listen for all
	                       messages of the requested type
	@param messageTypeName The message type to listen for
	
	@see Emitter::addReceiver
	
	@author Justin Randall
*/
void MessageManager::addReceiver(Receiver & target, const char * const messageTypeName)
{
	const unsigned long int messageType = MessageBase::makeMessageTypeFromString(messageTypeName);
	addReceiver(target, messageType);
}

//-----------------------------------------------------------------------

void MessageManager::addReceiver(Receiver & target, const MessageBase & source)
{
	const unsigned long int messageType = source.getType();
	addReceiver(target, messageType);
}

//-----------------------------------------------------------------------

void MessageManager::addReceiver(Receiver & target, const unsigned long int messageType)
{
	std::hash_map<unsigned long int, std::vector<Receiver *> >::iterator i = data->receivers.find(messageType);
	if(i != data->receivers.end())
	{
		target.setHasTargets(true);
		std::vector<Receiver *> & targets = (*i).second;
		if(!containsValue(targets, &target))
		{
			targets.push_back(&target);
		}
	}
	else
	{
		target.setHasTargets(true);
		std::vector<Receiver *> newTargets;
		newTargets.push_back(&target);
		data->receivers[messageType] = newTargets;
	}
}

//-----------------------------------------------------------------------

void MessageManager::addStaticCallback(void (*callback)(const Emitter &, const MessageBase &), const unsigned long int messageType)
{
	std::hash_map<unsigned long int, std::vector<StaticCallback> >::iterator f = data->staticCallbacks.find(messageType);
	if(f != data->staticCallbacks.end())
	{
		std::vector<StaticCallback> & targets = f->second;
		if(!containsValue(targets, callback))
		{
			targets.push_back(callback);
		}
	}
	else
	{
		std::vector<StaticCallback> newTargets;
		newTargets.push_back(callback);
		data->staticCallbacks[messageType] = newTargets;
	}
}

//---------------------------------------------------------------------
/**
	@brief Invoked by Emitter objects when a message is broadcast

	@param emitter      The source of the MessageBase object being broadcast
	@param message      The message being broadcast

	@see Emitter::emit
	@see Receiver::onReceive

	@author Justin Randall
*/
void MessageManager::emitMessage(const Emitter & emitter, const MessageBase & message) const
{
	const unsigned long int messageType = message.getType();
	std::hash_map<unsigned long int, std::vector<Receiver *> >::const_iterator i = data->receivers.find(messageType);
	if(i != data->receivers.end())
	{
		const std::vector<Receiver *> targets = (*i).second;
		for(std::vector<Receiver *>::const_iterator v = targets.begin(); v != targets.end(); ++v)
		{
			Receiver * r = (*v);
			if(! emitter.hasReceiver(*r, messageType))
			{
				r->receiveMessage(emitter, message);
			}
		}
	}

	std::hash_map<unsigned long int, std::vector<StaticCallback> >::iterator f = data->staticCallbacks.find(messageType);
	if(f != data->staticCallbacks.end())
	{
		const std::vector<StaticCallback> targets = f->second;
		for(std::vector<StaticCallback>::const_iterator v = targets.begin(); v != targets.end(); ++v)
		{
			StaticCallback callback = *v;
			callback(emitter, message);
		}
	}
}

//---------------------------------------------------------------------
/**
	@brief Invoked by a Receiver during it's destructor
*/
void MessageManager::receiverDestroyed(const Receiver & target)
{
	if (!target.getHasTargets())
	{
		return;
	}
	// find receiver
	std::hash_map<unsigned long int, std::vector<Receiver *> >::iterator i;
	for(i = data->receivers.begin(); i != data->receivers.end(); ++i)
	{
		std::vector<Receiver *> & targets = (*i).second;
		for(std::vector<Receiver *>::iterator j = targets.begin(); j != targets.end();)
		{
			if(*j == &target)
			{
				j = targets.erase(j);
			}
			else
			{
				++j;
			}
		}
	}
}

//---------------------------------------------------------------------
/**
	@brief break a Receiver->Message relationship

	When a Receiver no longer wants to receive all messages of a certain
	type, it invokes MessageManager::removeReceiver to irradicate
	any relationship with the message in the MessageManager target map.

	@param target            The Receiver object that is breaking the 
	                         connection.
	@param messageTypeName   Identifies the source MessageBase objects
	                         that the Receiver object will now ignore.

	@see Emitter::removeReceiver

	@author Justin Randall
*/
void MessageManager::removeReceiver(const Receiver & target, const char * const messageTypeName)
{
	const unsigned long int messageType = MessageBase::makeMessageTypeFromString(messageTypeName);
	removeReceiver(target, messageType);
}

//-----------------------------------------------------------------------

void MessageManager::removeReceiver(const Receiver & target, const unsigned long int messageType)
{
	std::hash_map<unsigned long int, std::vector<Receiver *> >::iterator i = data->receivers.find(messageType);
	if(i != data->receivers.end())
	{
		std::vector<Receiver *> & targets = (*i).second;
		for(std::vector<Receiver *>::iterator j = targets.begin(); j != targets.end();)
		{
			if(*j == &target)
			{
				j = targets.erase(j);
			}
			else
			{
				++j;
			}
		}
	}
}

//-----------------------------------------------------------------------

void connectToMessage(const char * const messageTypeName, void (*callback)(const Emitter &, const MessageBase &))
{
	const unsigned long int messageType = MessageBase::makeMessageTypeFromString(messageTypeName);
	MessageManager::getInstance().addStaticCallback(callback, messageType);
}

//---------------------------------------------------------------------

}//namespace MessageDispatch
