// ======================================================================
//
// VrDetachedHands.cpp
//
// Safe first-person VR hands: a detached hand-only skeletal rig driven by
// the same OpenXR hand poses that already drive the working wand path.
//
// ======================================================================

#include "clientGame/FirstClientGame.h"
#include "clientGame/VrDetachedHands.h"

#if defined(SWG_VR_PHYSICS_CLIENTOBJECT_HOOK)

#include "clientGame/ClientWorld.h"
#include "clientGame/ClientGameAppearanceEvents.h"
#include "clientGame/CreatureObject.h"
#include "clientGame/Game.h"
#include "clientGame/WeaponObject.h"
#include "clientGraphics/Camera.h"
#include "clientGraphics/RenderWorld.h"
#include "clientSkeletalAnimation/BasicMeshGeneratorTemplate.h"
#include "clientSkeletalAnimation/MeshGeneratorTemplate.h"
#include "clientSkeletalAnimation/MeshGeneratorTemplateList.h"
#include "clientSkeletalAnimation/SkeletalAppearance2.h"
#include "clientSkeletalAnimation/SkeletalAppearanceTemplate.h"
#include "clientSkeletalAnimation/SkeletalMeshGeneratorTemplate.h"
#include "clientSkeletalAnimation/Skeleton.h"
#include "clientSkeletalAnimation/TransformModifier.h"
#include "sharedObject/Appearance.h"
#include "sharedObject/AppearanceTemplate.h"
#include "sharedObject/CustomizationData.h"
#include "sharedObject/CustomizationDataProperty.h"
#include "sharedFile/TreeFile.h"
#include "sharedFoundation/TemporaryCrcString.h"
#include "sharedGame/SharedCreatureObjectTemplate.h"
#include "sharedMath/Transform.h"
#include "sharedMath/Vector.h"
#include "sharedObject/AppearanceTemplateList.h"
#include "sharedObject/CellProperty.h"
#include "sharedObject/MemoryBlockManagedObject.h"
#include "sharedObject/Object.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define VRPHYSICSBRIDGE_NO_IMPORT
#include "../../../../../application/Direct3d11/src/win32/VRPhysicsBridge.h"
#undef VRPHYSICSBRIDGE_NO_IMPORT

// ======================================================================

namespace
{
	typedef uint32_t (*BridgeAbiVersionFunction)();
	typedef bool (*GetHandStateFunction)(uint32_t, SwgVrPhysics::HandState *);
	typedef void (*SetSceneHandsActiveFunction)(bool);

	struct BridgeApi
	{
		BridgeApi() : lastResolveAttemptMilliseconds(0), abiVersion(0), getHandState(0), setSceneHandsActive(0) {}
		DWORD lastResolveAttemptMilliseconds;
		BridgeAbiVersionFunction abiVersion;
		GetHandStateFunction getHandState;
		SetSceneHandsActiveFunction setSceneHandsActive;
	};

	struct RuntimeHandPose
	{
		RuntimeHandPose() : valid(false), aimValid(false), gripValue(0.0f), sampleTimeSeconds(0.0), handFromWorld(), aimFromWorld() {}
		bool valid;
		bool aimValid;
		float gripValue;
		double sampleTimeSeconds;
		Transform handFromWorld;
		Transform aimFromWorld;
	};

	enum CurlRole
	{
		CR_thumb = 0,
		CR_finger = 1
	};

	enum TrackedHandRole
	{
		THR_terminal = 0,
		THR_collapse = 1
	};

	RuntimeHandPose s_handPose[SwgVrPhysics::Hand_Count];
	Object *s_handRigObject = 0;
	SkeletalAppearance2 *s_handRigAppearance = 0;
	Object *s_weaponObject = 0;
	char s_handRigMeshPath[128] = "";
	char s_weaponAppearanceTemplateName[256] = "";
	DWORD s_lastTraceMilliseconds = 0;
	DWORD s_lastSolveTraceMilliseconds[SwgVrPhysics::Hand_Count] = {0, 0};
	DWORD s_lastPoseCadenceTraceMilliseconds[SwgVrPhysics::Hand_Count] = {0, 0};
	double s_lastTracedSampleTimeSeconds[SwgVrPhysics::Hand_Count] = {0.0, 0.0};
	bool s_visualTwistLogged[SwgVrPhysics::Hand_Count] = {false, false};
	bool s_bridgeReadyLogged = false;
	bool s_rigCreatedLogged = false;
	bool s_handRigModifiersAttached = false;
	bool s_validPoseLogged[SwgVrPhysics::Hand_Count] = {false, false};

	bool environmentEnabled(char const *name)
	{
		char value[32];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return false;

		return _stricmp(value, "1") == 0 || _stricmp(value, "true") == 0 || _stricmp(value, "yes") == 0 || _stricmp(value, "on") == 0;
	}

	bool environmentEnabledDefault(char const *name, bool defaultValue)
	{
		char value[32];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return defaultValue;

		return _stricmp(value, "1") == 0 || _stricmp(value, "true") == 0 || _stricmp(value, "yes") == 0 || _stricmp(value, "on") == 0;
	}

	float getEnvironmentFloat(char const *name, float defaultValue)
	{
		char value[64];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return defaultValue;

		return static_cast<float>(atof(value));
	}

	bool detachedHandsEnabled()
	{
		if (!environmentEnabled("SWG_OG_VR") && !environmentEnabled("SWG_D3D11_VR"))
			return false;

		return environmentEnabledDefault("SWG_OG_VR_DETACHED_HANDS", true);
	}

	bool detachedArmCollapseEnabled()
	{
		return environmentEnabledDefault("SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE", false);
	}

	bool detachedUpperArmCollapseEnabled()
	{
		return environmentEnabledDefault("SWG_OG_VR_DETACHED_HANDS_UPPER_ARM_COLLAPSE", false);
	}

	bool traceEnabled()
	{
		return environmentEnabled("SWG_OG_VR_HAND_TRACE") || environmentEnabled("SWG_OG_VR_PHYSICS_TRACE");
	}

	bool requireTrackedHandPose()
	{
		return environmentEnabledDefault("SWG_OG_VR_DETACHED_HANDS_REQUIRE_TRACKED", true);
	}

	bool useAimPoseForHandTarget(SwgVrPhysics::HandState const &state)
	{
		return environmentEnabledDefault("SWG_OG_VR_DETACHED_HANDS_USE_AIM_POSE", false) &&
			(state.flags & SwgVrPhysics::MatrixFlagAimValid) != 0;
	}

	void traceLine(char const *line)
	{
		if (!line || !traceEnabled())
			return;

		OutputDebugStringA(line);
		OutputDebugStringA("\n");

		char path[MAX_PATH];
		DWORD length = GetEnvironmentVariableA("SWG_OG_VR_HAND_TRACE_FILE", path, sizeof(path));
		if (length == 0 || length >= sizeof(path))
			length = GetEnvironmentVariableA("SWG_OG_VR_PHYSICS_TRACE_FILE", path, sizeof(path));
		if (length == 0 || length >= sizeof(path))
			return;

		HANDLE const file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
		if (file == INVALID_HANDLE_VALUE)
			return;

		DWORD written = 0;
		DWORD const lineLength = static_cast<DWORD>(strlen(line));
		(void)WriteFile(file, line, lineLength, &written, 0);
		(void)WriteFile(file, "\r\n", 2, &written, 0);
		CloseHandle(file);
	}

	void traceRateLimited(char const *line)
	{
		DWORD const now = GetTickCount();
		if (now - s_lastTraceMilliseconds < 500)
			return;

		s_lastTraceMilliseconds = now;
		traceLine(line);
	}

