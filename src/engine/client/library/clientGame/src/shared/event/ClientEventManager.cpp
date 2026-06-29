// ======================================================================
//
// ClientEventManager.cpp
// Copyright 2002 Sony Online Entertainment Inc.
// All Rights Reserved.
//
// ======================================================================

#include "clientGame/FirstClientGame.h"
#include "clientGame/ClientEventManager.h"

#include "clientAudio/Audio.h"
#include "clientGame/ClientDataFile.h"
#include "clientGame/ClientEffectManager.h"
#include "clientGame/GameNetwork.h"
#include "sharedDebug/InstallTimer.h"
#include "sharedFoundation/Crc.h"
#include "sharedFoundation/CrcLowerString.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedFoundation/NetworkIdArchive.h"
#include "sharedGame/SharedObjectTemplate.h"
#include "sharedMessageDispatch/Emitter.h"
#include "sharedMessageDispatch/Message.h"
#include "sharedMessageDispatch/Receiver.h"
#include "sharedNetworkMessages/ClientEffectMessages.h"
#include "sharedObject/CellProperty.h"
#include "sharedObject/NetworkIdManager.h"
#include "sharedObject/Object.h"
#include "sharedTerrain/TerrainObject.h"

#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma warning(disable: 4503) //decorated name length exceeded

// ======================================================================

namespace
{
	bool s_installed = false;

	//NOTE: We need a seperate class to handle the Receiver, since it's non-static and
	class Listener : public MessageDispatch::Receiver
	{
	public:

		Listener()
		:	MessageDispatch::Receiver()
		{
			connectToMessage ("PlayClientEventObjectMessage");
			connectToMessage ("PlayClientEventLocMessage");
		}

		//----------------------------------------------------------------------

		void receiveMessage(const MessageDispatch::Emitter & source, const MessageDispatch::MessageBase & message)
		{
			UNREF(source);
			if (message.isType ("PlayClientEventObjectMessage"))
			{
				Archive::ReadIterator ri = dynamic_cast<const GameNetworkMessage &>(message).getByteStream().begin();

				PlayClientEventObjectMessage pceom(ri);
				ClientEventManager::playEvent(CrcLowerString(pceom.getEventName().c_str()), NetworkIdManager::getObjectById(pceom.getObjectId()), CrcLowerString(pceom.getHardpoint().c_str()));
			}
			else if (message.isType ("PlayClientEventLocMessage"))
			{
				Archive::ReadIterator ri = dynamic_cast<const GameNetworkMessage &>(message).getByteStream().begin();
				PlayClientEventLocMessage pcelm(ri);
				//TODO EAS get the terrain height, use terrain delta
				const TerrainObject* const terrain = NON_NULL(TerrainObject::getInstance());
				float terrainHeight = 0;
				bool result = terrain->getHeight(pcelm.getLocation(), terrainHeight);
				UNREF(result);
				DEBUG_WARNING(!result, ("Failed to get terrain height for script-based client effect"));
				const float terrainDelta = pcelm.getTerrainDelta();
				//adjust play location for height offset sent from script
				const Vector playLoc(pcelm.getLocation().x, terrainHeight + terrainDelta, pcelm.getLocation().z);
				ClientEventManager::playEvent(CrcLowerString(pcelm.getEventSourceName().c_str()), CrcLowerString(pcelm.getEventDestName().c_str()), CellProperty::getWorldCellProperty(), playLoc, Vector::unitY);
			}
		}
	};

	Listener* s_listener;

	void writeClientEventStartupTrace(char const *stage)
	{
		char const *path = getenv("SWG_STARTUP_TRACE_FILE");
		if (!path || !*path)
			path = getenv("SWG_STARTUP_TRACE");
		if (!path || !*path)
			return;
		FILE *file = fopen(path, "a");
		if (!file)
			return;
		fprintf(file, "ClientEventManager:%s\n", stage);
		fclose(file);
	}

	struct SourceDestEventEntry
	{
		uint32 sourceCrc;
		uint32 destCrc;
		char effect[256];
	};

	int const s_maxSourceDestEventEntries = 4096;
	SourceDestEventEntry s_sourceDestEventEntries[s_maxSourceDestEventEntries];
	int s_sourceDestEventEntryCount = 0;
}

// ======================================================================

bool ClientEventManager::ms_installed;
std::map<CrcLowerString, std::map<CrcLowerString, CrcLowerString> > *ClientEventManager::m_eventSourceDestMap;

// ======================================================================

