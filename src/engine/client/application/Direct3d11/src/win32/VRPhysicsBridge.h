// ======================================================================
//
// VRPhysicsBridge.h
//
// 64-bit clean client/DX11 proxy ABI for local VR object manipulation.
// This header intentionally contains only POD types and C-callable exports
// so the legacy SWG client and the 64-bit proxy DLL can share it safely.
//
// ======================================================================

#ifndef INCLUDED_VRPhysicsBridge_H
#define INCLUDED_VRPhysicsBridge_H

#include <stdint.h>

#if defined(_WIN32)
#	if defined(VRPHYSICSBRIDGE_NO_IMPORT)
#		define VRPHYSICSBRIDGE_API extern "C"
#	elif defined(VRPHYSICSBRIDGE_EXPORTS) || defined(DIRECT3D11_EXPORTS)
#		define VRPHYSICSBRIDGE_API extern "C" __declspec(dllexport)
#	else
#		define VRPHYSICSBRIDGE_API extern "C" __declspec(dllimport)
#	endif
#else
#	define VRPHYSICSBRIDGE_API extern "C"
#endif

namespace SwgVrPhysics
{
	enum Constants
	{
		BridgeAbiVersion = 6,
		MatrixFlagTracked = 1u << 0,
		MatrixFlagGripHeld = 1u << 1,
		MatrixFlagValid = 1u << 2,
		MatrixFlagAimValid = 1u << 3,
		MatrixFlagGripValid = 1u << 4,
		MatrixFlagUiOccluded = 1u << 5,
		MatrixFlagTriggerHeld = 1u << 6,
		MatrixFlagMenuHeld = 1u << 7,
		MatrixFlagAimTracked = 1u << 8,
		MatrixFlagGripTracked = 1u << 9,
		ObjectFlagGrabbed = 1u << 0,
		ObjectFlagReleasePending = 1u << 1,
		ObjectFlagPhysicsActive = 1u << 2,
		ObjectFlagSleeping = 1u << 3,
		BodyFlagHeadValid = 1u << 0,
		BodyFlagCalibrated = 1u << 1
	};

	enum Hand
	{
		Hand_Left = 0,
		Hand_Right = 1,
		Hand_Count = 2
	};

	struct Vector3
	{
		float x;
		float y;
		float z;
	};

	struct Quaternion
	{
		float x;
		float y;
		float z;
		float w;
	};

	struct Ray
	{
		Vector3 origin;
		Vector3 direction;
	};

	struct Matrix4x4
	{
		// Row-major storage with SWG/Transform semantics: local basis vectors
		// are the 3x3 columns, translation is m[3], m[7], m[11].
		float m[16];
	};

	struct HandState
	{
		uint32_t hand;
		uint32_t flags;
		double sampleTimeSeconds;
		Matrix4x4 aimFromWorld;
		Matrix4x4 gripFromWorld;
		Vector3 linearVelocityMetersPerSecond;
		Vector3 angularVelocityRadiansPerSecond;
		float gripValue;
		float triggerValue;
		float uiOcclusionDistanceMeters;
	};

	struct GrabbedObjectState
	{
		uint64_t networkId;
		uint32_t flags;
		uint32_t grabbingHand;
		double lastUpdateSeconds;
		Matrix4x4 objectFromWorld;
		Vector3 halfExtentsMeters;
		Vector3 linearVelocityMetersPerSecond;
		Vector3 angularVelocityRadiansPerSecond;
	};

	struct ReleasedObjectState
	{
		uint64_t networkId;
		uint32_t sourceHand;
		double releaseTimeSeconds;
		Matrix4x4 objectFromWorld;
		Vector3 halfExtentsMeters;
		Vector3 linearVelocityMetersPerSecond;
		Vector3 angularVelocityRadiansPerSecond;
	};

	struct FinalPlacementState
	{
		uint64_t networkId;
		double sleepTimeSeconds;
		Matrix4x4 objectFromWorld;
		Vector3 positionMeters;
		Quaternion orientation;
	};

	// Calibration data for full-body IK.  Stored in BodyState so the game side
	// and the bridge side share a single source of truth.
	struct VrBodyCalibration
	{
		float shoulderWidthMeters;       // lateral distance between shoulder joints
		float upperArmLengthMeters;      // shoulder to elbow (average both sides)
		float foreArmLengthMeters;       // elbow to wrist (average both sides)
		float shoulderDropMeters;        // downward offset from head origin to shoulder level
		float wristOffsetMeters;         // forward offset from grip origin to wrist pivot
		float bodyHeightMeters;          // total body height estimate (head to floor)
		float armLengthScaleLeft;        // per-side scale applied to default arm lengths
		float armLengthScaleRight;
	};

	// Per-frame body tracking state published by the D3D11/OpenXR layer.
	// Contains HMD head pose and calibration; arms are driven by HandState.
	struct BodyState
	{
		uint32_t flags;                  // BodyFlagHeadValid | BodyFlagCalibrated
		double sampleTimeSeconds;
		Matrix4x4 headFromWorld;         // HMD head/view pose in recentered world space
		VrBodyCalibration calibration;
	};

	Matrix4x4 identityMatrix();
	VrBodyCalibration defaultCalibration();
}

VRPHYSICSBRIDGE_API uint32_t SWGVRPhysics_BridgeAbiVersion();
VRPHYSICSBRIDGE_API void SWGVRPhysics_ResetBridge();

VRPHYSICSBRIDGE_API bool SWGVRPhysics_PublishHandState(SwgVrPhysics::HandState const *state);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_GetHandState(uint32_t hand, SwgVrPhysics::HandState *state);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_GetAimRay(uint32_t hand, SwgVrPhysics::Ray *ray);
VRPHYSICSBRIDGE_API void SWGVRPhysics_SetSceneHandsActive(bool active);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_AreSceneHandsActive();
VRPHYSICSBRIDGE_API void SWGVRPhysics_PublishWeaponInventoryToggle();
VRPHYSICSBRIDGE_API bool SWGVRPhysics_ConsumeWeaponInventoryToggle();

VRPHYSICSBRIDGE_API bool SWGVRPhysics_PublishBodyState(SwgVrPhysics::BodyState const *state);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_GetBodyState(SwgVrPhysics::BodyState *state);

VRPHYSICSBRIDGE_API bool SWGVRPhysics_BeginGrab(uint64_t networkId, uint32_t hand, SwgVrPhysics::Matrix4x4 const *objectFromWorld, SwgVrPhysics::Vector3 const *halfExtentsMeters);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_UpdateGrabPose(uint64_t networkId, SwgVrPhysics::Matrix4x4 const *objectFromWorld);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_EndGrab(uint64_t networkId, SwgVrPhysics::ReleasedObjectState *releasedState);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_GetGrabbedObjectState(uint64_t networkId, SwgVrPhysics::GrabbedObjectState *state);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_IsHandGrabbing(uint32_t hand);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_PublishReleasedObject(SwgVrPhysics::ReleasedObjectState const *state);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_TryConsumeReleasedObject(SwgVrPhysics::ReleasedObjectState *state);

VRPHYSICSBRIDGE_API bool SWGVRPhysics_PublishSimulatedObjectState(SwgVrPhysics::GrabbedObjectState const *state);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_ClearSimulatedObject(uint64_t networkId);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_SetPhysicsActive(uint64_t networkId, bool active);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_PublishFinalPlacement(SwgVrPhysics::FinalPlacementState const *state);
VRPHYSICSBRIDGE_API bool SWGVRPhysics_TryConsumeFinalPlacement(SwgVrPhysics::FinalPlacementState *state);

#endif