	FARPROC findBridgeExport(char const *name)
	{
		static char const * const rendererModules[] =
		{
			"gl05_r.dll",
			"gl05_o.dll",
			"gl05_d.dll",
			"Direct3d11.dll"
		};

		for (size_t i = 0; i != sizeof(rendererModules) / sizeof(rendererModules[0]); ++i)
		{
			HMODULE const module = GetModuleHandleA(rendererModules[i]);
			if (module)
			{
				FARPROC const proc = GetProcAddress(module, name);
				if (proc)
					return proc;
			}
		}

		return 0;
	}

	BridgeApi const &getBridgeApi()
	{
		static BridgeApi api;
		if (!api.getHandState)
		{
			DWORD const now = GetTickCount();
			if (api.lastResolveAttemptMilliseconds != 0 && now - api.lastResolveAttemptMilliseconds < 1000)
				return api;

			api.lastResolveAttemptMilliseconds = now;
			api.abiVersion = reinterpret_cast<BridgeAbiVersionFunction>(findBridgeExport("SWGVRPhysics_BridgeAbiVersion"));
			api.getHandState = reinterpret_cast<GetHandStateFunction>(findBridgeExport("SWGVRPhysics_GetHandState"));
			api.setSceneHandsActive = reinterpret_cast<SetSceneHandsActiveFunction>(findBridgeExport("SWGVRPhysics_SetSceneHandsActive"));

			if (!api.abiVersion || api.abiVersion() != SwgVrPhysics::BridgeAbiVersion)
				api.getHandState = 0;
		}

		return api;
	}

	void publishSceneHandsActive(bool active)
	{
		BridgeApi const &api = getBridgeApi();
		if (api.setSceneHandsActive)
			api.setSceneHandsActive(active);
	}

	Transform toSwgTransform(SwgVrPhysics::Matrix4x4 const &matrix)
	{
		Transform transform;
		transform.setLocalFrameIJK_p(
			Vector(matrix.m[0], matrix.m[4], matrix.m[8]),
			Vector(matrix.m[1], matrix.m[5], matrix.m[9]),
			Vector(matrix.m[2], matrix.m[6], matrix.m[10]));
		transform.setPosition_p(Vector(matrix.m[3], matrix.m[7], matrix.m[11]));
		transform.reorthonormalize();
		return transform;
	}

	Vector vrLocalVectorToCameraLocal(Vector const &vector)
	{
		return Vector(vector.x, vector.y, -vector.z);
	}

	Transform vrLocalToSwgWorldTransform(Transform const &vrLocal)
	{
		Camera const * const camera = Game::getConstCamera();
		if (!camera)
			return vrLocal;

		Transform const &cameraToWorld = camera->getTransform_o2w();
		Transform result;
		result.setLocalFrameIJK_p(
			cameraToWorld.rotate_l2p(vrLocalVectorToCameraLocal(vrLocal.getLocalFrameI_p())),
			cameraToWorld.rotate_l2p(vrLocalVectorToCameraLocal(vrLocal.getLocalFrameJ_p())),
			cameraToWorld.rotate_l2p(vrLocalVectorToCameraLocal(vrLocal.getLocalFrameK_p())));
		result.setPosition_p(cameraToWorld.rotateTranslate_l2p(vrLocalVectorToCameraLocal(vrLocal.getPosition_p())));
		result.reorthonormalize();
		return result;
	}

	void applyHandTargetOffset(Transform &handFromWorld, uint32_t hand);

	Transform getTrackedHandTarget(SwgVrPhysics::HandState const &state, uint32_t hand)
	{
		bool const useAimPose = useAimPoseForHandTarget(state);
		Transform result = toSwgTransform(useAimPose ? state.aimFromWorld : state.gripFromWorld);
		result.reorthonormalize();
		Transform handFromWorld = vrLocalToSwgWorldTransform(result);
		applyHandTargetOffset(handFromWorld, hand);
		return handFromWorld;
	}

	void updateRuntimeHandPose(uint32_t hand, BridgeApi const &api)
	{
		s_handPose[hand].valid = false;
		s_handPose[hand].aimValid = false;
		if (!api.getHandState)
			return;

		SwgVrPhysics::HandState state;
		ZeroMemory(&state, sizeof(state));
		if (!api.getHandState(hand, &state))
		{
			char line[128];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands hand=%u getHandState returned false", hand);
			line[sizeof(line) - 1] = '\0';
			traceRateLimited(line);
			return;
		}

		bool const useAimPose = useAimPoseForHandTarget(state);
		uint32_t requiredFlags = useAimPose ? SwgVrPhysics::MatrixFlagAimValid : SwgVrPhysics::MatrixFlagGripValid;
		if (requireTrackedHandPose())
			requiredFlags |= useAimPose ? SwgVrPhysics::MatrixFlagAimTracked : SwgVrPhysics::MatrixFlagGripTracked;
		if ((state.flags & requiredFlags) != requiredFlags)
		{
			char line[160];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands hand=%u waiting for %s %spose flags=0x%08x required=0x%08x",
				hand,
				requireTrackedHandPose() ? "tracked " : "",
				useAimPose ? "aim " : "grip ",
				state.flags,
				requiredFlags);
			line[sizeof(line) - 1] = '\0';
			traceRateLimited(line);
			return;
		}