namespace
{
	//the relative filename of the source to destination event type map
	const char* const s_sourceDestMapName = "misc/client_event_source_dest_map.iff";
}

// ======================================================================

void ClientEventManager::install()
{
	InstallTimer const installTimer("ClientEventManager::install");

	DEBUG_FATAL(ms_installed, ("double install"));
	writeClientEventStartupTrace("install-entry");
	m_eventSourceDestMap = 0;
	writeClientEventStartupTrace("install-before-listener");
	s_listener = new Listener;
	writeClientEventStartupTrace("install-after-listener");

	//load the source/dest map
	load();
	writeClientEventStartupTrace("install-after-load");
	ms_installed = true;

	ExitChain::add (remove, "ClientEventManager::remove");
	writeClientEventStartupTrace("install-exit");
}

// ----------------------------------------------------------------------

void ClientEventManager::remove()
{
	DEBUG_FATAL(!ms_installed, ("not installed"));
	delete m_eventSourceDestMap;
	m_eventSourceDestMap = 0;
	delete s_listener;
	s_listener = 0;
	s_sourceDestEventEntryCount = 0;
	ms_installed = false;
}

// ----------------------------------------------------------------------

/** Load the source/dest event map
 */
void ClientEventManager::load()
{
	writeClientEventStartupTrace("load-entry");
	Iff iff(s_sourceDestMapName);

	iff.enterForm(TAG (E,S,D,M));
		iff.enterForm(TAG(0,0,0,0));
			load_0000(iff);
		iff.exitForm(TAG(0,0,0,0));
	iff.exitForm(TAG(E,S,D,M));
	writeClientEventStartupTrace("load-exit");
}

// ----------------------------------------------------------------------

void ClientEventManager::load_0000(Iff& iff)
{
	writeClientEventStartupTrace("load-0000-entry");
	int row = 0;
	char source[128];
	char dest[128];
	char effect[256];
	while(!iff.atEndOfForm())
	{
		char stage[64];
		snprintf(stage, sizeof(stage), "load-0000-row-%d-before-chunk", row);
		writeClientEventStartupTrace(stage);
		iff.enterChunk(TAG(E,M,A,P));
			snprintf(stage, sizeof(stage), "load-0000-row-%d-before-read", row);
			writeClientEventStartupTrace(stage);
			iff.read_string(source, sizeof(source) - 1);
			iff.read_string(dest, sizeof(dest) - 1);
			iff.read_string(effect, sizeof(effect) - 1);
			snprintf(stage, sizeof(stage), "load-0000-row-%d-after-read", row);
			writeClientEventStartupTrace(stage);
			if (s_sourceDestEventEntryCount < s_maxSourceDestEventEntries)
			{
				SourceDestEventEntry &entry = s_sourceDestEventEntries[s_sourceDestEventEntryCount];
				entry.sourceCrc = Crc::normalizeAndCalculate(source);
				entry.destCrc = Crc::normalizeAndCalculate(dest);
				strncpy(entry.effect, effect, sizeof(entry.effect) - 1);
				entry.effect[sizeof(entry.effect) - 1] = '\0';
				++s_sourceDestEventEntryCount;
			}
			else
				DEBUG_WARNING(true, ("ClientEventManager source/dest event table overflow"));
			snprintf(stage, sizeof(stage), "load-0000-row-%d-after-store", row);
			writeClientEventStartupTrace(stage);
			snprintf(stage, sizeof(stage), "load-0000-row-%d-before-exit-chunk", row);
			writeClientEventStartupTrace(stage);
		iff.exitChunk(TAG(E,M,A,P));
		snprintf(stage, sizeof(stage), "load-0000-row-%d-after-exit-chunk", row);
		writeClientEventStartupTrace(stage);
		++row;
		snprintf(stage, sizeof(stage), "load-0000-row-%d-before-loop-test", row);
		writeClientEventStartupTrace(stage);
	}
	writeClientEventStartupTrace("load-0000-exit");
}

// ----------------------------------------------------------------------

/** Play an event on an object
 */
bool ClientEventManager::playEvent(const CrcLowerString& eventType, Object* object, const CrcLowerString& hardpoint)
{
	//redirect implementation to a "2-object" version, but in this case they're both the same object
	return playEvent(eventType, object, object, hardpoint, Transform::identity);
}

//----------------------------------------------------------------------

bool ClientEventManager::playEvent(const CrcLowerString& eventType, Object* object, Transform const & transform)
{
	return playEvent(eventType, object, object, CrcLowerString::empty, transform);
}

//----------------------------------------------------------------------

