// ======================================================================
//
// VRPhysicsBridge.cpp
//
// Thread-safe in-process state bridge between the OpenXR/DX11 runtime and
// the legacy SWG object loop. All ids crossing the ABI are uint64_t; never
// pass native object pointers through this layer.
//
// ======================================================================

#include "VRPhysicsBridge.h"

#include <windows.h>

#ifndef _STLP_WINDOWS_H_INCLUDED
#define _STLP_WINDOWS_H_INCLUDED
#endif

#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>

namespace
{
	using namespace SwgVrPhysics;

	struct CriticalSection
	{
		CriticalSection()
		{
			InitializeCriticalSection(&section);
		}

		~CriticalSection()
		{
			DeleteCriticalSection(&section);
		}

		void enter()
		{
			EnterCriticalSection(&section);
		}

		void leave()
		{
			LeaveCriticalSection(&section);
		}

	private:
		CRITICAL_SECTION section;

		CriticalSection(CriticalSection const &);
		CriticalSection &operator=(CriticalSection const &);
	};

	struct ScopedLock
	{
		explicit ScopedLock(CriticalSection &criticalSection) :
			m_criticalSection(criticalSection)
		{
			m_criticalSection.enter();
		}

		~ScopedLock()
		{
			m_criticalSection.leave();
		}

	private:
		CriticalSection &m_criticalSection;

		ScopedLock(ScopedLock const &);
		ScopedLock &operator=(ScopedLock const &);
	};

	struct GrabEntry
	{
		GrabbedObjectState state;
		Matrix4x4 objectFromGrip;
	};

	Vector3 zeroVector();

	struct BridgeState
	{
		BridgeState()
		{
			resetUnlocked();
		}

		void resetUnlocked()
		{
			for (uint32_t hand = 0; hand < Hand_Count; ++hand)
			{
				hands[hand].hand = hand;
				hands[hand].flags = 0;
				hands[hand].sampleTimeSeconds = 0.0;
				hands[hand].aimFromWorld = identityMatrix();
				hands[hand].gripFromWorld = identityMatrix();
				hands[hand].linearVelocityMetersPerSecond = zeroVector();
				hands[hand].angularVelocityRadiansPerSecond = zeroVector();
				hands[hand].gripValue = 0.0f;
				hands[hand].triggerValue = 0.0f;
				hands[hand].uiOcclusionDistanceMeters = 0.0f;
			}

			body.flags = 0;
			body.sampleTimeSeconds = 0.0;
			body.headFromWorld = identityMatrix();
			body.calibration = defaultCalibration();

			grabbedObjects.clear();
			simulatedObjects.clear();
			releasedObjects.clear();
			finalPlacements.clear();
			sceneHandsActive = false;
			weaponInventoryTogglePending = false;
		}

		CriticalSection mutex;
		HandState hands[Hand_Count];
		BodyState body;
		bool sceneHandsActive;
		bool weaponInventoryTogglePending;
		std::map<uint64_t, GrabEntry> grabbedObjects;
		std::map<uint64_t, GrabbedObjectState> simulatedObjects;
		std::deque<ReleasedObjectState> releasedObjects;
		std::deque<FinalPlacementState> finalPlacements;
	};

	BridgeState &state()
	{
		static BridgeState bridgeState;
		return bridgeState;
	}

	bool isValidHand(uint32_t hand)
	{
		return hand < Hand_Count;
	}

	Vector3 zeroVector()
	{
		Vector3 vector = {0.0f, 0.0f, 0.0f};
		return vector;
	}

	Vector3 vector3(float x, float y, float z)
	{
		Vector3 vector = {x, y, z};
		return vector;
	}

	Vector3 normalize(Vector3 const &value)
	{
		float const magnitude = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
		if (magnitude <= 0.00001f)
			return vector3(0.0f, 0.0f, -1.0f);

		float const invMagnitude = 1.0f / magnitude;
		return vector3(value.x * invMagnitude, value.y * invMagnitude, value.z * invMagnitude);
	}

	bool hasFlag(uint32_t flags, uint32_t flag)
	{
		return (flags & flag) != 0;
	}

	float environmentFloat(char const *name, float defaultValue)
	{
		char value[64];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return defaultValue;
		return static_cast<float>(std::atof(value));
	}