		s_handPose[hand].valid = true;
		s_handPose[hand].gripValue = state.gripValue;
		s_handPose[hand].sampleTimeSeconds = state.sampleTimeSeconds;
		s_handPose[hand].handFromWorld = getTrackedHandTarget(state, hand);
		if ((state.flags & SwgVrPhysics::MatrixFlagAimValid) != 0)
		{
			Transform aimTransform = toSwgTransform(state.aimFromWorld);
			aimTransform.reorthonormalize();
			s_handPose[hand].aimFromWorld = vrLocalToSwgWorldTransform(aimTransform);
			s_handPose[hand].aimValid = true;
		}
		if (traceEnabled())
		{
			DWORD const now = GetTickCount();
			if (now - s_lastPoseCadenceTraceMilliseconds[hand] >= 500)
			{
				double const previousSample = s_lastTracedSampleTimeSeconds[hand];
				double const sampleDeltaMs = previousSample > 0.0 ? (state.sampleTimeSeconds - previousSample) * 1000.0 : 0.0;
				s_lastTracedSampleTimeSeconds[hand] = state.sampleTimeSeconds;
				s_lastPoseCadenceTraceMilliseconds[hand] = now;
				Vector const position = s_handPose[hand].handFromWorld.getPosition_p();
				char line[384];
				_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands poseCadence hand=%u sample=%.6f deltaMs=%.3f grip=%.3f target_w=(%.4f,%.4f,%.4f)",
					hand,
					state.sampleTimeSeconds,
					sampleDeltaMs,
					state.gripValue,
					position.x,
					position.y,
					position.z);
				line[sizeof(line) - 1] = '\0';
				traceLine(line);
			}
		}
		if (!s_validPoseLogged[hand])
		{
			char line[192];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands hand=%u valid hand pose flags=0x%08x targetPose=%s requireTracked=%s",
				hand,
				state.flags,
				useAimPose ? "aim" : "grip",
				requireTrackedHandPose() ? "true" : "false");
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			s_validPoseLogged[hand] = true;
		}
	}

	bool skeletonHasTransform(SkeletalAppearance2 const &appearance, char const *name)
	{
		if (!name)
			return false;

		Skeleton const * const skeleton = appearance.getDisplayLodSkeleton();
		if (!skeleton)
			return false;

		bool found = false;
		int index = -1;
		skeleton->findTransformIndex(TemporaryCrcString(name, true), &index, &found);
		return found;
	}

	char const * const s_leftHandCandidates[] = {"lwrist", "l_wrist", "wrist_l", "l_hand", "lhand", "left_hand", "hand_l", "lh", "Bip01 L Hand", "bip01 l hand", "hold_l", "hp_hold_l"};
	char const * const s_rightHandCandidates[] = {"rwrist", "r_wrist", "wrist_r", "r_hand", "rhand", "right_hand", "hand_r", "rh", "Bip01 R Hand", "bip01 r hand", "hold_r", "hp_hold_r"};
	char const * const s_leftTrackedPointCandidates[] = {"lwrist", "l_wrist", "wrist_l", "l_hand", "lhand", "left_hand", "hand_l", "lh", "Bip01 L Hand", "bip01 l hand", "hold_l", "hp_hold_l"};
	char const * const s_rightTrackedPointCandidates[] = {"rwrist", "r_wrist", "wrist_r", "r_hand", "rhand", "right_hand", "hand_r", "rh", "Bip01 R Hand", "bip01 r hand", "hold_r", "hp_hold_r"};
	char const * const s_leftCollapseCandidates[] = {"l_forearm", "LeftForeArm", "lforearm"};
	char const * const s_rightCollapseCandidates[] = {"r_forearm", "RightForeArm", "rforearm", "rulna"};
	char const * const s_leftUpperArmCollapseCandidates[] = {"larm", "l_upperarm", "LeftArm"};
	char const * const s_rightUpperArmCollapseCandidates[] = {"rarm", "r_upperarm", "RightArm", "rclav"};

	float getHandVisualTwistDegrees(uint32_t hand, char const *axis)
	{
		char name[96];
		_snprintf(name, sizeof(name) - 1, "SWG_OG_VR_DETACHED_HANDS_%s_%s_DEGREES", hand == SwgVrPhysics::Hand_Left ? "LEFT" : "RIGHT", axis);
		name[sizeof(name) - 1] = '\0';
		return getEnvironmentFloat(name, 0.0f);
	}

	float getHandTargetOffsetMeters(uint32_t hand, char const *axis, float defaultValue)
	{
		char name[96];
		_snprintf(name, sizeof(name) - 1, "SWG_OG_VR_DETACHED_HANDS_%s_METERS", axis);
		name[sizeof(name) - 1] = '\0';
		float const globalValue = getEnvironmentFloat(name, defaultValue);

		_snprintf(name, sizeof(name) - 1, "SWG_OG_VR_DETACHED_HANDS_%s_%s_METERS", hand == SwgVrPhysics::Hand_Left ? "LEFT" : "RIGHT", axis);
		name[sizeof(name) - 1] = '\0';
		return getEnvironmentFloat(name, globalValue);
	}

	void applyHandTargetOffset(Transform &handFromWorld, uint32_t hand)
	{
		float const knuckleBackMeters = getHandTargetOffsetMeters(hand, "KNUCKLE_BACK", 0.0f);
		float const knuckleDownMeters = getHandTargetOffsetMeters(hand, "KNUCKLE_DOWN", 0.0f);
		float const knuckleOutMeters = getHandTargetOffsetMeters(hand, "KNUCKLE_OUT", 0.0f);

		if (knuckleBackMeters == 0.0f && knuckleDownMeters == 0.0f && knuckleOutMeters == 0.0f)
			return;

		Vector const offset_w =
			-handFromWorld.getLocalFrameK_p() * knuckleBackMeters +
			-handFromWorld.getLocalFrameJ_p() * knuckleDownMeters +
			handFromWorld.getLocalFrameI_p() * (hand == SwgVrPhysics::Hand_Left ? -knuckleOutMeters : knuckleOutMeters);
		handFromWorld.setPosition_p(handFromWorld.getPosition_p() + offset_w);
	}

	void applyHandVisualTwist(Transform &tracked_l2o, uint32_t hand)
	{
		float const yawDegrees = getHandVisualTwistDegrees(hand, "YAW");
		float const pitchDegrees = getHandVisualTwistDegrees(hand, "PITCH");
		float const rollDegrees = getHandVisualTwistDegrees(hand, "ROLL");

		if (yawDegrees != 0.0f)
			tracked_l2o.yaw_l(yawDegrees * PI_OVER_180);
		if (pitchDegrees != 0.0f)
			tracked_l2o.pitch_l(pitchDegrees * PI_OVER_180);
		if (rollDegrees != 0.0f)
			tracked_l2o.roll_l(rollDegrees * PI_OVER_180);

		if (yawDegrees != 0.0f || pitchDegrees != 0.0f || rollDegrees != 0.0f)
			tracked_l2o.reorthonormalize();

		if (traceEnabled() && !s_visualTwistLogged[hand])
		{
			char line[192];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands visualTwist hand=%u yaw=%.2f pitch=%.2f roll=%.2f", hand, yawDegrees, pitchDegrees, rollDegrees);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			s_visualTwistLogged[hand] = true;
		}
	}

	void applyArmCollapseTuck(Transform &collapse_l2o, uint32_t hand)
	{
		float const backMeters = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE_TUCK_BACK_METERS", 0.0f);
		float const downMeters = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE_TUCK_DOWN_METERS", 0.0f);
		float const inMeters = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE_TUCK_IN_METERS", 0.0f);

		if (backMeters == 0.0f && downMeters == 0.0f && inMeters == 0.0f)
			return;

		Vector const offset =
			-collapse_l2o.getLocalFrameK_p() * backMeters +
			-collapse_l2o.getLocalFrameJ_p() * downMeters +
			collapse_l2o.getLocalFrameI_p() * (hand == SwgVrPhysics::Hand_Left ? inMeters : -inMeters);

		collapse_l2o.setPosition_p(collapse_l2o.getPosition_p() + offset);
	}

	bool skeletonHasAnyTransform(SkeletalAppearance2 const &appearance, char const * const *names, size_t nameCount)
	{
		for (size_t i = 0; i != nameCount; ++i)
			if (skeletonHasTransform(appearance, names[i]))
				return true;

		return false;
	}

	void traceSkeletonTransforms(SkeletalAppearance2 const &appearance, char const *reason)
	{
		if (!traceEnabled())
			return;

		Skeleton const * const skeleton = appearance.getDisplayLodSkeleton();
		if (!skeleton)
		{
			traceLine("SWGVRDetachedHands skeleton trace skipped: no display skeleton");
			return;
		}

		char line[256];
		_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands skeleton trace reason=%s transformCount=%d", reason ? reason : "<null>", skeleton->getTransformCount());
		line[sizeof(line) - 1] = '\0';
		traceLine(line);

		for (int i = 0; i != skeleton->getTransformCount(); ++i)
		{
			char const * const name = skeleton->getTransformName(i).getString();
			if (!name || !*name)
				continue;

			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands skeleton joint[%03d]=%s", i, name);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
		}
	}

	bool findFirstExistingTransform(SkeletalAppearance2 const &appearance, char const * const *names, size_t nameCount, char const *&name)
	{
		for (size_t i = 0; i != nameCount; ++i)
		{
			if (skeletonHasTransform(appearance, names[i]))
			{
				name = names[i];
				return true;
			}
		}

		name = 0;
		return false;
	}

	bool computeBindRelativeTransform(SkeletalAppearance2 const &appearance, char const *anchorName, char const *trackedName, Transform &anchorToTracked)
	{
		Skeleton const * const skeleton = appearance.getDisplayLodSkeleton();
		if (!skeleton || !anchorName || !trackedName)
			return false;

		bool anchorFound = false;
		bool trackedFound = false;
		int anchorIndex = -1;
		int trackedIndex = -1;
		skeleton->findTransformIndex(TemporaryCrcString(anchorName, true), &anchorIndex, &anchorFound);
		skeleton->findTransformIndex(TemporaryCrcString(trackedName, true), &trackedIndex, &trackedFound);
		if (!anchorFound || !trackedFound)
			return false;

		Transform const * const transforms = skeleton->getJointToRootTransformArray();
		if (!transforms)
			return false;

		Transform rootToAnchor;
		rootToAnchor.invert(transforms[anchorIndex]);
		anchorToTracked.multiply(rootToAnchor, transforms[trackedIndex]);
		anchorToTracked.resetRotate_l2p();
		return anchorToTracked.validate(true);
	}

	class VrTrackedHandTransformModifier : public TransformModifier
	{
	public:
		VrTrackedHandTransformModifier(uint32_t hand, TrackedHandRole role) :
			TransformModifier(),
			m_hand(hand),
			m_role(role),
			m_anchorToTracked(Transform::identity),
			m_trackedToAnchor(Transform::identity),
			m_hasAnchorToTracked(false)
		{
		}

		VrTrackedHandTransformModifier(uint32_t hand, TrackedHandRole role, Transform const &anchorToTracked) :
			TransformModifier(),
			m_hand(hand),
			m_role(role),
			m_anchorToTracked(anchorToTracked),
			m_trackedToAnchor(Transform::identity),
			m_hasAnchorToTracked(true)
		{
			m_trackedToAnchor.invert(m_anchorToTracked);
		}

		virtual bool modifyTransform(float, Skeleton const &skeleton, CrcString const &, Transform const &, Transform const &, Transform &transform_l2o)
		{
			if (m_hand >= SwgVrPhysics::Hand_Count || !s_handPose[m_hand].valid)
				return false;

			Object const * const owner = skeleton.getSkeletalAppearance().getOwner();
			if (!owner || owner != s_handRigObject)
				return false;

			Transform tracked_l2o = owner->getTransform_o2w().rotateTranslate_p2l(s_handPose[m_hand].handFromWorld);
			applyHandVisualTwist(tracked_l2o, m_hand);

			if (m_role == THR_collapse && environmentEnabledDefault("SWG_OG_VR_DETACHED_HANDS_ARM_COLLAPSE_EXACT", false))
				transform_l2o = tracked_l2o;
			else if (m_role == THR_collapse && m_hasAnchorToTracked)
				transform_l2o.multiply(tracked_l2o, m_trackedToAnchor);
			else
				transform_l2o = tracked_l2o;

			if (m_role == THR_collapse)
				applyArmCollapseTuck(transform_l2o, m_hand);

			traceSolvedAnchor(owner, tracked_l2o, transform_l2o);
			return transform_l2o.validate(true);
		}

	private:
		void traceSolvedAnchor(Object const *owner, Transform const &tracked_l2o, Transform const &anchor_l2o) const
		{
			if (!traceEnabled() || !owner || m_role != THR_collapse || !m_hasAnchorToTracked)
				return;

			DWORD const now = GetTickCount();
			if (now - s_lastSolveTraceMilliseconds[m_hand] < 500)
				return;

			s_lastSolveTraceMilliseconds[m_hand] = now;

			Vector const target_o = tracked_l2o.getPosition_p();
			Vector const solved_o = anchor_l2o.rotateTranslate_l2p(m_anchorToTracked.getPosition_p());
			Vector const delta_o = solved_o - target_o;

			Transform const &objectToWorld = owner->getTransform_o2w();
			Vector const target_w = objectToWorld.rotateTranslate_l2p(target_o);
			Vector const solved_w = objectToWorld.rotateTranslate_l2p(solved_o);
			Vector const delta_w = solved_w - target_w;

			char line[512];
			_snprintf(line, sizeof(line) - 1,
				"SWGVRDetachedHands solveProof hand=%u anchor=hold target_o=(%.4f,%.4f,%.4f) solved_o=(%.4f,%.4f,%.4f) delta_o=(%.5f,%.5f,%.5f) deltaMag=%.6f target_w=(%.4f,%.4f,%.4f) solved_w=(%.4f,%.4f,%.4f) delta_w=(%.5f,%.5f,%.5f)",
				m_hand,
				target_o.x, target_o.y, target_o.z,
				solved_o.x, solved_o.y, solved_o.z,
				delta_o.x, delta_o.y, delta_o.z,
				delta_o.magnitude(),
				target_w.x, target_w.y, target_w.z,
				solved_w.x, solved_w.y, solved_w.z,
				delta_w.x, delta_w.y, delta_w.z);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
		}

		uint32_t m_hand;
		TrackedHandRole m_role;
		Transform m_anchorToTracked;
		Transform m_trackedToAnchor;
		bool m_hasAnchorToTracked;
		VrTrackedHandTransformModifier();
		VrTrackedHandTransformModifier(VrTrackedHandTransformModifier const &);
		VrTrackedHandTransformModifier &operator =(VrTrackedHandTransformModifier const &);
	};

	class VrFingerCurlTransformModifier : public TransformModifier
	{
	public:
		VrFingerCurlTransformModifier(uint32_t hand, CurlRole role, float curlScale) : TransformModifier(), m_hand(hand), m_role(role), m_curlScale(curlScale) {}

		virtual bool modifyTransform(float, Skeleton const &, CrcString const &, Transform const &transform_p2o, Transform const &transform_l2p, Transform &transform_l2o)
		{
			if (m_hand >= SwgVrPhysics::Hand_Count || !s_handPose[m_hand].valid)
				return false;

			float grip = s_handPose[m_hand].gripValue;
			if (grip < 0.0f)
				grip = 0.0f;
			if (grip > 1.0f)
				grip = 1.0f;

			float const maxFingerDegrees = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_FINGER_CURL_DEGREES", 55.0f);
			float const maxThumbDegrees = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_THUMB_CURL_DEGREES", 35.0f);
			float const maxDegrees = m_role == CR_thumb ? maxThumbDegrees : maxFingerDegrees;
			float const direction = m_hand == SwgVrPhysics::Hand_Left ? -1.0f : 1.0f;
			float const angle = direction * m_curlScale * maxDegrees * grip * PI_OVER_180;

			Transform curled_l2p(transform_l2p);
			switch (static_cast<int>(getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_CURL_AXIS", 0.0f)))
			{
			case 1:
				curled_l2p.yaw_l(angle);
				break;
			case 2:
				curled_l2p.roll_l(angle);
				break;
			case 0:
			default:
				curled_l2p.pitch_l(angle);
				break;
			}

			transform_l2o.multiply(transform_p2o, curled_l2p);
			return transform_l2o.validate(true);
		}

	private:
		uint32_t m_hand;
		CurlRole m_role;
		float m_curlScale;
		VrFingerCurlTransformModifier();
		VrFingerCurlTransformModifier(VrFingerCurlTransformModifier const &);
		VrFingerCurlTransformModifier &operator =(VrFingerCurlTransformModifier const &);
	};

	bool attachTrackedModifierIfPresent(SkeletalAppearance2 &appearance, uint32_t hand, TrackedHandRole role, char const *name)
	{
		if (!skeletonHasTransform(appearance, name))
			return false;

		appearance.addTransformModifierTakeOwnership(TemporaryCrcString(name, true), new VrTrackedHandTransformModifier(hand, role));
		char line[256];
		_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands attached %s hand=%u joint=%s", role == THR_terminal ? "hand" : "collapse", hand, name);
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
		return true;
	}

	bool attachFirstAvailableTrackedHandModifier(SkeletalAppearance2 &appearance, uint32_t hand, char const * const *names, size_t nameCount)
	{
		for (size_t i = 0; i != nameCount; ++i)
		{
			if (attachTrackedModifierIfPresent(appearance, hand, THR_terminal, names[i]))
				return true;
		}

		char line[256];
		_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands missing hand=%u no supported hand/wrist joint found", hand);
		line[sizeof(line) - 1] = '\0';
		traceRateLimited(line);
		return false;
	}

	int attachTrackedCollapseModifiers(SkeletalAppearance2 &appearance, uint32_t hand, char const * const *names, size_t nameCount)
	{
		char const * const *handNames = hand == SwgVrPhysics::Hand_Left ? s_leftTrackedPointCandidates : s_rightTrackedPointCandidates;
		size_t const handNameCount = hand == SwgVrPhysics::Hand_Left ? sizeof(s_leftTrackedPointCandidates) / sizeof(s_leftTrackedPointCandidates[0]) : sizeof(s_rightTrackedPointCandidates) / sizeof(s_rightTrackedPointCandidates[0]);
		char const *trackedName = 0;
		if (!findFirstExistingTransform(appearance, handNames, handNameCount, trackedName))
			return 0;

		int attachedCount = 0;
		for (size_t i = 0; i != nameCount; ++i)
		{
			if (!skeletonHasTransform(appearance, names[i]))
				continue;

			Transform anchorToTracked;
			if (!computeBindRelativeTransform(appearance, names[i], trackedName, anchorToTracked))
				continue;

			appearance.addTransformModifierTakeOwnership(TemporaryCrcString(names[i], true), new VrTrackedHandTransformModifier(hand, THR_collapse, anchorToTracked));
			char line[384];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands attached anchor hand=%u joint=%s tracked=%s bindMode=positionOnly bindOffset=(%.3f,%.3f,%.3f)", hand, names[i], trackedName, anchorToTracked.getPosition_p().x, anchorToTracked.getPosition_p().y, anchorToTracked.getPosition_p().z);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			++attachedCount;
		}

		return attachedCount;
	}

	bool attachFingerModifierIfPresent(SkeletalAppearance2 &appearance, uint32_t hand, CurlRole role, char const *name, float curlScale)
	{
		if (name && skeletonHasTransform(appearance, name))
		{
			appearance.addTransformModifierTakeOwnership(TemporaryCrcString(name, true), new VrFingerCurlTransformModifier(hand, role, curlScale));
			char line[256];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands attached finger hand=%u joint=%s role=%d", hand, name, static_cast<int>(role));
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			return true;
		}

		return false;
	}

	int attachFingerCandidates(SkeletalAppearance2 &appearance)
	{
		struct Candidate { uint32_t hand; CurlRole role; char const *name; float curlScale; };
		static Candidate const candidates[] =
		{
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "lthumb1", 0.55f},
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "lthumb01", 0.55f},
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "l_thumb01", 0.55f},
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "lthumb2", 0.80f},
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "lthumb02", 0.80f},
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "l_thumb02", 0.80f},
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "bip01 l finger0", 0.50f},
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "bip01 l finger01", 0.75f},
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "Bip01 L Finger0", 0.50f},
			{SwgVrPhysics::Hand_Left,  CR_thumb,  "Bip01 L Finger01", 0.75f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lindex1", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lindex01", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "l_index01", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lindex2", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lindex02", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "l_index02", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "bip01 l finger1", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "bip01 l finger2", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "Bip01 L Finger1", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "Bip01 L Finger2", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lmiddle1", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lmiddle01", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "l_middle01", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lmiddle2", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lmiddle02", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "l_middle02", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lring1", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lring01", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "l_ring01", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lring2", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lring02", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "l_ring02", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lpinky1", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lpinky01", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "l_pinky01", 0.65f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lpinky2", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "lpinky02", 1.00f},
			{SwgVrPhysics::Hand_Left,  CR_finger, "l_pinky02", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "rthumb1", 0.55f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "rthumb01", 0.55f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "r_thumb01", 0.55f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "rthumb2", 0.80f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "rthumb02", 0.80f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "r_thumb02", 0.80f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "bip01 r finger0", 0.50f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "bip01 r finger01", 0.75f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "Bip01 R Finger0", 0.50f},
			{SwgVrPhysics::Hand_Right, CR_thumb,  "Bip01 R Finger01", 0.75f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rindex1", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rindex01", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "r_index01", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rindex2", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rindex02", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "r_index02", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "bip01 r finger1", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "bip01 r finger2", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "Bip01 R Finger1", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "Bip01 R Finger2", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rmiddle1", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rmiddle01", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "r_middle01", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rmiddle2", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rmiddle02", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "r_middle02", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rring1", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rring01", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "r_ring01", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rring2", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rring02", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "r_ring02", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rpinky1", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rpinky01", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "r_pinky01", 0.65f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rpinky2", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "rpinky02", 1.00f},
			{SwgVrPhysics::Hand_Right, CR_finger, "r_pinky02", 1.00f}
		};

		int attachedCount = 0;
		for (size_t i = 0; i != sizeof(candidates) / sizeof(candidates[0]); ++i)
		{
			if (attachFingerModifierIfPresent(appearance, candidates[i].hand, candidates[i].role, candidates[i].name, candidates[i].curlScale))
				++attachedCount;
		}

		return attachedCount;
	}

	bool attachHandModifiers(SkeletalAppearance2 &appearance)
	{
		IGNORE_RETURN(appearance.rebuildIfDirtyAndAvailable());
		traceSkeletonTransforms(appearance, "attach-hand-modifiers");

		char const *leftHandName = 0;
		char const *rightHandName = 0;
		bool const leftHandFound = findFirstExistingTransform(appearance, s_leftHandCandidates, sizeof(s_leftHandCandidates) / sizeof(s_leftHandCandidates[0]), leftHandName);
		bool const rightHandFound = findFirstExistingTransform(appearance, s_rightHandCandidates, sizeof(s_rightHandCandidates) / sizeof(s_rightHandCandidates[0]), rightHandName);
		if (!leftHandFound || !rightHandFound)
		{
			char line[256];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands waiting for both wrist anchors left=%s right=%s", leftHandFound ? leftHandName : "missing", rightHandFound ? rightHandName : "missing");
			line[sizeof(line) - 1] = '\0';
			traceRateLimited(line);
			return false;
		}

		int leftCollapseCount = 0;
		int rightCollapseCount = 0;
		if (detachedArmCollapseEnabled())
		{
			leftCollapseCount = attachTrackedCollapseModifiers(appearance, SwgVrPhysics::Hand_Left, s_leftCollapseCandidates, sizeof(s_leftCollapseCandidates) / sizeof(s_leftCollapseCandidates[0]));
			rightCollapseCount = attachTrackedCollapseModifiers(appearance, SwgVrPhysics::Hand_Right, s_rightCollapseCandidates, sizeof(s_rightCollapseCandidates) / sizeof(s_rightCollapseCandidates[0]));
			if (detachedUpperArmCollapseEnabled())
			{
				leftCollapseCount += attachTrackedCollapseModifiers(appearance, SwgVrPhysics::Hand_Left, s_leftUpperArmCollapseCandidates, sizeof(s_leftUpperArmCollapseCandidates) / sizeof(s_leftUpperArmCollapseCandidates[0]));
				rightCollapseCount += attachTrackedCollapseModifiers(appearance, SwgVrPhysics::Hand_Right, s_rightUpperArmCollapseCandidates, sizeof(s_rightUpperArmCollapseCandidates) / sizeof(s_rightUpperArmCollapseCandidates[0]));
			}
		}

		bool const leftAttached = attachTrackedModifierIfPresent(appearance, SwgVrPhysics::Hand_Left, THR_terminal, leftHandName);
		bool const rightAttached = attachTrackedModifierIfPresent(appearance, SwgVrPhysics::Hand_Right, THR_terminal, rightHandName);
		int fingerCurlCount = 0;
		if (leftAttached || rightAttached)
			fingerCurlCount = attachFingerCandidates(appearance);

		char line[256];
		_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands collapse enabled=%s counts left=%d right=%d fingerCurlCount=%d", detachedArmCollapseEnabled() ? "true" : "false", leftCollapseCount, rightCollapseCount, fingerCurlCount);
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
		return leftAttached || rightAttached;
	}

	void destroyWeaponProxy()
	{
		if (s_weaponObject)
		{
			s_weaponObject->kill();
			s_weaponObject->scheduleForAlter();
		}

		s_weaponObject = 0;
		s_weaponAppearanceTemplateName[0] = '\0';
	}

	void copyCustomizationDataIfPresent(Object const &source, Appearance &destinationAppearance)
	{
		CustomizationDataProperty * const property = safe_cast<CustomizationDataProperty *>(const_cast<Object &>(source).getProperty(CustomizationDataProperty::getClassPropertyId()));
		if (!property)
			return;

		CustomizationData * const customizationData = property->fetchCustomizationData();
		if (!customizationData)
			return;

		destinationAppearance.setCustomizationData(customizationData);
		customizationData->release();
	}

	CellProperty *getHandRigCell(CreatureObject const &creature);
	Transform const &getHandRigTransform(CreatureObject const &creature);

	Transform getRightHandVisualTransform()
	{
		Transform transform = s_handPose[SwgVrPhysics::Hand_Right].handFromWorld;
		applyHandVisualTwist(transform, SwgVrPhysics::Hand_Right);
		return transform;
	}

	void applyRightWeaponVisualTwist(Transform &transform)
	{
		float const yawDegrees = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_YAW_DEGREES", getHandVisualTwistDegrees(SwgVrPhysics::Hand_Right, "YAW"));
		float const pitchDegrees = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_PITCH_DEGREES", getHandVisualTwistDegrees(SwgVrPhysics::Hand_Right, "PITCH"));
		float const rollDegrees = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_ROLL_DEGREES", getHandVisualTwistDegrees(SwgVrPhysics::Hand_Right, "ROLL"));

		if (yawDegrees != 0.0f)
			transform.yaw_l(yawDegrees * PI_OVER_180);
		if (pitchDegrees != 0.0f)
			transform.pitch_l(pitchDegrees * PI_OVER_180);
		if (rollDegrees != 0.0f)
			transform.roll_l(rollDegrees * PI_OVER_180);

		if (yawDegrees != 0.0f || pitchDegrees != 0.0f || rollDegrees != 0.0f)
			transform.reorthonormalize();
	}

	void applyRightWeaponPalmTwist(Transform &transform)
	{
		float const yawDegrees = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_PALM_YAW_DEGREES", 0.0f);
		float const pitchDegrees = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_PALM_PITCH_DEGREES", 0.0f);
		float const rollDegrees = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_PALM_ROLL_DEGREES", 0.0f);

		if (yawDegrees != 0.0f)
			transform.yaw_l(yawDegrees * PI_OVER_180);
		if (pitchDegrees != 0.0f)
			transform.pitch_l(pitchDegrees * PI_OVER_180);
		if (rollDegrees != 0.0f)
			transform.roll_l(rollDegrees * PI_OVER_180);

		if (yawDegrees != 0.0f || pitchDegrees != 0.0f || rollDegrees != 0.0f)
			transform.reorthonormalize();
	}

	void applyRightWeaponVisualOffset(Transform &transform)
	{
		float const rightMeters = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_RIGHT_METERS", 0.0f);
		float const upMeters = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_UP_METERS", 0.0f);
		float const forwardMeters = getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_FORWARD_METERS", 0.0f);
		if (rightMeters == 0.0f && upMeters == 0.0f && forwardMeters == 0.0f)
			return;

		Vector const offset_w =
			transform.getLocalFrameI_p() * rightMeters +
			transform.getLocalFrameJ_p() * upMeters +
			transform.getLocalFrameK_p() * forwardMeters;
		transform.setPosition_p(transform.getPosition_p() + offset_w);
	}

	Transform getRightWeaponVisualTransform()
	{
		Transform transform = s_handPose[SwgVrPhysics::Hand_Right].handFromWorld;
		if (s_handRigObject && s_handRigAppearance && environmentEnabledDefault("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_USE_HOLD_R", true))
		{
			Transform holdToRig(Transform::IF_none);
			if (s_handRigAppearance->findHardpoint(TemporaryCrcString("hold_r", true), holdToRig))
				transform.multiply(s_handRigObject->getTransform_o2w(), holdToRig);
		}
		if (environmentEnabledDefault("SWG_OG_VR_DETACHED_HANDS_RIGHT_WEAPON_USE_AIM_ORIENTATION", true) && s_handPose[SwgVrPhysics::Hand_Right].aimValid)
		{
			Vector const holdPosition = transform.getPosition_p();
			transform = s_handPose[SwgVrPhysics::Hand_Right].aimFromWorld;
			transform.setPosition_p(holdPosition);
		}
		else
			applyRightWeaponPalmTwist(transform);
		applyRightWeaponVisualTwist(transform);
		applyRightWeaponVisualOffset(transform);
		return transform;
	}

	void ensureEquippedWeaponProxy(CreatureObject &creature)
	{
		WeaponObject * const weapon = creature.getCurrentWeapon();
		Appearance const * const weaponAppearance = weapon ? weapon->getAppearance() : 0;
		AppearanceTemplate const * const weaponAppearanceTemplate = weaponAppearance ? weaponAppearance->getAppearanceTemplate() : 0;
		char const * const weaponAppearanceName = weaponAppearanceTemplate ? weaponAppearanceTemplate->getName() : 0;
		if (!weapon || !weaponAppearanceTemplate || !weaponAppearanceName || !*weaponAppearanceName)
		{
			destroyWeaponProxy();
			return;
		}

		if (!s_handPose[SwgVrPhysics::Hand_Right].valid)
		{
			destroyWeaponProxy();
			return;
		}

		if (s_weaponObject && strcmp(s_weaponAppearanceTemplateName, weaponAppearanceName) == 0)
		{
			CellProperty * const desiredCell = getHandRigCell(creature);
			if (s_weaponObject->getParentCell() != desiredCell)
				s_weaponObject->setParentCell(desiredCell);
			s_weaponObject->setTransform_o2w(getRightWeaponVisualTransform());
			return;
		}

		destroyWeaponProxy();

		Appearance * const proxyAppearance = weaponAppearanceTemplate->createAppearance();
		if (!proxyAppearance)
		{
			traceRateLimited("SWGVRDetachedHands weapon proxy failed: equipped inventory weapon has no creatable appearance");
			return;
		}

		copyCustomizationDataIfPresent(*weapon, *proxyAppearance);
		proxyAppearance->onEvent(ClientGameAppearanceEvents::getOnInitializeCopyEquippedInCombatStateEventId());

		Object * const weaponObject = new MemoryBlockManagedObject();
		weaponObject->setAppearance(proxyAppearance);
		weaponObject->addNotification(ClientWorld::getIntangibleNotification());
		RenderWorld::addObjectNotifications(*weaponObject);
		weaponObject->setParentCell(getHandRigCell(creature));
		weaponObject->setTransform_o2w(getRightWeaponVisualTransform());
		weaponObject->addToWorld();
		weaponObject->scheduleForAlter();

		s_weaponObject = weaponObject;
		_snprintf(s_weaponAppearanceTemplateName, sizeof(s_weaponAppearanceTemplateName) - 1, "%s", weaponAppearanceName);
		s_weaponAppearanceTemplateName[sizeof(s_weaponAppearanceTemplateName) - 1] = '\0';

		{
			char line[384];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands equipped inventory weapon proxy appearance=%s", s_weaponAppearanceTemplateName);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
		}
	}

	CellProperty *getHandRigCell(CreatureObject const &creature)
	{
		Camera const * const camera = Game::getConstCamera();
		if (camera && camera->getParentCell())
			return camera->getParentCell();

		return creature.getParentCell() ? creature.getParentCell() : CellProperty::getWorldCellProperty();
	}

	Transform const &getHandRigTransform(CreatureObject const &creature)
	{
		Camera const * const camera = Game::getConstCamera();
		if (camera)
			return camera->getTransform_o2w();

		return creature.getTransform_o2w();
	}

	char const *getSpeciesPrefix(int species)
	{
		switch (static_cast<SharedCreatureObjectTemplate::Species>(species))
		{
		case SharedCreatureObjectTemplate::SP_bothan:
			return "bth";
		case SharedCreatureObjectTemplate::SP_monCalamari:
			return "mon";
		case SharedCreatureObjectTemplate::SP_rodian:
			return "rod";
		case SharedCreatureObjectTemplate::SP_trandoshan:
			return "trn";
		case SharedCreatureObjectTemplate::SP_twilek:
			return "twk";
		case SharedCreatureObjectTemplate::SP_zabrak:
			return "zab";
		case SharedCreatureObjectTemplate::SP_human:
		default:
			return "hum";
		}
	}

	char const *getGenderSuffix(int gender)
	{
		return static_cast<SharedCreatureObjectTemplate::Gender>(gender) == SharedCreatureObjectTemplate::GE_female ? "f" : "m";
	}

	char const *getCreatureSkeletonPath(CreatureObject const &creature)
	{
		if (static_cast<SharedCreatureObjectTemplate::Species>(creature.getSpecies()) == SharedCreatureObjectTemplate::SP_ithorian)
			return "appearance/skeleton/ithorian.skt";

		return "appearance/skeleton/all_b.skt";
	}

	int getDetachedHandMeshLod()
	{
		int const lod = static_cast<int>(getEnvironmentFloat("SWG_OG_VR_DETACHED_HANDS_MESH_LOD", 0.0f));
		if (lod < 0)
			return 0;
		if (lod > 3)
			return 3;

		return lod;
	}

	bool tryBuildHandMeshPath(char const *speciesPrefix, char const *genderSuffix, int lod, char *buffer, size_t bufferSize)
	{
		_snprintf(buffer, bufferSize - 1, "appearance/mesh/%s_%s_hands_l%d.mgn", speciesPrefix, genderSuffix, lod);
		buffer[bufferSize - 1] = '\0';
		return TreeFile::exists(buffer);
	}

	void buildHandMeshPath(CreatureObject const &creature, char *buffer, size_t bufferSize)
	{
		char const * const speciesPrefix = getSpeciesPrefix(creature.getSpecies());
		char const * const genderSuffix = getGenderSuffix(creature.getGender());
		int const preferredLod = getDetachedHandMeshLod();
		if (tryBuildHandMeshPath(speciesPrefix, genderSuffix, preferredLod, buffer, bufferSize))
			return;

		for (int lod = 0; lod <= 3; ++lod)
		{
			if (lod != preferredLod && tryBuildHandMeshPath(speciesPrefix, genderSuffix, lod, buffer, bufferSize))
				return;
		}

		if (tryBuildHandMeshPath("hum", genderSuffix, preferredLod, buffer, bufferSize))
			return;

		for (int lod = 0; lod <= 3; ++lod)
		{
			if (lod != preferredLod && tryBuildHandMeshPath("hum", genderSuffix, lod, buffer, bufferSize))
				return;
		}

		_snprintf(buffer, bufferSize - 1, "appearance/mesh/hum_m_hands_l0.mgn");
		buffer[bufferSize - 1] = '\0';
	}

	SkeletalAppearance2 *createHandAppearanceFromNativeMeshSkeleton(char const *filename)
	{
		if (!filename || !TreeFile::exists(filename))
			return 0;

		MeshGeneratorTemplate const * const meshTemplate = MeshGeneratorTemplateList::fetch(TemporaryCrcString(filename, true));
		if (!meshTemplate)
		{
			char line[256];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands native create failed step=fetch mesh=%s", filename);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			return 0;
		}

		Appearance *baseAppearance = 0;
		for (int lodIndex = 0; lodIndex != meshTemplate->getDetailCount() && !baseAppearance; ++lodIndex)
		{
			BasicMeshGeneratorTemplate const * const basicTemplate = meshTemplate->fetchBasicMeshGeneratorTemplate(lodIndex);
			if (basicTemplate)
			{
				SkeletalMeshGeneratorTemplate const * const skeletalTemplate = safe_cast<SkeletalMeshGeneratorTemplate const *>(basicTemplate);
				if (skeletalTemplate)
					baseAppearance = skeletalTemplate->createAppearance();

				basicTemplate->release();
			}
		}

		meshTemplate->release();

		SkeletalAppearance2 * const appearance = baseAppearance ? baseAppearance->asSkeletalAppearance2() : 0;
		if (!appearance)
		{
			char line[256];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands native create failed step=create-appearance mesh=%s", filename);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			delete baseAppearance;
			return 0;
		}

		IGNORE_RETURN(appearance->rebuildIfDirtyAndAvailable());
		{
			char line[256];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands native created appearance mesh=%s", filename);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
		}
		traceSkeletonTransforms(*appearance, "native-mesh");
		if (!skeletonHasAnyTransform(*appearance, s_leftHandCandidates, sizeof(s_leftHandCandidates) / sizeof(s_leftHandCandidates[0])) ||
			!skeletonHasAnyTransform(*appearance, s_rightHandCandidates, sizeof(s_rightHandCandidates) / sizeof(s_rightHandCandidates[0])))
		{
			traceLine("SWGVRDetachedHands native create ignored: missing supported left/right hand transforms");
			delete baseAppearance;
			return 0;
		}

		return appearance;
	}

	SkeletalAppearance2 *createHandAppearanceFromExplicitSkeleton(char const *filename, char const *skeletonPath, char const *reason)
	{
		if (!filename || !TreeFile::exists(filename))
		{
			char line[256];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands create failed step=missing-file mesh=%s", filename ? filename : "<null>");
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			return 0;
		}

		if (!skeletonPath || !TreeFile::exists(skeletonPath))
		{
			char line[384];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands create failed step=explicit-skeleton-path reason=%s skeleton=%s", reason ? reason : "<null>", skeletonPath ? skeletonPath : "<null>");
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			return 0;
		}

		SkeletalAppearanceTemplate *const sat = new SkeletalAppearanceTemplate();
		IGNORE_RETURN(AppearanceTemplateList::fetchNew(sat));
		IGNORE_RETURN(sat->addSkeletonTemplate(skeletonPath, ""));
		IGNORE_RETURN(sat->addMeshGenerator(filename));

		Appearance * const baseAppearance = sat->createAppearance();
		SkeletalAppearance2 * const appearance = baseAppearance ? baseAppearance->asSkeletalAppearance2() : 0;
		AppearanceTemplateList::release(sat);
		if (!appearance)
		{
			char line[384];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands create failed step=explicit-create-appearance reason=%s mesh=%s skeleton=%s", reason ? reason : "<null>", filename, skeletonPath);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			delete baseAppearance;
			return 0;
		}

		IGNORE_RETURN(appearance->rebuildIfDirtyAndAvailable());
		{
			char line[384];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands created explicit appearance reason=%s mesh=%s skeleton=%s", reason ? reason : "<null>", filename, skeletonPath);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
		}
		traceSkeletonTransforms(*appearance, reason);
		if (!appearance->getDisplayLodSkeleton())
		{
			traceLine("SWGVRDetachedHands explicit create accepted: display skeleton pending");
			return appearance;
		}

		if (!skeletonHasAnyTransform(*appearance, s_leftHandCandidates, sizeof(s_leftHandCandidates) / sizeof(s_leftHandCandidates[0])) ||
			!skeletonHasAnyTransform(*appearance, s_rightHandCandidates, sizeof(s_rightHandCandidates) / sizeof(s_rightHandCandidates[0])))
		{
			traceLine("SWGVRDetachedHands explicit create ignored: missing supported left/right hand transforms");
			delete baseAppearance;
			return 0;
		}

		return appearance;
	}

	SkeletalAppearance2 *createHandAppearanceFromMeshGenerator(CreatureObject const &creature, char const *filename)
	{
		SkeletalAppearance2 * const nativeAppearance = createHandAppearanceFromNativeMeshSkeleton(filename);
		if (nativeAppearance)
			return nativeAppearance;

		SkeletalAppearance2 * const handSkeletonAppearance = createHandAppearanceFromExplicitSkeleton(filename, getCreatureSkeletonPath(creature), "explicit-character-skeleton");
		if (handSkeletonAppearance)
			return handSkeletonAppearance;

		SkeletalAppearance2 * const humanoidSkeletonAppearance = createHandAppearanceFromExplicitSkeleton(filename, "appearance/skeleton/all_b.skt", "explicit-humanoid-fallback");
		if (humanoidSkeletonAppearance)
			return humanoidSkeletonAppearance;

		traceLine("SWGVRDetachedHands hand rig failed: character hand mesh did not create with hand skeleton");
		return 0;
	}

	void destroyRig()
	{
		publishSceneHandsActive(false);
		if (s_handRigObject)
		{
			s_handRigObject->kill();
			s_handRigObject->scheduleForAlter();
		}

		s_handRigObject = 0;
		s_handRigAppearance = 0;
		s_handRigMeshPath[0] = '\0';
		s_rigCreatedLogged = false;
		s_handRigModifiersAttached = false;
	}

	bool ensureRig(CreatureObject &creature, char const *meshPath)
	{
		if (!meshPath || !*meshPath)
			return false;

		if (s_handRigObject && strcmp(s_handRigMeshPath, meshPath) == 0)
			return true;

		destroyRig();
		SkeletalAppearance2 * const appearance = createHandAppearanceFromMeshGenerator(creature, meshPath);
		if (!appearance)
		{
			char line[256];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands failed to create hand appearance mesh=%s", meshPath ? meshPath : "<null>");
			line[sizeof(line) - 1] = '\0';
			traceRateLimited(line);
			return false;
		}

		Object * const object = new MemoryBlockManagedObject();
		object->setAppearance(appearance);
		object->addNotification(ClientWorld::getIntangibleNotification());
		RenderWorld::addObjectNotifications(*object);
		object->setParentCell(getHandRigCell(creature));
		object->setTransform_o2w(getHandRigTransform(creature));
		object->addToWorld();
		object->scheduleForAlter();

		s_handRigObject = object;
		s_handRigAppearance = appearance;
		_snprintf(s_handRigMeshPath, sizeof(s_handRigMeshPath) - 1, "%s", meshPath);
		s_handRigMeshPath[sizeof(s_handRigMeshPath) - 1] = '\0';
		s_handRigModifiersAttached = attachHandModifiers(*appearance);
		appearance->setShowMesh(s_handRigModifiersAttached);
		if (!s_handRigModifiersAttached)
			traceRateLimited("SWGVRDetachedHands hand rig not ready: character hand skeleton joints unresolved");

		if (!s_rigCreatedLogged)
		{
			char line[256];
			_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands created detached hand rig mesh=%s", s_handRigMeshPath);
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
			s_rigCreatedLogged = true;
		}

		return true;
	}

	bool ensureHandModifiersReady()
	{
		if (s_handRigModifiersAttached)
			return true;

		if (!s_handRigAppearance)
			return false;

		s_handRigModifiersAttached = attachHandModifiers(*s_handRigAppearance);
		s_handRigAppearance->setShowMesh(s_handRigModifiersAttached);
		if (!s_handRigModifiersAttached)
			traceRateLimited("SWGVRDetachedHands hand rig not ready: supported hand joints unresolved");

		return s_handRigModifiersAttached;
	}
}

