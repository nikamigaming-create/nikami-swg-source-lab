// ======================================================================
//
// SetupClientObject.cpp
// copyright (c) 2001 Sony Online Entertainment
//
// ======================================================================

#include "clientObject/FirstClientObject.h"
#include "clientObject/SetupClientObject.h"

#include "clientObject/BeamAppearance.h"
#include "clientObject/BeamAppearanceTemplate.h"
#include "clientObject/ConfigClientObject.h"
#include "clientObject/ComponentAppearanceTemplate.h"
#include "clientObject/DetailAppearance.h"
#include "clientObject/DetailAppearanceTemplate.h"
#include "clientObject/GameCamera.h"
#include "clientObject/HardpointObject.h"
#include "clientObject/InteriorEnvironmentBlockManager.h"
#include "clientObject/MarkerAppearance.h"
#include "clientObject/MarkerAppearanceTemplate.h"
#include "clientObject/MeshAppearance.h"
#include "clientObject/MeshAppearanceTemplate.h"
#include "clientObject/MouseCursor.h"
#include "clientObject/RibbonAppearance.h"
#include "clientObject/RibbonTrailAppearance.h"
#include "clientObject/ShadowBlobManager.h"
#include "clientObject/ShadowManager.h"
#include "clientObject/ShadowVolume.h"
#include "clientObject/SpriteAppearance.h"
#include "clientObject/SpriteAppearanceTemplate.h"
#include "clientObject/ReticleManager.h"
#include "clientObject/TimerObject.h"
#include "clientObject/TrailAppearance.h"
#include "sharedDebug/InstallTimer.h"
#include "sharedFoundation/ExitChain.h"

#include <stdio.h>
#include <stdlib.h>

// ======================================================================
// SetupClientObjectNamespace
// ======================================================================

namespace SetupClientObjectNamespace
{
	void remove ();
	void writeStartupTrace(char const *stage);
}

using namespace SetupClientObjectNamespace;

void SetupClientObjectNamespace::writeStartupTrace(char const *stage)
{
	char const *path = getenv("SWG_STARTUP_TRACE_FILE");
	if (!path || !*path)
		return;

	FILE *file = fopen(path, "ab");
	if (!file)
		return;

	fprintf(file, "SetupClientObject:%s\n", stage);
	fclose(file);
}

// ======================================================================
// SetupClientObject::Data
// ======================================================================

SetupClientObject::Data::Data () :
	m_viewer (false)
{
}

// ======================================================================
// STATIC PUBLIC SetupClientObject
// ======================================================================

void SetupClientObject::install (const Data& data)
{
	InstallTimer const installTimer("SetupClientObject::install");
	writeStartupTrace("install-entry");

	ConfigClientObject::install ();
	writeStartupTrace("after-config");

#ifdef PLATFORM_WIN32
	// setup the default mouse acceleration
	writeStartupTrace("before-mouse-accel");
	DWORD mouseData[3];
	memset(mouseData, 0, sizeof(mouseData));
	const BOOL result = SystemParametersInfo(SPI_GETMOUSE, 0, mouseData, 0);
	DEBUG_FATAL(!result, ("SystemParametersInfo SPI_GETMOUSE failed"));
	if (result)
		MouseCursor::setAcceleration(static_cast<int>(mouseData[2]), static_cast<int>(mouseData[0]), static_cast<int>(mouseData[1]));
	writeStartupTrace("after-mouse-accel");
#endif

	TimerObject::install ();
	writeStartupTrace("after-timer-object");
	HardpointObject::install ();
	writeStartupTrace("after-hardpoint-object");
	SpriteAppearance::install ();
	writeStartupTrace("after-sprite-appearance");
	SpriteAppearanceTemplate::install ();
	writeStartupTrace("after-sprite-appearance-template");
	DetailAppearance::install ();
	writeStartupTrace("after-detail-appearance");
	DetailAppearanceTemplate::install (data.m_viewer);
	writeStartupTrace("after-detail-appearance-template");
	ComponentAppearanceTemplate::install ();
	writeStartupTrace("after-component-appearance-template");
	BeamAppearance::install ();
	writeStartupTrace("after-beam-appearance");
	BeamAppearanceTemplate::install();
	writeStartupTrace("after-beam-appearance-template");
	MarkerAppearance::install ();
	writeStartupTrace("after-marker-appearance");
	MarkerAppearanceTemplate::install ();
	writeStartupTrace("after-marker-appearance-template");
	MeshAppearanceTemplate::install();
	writeStartupTrace("after-mesh-appearance-template");
	ShadowManager::install ();
	writeStartupTrace("after-shadow-manager");
	ShadowVolume::install ();
	writeStartupTrace("after-shadow-volume");
	GameCamera::install ();
	writeStartupTrace("after-game-camera");
	TrailAppearance::install ();
	writeStartupTrace("after-trail-appearance");
	RibbonAppearance::install ();
	writeStartupTrace("after-ribbon-appearance");
	RibbonTrailAppearance::install ();
	writeStartupTrace("after-ribbon-trail-appearance");
	InteriorEnvironmentBlockManager::install ();
	writeStartupTrace("after-interior-environment-block-manager");
	ShadowBlobManager::install ();
	writeStartupTrace("after-shadow-blob-manager");
	ReticleManager::install ();
	writeStartupTrace("after-reticle-manager");
	MeshAppearance::install ();
	writeStartupTrace("after-mesh-appearance");

	ExitChain::add (SetupClientObjectNamespace::remove, "SetupClientObject");
	writeStartupTrace("install-exit");
}

// ----------------------------------------------------------------------

void SetupClientObject::setupGameData (Data& data)
{
	data.m_viewer = false;
}

// ----------------------------------------------------------------------

void SetupClientObject::setupToolData (Data& data)
{
	data.m_viewer = true;
}

// ======================================================================
// SetupClientObjectNamespace
// ======================================================================

void SetupClientObjectNamespace::remove ()
{
}

// ======================================================================