	Matrix4x4 multiply(Matrix4x4 const &lhs, Matrix4x4 const &rhs)
	{
		Matrix4x4 out = identityMatrix();
		for (int row = 0; row != 3; ++row)
		{
			for (int column = 0; column != 3; ++column)
			{
				out.m[row * 4 + column] =
					lhs.m[row * 4 + 0] * rhs.m[0 * 4 + column] +
					lhs.m[row * 4 + 1] * rhs.m[1 * 4 + column] +
					lhs.m[row * 4 + 2] * rhs.m[2 * 4 + column];
			}

			out.m[row * 4 + 3] =
				lhs.m[row * 4 + 0] * rhs.m[3] +
				lhs.m[row * 4 + 1] * rhs.m[7] +
				lhs.m[row * 4 + 2] * rhs.m[11] +
				lhs.m[row * 4 + 3];
		}

		return out;
	}

	Matrix4x4 invertRigid(Matrix4x4 const &matrix)
	{
		Matrix4x4 out = identityMatrix();

		out.m[0] = matrix.m[0];
		out.m[1] = matrix.m[4];
		out.m[2] = matrix.m[8];
		out.m[4] = matrix.m[1];
		out.m[5] = matrix.m[5];
		out.m[6] = matrix.m[9];
		out.m[8] = matrix.m[2];
		out.m[9] = matrix.m[6];
		out.m[10] = matrix.m[10];

		float const tx = matrix.m[3];
		float const ty = matrix.m[7];
		float const tz = matrix.m[11];
		out.m[3] = -(out.m[0] * tx + out.m[1] * ty + out.m[2] * tz);
		out.m[7] = -(out.m[4] * tx + out.m[5] * ty + out.m[6] * tz);
		out.m[11] = -(out.m[8] * tx + out.m[9] * ty + out.m[10] * tz);

		return out;
	}

	void refreshGrabbedStateFromHand(GrabEntry &entry, HandState const &handState)
	{
		if (!hasFlag(handState.flags, MatrixFlagGripValid))
			return;

		entry.state.objectFromWorld = multiply(handState.gripFromWorld, entry.objectFromGrip);
		entry.state.lastUpdateSeconds = handState.sampleTimeSeconds;
		entry.state.linearVelocityMetersPerSecond = handState.linearVelocityMetersPerSecond;
		entry.state.angularVelocityRadiansPerSecond = handState.angularVelocityRadiansPerSecond;
	}
}

namespace SwgVrPhysics
{
	Matrix4x4 identityMatrix()
	{
		Matrix4x4 matrix;
		for (int i = 0; i != 16; ++i)
			matrix.m[i] = 0.0f;

		matrix.m[0] = 1.0f;
		matrix.m[5] = 1.0f;
		matrix.m[10] = 1.0f;
		matrix.m[15] = 1.0f;
		return matrix;
	}

	VrBodyCalibration defaultCalibration()
	{
		VrBodyCalibration cal;
		cal.shoulderWidthMeters   = 0.38f;
		cal.upperArmLengthMeters  = 0.28f;
		cal.foreArmLengthMeters   = 0.24f;
		cal.shoulderDropMeters    = 0.22f;  // head origin to shoulder level
		cal.wristOffsetMeters     = 0.05f;  // grip pivot to wrist centre
		cal.bodyHeightMeters      = 1.75f;
		cal.armLengthScaleLeft    = 1.0f;
		cal.armLengthScaleRight   = 1.0f;
		return cal;
	}
}

uint32_t SWGVRPhysics_BridgeAbiVersion()
{
	return SwgVrPhysics::BridgeAbiVersion;
}

void SWGVRPhysics_ResetBridge()
{
	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	bridge.resetUnlocked();
}

bool SWGVRPhysics_PublishHandState(SwgVrPhysics::HandState const *incoming)
{
	if (!incoming || !isValidHand(incoming->hand))
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	bridge.hands[incoming->hand] = *incoming;
	return true;
}

bool SWGVRPhysics_GetHandState(uint32_t hand, SwgVrPhysics::HandState *outgoing)
{
	if (!outgoing || !isValidHand(hand))
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	*outgoing = bridge.hands[hand];
	return true;
}