// ======================================================================

void VrDetachedHands::setMenuPreviewCreature(CreatureObject *)
{
}

// ----------------------------------------------------------------------

void VrDetachedHands::update(CreatureObject &creature)
{
	CreatureObject const * const playerCreature = Game::getPlayerCreature();
	if (!playerCreature)
	{
		destroyRig();
		destroyWeaponProxy();
		return;
	}

	if (&creature != playerCreature)
		return;

	if (!detachedHandsEnabled())
	{
		destroyRig();
		destroyWeaponProxy();
		return;
	}

	BridgeApi const &api = getBridgeApi();
	if (!api.getHandState)
	{
		traceRateLimited("SWGVRDetachedHands bridge unavailable: missing hand-state export or ABI mismatch");
		destroyRig();
		destroyWeaponProxy();
		return;
	}

	if (!s_bridgeReadyLogged)
	{
		traceLine("SWGVRDetachedHands bridge ready");
		s_bridgeReadyLogged = true;
	}

	updateRuntimeHandPose(SwgVrPhysics::Hand_Left, api);
	updateRuntimeHandPose(SwgVrPhysics::Hand_Right, api);
	if (!s_handPose[SwgVrPhysics::Hand_Right].valid)
	{
		destroyRig();
		destroyWeaponProxy();
		return;
	}

	char meshPath[128];
	buildHandMeshPath(creature, meshPath, sizeof(meshPath));
	if (!ensureRig(creature, meshPath))
	{
		ensureEquippedWeaponProxy(creature);
		return;
	}

	CellProperty * const desiredCell = getHandRigCell(creature);
	if (s_handRigObject->getParentCell() != desiredCell)
		s_handRigObject->setParentCell(desiredCell);

	s_handRigObject->setTransform_o2w(getHandRigTransform(creature));
	if (s_handRigAppearance)
	{
		if (!ensureHandModifiersReady())
		{
			return;
		}

		s_handRigAppearance->setShowMesh(true);
		s_handRigAppearance->setShowAttachments(false);
		publishSceneHandsActive(true);
	}

	ensureEquippedWeaponProxy(creature);
}

