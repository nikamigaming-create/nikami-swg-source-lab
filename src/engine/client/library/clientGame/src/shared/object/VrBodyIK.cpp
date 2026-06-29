// ======================================================================
//
// VrBodyIK.cpp
//
// Full-body VR inverse kinematics solver implementation.
//
// Algorithm summary:
//   Arms   — Analytical Two-Bone IK (shoulder->elbow->wrist) with
//             pole-vector constraint (elbows point laterally-down).
//   Spine  — Proportional yaw+pitch lean derived from HMD orientation
//             blended across spine1 and (if present) spine2.
//   Neck   — HMD orientation applied directly minus the spine contribution.
//   Head   — Full HMD orientation (look-at from neck).
//
// All poses come from VRPhysicsBridge: HandState for controllers,
// BodyState for HMD head pose.
//
// Bone names are resolved once at attach time and cached.
// Unknown bones (species variant) are skipped gracefully.
//
// ======================================================================

#include "clientGame/FirstClientGame.h"
#include "clientGame/VrBodyIK.h"

#if defined(SWG_VR_PHYSICS_CLIENTOBJECT_HOOK)

#include "clientGame/CreatureController.h"
#include "clientGame/CreatureObject.h"
#include "clientGame/Game.h"
#include "clientGame/VrBodyTransformModifier.h"
#include "clientGraphics/Camera.h"
#include "clientSkeletalAnimation/SkeletalAppearance2.h"
#include "clientSkeletalAnimation/Skeleton.h"
#include "sharedFoundation/TemporaryCrcString.h"
#include "sharedMath/Quaternion.h"
#include "sharedMath/Transform.h"
#include "sharedMath/Vector.h"

#include <math.h>
#include <string.h>
#include <windows.h>

#define VRPHYSICSBRIDGE_NO_IMPORT
#include "../../../../../application/Direct3d11/src/win32/VRPhysicsBridge.h"
#undef VRPHYSICSBRIDGE_NO_IMPORT

// ======================================================================

namespace
{
	// -----------------------------------------------------------------------
	// Runtime IK state
	// -----------------------------------------------------------------------

	typedef bool (*GetBodyStateFunction)(SwgVrPhysics::BodyState *);
	typedef bool (*GetHandStateFunction)(uint32_t, SwgVrPhysics::HandState *);

	struct VrBodyBridgeApi
	{
		GetBodyStateFunction getBodyState;
		GetHandStateFunction getHandState;

		VrBodyBridgeApi() : getBodyState(0), getHandState(0) {}
		bool ready() const { return getBodyState && getHandState; }
	};

	// Solved joint transforms (object space, l2o) indexed by joint role.
	enum JointRole
	{
		JR_spine1 = 0,
		JR_spine2,
		JR_neck,
		JR_head,
		JR_l_upperarm,
		JR_l_forearm,
		JR_l_hand,
		JR_l_skinhand,
		JR_r_upperarm,
		JR_r_forearm,
		JR_r_hand,
		JR_r_skinhand,
		JR_l_thumb1,
		JR_l_thumb2,
		JR_l_index1,
		JR_l_index2,
		JR_l_ring1,
		JR_l_ring2,
		JR_r_thumb1,
		JR_r_thumb2,
		JR_r_index1,
		JR_r_index2,
		JR_r_ring1,
		JR_r_ring2,
		// Lower-body (heuristic / experimental, gated by SWG_OG_VR_FB_LEGS).
		JR_pelvis,
		JR_l_thigh,
		JR_l_calf,
		JR_r_thigh,
		JR_r_calf,
		JR_COUNT
	};

	// Canonical bone name candidates per role (first match wins).
	char const * const s_boneCandidates[JR_COUNT][12] =
	{
		/* JR_spine1     */ { "spine1",     "Spine1",    "spine_01",  0 },
		/* JR_spine2     */ { "spine2",     "Spine2",    "spine_02",  0 },
		/* JR_neck       */ { "neck",       "Neck",      "neck_01",   0 },
		/* JR_head       */ { "head",       "Head",      "head_eye",  0 },
		/* JR_l_upperarm */ { "larm",       "l_upperarm", "LeftArm",   "l_bicep",   0 },
		/* JR_l_forearm  */ { "l_forearm",  "LeftForeArm","lforearm", 0 },
		/* JR_l_hand     */ { "hold_l",     0 },
		/* JR_l_skinhand */ { "l_hand",     "lhand",     "lwrist",    0 },
		/* JR_r_upperarm */ { "rarm",       "r_upperarm", "RightArm",  "r_bicep",   0 },
		/* JR_r_forearm  */ { "r_forearm",  "RightForeArm","rforearm",0 },
		/* JR_r_hand     */ { "hold_r",     0 },
		/* JR_r_skinhand */ { "r_hand",     "rhand",     "rwrist",    0 },
		/* JR_l_thumb1   */ { "lthumb01",   "l_thumb01", "lthumb1",   0 },
		/* JR_l_thumb2   */ { "lthumb02",   "l_thumb02", "lthumb2",   0 },
		/* JR_l_index1   */ { "lindex01",   "l_index01", "lindex1",   0 },
		/* JR_l_index2   */ { "lindex02",   "l_index02", "lindex2",   0 },
		/* JR_l_ring1    */ { "lring01",    "l_ring01",  "lring1",    0 },
		/* JR_l_ring2    */ { "lring02",    "l_ring02",  "lring2",    0 },
		/* JR_r_thumb1   */ { "rthumb01",   "r_thumb01", "rthumb1",   0 },
		/* JR_r_thumb2   */ { "rthumb02",   "r_thumb02", "rthumb2",   0 },
		/* JR_r_index1   */ { "rindex01",   "r_index01", "rindex1",   0 },
		/* JR_r_index2   */ { "rindex02",   "r_index02", "rindex2",   0 },
		/* JR_r_ring1    */ { "rring01",    "r_ring01",  "rring1",    0 },
		/* JR_r_ring2    */ { "rring02",    "r_ring02",  "rring2",    0 },
		/* JR_pelvis     */ { "pelvis",     "Pelvis",    "hips",      0 },
		/* JR_l_thigh    */ { "l_thigh",    "LeftUpLeg", "l_leg",     0 },
		/* JR_l_calf     */ { "l_calf",     "LeftLeg",   "l_knee",    0 },
		/* JR_r_thigh    */ { "r_thigh",    "RightUpLeg","r_leg",     0 },
		/* JR_r_calf     */ { "r_calf",     "RightLeg",  "r_knee",    0 },
	};

	struct SolvedFrame
	{
		Transform transforms[JR_COUNT];
		bool       valid[JR_COUNT];
		bool       frameActive;
		float      armIkWeight;

		SolvedFrame()
		{
			frameActive = false;
			armIkWeight = 1.0f;
			for (int i = 0; i < JR_COUNT; ++i)
			{
				transforms[i] = Transform::identity;
				valid[i] = false;
			}
		}
	};

