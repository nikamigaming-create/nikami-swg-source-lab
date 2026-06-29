// ======================================================================
//
// VRPhysicsManager.h
//
// Client-side 60 Hz rigid body manager for released VR objects. The manager
// is intentionally local-only: final placement is surfaced through a callback
// or the VRPhysicsBridge queue, not through network code.
//
// ======================================================================

#ifndef INCLUDED_VRPhysicsManager_H
#define INCLUDED_VRPhysicsManager_H

#include "VRPhysicsBridge.h"

#include <windows.h>

#ifndef _STLP_WINDOWS_H_INCLUDED
#define _STLP_WINDOWS_H_INCLUDED
#endif

#include <map>
#include <stddef.h>

class VRPhysicsManager
{
public:
	typedef void (*FinalPlacementCallback)(SwgVrPhysics::FinalPlacementState const &placement, void *context);

	VRPhysicsManager();
	~VRPhysicsManager();

	void start();
	void stop();
	bool isRunning() const;

	void setFinalPlacementCallback(FinalPlacementCallback callback, void *context);
	void recordControllerSample(uint32_t hand, SwgVrPhysics::Matrix4x4 const &gripFromWorld, double sampleTimeSeconds, bool gripHeld);
	bool spawnReleasedItem(SwgVrPhysics::ReleasedObjectState const &released);

private:
	struct ControllerSample
	{
		SwgVrPhysics::Matrix4x4 gripFromWorld;
		double sampleTimeSeconds;
		bool gripHeld;
	};

	struct ControllerHistory
	{
		ControllerHistory();

		ControllerSample samples[5];
		size_t count;
		size_t next;
	};

	struct BodyState;
	struct WorldState;
	typedef std::map<uint64_t, BodyState *> BodyMap;

	static DWORD WINAPI threadProc(void *context);

	void initializeWorld();
	void shutdownWorld();
	void clearBodiesUnlocked();
	void runLoop();
	void step(float fixedDeltaSeconds);
	void dispatchFinalPlacement(SwgVrPhysics::FinalPlacementState const &placement);

	SwgVrPhysics::Vector3 estimateLinearVelocity(uint32_t hand, SwgVrPhysics::Vector3 fallbackVelocity) const;
	SwgVrPhysics::Vector3 estimateAngularVelocity(uint32_t hand, SwgVrPhysics::Vector3 fallbackVelocity) const;

	mutable CRITICAL_SECTION m_mutex;
	volatile LONG m_running;
	HANDLE m_thread;
	FinalPlacementCallback m_finalPlacementCallback;
	void *m_finalPlacementCallbackContext;
	ControllerHistory m_controllerHistory[SwgVrPhysics::Hand_Count];
	BodyMap m_bodies;
	WorldState *m_world;

	VRPhysicsManager(VRPhysicsManager const &);
	VRPhysicsManager &operator=(VRPhysicsManager const &);
};

#endif