bool SWGVRPhysics_GetAimRay(uint32_t hand, SwgVrPhysics::Ray *outgoing)
{
	if (!outgoing || !isValidHand(hand))
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	HandState const &handState = bridge.hands[hand];
	if (!hasFlag(handState.flags, MatrixFlagAimValid))
		return false;

	Matrix4x4 const &aim = handState.aimFromWorld;
	float const offsetX = environmentFloat("SWG_OG_VR_AIM_RAY_OFFSET_X_METERS", 0.0f);
	float const offsetY = environmentFloat("SWG_OG_VR_AIM_RAY_OFFSET_Y_METERS", -0.10f);
	float const offsetZ = environmentFloat("SWG_OG_VR_AIM_RAY_OFFSET_Z_METERS", 0.0f);
	outgoing->origin = vector3(
		aim.m[3] + aim.m[0] * offsetX + aim.m[1] * offsetY + aim.m[2] * offsetZ,
		aim.m[7] + aim.m[4] * offsetX + aim.m[5] * offsetY + aim.m[6] * offsetZ,
		aim.m[11] + aim.m[8] * offsetX + aim.m[9] * offsetY + aim.m[10] * offsetZ);
	outgoing->direction = normalize(vector3(-aim.m[2], -aim.m[6], -aim.m[10]));
	return true;
}

bool SWGVRPhysics_PublishBodyState(SwgVrPhysics::BodyState const *incoming)
{
	if (!incoming)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	bridge.body = *incoming;
	return true;
}

bool SWGVRPhysics_GetBodyState(SwgVrPhysics::BodyState *outgoing)
{
	if (!outgoing)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	*outgoing = bridge.body;
	return true;
}

void SWGVRPhysics_SetSceneHandsActive(bool active)
{
	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	bridge.sceneHandsActive = active;
}

bool SWGVRPhysics_AreSceneHandsActive()
{
	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	return bridge.sceneHandsActive;
}

void SWGVRPhysics_PublishWeaponInventoryToggle()
{
	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	bridge.weaponInventoryTogglePending = true;
}

bool SWGVRPhysics_ConsumeWeaponInventoryToggle()
{
	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	if (!bridge.weaponInventoryTogglePending)
		return false;

	bridge.weaponInventoryTogglePending = false;
	return true;
}

bool SWGVRPhysics_BeginGrab(uint64_t networkId, uint32_t hand, SwgVrPhysics::Matrix4x4 const *objectFromWorld, SwgVrPhysics::Vector3 const *halfExtentsMeters)
{
	if (networkId == 0 || !isValidHand(hand) || !objectFromWorld)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);

	HandState const &handState = bridge.hands[hand];
	if (!hasFlag(handState.flags, MatrixFlagGripValid))
		return false;

	GrabEntry entry;
	entry.state.networkId = networkId;
	entry.state.flags = ObjectFlagGrabbed;
	entry.state.grabbingHand = hand;
	entry.state.lastUpdateSeconds = handState.sampleTimeSeconds;
	entry.state.objectFromWorld = *objectFromWorld;
	entry.state.halfExtentsMeters = halfExtentsMeters ? *halfExtentsMeters : vector3(0.25f, 0.25f, 0.25f);
	entry.state.linearVelocityMetersPerSecond = zeroVector();
	entry.state.angularVelocityRadiansPerSecond = zeroVector();
	entry.objectFromGrip = multiply(invertRigid(handState.gripFromWorld), *objectFromWorld);

	bridge.grabbedObjects[networkId] = entry;
	bridge.simulatedObjects.erase(networkId);
	return true;
}

bool SWGVRPhysics_UpdateGrabPose(uint64_t networkId, SwgVrPhysics::Matrix4x4 const *objectFromWorld)
{
	if (networkId == 0 || !objectFromWorld)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);

	std::map<uint64_t, GrabEntry>::iterator it = bridge.grabbedObjects.find(networkId);
	if (it == bridge.grabbedObjects.end())
		return false;

	GrabEntry &entry = it->second;
	HandState const &handState = bridge.hands[entry.state.grabbingHand];
	entry.state.objectFromWorld = *objectFromWorld;
	entry.state.lastUpdateSeconds = handState.sampleTimeSeconds;
	entry.state.linearVelocityMetersPerSecond = handState.linearVelocityMetersPerSecond;
	entry.state.angularVelocityRadiansPerSecond = handState.angularVelocityRadiansPerSecond;
	entry.objectFromGrip = hasFlag(handState.flags, MatrixFlagGripValid) ? multiply(invertRigid(handState.gripFromWorld), *objectFromWorld) : identityMatrix();
	return true;
}