	SwgVrPhysics::VrBodyCalibration defaultBodyCalibration()
	{
		SwgVrPhysics::VrBodyCalibration cal;
		cal.shoulderWidthMeters   = 0.38f;
		cal.upperArmLengthMeters  = 0.28f;
		cal.foreArmLengthMeters   = 0.24f;
		cal.shoulderDropMeters    = 0.22f;
		cal.wristOffsetMeters     = 0.05f;
		cal.bodyHeightMeters      = 1.75f;
		cal.armLengthScaleLeft    = 1.0f;
		cal.armLengthScaleRight   = 1.0f;
		return cal;
	}

	struct RuntimeCalibration
	{
		SwgVrPhysics::VrBodyCalibration cal;
		bool calibrated;

		RuntimeCalibration()
		{
			memset(this, 0, sizeof(*this));
			cal = defaultBodyCalibration();
			calibrated = false;
		}
	};

	// Resolved bone names for the currently attached creature.
	// Uses plain char arrays to avoid ConstCharCrcString disabled ctors.
	struct ResolvedBones
	{
		char names[JR_COUNT][64];
		int  indices[JR_COUNT];
		Vector driveAxis_l[JR_COUNT];
		bool resolved[JR_COUNT];
		bool attachAttempted;

		ResolvedBones() : attachAttempted(false)
		{
			for (int i = 0; i < JR_COUNT; ++i)
			{
				names[i][0] = '\0';
				indices[i] = -1;
				driveAxis_l[i] = Vector::unitX;
				resolved[i] = false;
			}
		}
	};

	SolvedFrame         s_frame;
	RuntimeCalibration  s_calibration;
	ResolvedBones       s_bones;
	bool                s_modifiersAttached = false;
	DWORD               s_lastTraceMilliseconds = 0;
	bool                s_skeletonOverlayConfigured = false;
	bool                s_bridgeReadyLogged = false;
	bool                s_modifiersLogged = false;
	DWORD               s_lastHandProofTraceMilliseconds[2] = { 0, 0 };
	DWORD               s_lastWeightMilliseconds = 0;

	// -----------------------------------------------------------------------
	// Environment helpers
	// -----------------------------------------------------------------------

	bool envFlag(char const *name, bool defaultValue)
	{
		char buf[32];
		DWORD len = GetEnvironmentVariableA(name, buf, sizeof(buf));
		if (!len || len >= sizeof(buf))
			return defaultValue;
		return _stricmp(buf, "1") == 0 || _stricmp(buf, "true") == 0 || _stricmp(buf, "yes") == 0;
	}

	float envFloat(char const *name, float defaultValue)
	{
		char buf[32];
		DWORD len = GetEnvironmentVariableA(name, buf, sizeof(buf));
		if (!len || len >= sizeof(buf))
			return defaultValue;
		return static_cast<float>(atof(buf));
	}

	bool traceEnabled()
	{
		return envFlag("SWG_OG_VR_HAND_TRACE", false) || envFlag("SWG_OG_VR_PHYSICS_TRACE", false);
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
		if (now - s_lastTraceMilliseconds < 1000)
			return;

		s_lastTraceMilliseconds = now;
		traceLine(line);
	}

	void traceHandProof(
		char const      *handName,
		Transform const &rawGrip_w,
		JointRole       handRole,
		Transform const &creatureT)
	{
		if (!traceEnabled() || !s_frame.valid[handRole])
			return;

		int const handIndex = (handRole == JR_r_hand) ? 1 : 0;
		DWORD const now = GetTickCount();
		if (now - s_lastHandProofTraceMilliseconds[handIndex] < 500)
			return;

		s_lastHandProofTraceMilliseconds[handIndex] = now;

		Vector const rawGripPos_w = rawGrip_w.getPosition_p();
		Vector const solvedGripPos_o = s_frame.transforms[handRole].getPosition_p();
		Vector const solvedGripPos_w = creatureT.rotateTranslate_l2p(solvedGripPos_o);
		Vector const delta_w = solvedGripPos_w - rawGripPos_w;

		char line[1024];
		_snprintf(line, sizeof(line) - 1,
			"SWGVRBodyIKProof kind=solve hand=%s bone=%s rawGrip_w=(%.5f,%.5f,%.5f) solvedHold_o=(%.5f,%.5f,%.5f) solvedHold_w=(%.5f,%.5f,%.5f) solvedMinusRaw_w=(%.6f,%.6f,%.6f) solvedMinusRawMag=%.6f i=(%.5f,%.5f,%.5f) j=(%.5f,%.5f,%.5f) k=(%.5f,%.5f,%.5f)",
			handName ? handName : "?",
			s_bones.resolved[handRole] ? s_bones.names[handRole] : "<unresolved>",
			rawGripPos_w.x, rawGripPos_w.y, rawGripPos_w.z,
			solvedGripPos_o.x, solvedGripPos_o.y, solvedGripPos_o.z,
			solvedGripPos_w.x, solvedGripPos_w.y, solvedGripPos_w.z,
			delta_w.x, delta_w.y, delta_w.z,
			delta_w.magnitude(),
			s_frame.transforms[handRole].getLocalFrameI_p().x,
			s_frame.transforms[handRole].getLocalFrameI_p().y,
			s_frame.transforms[handRole].getLocalFrameI_p().z,
			s_frame.transforms[handRole].getLocalFrameJ_p().x,
			s_frame.transforms[handRole].getLocalFrameJ_p().y,
			s_frame.transforms[handRole].getLocalFrameJ_p().z,
			s_frame.transforms[handRole].getLocalFrameK_p().x,
			s_frame.transforms[handRole].getLocalFrameK_p().y,
			s_frame.transforms[handRole].getLocalFrameK_p().z);
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
	}

	int countValidSolvedRoles()
	{
		int count = 0;
		for (int i = 0; i < JR_COUNT; ++i)
		{
			if (s_frame.valid[i])
				++count;
		}
		return count;
	}

	int countResolvedBones()
	{
		int count = 0;
		for (int i = 0; i < JR_COUNT; ++i)
		{
			if (s_bones.resolved[i])
				++count;
		}
		return count;
	}

	// -----------------------------------------------------------------------
	// Bridge API loader (mirrors VrDetachedHands pattern)
	// -----------------------------------------------------------------------

	VrBodyBridgeApi const &getBodyBridgeApi()
	{
		static VrBodyBridgeApi s_api;
		static DWORD s_lastAttempt = 0;

		if (s_api.ready())
			return s_api;

		DWORD const now = GetTickCount();
		if (now - s_lastAttempt < 2000)
			return s_api;
		s_lastAttempt = now;

		static char const * const rendererModules[] =
		{
			"gl05_r.dll",
			"gl05_o.dll",
			"gl05_d.dll",
			"Direct3d11.dll",
			"Direct3d11_x64.dll"
		};

		for (size_t i = 0; i != sizeof(rendererModules) / sizeof(rendererModules[0]); ++i)
		{
			HMODULE const hm = GetModuleHandleA(rendererModules[i]);
			if (!hm)
				continue;

			s_api.getBodyState = reinterpret_cast<GetBodyStateFunction>(
				GetProcAddress(hm, "SWGVRPhysics_GetBodyState"));
			s_api.getHandState = reinterpret_cast<GetHandStateFunction>(
				GetProcAddress(hm, "SWGVRPhysics_GetHandState"));
			if (s_api.ready())
				return s_api;
		}

		return s_api;
	}

	// -----------------------------------------------------------------------
	// Small math helpers (no external dependencies)
	// -----------------------------------------------------------------------