// ----------------------------------------------------------------------

void VrDetachedHands::updateMenuPreview()
{
}

// ----------------------------------------------------------------------

void VrDetachedHands::renderMenuPreviewStereo()
{
}

// ----------------------------------------------------------------------

bool VrDetachedHands::getRightWeaponMuzzlePosition_w(CrcLowerString const &hardpointName, Vector &position_w, CellProperty const *&cellProperty)
{
	if (!s_weaponObject || !s_weaponObject->getAppearance())
		return false;

	Transform hardpointToWeapon(Transform::IF_none);
	if (!s_weaponObject->getAppearance()->findHardpoint(hardpointName, hardpointToWeapon))
		return false;

	position_w = s_weaponObject->rotateTranslate_o2w(hardpointToWeapon.getPosition_p());
	cellProperty = s_weaponObject->getParentCell() ? s_weaponObject->getParentCell() : CellProperty::getWorldCellProperty();

	if (traceEnabled())
	{
		char line[256];
		_snprintf(line, sizeof(line) - 1, "SWGVRDetachedHands muzzleOrigin hardpoint=%s position=(%.3f,%.3f,%.3f)", hardpointName.getString(), position_w.x, position_w.y, position_w.z);
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
	}

	return true;
}

// ----------------------------------------------------------------------

void VrDetachedHands::reset()
{
	destroyRig();
	destroyWeaponProxy();
}

// ======================================================================

#else

void VrDetachedHands::setMenuPreviewCreature(CreatureObject *)
{
}

// ----------------------------------------------------------------------

void VrDetachedHands::update(CreatureObject &)
{
}

// ----------------------------------------------------------------------

void VrDetachedHands::updateMenuPreview()
{
}

// ----------------------------------------------------------------------

void VrDetachedHands::renderMenuPreviewStereo()
{
}

// ----------------------------------------------------------------------

bool VrDetachedHands::getRightWeaponMuzzlePosition_w(CrcLowerString const &, Vector &, CellProperty const *&)
{
	return false;
}

// ----------------------------------------------------------------------

void VrDetachedHands::reset()
{
}

#endif