bool ClientEventManager::playEvent(const CrcLowerString& eventType, Object* objectToPlayEffectOn, const Object* objectWithEventBinding, const CrcLowerString& hardpoint)
{
	return ClientEventManager::playEvent(eventType, objectToPlayEffectOn, objectWithEventBinding, hardpoint, Transform::identity);
}

// ----------------------------------------------------------------------

/** This function allows playing an event on an object.  This API maps the event name to the objectWithEventBinding object, but then plays the actual event
 * (on the optional hardpoint) of the other object, objectToPlayEffectOn.  An example where this is useful might be the following.  A gun is fired, and the
 * event-effect binding lives on the gun template.  But the player owns the hardpoint, and so has to be the "effect firer".
 */
bool ClientEventManager::playEvent(const CrcLowerString& eventType, Object* objectToPlayEffectOn, const Object* objectWithEventBinding, const CrcLowerString& hardpoint, Transform const & transform)
{
	if (!objectWithEventBinding)
	{
		DEBUG_WARNING (true, ("ClientEventManager::playEvent: objectWithEventBinding is null"));
		return false;
	}

	if (!objectToPlayEffectOn)
	{
		DEBUG_WARNING (true, ("ClientEventManager::playEvent: objectToPlayEffectOn is null"));
		return false;
	}

	const SharedObjectTemplate* const objectTemplate = dynamic_cast<const SharedObjectTemplate*> (objectWithEventBinding->getObjectTemplate ());
	if (!objectTemplate)
		return false;

	const ClientDataFile* const clientData = dynamic_cast<const ClientDataFile*> (objectTemplate->getClientData ());
	if (!clientData)
		return false;

	bool success = false;

	const CrcLowerString& effect = clientData->getEffectForEvent (eventType);
	if (effect != CrcLowerString::empty)
	{
		char const * const string = effect.getString ();
		if (string)
		{
			switch (string[0])
			{
				case 'c':
					if (hardpoint.isEmpty())
						success = ClientEffectManager::playClientEffect(effect, objectToPlayEffectOn, transform);
					else
						success = ClientEffectManager::playClientEffect(effect, objectToPlayEffectOn, hardpoint);
					break;

				case 's':
				case 'v':
					Audio::playSound(effect.getString (), objectToPlayEffectOn->getPosition_w (), objectToPlayEffectOn->getParentCell());
					success = true;
					break;

				default:
					DEBUG_WARNING(true, ("Effect name for [%s], event [%s], does not begin with c or s: %s", clientData->getName(), eventType.getString (), string));
					success = false;
					break;
			}
		}
	}

	return success;
}

// ----------------------------------------------------------------------

void ClientEventManager::getEffectName(const CrcLowerString& eventType, const Object& object, CrcLowerString& effectName)
{
	const SharedObjectTemplate* const objectTemplate = dynamic_cast<const SharedObjectTemplate*> (object.getObjectTemplate ());
	if (!objectTemplate)
	{
		effectName = CrcLowerString::empty;
		return;
	}

	const ClientDataFile* const clientData = dynamic_cast<const ClientDataFile*> (objectTemplate->getClientData ());
	if (!clientData)
	{
		effectName = CrcLowerString::empty;
		return;
	}

	effectName = clientData->getEffectForEvent (eventType);
}

// ----------------------------------------------------------------------

/** Play an event at a position in space.  Since we don't have an object on which to look up the event, we need enough information to deduce an effect.  This is done by
 *  by passing 2 events in, a "source" and a "destination".  For instance, a "footstep" on "grass" or a "blasterhit" on "building".
 */
bool ClientEventManager::playEvent(const CrcLowerString& sourceType, const CrcLowerString& destType, const CellProperty* cell, const Vector& position, const Vector& up)
{
	NOT_NULL(cell);

	bool success = false;

	for (int i = 0; i < s_sourceDestEventEntryCount; ++i)
	{
		SourceDestEventEntry const &entry = s_sourceDestEventEntries[i];
		if (entry.sourceCrc == sourceType.getCrc() && entry.destCrc == destType.getCrc())
		{
			CrcLowerString const effectName(entry.effect);
			if (effectName != CrcLowerString::empty)
				success = ClientEffectManager::playClientEffect(effectName, cell, position, up);
			break;
		}
	}
//	DEBUG_WARNING(effect == CrcLowerString::empty, ("The Event sourceDestMap does not have a mapping for source=%s dest=%s, cannot play event\n", sourceType.getString(), destType.getString()));
	return success;
}

// ======================================================================