	inline float clampF(float v, float lo, float hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	inline float dot3(Vector const &a, Vector const &b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	inline Vector cross3(Vector const &a, Vector const &b)
	{
		return Vector(a.y * b.z - a.z * b.y,
		              a.z * b.x - a.x * b.z,
		              a.x * b.y - a.y * b.x);
	}

	inline float lengthSq(Vector const &v)
	{
		return v.x * v.x + v.y * v.y + v.z * v.z;
	}

	inline float length(Vector const &v)
	{
		return sqrtf(lengthSq(v));
	}

	inline Vector safeNormalize(Vector const &v, Vector const &fallback)
	{
		float const mag = length(v);
		if (mag < 0.0001f)
			return fallback;
		float inv = 1.0f / mag;
		return Vector(v.x * inv, v.y * inv, v.z * inv);
	}

	void applyLocalRotation(Transform &transform, Vector const &axis_l, float degrees)
	{
		if (fabsf(degrees) < 0.001f)
			return;

		float const radians = degrees * 3.14159265358979323846f / 180.0f;
		Quaternion const rotation(radians, axis_l);
		Transform rotationTransform;
		rotation.getTransformPreserveTranslation(&rotationTransform);

		Vector const position = transform.getPosition_p();
		transform.multiply(transform, rotationTransform);
		transform.setPosition_p(position);
		transform.reorthonormalize();
	}

	Transform applyWristOrientationCalibration(bool isLeft, Transform const &target)
	{
		Transform calibrated(target);
		char const * const prefix = isLeft ? "SWG_OG_VR_IK_LEFT_" : "SWG_OG_VR_IK_RIGHT_";

		char name[96];
		_snprintf(name, sizeof(name) - 1, "%sWRIST_PITCH_DEG", prefix);
		name[sizeof(name) - 1] = '\0';
		float const pitch = envFloat(name, isLeft ? 0.0f : 180.0f);

		_snprintf(name, sizeof(name) - 1, "%sWRIST_YAW_DEG", prefix);
		name[sizeof(name) - 1] = '\0';
		float const yaw = envFloat(name, 180.0f);

		_snprintf(name, sizeof(name) - 1, "%sWRIST_ROLL_DEG", prefix);
		name[sizeof(name) - 1] = '\0';
		float const roll = envFloat(name, 0.0f);

		applyLocalRotation(calibrated, Vector::unitX, pitch);
		applyLocalRotation(calibrated, Vector::unitY, yaw);
		applyLocalRotation(calibrated, Vector::unitZ, roll);
		return calibrated;
	}

	Transform applyHoldOrientationCalibration(bool isLeft, Transform const &target)
	{
		Transform calibrated(target);
		char const * const prefix = isLeft ? "SWG_OG_VR_IK_LEFT_HOLD_" : "SWG_OG_VR_IK_RIGHT_HOLD_";

		char name[112];
		_snprintf(name, sizeof(name) - 1, "%sPITCH_DEG", prefix);
		name[sizeof(name) - 1] = '\0';
		float const pitch = envFloat(name, isLeft ? 0.0f : 180.0f);

		_snprintf(name, sizeof(name) - 1, "%sYAW_DEG", prefix);
		name[sizeof(name) - 1] = '\0';
		float const yaw = envFloat(name, isLeft ? 180.0f : 180.0f);

		_snprintf(name, sizeof(name) - 1, "%sROLL_DEG", prefix);
		name[sizeof(name) - 1] = '\0';
		float const roll = envFloat(name, 0.0f);

		applyLocalRotation(calibrated, Vector::unitX, pitch);
		applyLocalRotation(calibrated, Vector::unitY, yaw);
		applyLocalRotation(calibrated, Vector::unitZ, roll);
		return calibrated;
	}

	float wristInsetMeters(bool isLeft)
	{
		float inset = envFloat("SWG_OG_VR_IK_WRIST_INSET_M", 0.06f);
		char name[96];
		_snprintf(name, sizeof(name) - 1, "SWG_OG_VR_IK_%s_WRIST_INSET_M", isLeft ? "LEFT" : "RIGHT");
		name[sizeof(name) - 1] = '\0';
		inset = envFloat(name, inset);
		return clampF(inset, 0.0f, 0.18f);
	}

	void applyAimRayOriginOffset(Transform &transform)
	{
		Vector const offset_l(
			envFloat("SWG_OG_VR_AIM_RAY_OFFSET_X_METERS", 0.0f),
			envFloat("SWG_OG_VR_AIM_RAY_OFFSET_Y_METERS", -0.10f),
			-envFloat("SWG_OG_VR_AIM_RAY_OFFSET_Z_METERS", 0.0f));
		transform.setPosition_p(transform.getPosition_p() + transform.rotate_l2p(offset_l));
	}

	// Linear interpolate between two floats.
	inline float lerpF(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	float computeArmIkTargetWeight(CreatureObject const &creature)
	{
		CreatureController const * const controller = dynamic_cast<CreatureController const *>(creature.getController());
		float const speed = controller ? fabsf(controller->getCurrentSpeed()) : 0.0f;
		float const startSpeed = envFloat("SWG_OG_VR_IK_MOVE_BLEND_START_SPEED", 0.10f);
		float const fullSpeed = envFloat("SWG_OG_VR_IK_MOVE_BLEND_FULL_SPEED", 2.00f);
		float const movingWeight = envFloat("SWG_OG_VR_IK_MOVING_WEIGHT", 0.80f);
		if (speed <= startSpeed)
			return 1.0f;
		if (speed >= fullSpeed)
			return movingWeight;

		float const t = (speed - startSpeed) / (fullSpeed - startSpeed);
		return lerpF(1.0f, movingWeight, t);
	}

	void updateArmIkWeight(CreatureObject const &creature)
	{
		DWORD const now = GetTickCount();
		if (s_lastWeightMilliseconds == 0)
			s_lastWeightMilliseconds = now;

		float const dt = clampF(static_cast<float>(now - s_lastWeightMilliseconds) * 0.001f, 0.0f, 0.10f);
		s_lastWeightMilliseconds = now;

		float const target = computeArmIkTargetWeight(creature);
		float const rate = target > s_frame.armIkWeight
			? envFloat("SWG_OG_VR_IK_STOP_REACQUIRE_RATE", 3.0f)
			: envFloat("SWG_OG_VR_IK_MOVE_RELEASE_RATE", 2.0f);
		float const step = rate * dt;
		if (s_frame.armIkWeight < target)
			s_frame.armIkWeight = clampF(s_frame.armIkWeight + step, s_frame.armIkWeight, target);
		else
			s_frame.armIkWeight = clampF(s_frame.armIkWeight - step, target, s_frame.armIkWeight);
	}

	// Build a Transform from K (forward/bone-direction) and J (up) vectors,
	// with the bone root at 'position'.
	// I is derived from J x K; J is re-orthogonalised if nearly parallel to K.
	Transform buildBoneTransform(Vector const &position, Vector const &kDir, Vector const &jHint)
	{
		Vector const k = safeNormalize(kDir, Vector(0.0f, 0.0f, 1.0f));

		// Re-orthogonalize j hint against k.
		Vector jOrtho = jHint - k * dot3(jHint, k);
		if (lengthSq(jOrtho) < 1e-6f)
		{
			// Degenerate: choose an arbitrary perpendicular.
			jOrtho = (fabsf(k.y) < 0.9f) ? Vector(0.0f, 1.0f, 0.0f) : Vector(1.0f, 0.0f, 0.0f);
			jOrtho = jOrtho - k * dot3(jOrtho, k);
		}
		Vector const j = safeNormalize(jOrtho, Vector(0.0f, 1.0f, 0.0f));
		Vector const i = cross3(j, k);

		Transform t;
		t.setLocalFrameIJK_p(i, j, k);
		t.setPosition_p(position);
		return t;
	}

	// Convert a VRPhysicsBridge Matrix4x4 to a Transform in the bridge's
	// recentered VR-local space.
	// Bridge layout: row-major, columns = basis vectors, m[3/7/11] = translation.
	Transform bridgeMatrixToTransform(SwgVrPhysics::Matrix4x4 const &m)
	{
		// Columns are basis vectors (i, j, k).
		Vector const i(m.m[0], m.m[4], m.m[8]);
		Vector const j(m.m[1], m.m[5], m.m[9]);
		Vector const k(m.m[2], m.m[6], m.m[10]);
		Vector const pos(m.m[3], m.m[7], m.m[11]);

		Transform t;
		t.setLocalFrameIJK_p(i, j, k);
		t.setPosition_p(pos);
		return t;
	}

	Vector vrLocalVectorToCameraLocal(Vector const &vector)
	{
		return Vector(vector.x, vector.y, -vector.z);
	}

	bool bridgeVrLocalToSwgWorldTransform(SwgVrPhysics::Matrix4x4 const &matrix, Transform &result)
	{
		Camera const * const camera = Game::getConstCamera();
		if (!camera)
			return false;

		Transform const vrLocal = bridgeMatrixToTransform(matrix);
		Transform const &cameraToWorld = camera->getTransform_o2w();
		result.setLocalFrameIJK_p(
			cameraToWorld.rotate_l2p(vrLocalVectorToCameraLocal(vrLocal.getLocalFrameI_p())),
			cameraToWorld.rotate_l2p(vrLocalVectorToCameraLocal(vrLocal.getLocalFrameJ_p())),
			cameraToWorld.rotate_l2p(vrLocalVectorToCameraLocal(vrLocal.getLocalFrameK_p())));
		result.setPosition_p(cameraToWorld.rotateTranslate_l2p(vrLocalVectorToCameraLocal(vrLocal.getPosition_p())));
		result.reorthonormalize();
		return true;
	}

	// -----------------------------------------------------------------------
	// Analytical Two-Bone IK
	//
	// Solves a 3-joint chain: root -> mid -> end.
	// Returns positions of root, mid, and end in the same space as the inputs.
	// poleHint: world-space direction toward which the mid joint should bend.
	// -----------------------------------------------------------------------

	void solveTwoBoneIK(
		Vector const &rootPos,
		Vector const &targetPos,
		float         upperLen,
		float         lowerLen,
		Vector const &poleHint,
		Vector       &outMidPos,
		Vector       &outEndPos)
	{
		float const totalLen = upperLen + lowerLen - 0.001f;
		Vector rawVec = targetPos - rootPos;
		float rawDist = length(rawVec);

		// Clamp target distance so the chain is always solvable.
		float const dist = clampF(rawDist, fabsf(upperLen - lowerLen) + 0.001f, totalLen);

		// Re-position target at clamped distance.
		Vector const dir = safeNormalize(rawVec, Vector(0.0f, 0.0f, 1.0f));
		outEndPos = rootPos + dir * dist;

		// Law of cosines: find angle at root between (root->end) and (root->mid).
		float cosRoot = (upperLen * upperLen + dist * dist - lowerLen * lowerLen) /
		                (2.0f * upperLen * dist);
		cosRoot = clampF(cosRoot, -1.0f, 1.0f);
		float const sinRoot = sqrtf(1.0f - cosRoot * cosRoot);

		// Compute perpendicular to dir in the plane defined by dir and poleHint.
		Vector polePerp = cross3(dir, cross3(poleHint, dir));
		if (lengthSq(polePerp) < 1e-6f)
		{
			// Fallback: use world up projected away from dir.
			Vector worldUp(0.0f, 1.0f, 0.0f);
			polePerp = cross3(dir, cross3(worldUp, dir));
		}
		polePerp = safeNormalize(polePerp, Vector(0.0f, 1.0f, 0.0f));

		// Rotate dir by rootAngle toward polePerp.
		Vector const midDir = dir * cosRoot + polePerp * sinRoot;
		outMidPos = rootPos + midDir * upperLen;
	}

	// -----------------------------------------------------------------------
	// Shoulder position estimation from HMD head pose
	// -----------------------------------------------------------------------

	void computeShoulderPositions(
		Transform const                       &headT,
		SwgVrPhysics::VrBodyCalibration const &cal,
		Vector                                &leftShoulder,
		Vector                                &rightShoulder)
	{
		Vector const headPos  = headT.getPosition_p();

		// Project head facing onto the horizontal plane (yaw only).
		Vector headFwd = headT.getLocalFrameK_p();
		headFwd.y = 0.0f;
		headFwd = safeNormalize(headFwd, Vector(0.0f, 0.0f, 1.0f));
		Vector const headRight = cross3(headFwd, Vector(0.0f, 1.0f, 0.0f));  // right = fwd x up

		float const halfWidth = cal.shoulderWidthMeters * 0.5f;
		float const drop      = cal.shoulderDropMeters;

		// Left shoulder: offset -X in head-facing space, down by drop.
		leftShoulder  = headPos - headRight * halfWidth - Vector(0.0f, drop, 0.0f);
		// Right shoulder: offset +X in head-facing space, down by drop.
		rightShoulder = headPos + headRight * halfWidth - Vector(0.0f, drop, 0.0f);
	}

	Transform characterAnchoredHeadTransform(CreatureObject const &creature, Transform const &headT, SwgVrPhysics::VrBodyCalibration const &cal)
	{
		if (!envFlag("SWG_OG_VR_IK_SEATED_HEAD_ANCHOR", true))
			return headT;

		Transform anchored(headT);
		Vector pos = anchored.getPosition_p();

		float const fallbackHeight = cal.bodyHeightMeters * envFloat("SWG_OG_VR_IK_HEAD_HEIGHT_BODY_FRACTION", 0.92f);
		float const characterEyeHeight = creature.getCameraHeight() > 0.1f ? creature.getCameraHeight() : fallbackHeight;
		pos.y = envFloat("SWG_OG_VR_IK_AVATAR_HEAD_HEIGHT_M", characterEyeHeight);

		float const xzClamp = envFloat("SWG_OG_VR_IK_HEAD_XZ_CLAMP_M", 0.12f);
		if (xzClamp >= 0.0f)
		{
			pos.x = clampF(pos.x, -xzClamp, xzClamp);
			pos.z = clampF(pos.z, -xzClamp, xzClamp);
		}

		anchored.setPosition_p(pos);
		return anchored;
	}

	// -----------------------------------------------------------------------
	// Solve one arm and write results into s_frame
	// -----------------------------------------------------------------------

	void solveArm(
		bool                                   isLeft,
		Vector const                          &shoulderPos,
		Transform const                       &handTargetT,
		Transform const                       &holdTargetT,
		SwgVrPhysics::VrBodyCalibration const &cal,
		float                                  triggerValue,
		float                                  gripValue,
		JointRole                              upperRole,
		JointRole                              lowerRole,
		JointRole                              handRole,
		JointRole                              skinHandRole)
	{
		float const armScale = isLeft ? cal.armLengthScaleLeft : cal.armLengthScaleRight;
		float upperLen = cal.upperArmLengthMeters * armScale;
		float lowerLen = cal.foreArmLengthMeters  * armScale;

		// Live VR contract: grip drives the rendered hand/wrist; aim remains for raycast/UI.
		Vector const rawWristTarget = handTargetT.getPosition_p();
		Vector const shoulderToTarget = rawWristTarget - shoulderPos;
		float const inset = wristInsetMeters(isLeft);
		Vector const wristTarget = length(shoulderToTarget) > inset + 0.01f
			? rawWristTarget - safeNormalize(shoulderToTarget, Vector(0.0f, 0.0f, 1.0f)) * inset
			: rawWristTarget;

		// Pole vector: elbows bend outward-and-down.
		// Left elbow goes to -X side, right elbow goes to +X side, both down.
		float const poleSign = isLeft ? -1.0f : 1.0f;
		Vector const poleHint = safeNormalize(
			Vector(poleSign, -1.0f, 0.0f), Vector(poleSign, 0.0f, 0.0f));

		Vector midPos, endPos;
		solveTwoBoneIK(shoulderPos, wristTarget, upperLen, lowerLen, poleHint, midPos, endPos);

		if (!envFlag("SWG_OG_VR_IK_HANDS_ONLY", false))
		{
			// Upper arm: shoulder -> elbow.
			// J hint: elbow->shoulder cross elbow->wrist (keeps roll stable).
			Vector const upperK = safeNormalize(midPos - shoulderPos, Vector(0.0f, 0.0f, 1.0f));
			Vector const jHintUpper = safeNormalize(cross3(upperK, endPos - midPos), Vector(0.0f, 1.0f, 0.0f));
			s_frame.transforms[upperRole] = buildBoneTransform(shoulderPos, upperK, jHintUpper);
			s_frame.valid[upperRole] = true;

			// Lower arm: elbow -> wrist.
			Vector const lowerK = safeNormalize(endPos - midPos, Vector(0.0f, 0.0f, 1.0f));
			s_frame.transforms[lowerRole] = buildBoneTransform(midPos, lowerK, jHintUpper);
			s_frame.valid[lowerRole] = true;
		}

		Transform wristSolvedT(handTargetT);
		wristSolvedT.setPosition_p(endPos);
		s_frame.transforms[handRole] = holdTargetT;
		s_frame.valid[handRole] = true;
		s_frame.transforms[skinHandRole] = handTargetT;
		s_frame.valid[skinHandRole] = true;
		UNREF(triggerValue);
		UNREF(gripValue);
	}

	// -----------------------------------------------------------------------
	// Solve spine lean and neck/head from HMD orientation
	// -----------------------------------------------------------------------

	void solveSpineAndHead(
		Transform const &headT,
		Transform const &headT_o,
		Transform const &creatureT)
	{
		// Extract creature yaw so we can express head-relative rotation.
		Vector creatureFwd = creatureT.getLocalFrameK_p();
		creatureFwd.y = 0.0f;
		creatureFwd = safeNormalize(creatureFwd, Vector(0.0f, 0.0f, 1.0f));
		Vector const creatureRight = cross3(creatureFwd, Vector(0.0f, 1.0f, 0.0f));

		// Head forward in world space.
		Vector headFwd = headT.getLocalFrameK_p();
		// Relative yaw (horizontal turn).
		float relYaw   = atan2f(dot3(headFwd, creatureRight), dot3(headFwd, creatureFwd));
		// Relative pitch (vertical look).
		float relPitch = -asinf(clampF(headFwd.y, -1.0f, 1.0f));

		// Clamp ranges so spine doesn't look insane.
		relYaw   = clampF(relYaw,   -1.40f,  1.40f);   // ~80 deg
		relPitch = clampF(relPitch, -0.60f,  0.60f);   // ~35 deg

		// Spine1 absorbs 40% of yaw and 25% of pitch.
		{
			Transform spineT = Transform::identity;
			spineT.yaw_l(relYaw   * 0.4f);
			spineT.pitch_l(relPitch * 0.25f);
			s_frame.transforms[JR_spine1] = spineT;
			s_frame.valid[JR_spine1] = true;
		}

		// Spine2 absorbs another 25% yaw and 25% pitch (if bone exists).
		{
			Transform spine2T = Transform::identity;
			spine2T.yaw_l(relYaw   * 0.25f);
			spine2T.pitch_l(relPitch * 0.25f);
			s_frame.transforms[JR_spine2] = spine2T;
			s_frame.valid[JR_spine2] = true;
		}

		// Neck absorbs 20% yaw and 25% pitch.
		{
			Transform neckT = Transform::identity;
			neckT.yaw_l(relYaw   * 0.20f);
			neckT.pitch_l(relPitch * 0.25f);
			s_frame.transforms[JR_neck] = neckT;
			s_frame.valid[JR_neck] = true;
		}

		// Head gets the remaining yaw and pitch.
		{
			Transform headBoneT = Transform::identity;
			headBoneT.yaw_l(relYaw   * 0.15f);
			headBoneT.pitch_l(relPitch * 0.25f);
			UNREF(headT_o);
			s_frame.transforms[JR_head] = headBoneT;
			s_frame.valid[JR_head] = true;
		}
	}

	// -----------------------------------------------------------------------
	// Bone name resolution helper
	// -----------------------------------------------------------------------

	char const *resolveFirstBone(Skeleton const &skeleton, char const * const candidates[], int *index_out)
	{
		if (index_out)
			*index_out = -1;

		for (int i = 0; candidates[i]; ++i)
		{
			TemporaryCrcString boneName(candidates[i], true);
			int transformIndex = -1;
			bool found = false;
			skeleton.findTransformIndex(boneName, &transformIndex, &found);
			if (found && transformIndex >= 0)
			{
				if (index_out)
					*index_out = transformIndex;
				return candidates[i];
			}
		}
		return 0;
	}

	bool shouldAttachModifierForRole(int role)
	{
		if (role == JR_l_hand || role == JR_r_hand ||
			role == JR_l_skinhand || role == JR_r_skinhand)
			return true;

		if (envFlag("SWG_OG_VR_IK_HANDS_ONLY", false))
			return false;

		return role == JR_l_upperarm || role == JR_l_forearm ||
			role == JR_r_upperarm || role == JR_r_forearm;
	}

	VrBodyTransformModifier::Mode modifierModeForRole(int role)
	{
		if (role == JR_l_upperarm || role == JR_r_upperarm)
			return envFlag("SWG_OG_VR_IK_ARM_POSITION_LOCK", false)
				? VrBodyTransformModifier::M_alignBoneAxisAndPosition
				: VrBodyTransformModifier::M_alignBoneAxis;

		if (role == JR_l_hand || role == JR_r_hand)
			return VrBodyTransformModifier::M_matchSolvedTransform;

		if (role == JR_l_skinhand || role == JR_r_skinhand)
			return VrBodyTransformModifier::M_matchSolvedTransform;

		return VrBodyTransformModifier::M_alignBoneAxis;
	}

	JointRole preferredChildRoleForAxis(int role)
	{
		switch (role)
		{
		case JR_l_upperarm:
			return JR_l_forearm;
		case JR_l_forearm:
			return s_bones.resolved[JR_l_skinhand] ? JR_l_skinhand : JR_l_hand;
		case JR_r_upperarm:
			return JR_r_forearm;
		case JR_r_forearm:
			return s_bones.resolved[JR_r_skinhand] ? JR_r_skinhand : JR_r_hand;
		default:
			return JR_COUNT;
		}
	}

	bool computeDriveAxis_l(Skeleton const &skeleton, int role, Vector &axis_l_out)
	{
		if (role < 0 || role >= JR_COUNT || !s_bones.resolved[role] || s_bones.indices[role] < 0)
			return false;

		int const boneIndex = s_bones.indices[role];

		int childIndex = -1;
		JointRole const childRole = preferredChildRoleForAxis(role);
		if (childRole != JR_COUNT && s_bones.resolved[childRole] && s_bones.indices[childRole] >= 0 &&
			skeleton.getParentTransformIndex(s_bones.indices[childRole]) == boneIndex)
		{
			childIndex = s_bones.indices[childRole];
		}
		else
		{
			for (int transformIndex = 0; transformIndex < skeleton.getTransformCount(); ++transformIndex)
			{
				if (skeleton.getParentTransformIndex(transformIndex) == boneIndex)
				{
					childIndex = transformIndex;
					break;
				}
			}
		}

		if (childIndex < 0)
			return false;

		Transform const * const jointToRoot = skeleton.getJointToRootTransformArray();
		if (!jointToRoot)
			return false;

		Transform const &boneT = jointToRoot[boneIndex];
		Transform const &childT = jointToRoot[childIndex];
		Vector axis_l = boneT.rotate_p2l(childT.getPosition_p() - boneT.getPosition_p());
		if (!axis_l.normalize())
			return false;

		axis_l_out = axis_l;
		return true;
	}

	// -----------------------------------------------------------------------
	// Heuristic lower-body (no foot trackers required)
	//
	// Approach:
	//   1. Pelvis position: average of shoulder offsets, at a fixed height
	//      below the head (using calibrated body height).
	//   2. Pelvis yaw: matches head yaw (no independent hip turn).
	//   3. Legs: simple analytical pose with knees bent slightly forward,
	//      ankles/feet estimated at floor level.
	//
	// Gated by SWG_OG_VR_FB_LEGS=1.  Results degrade gracefully when the
	// floor height cannot be estimated.
	// -----------------------------------------------------------------------

	void solveHeuristicLegs(
		Transform const                       &headT,
		SwgVrPhysics::VrBodyCalibration const &cal)
	{
		Vector const headPos = headT.getPosition_p();

		// Estimate floor height: head height minus body height.
		float const floorY    = headPos.y - cal.bodyHeightMeters;
		// Pelvis is roughly 55% of body height above the floor.
		float const pelvisY   = floorY + cal.bodyHeightMeters * 0.55f;
		// Pelvis XZ matches head XZ (no lag for MVP).
		Vector const pelvisPos(headPos.x, pelvisY, headPos.z);

		// Pelvis yaw from head yaw (projected to XZ).
		Vector headFwd = headT.getLocalFrameK_p();
		headFwd.y = 0.0f;
		headFwd = safeNormalize(headFwd, Vector(0.0f, 0.0f, 1.0f));
		Vector const pelvisRight = cross3(headFwd, Vector(0.0f, 1.0f, 0.0f));

		Transform pelvisT = Transform::identity;
		pelvisT.setLocalFrameIJK_p(
			pelvisRight,
			Vector(0.0f, 1.0f, 0.0f),
			headFwd);
		pelvisT.setPosition_p(pelvisPos);
		s_frame.transforms[JR_pelvis] = pelvisT;
		s_frame.valid[JR_pelvis] = true;

		// Leg length constants.
		float const thighLen  = cal.bodyHeightMeters * 0.24f;
		float const calfLen   = cal.bodyHeightMeters * 0.24f;
		float const hipOffset = cal.shoulderWidthMeters * 0.35f;  // hip half-width
		float const ankleY    = floorY + 0.05f;  // small clearance above floor

		for (int side = 0; side < 2; ++side)
		{
			float const sign     = (side == 0) ? -1.0f : 1.0f;
			JointRole thighRole  = (side == 0) ? JR_l_thigh : JR_r_thigh;
			JointRole calfRole   = (side == 0) ? JR_l_calf  : JR_r_calf;

			Vector hipPos = pelvisPos + pelvisRight * (sign * hipOffset);
			// Ankle target is directly below hip at floor level.
			Vector ankleTarget(hipPos.x, ankleY, hipPos.z);

			Vector kneePos, anklePos;
			// Pole vector: knees point slightly forward.
			Vector const kneePole = safeNormalize(headFwd + Vector(0.0f, -0.3f, 0.0f), headFwd);
			solveTwoBoneIK(hipPos, ankleTarget, thighLen, calfLen, kneePole, kneePos, anklePos);

			Vector const thighK = safeNormalize(kneePos - hipPos, Vector(0.0f, -1.0f, 0.0f));
			Vector const thighJ = safeNormalize(cross3(thighK, pelvisRight), Vector(0.0f, 0.0f, 1.0f));
			s_frame.transforms[thighRole] = buildBoneTransform(hipPos, thighK, thighJ);
			s_frame.valid[thighRole] = true;

			Vector const calfK = safeNormalize(anklePos - kneePos, Vector(0.0f, -1.0f, 0.0f));
			s_frame.transforms[calfRole] = buildBoneTransform(kneePos, calfK, thighJ);
			s_frame.valid[calfRole] = true;
		}
	}

} // namespace

// ======================================================================

bool VrBodyIK::isEnabled()
{
	if ((envFlag("SWG_OG_VR", false) || envFlag("SWG_D3D11_VR", false)) &&
		envFlag("SWG_OG_VR_DETACHED_HANDS", true) &&
		!envFlag("SWG_OG_VR_BODY_IK_FORCE", false))
	{
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------

bool VrBodyIK::isActive()
{
	return s_frame.frameActive;
}

// ----------------------------------------------------------------------

float VrBodyIK::getArmIkWeight()
{
	return s_frame.armIkWeight;
}

// ----------------------------------------------------------------------

bool VrBodyIK::getSolvedTransform(CrcString const &boneName, Transform &l2o_out)
{
	if (!s_frame.frameActive)
		return false;

	char const * const boneStr = boneName.getString();
	if (!boneStr)
		return false;

	for (int role = 0; role < JR_COUNT; ++role)
	{
		if (!s_frame.valid[role])
			continue;
		if (s_bones.resolved[role] &&
		    _stricmp(s_bones.names[role], boneStr) == 0)
		{
			l2o_out = s_frame.transforms[role];
			return true;
		}
	}
	return false;
}

// ----------------------------------------------------------------------

void VrBodyIK::update(CreatureObject &creature)
{
	s_frame.frameActive = false;

	if (!isEnabled())
		return;

	if (!s_skeletonOverlayConfigured)
	{
		bool const showSkeletonOverlay = envFlag("SWG_OG_VR_BODY_IK_SHOW_SKELETON", false) || envFlag("SWG_OG_VR_IK_SHOW_SKELETON", false);
		SkeletalAppearance2::setShowSkeleton(showSkeletonOverlay);
		s_skeletonOverlayConfigured = true;
		traceLine(showSkeletonOverlay ? "SWGVRBodyIK skeleton debug overlay enabled" : "SWGVRBodyIK skeleton debug overlay disabled");
	}

	VrBodyBridgeApi const &api = getBodyBridgeApi();
	if (!api.ready())
	{
		traceRateLimited("SWGVRBodyIK waiting bridge exports getBodyState/getHandState");
		return;
	}
	if (!s_bridgeReadyLogged)
	{
		traceLine("SWGVRBodyIK bridge ready");
		s_bridgeReadyLogged = true;
	}

	SwgVrPhysics::BodyState bodyState{};
	if (!api.getBodyState(&bodyState))
	{
		traceRateLimited("SWGVRBodyIK getBodyState returned false");
		return;
	}
	if (!(bodyState.flags & SwgVrPhysics::BodyFlagHeadValid))
	{
		traceRateLimited("SWGVRBodyIK waiting for head-valid body state");
		return;
	}

	SwgVrPhysics::HandState leftState{}, rightState{};
	bool const leftOk  = api.getHandState(SwgVrPhysics::Hand_Left,  &leftState)
	                     && (leftState.flags  & SwgVrPhysics::MatrixFlagGripValid);
	bool const rightOk = api.getHandState(SwgVrPhysics::Hand_Right, &rightState)
	                     && (rightState.flags & SwgVrPhysics::MatrixFlagGripValid);
	bool const leftAimOk  = leftOk  && (leftState.flags  & SwgVrPhysics::MatrixFlagAimValid);
	bool const rightAimOk = rightOk && (rightState.flags & SwgVrPhysics::MatrixFlagAimValid);

	if (!leftOk && !rightOk)
	{
		traceRateLimited("SWGVRBodyIK waiting for at least one grip-valid hand");
		return;
	}

	// Attach modifiers before solving so bind offsets are available to this frame.
	ensureModifiersAttached(creature);

	// Invalidate solved results.
	for (int i = 0; i < JR_COUNT; ++i)
		s_frame.valid[i] = false;

	updateArmIkWeight(creature);

	SwgVrPhysics::VrBodyCalibration const &cal = bodyState.calibration;

	// Creature object transform for body-space work.
	Transform const &creatureT = creature.getTransform_o2w();

	// HMD/controller transforms arrive from the bridge in recentered VR-local
	// space. Convert them through the same camera/world path used by the proven
	// wand/raycast code before writing skeleton object-space modifiers.
	Transform headT_w;
	if (!bridgeVrLocalToSwgWorldTransform(bodyState.headFromWorld, headT_w))
	{
		traceRateLimited("SWGVRBodyIK waiting for camera before pose conversion");
		return;
	}
	Transform const headT = creatureT.rotateTranslate_p2l(headT_w);
	Transform const anchoredHeadT = characterAnchoredHeadTransform(creature, headT, cal);

	// Solve spine and head first (needs creature transform for yaw offset).
	solveSpineAndHead(headT_w, headT, creatureT);

	// Shoulder positions from HMD head.
	Vector leftShoulder, rightShoulder;
	computeShoulderPositions(anchoredHeadT, cal, leftShoulder, rightShoulder);

	if (leftOk)
	{
		Transform leftGripT_w;
		if (!bridgeVrLocalToSwgWorldTransform(leftState.gripFromWorld, leftGripT_w))
			return;
		Transform leftAimT_w;
		if (leftAimOk && !bridgeVrLocalToSwgWorldTransform(leftState.aimFromWorld, leftAimT_w))
			return;
		Transform leftTargetT_w(leftGripT_w);
		if (leftAimOk && envFlag("SWG_OG_VR_IK_USE_AIM_POSITION", false))
			leftTargetT_w.setPosition_p(leftAimT_w.getPosition_p());
		Transform const leftTargetT = applyWristOrientationCalibration(true, creatureT.rotateTranslate_p2l(leftTargetT_w));
		solveArm(true, leftShoulder, leftTargetT, leftTargetT, cal, leftState.triggerValue, leftState.gripValue, JR_l_upperarm, JR_l_forearm, JR_l_hand, JR_l_skinhand);
		traceHandProof("left", leftTargetT_w, JR_l_skinhand, creatureT);
	}

	if (rightOk)
	{
		Transform rightGripT_w;
		if (!bridgeVrLocalToSwgWorldTransform(rightState.gripFromWorld, rightGripT_w))
			return;
		Transform rightAimT_w;
		if (rightAimOk && !bridgeVrLocalToSwgWorldTransform(rightState.aimFromWorld, rightAimT_w))
			return;
		Transform rightTargetT_w(rightGripT_w);
		if (rightAimOk && envFlag("SWG_OG_VR_IK_USE_AIM_POSITION", false))
			rightTargetT_w.setPosition_p(rightAimT_w.getPosition_p());
		if (rightAimOk && envFlag("SWG_OG_VR_IK_RIGHT_HAND_USES_AIM_POSE", true))
			rightTargetT_w = rightAimT_w;
		Transform const rightTargetT = applyWristOrientationCalibration(false, creatureT.rotateTranslate_p2l(rightTargetT_w));
		Transform rightHoldTargetT_w(rightTargetT_w);
		if (rightAimOk && envFlag("SWG_OG_VR_IK_RIGHT_HOLD_USES_AIM_RAY_ORIGIN", true))
		{
			rightHoldTargetT_w = rightAimT_w;
		}
		Transform const rightHoldTargetT = applyHoldOrientationCalibration(false, creatureT.rotateTranslate_p2l(rightHoldTargetT_w));
		solveArm(false, rightShoulder, rightTargetT, rightHoldTargetT, cal, rightState.triggerValue, rightState.gripValue, JR_r_upperarm, JR_r_forearm, JR_r_hand, JR_r_skinhand);
		traceHandProof("right", rightTargetT_w, JR_r_skinhand, creatureT);
	}

	// Optional heuristic legs (requires SWG_OG_VR_FB_LEGS=1).
	if (envFlag("SWG_OG_VR_FB_LEGS", false))
		solveHeuristicLegs(anchoredHeadT, cal);

	s_frame.frameActive = true;

	{
		char line[512];
		Vector const headPos = headT_w.getPosition_p();
		_snprintf(line, sizeof(line) - 1,
			"SWGVRBodyIK solved validRoles=%d resolvedBones=%d left=%s right=%s armIkWeight=%.3f head=(%.3f,%.3f,%.3f) shoulderWidth=%.3f upper=%.3f fore=%.3f",
			countValidSolvedRoles(),
			countResolvedBones(),
			leftOk ? "true" : "false",
			rightOk ? "true" : "false",
			s_frame.armIkWeight,
			headPos.x, headPos.y, headPos.z,
			cal.shoulderWidthMeters,
			cal.upperArmLengthMeters,
			cal.foreArmLengthMeters);
		line[sizeof(line) - 1] = '\0';
		traceRateLimited(line);
	}
}

// ----------------------------------------------------------------------

void VrBodyIK::ensureModifiersAttached(CreatureObject &creature)
{
	if (s_modifiersAttached)
		return;

	Appearance * const app = creature.getAppearance();
	if (!app)
	{
		traceRateLimited("SWGVRBodyIK attach waiting: creature has no appearance");
		return;
	}

	SkeletalAppearance2 * const skelApp = dynamic_cast<SkeletalAppearance2 *>(app);
	if (!skelApp)
	{
		traceRateLimited("SWGVRBodyIK attach waiting: appearance is not SkeletalAppearance2");
		return;
	}

	Skeleton const * const skeleton = skelApp->getDisplayLodSkeleton();
	if (!skeleton)
	{
		traceRateLimited("SWGVRBodyIK attach waiting: display LOD skeleton not ready");
		return;
	}

	{
		char line[256];
		_snprintf(line, sizeof(line) - 1, "SWGVRBodyIK attach skeleton transformCount=%d", skeleton->getTransformCount());
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
	}

	for (int transformIndex = 0; transformIndex < skeleton->getTransformCount(); ++transformIndex)
	{
		CrcString const &transformName = skeleton->getTransformName(transformIndex);
		char const * const transformNameString = transformName.getString();
		char line[256];
		_snprintf(line, sizeof(line) - 1, "SWGVRBodyIK skeletonTransform index=%d name=%s", transformIndex, transformNameString ? transformNameString : "<null>");
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
	}

	// Resolve bone names for this skeleton.
	s_bones.attachAttempted = true;
	for (int role = 0; role < JR_COUNT; ++role)
	{
		int resolvedIndex = -1;
		char const *resolved = resolveFirstBone(*skeleton, s_boneCandidates[role], &resolvedIndex);
		if (resolved && resolved[0])
		{
			strncpy(s_bones.names[role], resolved, sizeof(s_bones.names[role]) - 1);
			s_bones.names[role][sizeof(s_bones.names[role]) - 1] = '\0';
			s_bones.indices[role] = resolvedIndex;
			s_bones.resolved[role] = true;
		}
		else
		{
			s_bones.names[role][0] = '\0';
			s_bones.indices[role] = -1;
			s_bones.resolved[role] = false;
		}
	}

	for (int role = 0; role < JR_COUNT; ++role)
	{
		if (!shouldAttachModifierForRole(role))
			continue;

		if (s_bones.resolved[role])
		{
			Vector driveAxis_l = Vector::unitX;
			VrBodyTransformModifier::Mode const mode = modifierModeForRole(role);
			if ((mode == VrBodyTransformModifier::M_alignBoneAxis || mode == VrBodyTransformModifier::M_alignBoneAxisAndPosition) &&
				!computeDriveAxis_l(*skeleton, role, driveAxis_l))
			{
				char line[256];
				_snprintf(line, sizeof(line) - 1, "SWGVRBodyIK skipped role=%d bone=%s: no direct child axis",
					role, s_bones.names[role]);
				line[sizeof(line) - 1] = '\0';
				traceLine(line);
				continue;
			}
			s_bones.driveAxis_l[role] = driveAxis_l;

			// Attach a modifier for this bone.
			TemporaryCrcString nameCrc(s_bones.names[role], true);
			skelApp->addTransformModifierTakeOwnership(
				nameCrc,
				new VrBodyTransformModifier(s_bones.driveAxis_l[role], mode));

			{
				char line[256];
				_snprintf(line, sizeof(line) - 1, "SWGVRBodyIK attached role=%d bone=%s mode=%d axis_l=(%.5f,%.5f,%.5f)",
					role,
					s_bones.names[role],
					static_cast<int>(mode),
					s_bones.driveAxis_l[role].x,
					s_bones.driveAxis_l[role].y,
					s_bones.driveAxis_l[role].z);
				line[sizeof(line) - 1] = '\0';
				traceLine(line);
			}
		}
		else
		{
			char line[256];
			_snprintf(line, sizeof(line) - 1, "SWGVRBodyIK missing role=%d candidates=%s/%s/%s",
				role,
				s_boneCandidates[role][0] ? s_boneCandidates[role][0] : "<null>",
				s_boneCandidates[role][1] ? s_boneCandidates[role][1] : "<null>",
				s_boneCandidates[role][2] ? s_boneCandidates[role][2] : "<null>");
			line[sizeof(line) - 1] = '\0';
			traceLine(line);
		}
	}

	s_modifiersAttached = true;
	if (!s_modifiersLogged)
	{
		char line[256];
		_snprintf(line, sizeof(line) - 1, "SWGVRBodyIK modifiers attached resolvedBones=%d", countResolvedBones());
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
		s_modifiersLogged = true;
	}
}

// ----------------------------------------------------------------------

void VrBodyIK::markModifiersDirty()
{
	if (!s_modifiersAttached)
		return;

	s_modifiersAttached = false;
	s_modifiersLogged = false;
	for (int i = 0; i < JR_COUNT; ++i)
	{
		s_bones.resolved[i] = false;
		s_bones.indices[i] = -1;
		s_bones.driveAxis_l[i] = Vector::unitX;
	}

	traceLine("SWGVRBodyIK modifiers dirtied by appearance clear");
}

// ----------------------------------------------------------------------

void VrBodyIK::detachModifiers(CreatureObject &creature)
{
	if (!s_modifiersAttached)
		return;

	Appearance * const app = creature.getAppearance();
	if (app)
	{
		SkeletalAppearance2 * const skelApp = dynamic_cast<SkeletalAppearance2 *>(app);
		if (skelApp)
			skelApp->clearAllTransformModifiers();
	}

	s_modifiersAttached = false;
	for (int i = 0; i < JR_COUNT; ++i)
	{
		s_bones.resolved[i] = false;
		s_bones.indices[i] = -1;
		s_bones.driveAxis_l[i] = Vector::unitX;
	}
}

// ----------------------------------------------------------------------

void VrBodyIK::recalibrate(CreatureObject &/*creature*/)
{
	// On next frame, the default calibration from the bridge will be used.
	// A future enhancement can capture T-pose and compute per-user arm lengths.
	// For now just reset the per-user scales.
	s_calibration.cal = defaultBodyCalibration();
	s_calibration.calibrated = false;
}

// ----------------------------------------------------------------------

void VrBodyIK::reset()
{
	s_frame      = SolvedFrame();
	s_calibration = RuntimeCalibration();
	s_bones      = ResolvedBones();
	s_modifiersAttached = false;
	s_modifiersLogged = false;
	s_bridgeReadyLogged = false;
	s_lastTraceMilliseconds = 0;
	s_lastHandProofTraceMilliseconds[0] = 0;
	s_lastHandProofTraceMilliseconds[1] = 0;
	s_lastWeightMilliseconds = 0;
	s_skeletonOverlayConfigured = false;
}

// ======================================================================

#endif // SWG_VR_PHYSICS_CLIENTOBJECT_HOOK
