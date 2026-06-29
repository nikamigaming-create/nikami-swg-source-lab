// ======================================================================
//
// VrBodyTransformModifier.cpp
//
// ======================================================================

#include "clientGame/FirstClientGame.h"
#include "clientGame/VrBodyTransformModifier.h"

#if defined(SWG_VR_PHYSICS_CLIENTOBJECT_HOOK)

#include "clientGame/VrBodyIK.h"
#include "sharedFoundation/CrcString.h"
#include "sharedMath/Quaternion.h"
#include "sharedMath/Transform.h"
#include "sharedMath/Vector.h"

#include <string.h>
#include <windows.h>

// ======================================================================

namespace
{
	bool traceEnabled()
	{
		char buf[32];
		DWORD len = GetEnvironmentVariableA("SWG_OG_VR_HAND_TRACE", buf, sizeof(buf));
		if (len > 0 && len < sizeof(buf) && (_stricmp(buf, "1") == 0 || _stricmp(buf, "true") == 0 || _stricmp(buf, "yes") == 0))
			return true;

		len = GetEnvironmentVariableA("SWG_OG_VR_PHYSICS_TRACE", buf, sizeof(buf));
		return len > 0 && len < sizeof(buf) && (_stricmp(buf, "1") == 0 || _stricmp(buf, "true") == 0 || _stricmp(buf, "yes") == 0);
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

	void traceAppliedTransform(CrcString const &transformName, Transform const &transform_l2o)
	{
		char const * const name = transformName.getString();
		if (!name)
			return;

		int handIndex = -1;
		if (_stricmp(name, "hold_l") == 0 || _stricmp(name, "l_hand") == 0 || _stricmp(name, "lhand") == 0 || _stricmp(name, "lwrist") == 0)
			handIndex = 0;
		else if (_stricmp(name, "hold_r") == 0 || _stricmp(name, "r_hand") == 0 || _stricmp(name, "rhand") == 0 || _stricmp(name, "rwrist") == 0)
			handIndex = 1;
		else
			return;

		static DWORD s_lastApplyTraceMilliseconds[2] = { 0, 0 };
		DWORD const now = GetTickCount();
		if (now - s_lastApplyTraceMilliseconds[handIndex] < 500)
			return;

		s_lastApplyTraceMilliseconds[handIndex] = now;

		Vector const position = transform_l2o.getPosition_p();
		char line[512];
		_snprintf(line, sizeof(line) - 1,
			"SWGVRBodyIKProof kind=modifier hand=%s bone=%s appliedHold_o=(%.5f,%.5f,%.5f) i=(%.5f,%.5f,%.5f) j=(%.5f,%.5f,%.5f) k=(%.5f,%.5f,%.5f)",
			handIndex == 0 ? "left" : "right",
			name,
			position.x, position.y, position.z,
			transform_l2o.getLocalFrameI_p().x, transform_l2o.getLocalFrameI_p().y, transform_l2o.getLocalFrameI_p().z,
			transform_l2o.getLocalFrameJ_p().x, transform_l2o.getLocalFrameJ_p().y, transform_l2o.getLocalFrameJ_p().z,
			transform_l2o.getLocalFrameK_p().x, transform_l2o.getLocalFrameK_p().y, transform_l2o.getLocalFrameK_p().z);
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
	}

	void traceModifierCallback(CrcString const &transformName, bool modified)
	{
		if (!traceEnabled())
			return;

		char const * const name = transformName.getString();
		if (!name)
			return;

		static DWORD s_lastCallbackTraceMilliseconds = 0;
		DWORD const now = GetTickCount();
		if (now - s_lastCallbackTraceMilliseconds < 500)
			return;

		s_lastCallbackTraceMilliseconds = now;

		char line[256];
		_snprintf(line, sizeof(line) - 1,
			"SWGVRBodyIKProof kind=modifierCallback bone=%s modified=%s",
			name,
			modified ? "true" : "false");
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
	}

	Vector safeNormalize(Vector const &value, Vector const &fallback)
	{
		Vector result = value;
		if (!result.normalize())
			return fallback;
		return result;
	}

	float clampF(float value, float lo, float hi)
	{
		return value < lo ? lo : (value > hi ? hi : value);
	}

	float envFloat(char const *name, float defaultValue)
	{
		char buf[32];
		DWORD len = GetEnvironmentVariableA(name, buf, sizeof(buf));
		if (!len || len >= sizeof(buf))
			return defaultValue;
		return static_cast<float>(atof(buf));
	}

	Vector lerpVector(Vector const &a, Vector const &b, float t)
	{
		return a + (b - a) * t;
	}

	bool rotateExistingJointToward(Transform &transform_l2o, Vector const &alignedBoneAxis_l, Vector const &desiredDirection_o, float weight)
	{
		if (weight <= 0.001f)
			return false;

		Vector desired = safeNormalize(desiredDirection_o, Vector::unitX);
		Vector const localAxis = safeNormalize(alignedBoneAxis_l, Vector::unitX);
		Vector desired_l = safeNormalize(transform_l2o.rotate_p2l(desired), localAxis);

		float const cosAngle = clampF(localAxis.dot(desired_l), -1.0f, 1.0f);
		if (cosAngle > 0.9995f)
			return false;

		Vector rotationAxis_l = localAxis.cross(desired_l);
		if (!rotationAxis_l.normalize())
			return false;

		float const angle = acosf(cosAngle) * clampF(weight, 0.0f, 1.0f);
		Quaternion const rotation_l(angle, rotationAxis_l);
		Transform rotationTransform;
		rotation_l.getTransformPreserveTranslation(&rotationTransform);

		Vector const oldPosition_o = transform_l2o.getPosition_p();
		transform_l2o.multiply(transform_l2o, rotationTransform);
		transform_l2o.setPosition_p(oldPosition_o);
		return true;
	}
}

VrBodyTransformModifier::VrBodyTransformModifier(Vector const &alignedBoneAxis_l, Mode mode)
	: TransformModifier(),
	  m_alignedBoneAxis_l(safeNormalize(alignedBoneAxis_l, Vector::unitX)),
	  m_mode(mode)
{
}

VrBodyTransformModifier::~VrBodyTransformModifier()
{
}

// ----------------------------------------------------------------------

bool VrBodyTransformModifier::modifyTransform(
	float           /*elapsedTime*/,
	Skeleton const  &/*skeleton*/,
	CrcString const &transformName,
	Transform const &transform_p2o,
	Transform const &transform_l2p,
	Transform       &transform_l2o)
{
	Transform solved_l2o;
	bool const hasSolved = VrBodyIK::getSolvedTransform(transformName, solved_l2o);
	if (hasSolved)
		transform_l2o.multiply(transform_p2o, transform_l2p);

	bool modified = false;
	if (hasSolved)
	{
		float const armIkWeight = clampF(VrBodyIK::getArmIkWeight(), 0.0f, 1.0f);
		if (armIkWeight <= 0.001f)
		{
			traceModifierCallback(transformName, false);
			return false;
		}

		if (m_mode == M_matchSolvedTransform)
		{
			float const handWeight = clampF(envFloat("SWG_OG_VR_IK_HAND_MOVE_WEIGHT", 0.95f), armIkWeight, 1.0f);
			if (handWeight >= 0.999f)
			{
				transform_l2o = solved_l2o;
			}
			else
			{
				transform_l2o.setLocalFrameIJK_p(
					solved_l2o.getLocalFrameI_p(),
					solved_l2o.getLocalFrameJ_p(),
					solved_l2o.getLocalFrameK_p());
				transform_l2o.setPosition_p(lerpVector(transform_l2o.getPosition_p(), solved_l2o.getPosition_p(), handWeight));
			}
			modified = true;
			traceModifierCallback(transformName, modified);
			traceAppliedTransform(transformName, transform_l2o);
			return modified;
		}

		if (m_mode == M_matchSolvedOrientation)
		{
			Vector const oldPosition_o = transform_l2o.getPosition_p();
			transform_l2o.setLocalFrameIJK_p(
				solved_l2o.getLocalFrameI_p(),
				solved_l2o.getLocalFrameJ_p(),
				solved_l2o.getLocalFrameK_p());
			transform_l2o.setPosition_p(oldPosition_o);
			modified = true;
		}
		else if (m_mode == M_matchSolvedPosition)
		{
			transform_l2o.setPosition_p(lerpVector(transform_l2o.getPosition_p(), solved_l2o.getPosition_p(), armIkWeight));
			modified = true;
		}
		else if (m_mode == M_applyLocalDelta)
		{
			Transform localDelta_l2p(solved_l2o);
			localDelta_l2p.setPosition_p(Vector::zero);
			Transform base_l2o(transform_l2o);
			transform_l2o.multiply(base_l2o, localDelta_l2p);
			transform_l2o.setPosition_p(base_l2o.getPosition_p());
			modified = true;
		}
		else
		{
			float const forearmWeight = (m_mode == M_alignBoneAxisAndPosition)
				? clampF(envFloat("SWG_OG_VR_IK_FOREARM_MOVE_WEIGHT", 0.95f), armIkWeight, 1.0f)
				: armIkWeight;
			float const rotationWeight = forearmWeight * clampF(envFloat("SWG_OG_VR_IK_LIMB_ROTATION_WEIGHT", 0.75f), 0.0f, 1.0f);
			modified = rotateExistingJointToward(transform_l2o, m_alignedBoneAxis_l, solved_l2o.getLocalFrameK_p(), rotationWeight);
			if (m_mode == M_alignBoneAxisAndPosition)
			{
				transform_l2o.setPosition_p(lerpVector(transform_l2o.getPosition_p(), solved_l2o.getPosition_p(), forearmWeight));
				modified = true;
			}
		}
	}
	traceModifierCallback(transformName, modified);
	if (modified)
		traceAppliedTransform(transformName, transform_l2o);
	return modified;
}

// ======================================================================

#endif // SWG_VR_PHYSICS_CLIENTOBJECT_HOOK