bool SWGVRPhysics_EndGrab(uint64_t networkId, SwgVrPhysics::ReleasedObjectState *releasedState)
{
	if (networkId == 0 || !releasedState)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);

	std::map<uint64_t, GrabEntry>::iterator it = bridge.grabbedObjects.find(networkId);
	if (it == bridge.grabbedObjects.end())
		return false;

	GrabEntry &entry = it->second;
	HandState const &handState = bridge.hands[entry.state.grabbingHand];
	refreshGrabbedStateFromHand(entry, handState);
	GrabbedObjectState const grabbed = entry.state;

	releasedState->networkId = grabbed.networkId;
	releasedState->sourceHand = grabbed.grabbingHand;
	releasedState->releaseTimeSeconds = handState.sampleTimeSeconds;
	releasedState->objectFromWorld = grabbed.objectFromWorld;
	releasedState->halfExtentsMeters = grabbed.halfExtentsMeters;
	releasedState->linearVelocityMetersPerSecond = handState.linearVelocityMetersPerSecond;
	releasedState->angularVelocityRadiansPerSecond = handState.angularVelocityRadiansPerSecond;

	bridge.releasedObjects.push_back(*releasedState);
	bridge.grabbedObjects.erase(it);
	return true;
}

bool SWGVRPhysics_GetGrabbedObjectState(uint64_t networkId, SwgVrPhysics::GrabbedObjectState *outgoing)
{
	if (networkId == 0 || !outgoing)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);

	std::map<uint64_t, GrabEntry>::iterator it = bridge.grabbedObjects.find(networkId);
	if (it == bridge.grabbedObjects.end())
	{
		std::map<uint64_t, GrabbedObjectState>::iterator simulatedIt = bridge.simulatedObjects.find(networkId);
		if (simulatedIt == bridge.simulatedObjects.end())
			return false;

		*outgoing = simulatedIt->second;
		return true;
	}

	GrabEntry &entry = it->second;
	HandState const &handState = bridge.hands[entry.state.grabbingHand];
	refreshGrabbedStateFromHand(entry, handState);
	*outgoing = entry.state;
	return true;
}

bool SWGVRPhysics_IsHandGrabbing(uint32_t hand)
{
	if (!isValidHand(hand))
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	for (std::map<uint64_t, GrabEntry>::const_iterator it = bridge.grabbedObjects.begin(); it != bridge.grabbedObjects.end(); ++it)
	{
		if (it->second.state.grabbingHand == hand)
			return true;
	}

	return false;
}

bool SWGVRPhysics_PublishReleasedObject(SwgVrPhysics::ReleasedObjectState const *incoming)
{
	if (!incoming || incoming->networkId == 0)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	bridge.releasedObjects.push_back(*incoming);
	return true;
}

bool SWGVRPhysics_TryConsumeReleasedObject(SwgVrPhysics::ReleasedObjectState *outgoing)
{
	if (!outgoing)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	if (bridge.releasedObjects.empty())
		return false;

	*outgoing = bridge.releasedObjects.front();
	bridge.releasedObjects.pop_front();
	return true;
}

bool SWGVRPhysics_PublishSimulatedObjectState(SwgVrPhysics::GrabbedObjectState const *incoming)
{
	if (!incoming || incoming->networkId == 0)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	bridge.simulatedObjects[incoming->networkId] = *incoming;
	return true;
}

bool SWGVRPhysics_ClearSimulatedObject(uint64_t networkId)
{
	if (networkId == 0)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	return bridge.simulatedObjects.erase(networkId) != 0;
}

bool SWGVRPhysics_SetPhysicsActive(uint64_t networkId, bool active)
{
	if (networkId == 0)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);

	std::map<uint64_t, GrabEntry>::iterator it = bridge.grabbedObjects.find(networkId);
	if (it == bridge.grabbedObjects.end())
	{
		std::map<uint64_t, GrabbedObjectState>::iterator simulatedIt = bridge.simulatedObjects.find(networkId);
		if (simulatedIt == bridge.simulatedObjects.end())
			return false;

		if (active)
			simulatedIt->second.flags |= ObjectFlagPhysicsActive;
		else
			simulatedIt->second.flags &= ~ObjectFlagPhysicsActive;

		return true;
	}

	if (active)
		it->second.state.flags |= ObjectFlagPhysicsActive;
	else
		it->second.state.flags &= ~ObjectFlagPhysicsActive;

	return true;
}

bool SWGVRPhysics_PublishFinalPlacement(SwgVrPhysics::FinalPlacementState const *incoming)
{
	if (!incoming || incoming->networkId == 0)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	bridge.finalPlacements.push_back(*incoming);
	return true;
}

bool SWGVRPhysics_TryConsumeFinalPlacement(SwgVrPhysics::FinalPlacementState *outgoing)
{
	if (!outgoing)
		return false;

	BridgeState &bridge = state();
	ScopedLock lock(bridge.mutex);
	if (bridge.finalPlacements.empty())
		return false;

	*outgoing = bridge.finalPlacements.front();
	bridge.finalPlacements.pop_front();
	return true;
}
