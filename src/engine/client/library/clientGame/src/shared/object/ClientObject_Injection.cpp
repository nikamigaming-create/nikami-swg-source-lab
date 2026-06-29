// ======================================================================
//
// ClientObject_Injection.cpp
//
// Optional VR physics visual override for ClientObject::alter. Compile this
// file into clientGame and define SWG_VR_PHYSICS_CLIENTOBJECT_HOOK to let the
// DX11/OpenXR bridge drive grabbed object transforms locally.
//
// ======================================================================

#include "clientGame/FirstClientGame.h"

#if defined(SWG_VR_PHYSICS_CLIENTOBJECT_HOOK)

#include "clientGame/ClientObject.h"
#include "clientGame/ClientWorld.h"
#include "clientGame/Game.h"
#include "clientGame/TangibleObject.h"
#include "clientGraphics/Camera.h"
#include "sharedCollision/CollideParameters.h"
#include "sharedCollision/CollisionInfo.h"
#include "sharedFoundation/NetworkId.h"
#include "sharedGame/GameObjectTypes.h"
#include "sharedGame/SharedObjectTemplate.h"
#include "sharedMath/Sphere.h"
#include "sharedMath/Transform.h"
#include "sharedMath/Vector.h"

#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define VRPHYSICSBRIDGE_NO_IMPORT
#include "../../../../../application/Direct3d11/src/win32/VRPhysicsBridge.h"
#undef VRPHYSICSBRIDGE_NO_IMPORT

namespace
{
	typedef uint32_t (*BridgeAbiVersionFunction)();
	typedef bool (*GetHandStateFunction)(uint32_t, SwgVrPhysics::HandState *);
	typedef bool (*GetAimRayFunction)(uint32_t, SwgVrPhysics::Ray *);
	typedef bool (*BeginGrabFunction)(uint64_t, uint32_t, SwgVrPhysics::Matrix4x4 const *, SwgVrPhysics::Vector3 const *);
	typedef bool (*EndGrabFunction)(uint64_t, SwgVrPhysics::ReleasedObjectState *);
	typedef bool (*GetGrabbedObjectStateFunction)(uint64_t, SwgVrPhysics::GrabbedObjectState *);

	struct BridgeApi
	{
		BridgeApi() :
			lastResolveAttemptMilliseconds(0),
			abiVersion(0),
			getHandState(0),
			getAimRay(0),
			beginGrab(0),
			endGrab(0),
			getGrabbedObjectState(0)
		{
		}

		DWORD lastResolveAttemptMilliseconds;
		BridgeAbiVersionFunction abiVersion;
		GetHandStateFunction getHandState;
		GetAimRayFunction getAimRay;
		BeginGrabFunction beginGrab;
		EndGrabFunction endGrab;
		GetGrabbedObjectStateFunction getGrabbedObjectState;
	};

	uint64_t s_activeGrabNetworkId[SwgVrPhysics::Hand_Count] = {0, 0};
	uint64_t s_activePullNetworkId[SwgVrPhysics::Hand_Count] = {0, 0};
	uint64_t s_openPullCandidateNetworkId[SwgVrPhysics::Hand_Count] = {0, 0};
	double s_openPullCandidateStartSeconds[SwgVrPhysics::Hand_Count] = {0.0, 0.0};
	double s_activePullLastUpdateSeconds[SwgVrPhysics::Hand_Count] = {0.0, 0.0};
	DWORD s_lastTraceMilliseconds = 0;

	bool environmentEnabled(char const *name)
	{
		char value[32];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return false;

		return _stricmp(value, "1") == 0 ||
			_stricmp(value, "true") == 0 ||
			_stricmp(value, "yes") == 0 ||
			_stricmp(value, "on") == 0;
	}

	bool environmentEnabledDefault(char const *name, bool defaultValue)
	{
		char value[32];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return defaultValue;

		return _stricmp(value, "1") == 0 ||
			_stricmp(value, "true") == 0 ||
			_stricmp(value, "yes") == 0 ||
			_stricmp(value, "on") == 0;
	}

	bool traceEnabled()
	{
		return environmentEnabled("SWG_OG_VR_PHYSICS_TRACE");
	}

	void traceLine(char const *line)
	{
		if (!line || !traceEnabled())
			return;

		OutputDebugStringA(line);
		OutputDebugStringA("\n");

		char path[MAX_PATH];
		DWORD const length = GetEnvironmentVariableA("SWG_OG_VR_PHYSICS_TRACE_FILE", path, sizeof(path));
		if (length == 0 || length >= sizeof(path))
			return;

		HANDLE const file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
		if (file == INVALID_HANDLE_VALUE)
			return;

		DWORD written = 0;
		DWORD const lineLength = static_cast<DWORD>(std::strlen(line));
		(void)WriteFile(file, line, lineLength, &written, 0);
		(void)WriteFile(file, "\r\n", 2, &written, 0);
		CloseHandle(file);
	}

	void traceGrabCandidate(ClientObject const &object, uint32_t hand, char const *result, char const *reason, float alongRay, float missDistanceSquared, float radius)
	{
		DWORD const now = GetTickCount();
		if (now - s_lastTraceMilliseconds < 100)
			return;

		s_lastTraceMilliseconds = now;
		char const * const templateName = object.getObjectTemplateName() ? object.getObjectTemplateName() : "";
		char line[1024];
		_snprintf(line, sizeof(line) - 1,
			"SWGVRPhysics grabCandidate hand=%u result=%s reason=%s networkId=%I64u template=%s radius=%.3f volume=%d complexity=%.3f children=%d alongRay=%.3f miss2=%.3f",
			hand,
			result ? result : "",
			reason ? reason : "",
			static_cast<unsigned __int64>(object.getNetworkId().getValue()),
			templateName,
			radius,
			object.getVolume(),
			object.getComplexity(),
			object.getNumberOfChildObjects(),
			alongRay,
			missDistanceSquared);
		line[sizeof(line) - 1] = '\0';
		traceLine(line);
	}

	bool containsPathToken(char const * const path, char const * const token)
	{
		return path != 0 && token != 0 && std::strstr(path, token) != 0;
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
		if (!api.getGrabbedObjectState)
		{
			DWORD const now = GetTickCount();
			if (api.lastResolveAttemptMilliseconds != 0 && now - api.lastResolveAttemptMilliseconds < 1000)
				return api;

			api.lastResolveAttemptMilliseconds = now;
			api.abiVersion = reinterpret_cast<BridgeAbiVersionFunction>(findBridgeExport("SWGVRPhysics_BridgeAbiVersion"));
			api.getHandState = reinterpret_cast<GetHandStateFunction>(findBridgeExport("SWGVRPhysics_GetHandState"));
			api.getAimRay = reinterpret_cast<GetAimRayFunction>(findBridgeExport("SWGVRPhysics_GetAimRay"));
			api.beginGrab = reinterpret_cast<BeginGrabFunction>(findBridgeExport("SWGVRPhysics_BeginGrab"));
			api.endGrab = reinterpret_cast<EndGrabFunction>(findBridgeExport("SWGVRPhysics_EndGrab"));
			api.getGrabbedObjectState = reinterpret_cast<GetGrabbedObjectStateFunction>(findBridgeExport("SWGVRPhysics_GetGrabbedObjectState"));

			if (!api.abiVersion || api.abiVersion() != SwgVrPhysics::BridgeAbiVersion)
			{
				api.getHandState = 0;
				api.getAimRay = 0;
				api.beginGrab = 0;
				api.endGrab = 0;
				api.getGrabbedObjectState = 0;
			}
		}

		return api;
	}

	float getEnvironmentFloat(char const *name, float defaultValue)
	{
		char value[64];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return defaultValue;

		return static_cast<float>(atof(value));
	}

	char const *getEnvironmentString(char const *name, char const *defaultValue)
	{
		static char value[260];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return defaultValue;

		return value;
	}

	bool isBlockedTemplateFamily(ClientObject const &object)
	{
		char const * const templateName = object.getObjectTemplateName();
		if (templateName == 0)
			return true;

		// Keep first-pass VR manipulation to small tangible props. These
		// families are either static world structure, actors, vehicles, or
		// gameplay controllers where a local transform override is unsafe.
		static char const * const blockedTokens[] =
		{
			"object/building/",
			"object/cell/",
			"object/creature/",
			"object/installation/",
			"object/intangible/",
			"object/mission/",
			"object/player/",
			"object/ship/",
			"object/static/",
			"object/terrain/",
			"object/tangible/beta/",
			"object/tangible/deed/",
			"object/tangible/dungeon/",
			"object/tangible/furniture/",
			"object/tangible/terminal/",
			"object/tangible/travel/",
			"object/tangible/vendor/",
			"object/universe/"
		};

		for (size_t i = 0; i != sizeof(blockedTokens) / sizeof(blockedTokens[0]); ++i)
		{
			if (containsPathToken(templateName, blockedTokens[i]))
				return true;
		}

		return false;
	}

	bool isBlockedGameObjectType(ClientObject const &object)
	{
		int const gameObjectType = object.getGameObjectType();
		static int const blockedTypes[] =
		{
			SharedObjectTemplate::GOT_misc_container,
			SharedObjectTemplate::GOT_misc_container_wearable,
			SharedObjectTemplate::GOT_misc_container_ship_loot,
			SharedObjectTemplate::GOT_misc_container_public,
			SharedObjectTemplate::GOT_misc_crafting_station,
			SharedObjectTemplate::GOT_misc_factory_crate,
			SharedObjectTemplate::GOT_misc_furniture,
			SharedObjectTemplate::GOT_misc_sign,
			SharedObjectTemplate::GOT_misc_pob_ship_pilot_chair,
			SharedObjectTemplate::GOT_misc_operations_chair,
			SharedObjectTemplate::GOT_misc_turret_access_ladder,
			SharedObjectTemplate::GOT_misc_ground_target,
			SharedObjectTemplate::GOT_misc_appearance_only,
			SharedObjectTemplate::GOT_misc_appearance_only_invisible
		};

		for (size_t i = 0; i != sizeof(blockedTypes) / sizeof(blockedTypes[0]); ++i)
		{
			if (GameObjectTypes::isTypeOf(gameObjectType, blockedTypes[i]))
				return true;
		}

		return false;
	}

	bool isOpenPullGesture(SwgVrPhysics::HandState const &handState)
	{
		if (!environmentEnabledDefault("SWG_OG_VR_PHYSICS_OPEN_PULL", true))
			return false;

		uint32_t const requiredFlags = SwgVrPhysics::MatrixFlagGripValid | SwgVrPhysics::MatrixFlagGripTracked;
		if ((handState.flags & requiredFlags) != requiredFlags)
			return false;

		float const openGripMax = getEnvironmentFloat("SWG_OG_VR_PHYSICS_OPEN_PULL_GRIP_MAX", 0.08f);
		return handState.gripValue <= openGripMax;
	}

	void clearOpenPullCandidate(uint32_t hand)
	{
		if (hand >= SwgVrPhysics::Hand_Count)
			return;

		s_openPullCandidateNetworkId[hand] = 0;
		s_openPullCandidateStartSeconds[hand] = 0.0;
	}

	bool hasBlockedTangibleState(TangibleObject const &tangible)
	{
		int const blockedConditions =
			TangibleObject::C_vendor |
			TangibleObject::C_hibernating |
			TangibleObject::C_invulnerable |
			TangibleObject::C_mount |
			TangibleObject::C_docking |
			TangibleObject::C_destroying |
			TangibleObject::C_encounterLocked |
			TangibleObject::C_spawnedCreature |
			TangibleObject::C_locked;

		return !tangible.isVisible() ||
			tangible.isInvulnerable() ||
			tangible.isInCombat() ||
			tangible.isPlayer() ||
			((tangible.getCondition() & blockedConditions) != 0);
	}

	float manipulationRadius(ClientObject const &object)
	{
		float const collisionRadius = object.getCollisionSphereExtent_o().getRadius();
		float const appearanceRadius = object.getAppearanceSphereRadius();
		return collisionRadius > appearanceRadius ? collisionRadius : appearanceRadius;
	}

	bool isWithinVrManipulationBudget(ClientObject const &object)
	{
		float const radius = manipulationRadius(object);
		// These caps keep force-pull from grabbing buildings, large machinery, or
		// high-complexity set pieces until server-side placement rules exist.
		return radius > 0.0f &&
			radius <= 3.0f &&
			object.getVolume() <= 200 &&
			object.getComplexity() <= 100.0f &&
			object.getNumberOfChildObjects() == 0;
	}

	bool isStaticSetDressingCandidate(ClientObject const &object)
	{
		if (!environmentEnabledDefault("SWG_OG_VR_PHYSICS_ALLOW_STATIC_SETDRESSING", true))
			return false;
		if (object.asStaticObject() == 0)
			return false;
		if (!isWithinVrManipulationBudget(object))
			return false;

		char const * const templateName = object.getObjectTemplateName();
		if (!templateName)
			return false;

		static char const * const blockedTokens[] =
		{
			"building",
			"chair",
			"door",
			"elevator",
			"terminal",
			"table",
			"vendor",
			"vehicle",
			"speeder",
			"shuttle",
			"sign",
			"bank",
			"bazaar",
			"mission",
			"travel"
		};

		for (size_t i = 0; i != sizeof(blockedTokens) / sizeof(blockedTokens[0]); ++i)
		{
			if (containsPathToken(templateName, blockedTokens[i]))
				return false;
		}

		return true;
	}

	uint64_t objectBridgeId(ClientObject const &object)
	{
		NetworkId const networkId = object.getNetworkId();
		if (networkId.isValid())
			return static_cast<uint64_t>(networkId.getValue());

		if (isStaticSetDressingCandidate(object))
			return 0x8000000000000000ULL | (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&object)) & 0x7fffffffffffffffULL);

		return 0;
	}

	char const *eligibilityRejectReason(ClientObject const &object)
	{
		NetworkId const networkId = object.getNetworkId();
		bool const staticSetDressing = isStaticSetDressingCandidate(object);
		if (!networkId.isValid() && !staticSetDressing)
			return "invalid-network-id";
		if (networkId == Game::getPlayerNetworkId())
			return "player";
		if (!object.isInitialized())
			return "not-initialized";
		if (!object.isInWorld())
			return "not-in-world";
		if (object.getKill())
			return "pending-kill";
		if (object.isClientCached())
			return "client-cached";
		if (object.isChildObject())
			return "child-object";
		if (object.hasChildObjects())
			return "has-child-objects";
		if (object.asCellObject() != 0)
			return "cell-object";
		if (object.asCreatureObject() != 0)
			return "creature-object";
		if (object.asShipObject() != 0)
			return "ship-object";
		if (object.asStaticObject() != 0)
			return staticSetDressing ? 0 : "static-object";

		TangibleObject const * const tangible = object.asTangibleObject();
		if (tangible == 0)
			return "not-tangible";
		if (hasBlockedTangibleState(*tangible))
			return "blocked-tangible-state";
		if (environmentEnabledDefault("SWG_OG_VR_PHYSICS_REQUIRE_TRANSFERABLE", true) && !tangible->hasCondition(TangibleObject::C_transferable))
			return "not-transferable";
		if (isBlockedTemplateFamily(object))
			return "blocked-template-family";
		if (isBlockedGameObjectType(object))
			return "blocked-game-object-type";
		if (!isWithinVrManipulationBudget(object))
			return "outside-budget";

		return 0;
	}

	SwgVrPhysics::Matrix4x4 toBridgeMatrix(Transform const &transform)
	{
		SwgVrPhysics::Matrix4x4 matrix;
		ZeroMemory(&matrix, sizeof(matrix));
		matrix.m[15] = 1.0f;
		Vector const i = transform.getLocalFrameI_p();
		Vector const j = transform.getLocalFrameJ_p();
		Vector const k = transform.getLocalFrameK_p();
		Vector const p = transform.getPosition_p();

		matrix.m[0] = i.x;
		matrix.m[1] = j.x;
		matrix.m[2] = k.x;
		matrix.m[3] = p.x;
		matrix.m[4] = i.y;
		matrix.m[5] = j.y;
		matrix.m[6] = k.y;
		matrix.m[7] = p.y;
		matrix.m[8] = i.z;
		matrix.m[9] = j.z;
		matrix.m[10] = k.z;
		matrix.m[11] = p.z;
		return matrix;
	}

	Vector vrLocalVectorToCameraLocal(Vector const &vector)
	{
		return Vector(vector.x, vector.y, -vector.z);
	}

	Vector cameraLocalVectorToVrLocal(Vector const &vector)
	{
		return Vector(vector.x, vector.y, -vector.z);
	}

	Transform vrLocalToSwgWorldTransform(SwgVrPhysics::Matrix4x4 const &matrix)
	{
		Transform vrLocal;
		vrLocal.setLocalFrameIJK_p(
			Vector(matrix.m[0], matrix.m[4], matrix.m[8]),
			Vector(matrix.m[1], matrix.m[5], matrix.m[9]),
			Vector(matrix.m[2], matrix.m[6], matrix.m[10]));
		vrLocal.setPosition_p(Vector(matrix.m[3], matrix.m[7], matrix.m[11]));
		vrLocal.reorthonormalize();
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

	SwgVrPhysics::Matrix4x4 swgWorldToVrLocalMatrix(Transform const &worldTransform)
	{
		Camera const * const camera = Game::getConstCamera();
		if (!camera)
			return toBridgeMatrix(worldTransform);

		Transform const &cameraToWorld = camera->getTransform_o2w();
		Transform vrLocal;
		vrLocal.setLocalFrameIJK_p(
			cameraLocalVectorToVrLocal(cameraToWorld.rotate_p2l(worldTransform.getLocalFrameI_p())),
			cameraLocalVectorToVrLocal(cameraToWorld.rotate_p2l(worldTransform.getLocalFrameJ_p())),
			cameraLocalVectorToVrLocal(cameraToWorld.rotate_p2l(worldTransform.getLocalFrameK_p())));
		vrLocal.setPosition_p(cameraLocalVectorToVrLocal(cameraToWorld.rotateTranslate_p2l(worldTransform.getPosition_p())));
		vrLocal.reorthonormalize();
		return toBridgeMatrix(vrLocal);
	}

	SwgVrPhysics::Ray vrLocalRayToSwgWorldRay(SwgVrPhysics::Ray const &ray)
	{
		Camera const * const camera = Game::getConstCamera();
		if (!camera)
			return ray;

		Transform const &cameraToWorld = camera->getTransform_o2w();
		Vector const origin = cameraToWorld.rotateTranslate_l2p(vrLocalVectorToCameraLocal(Vector(ray.origin.x, ray.origin.y, ray.origin.z)));
		Vector direction = cameraToWorld.rotate_l2p(vrLocalVectorToCameraLocal(Vector(ray.direction.x, ray.direction.y, ray.direction.z)));
		IGNORE_RETURN(direction.normalize());

		SwgVrPhysics::Ray result;
		result.origin.x = origin.x;
		result.origin.y = origin.y;
		result.origin.z = origin.z;
		result.direction.x = direction.x;
		result.direction.y = direction.y;
		result.direction.z = direction.z;
		return result;
	}

	SwgVrPhysics::HandState vrLocalHandStateToSwgWorld(SwgVrPhysics::HandState const &handState)
	{
		SwgVrPhysics::HandState result = handState;
		if ((result.flags & SwgVrPhysics::MatrixFlagAimValid) != 0)
			result.aimFromWorld = toBridgeMatrix(vrLocalToSwgWorldTransform(handState.aimFromWorld));
		if ((result.flags & SwgVrPhysics::MatrixFlagGripValid) != 0)
			result.gripFromWorld = toBridgeMatrix(vrLocalToSwgWorldTransform(handState.gripFromWorld));
		return result;
	}

	SwgVrPhysics::Vector3 halfExtentsForObject(ClientObject const &object)
	{
		float const collisionRadius = object.getCollisionSphereExtent_o().getRadius();
		float const appearanceRadius = object.getAppearanceSphereRadius();
		float radius = collisionRadius > appearanceRadius ? collisionRadius : appearanceRadius;
		if (radius < 0.10f)
			radius = 0.10f;
		if (radius > 3.0f)
			radius = 3.0f;

		SwgVrPhysics::Vector3 halfExtents = {radius, radius, radius};
		return halfExtents;
	}

	void setHoverHighlight(ClientObject &object, bool active)
	{
		if (!environmentEnabledDefault("SWG_OG_VR_PHYSICS_HIGHLIGHT", true))
			active = false;

		TangibleObject * const tangible = object.asTangibleObject();
		if (!tangible)
			return;

		if (active)
		{
			char const * const effectFile = getEnvironmentString("SWG_OG_VR_PHYSICS_HIGHLIGHT_EFFECT", "appearance/pt_ui_subtle_glow.prt");
			float const scale = getEnvironmentFloat("SWG_OG_VR_PHYSICS_HIGHLIGHT_SCALE", 0.65f);
			tangible->setClientOnlyVrPhysicsHoverEffect(true, effectFile, scale);
		}
		else
		{
			tangible->setClientOnlyVrPhysicsHoverEffect(false, 0, 0.0f);
		}
	}

	bool rayCanSelectObject(ClientObject const &object, SwgVrPhysics::Ray const &ray, float *alongRayOut, float *missDistanceSquaredOut, float *radiusOut)
	{
		float const maxDistance = getEnvironmentFloat("SWG_OG_VR_PHYSICS_GRAB_RANGE", 8.0f);
		float const extraRadius = getEnvironmentFloat("SWG_OG_VR_PHYSICS_GRAB_PAD", 0.35f);
		Vector const origin(ray.origin.x, ray.origin.y, ray.origin.z);
		Vector direction(ray.direction.x, ray.direction.y, ray.direction.z);
		if (!direction.normalize())
			return false;
		Vector const center = object.getAppearanceSphereCenter_w();
		Vector const toCenter = center - origin;
		float const alongRay = toCenter.dot(direction);
		if (alongRayOut)
			*alongRayOut = alongRay;
		if (alongRay < 0.15f || alongRay > maxDistance)
			return false;

		Vector const closest = origin + direction * alongRay;
		float radius = manipulationRadius(object);
		radius += extraRadius;
		float const missDistanceSquared = closest.magnitudeBetweenSquared(center);
		if (missDistanceSquaredOut)
			*missDistanceSquaredOut = missDistanceSquared;
		if (radiusOut)
			*radiusOut = radius;

		if (missDistanceSquared > radius * radius)
			return false;

		if (environmentEnabledDefault("SWG_OG_VR_PHYSICS_REQUIRE_MESH_HIT", true))
		{
			Vector const end = origin + direction * maxDistance;
			CollisionInfo meshHit;
			if (!ClientWorld::collideObject(&object, origin, end, CollideParameters::cms_toolPickDefault, meshHit, ClientWorld::CF_all))
				return false;

			float const hitAlongRay = (meshHit.getPoint() - origin).dot(direction);
			if (hitAlongRay < 0.0f || hitAlongRay > maxDistance)
				return false;

			if (alongRayOut)
				*alongRayOut = hitAlongRay;
			if (missDistanceSquaredOut)
				*missDistanceSquaredOut = 0.0f;
		}

		return true;
	}

	bool handContactCanSelectObject(ClientObject const &object, SwgVrPhysics::HandState const &handState, float *missDistanceSquaredOut, float *radiusOut)
	{
		if ((handState.flags & SwgVrPhysics::MatrixFlagGripValid) == 0)
			return false;

		float const extraRadius = getEnvironmentFloat("SWG_OG_VR_PHYSICS_HAND_CONTACT_PAD", 0.22f);
		Vector const handPosition(handState.gripFromWorld.m[3], handState.gripFromWorld.m[7], handState.gripFromWorld.m[11]);
		Vector const center = object.getAppearanceSphereCenter_w();
		float radius = manipulationRadius(object) + extraRadius;
		float const missDistanceSquared = handPosition.magnitudeBetweenSquared(center);
		if (missDistanceSquaredOut)
			*missDistanceSquaredOut = missDistanceSquared;
		if (radiusOut)
			*radiusOut = radius;
		if (missDistanceSquared > radius * radius)
			return false;

		Vector palmForward(-handState.gripFromWorld.m[2], -handState.gripFromWorld.m[6], -handState.gripFromWorld.m[10]);
		Vector palmRight(handState.gripFromWorld.m[0], handState.gripFromWorld.m[4], handState.gripFromWorld.m[8]);
		Vector palmUp(handState.gripFromWorld.m[1], handState.gripFromWorld.m[5], handState.gripFromWorld.m[9]);
		if (!palmForward.normalize() || !palmRight.normalize() || !palmUp.normalize())
			return false;

		float const backMeters = getEnvironmentFloat("SWG_OG_VR_PHYSICS_HAND_CONTACT_BACK", 0.10f);
		float const forwardMeters = getEnvironmentFloat("SWG_OG_VR_PHYSICS_HAND_CONTACT_FORWARD", 0.20f);
		Vector const starts[] =
		{
			handPosition - palmForward * backMeters,
			handPosition + palmRight * 0.055f - palmForward * backMeters,
			handPosition - palmRight * 0.055f - palmForward * backMeters,
			handPosition + palmUp * 0.055f - palmForward * backMeters,
			handPosition - palmUp * 0.055f - palmForward * backMeters
		};
		Vector const ends[] =
		{
			handPosition + palmForward * forwardMeters,
			handPosition + palmRight * 0.055f + palmForward * forwardMeters,
			handPosition - palmRight * 0.055f + palmForward * forwardMeters,
			handPosition + palmUp * 0.055f + palmForward * forwardMeters,
			handPosition - palmUp * 0.055f + palmForward * forwardMeters
		};

		for (size_t i = 0; i != sizeof(starts) / sizeof(starts[0]); ++i)
		{
			CollisionInfo contactHit;
			if (ClientWorld::collideObject(&object, starts[i], ends[i], CollideParameters::cms_toolPickDefault, contactHit, ClientWorld::CF_all))
				return true;
		}

		return false;
	}

	SwgVrPhysics::Matrix4x4 snapToHandMatrix(SwgVrPhysics::HandState const &handState, float objectRadius)
	{
		SwgVrPhysics::Matrix4x4 matrix = handState.gripFromWorld;
		float distance = getEnvironmentFloat("SWG_OG_VR_PHYSICS_HOLD_DISTANCE", 0.28f);
		float const radiusContribution = getEnvironmentFloat("SWG_OG_VR_PHYSICS_HOLD_RADIUS_SCALE", 0.35f);
		float const up = getEnvironmentFloat("SWG_OG_VR_PHYSICS_HOLD_UP", -0.03f);
		float const right = getEnvironmentFloat("SWG_OG_VR_PHYSICS_HOLD_RIGHT", 0.0f);
		if (objectRadius > 0.0f)
			distance += objectRadius * radiusContribution;

		matrix.m[3] =
			handState.gripFromWorld.m[3] +
			handState.gripFromWorld.m[0] * right +
			handState.gripFromWorld.m[1] * up -
			handState.gripFromWorld.m[2] * distance;
		matrix.m[7] =
			handState.gripFromWorld.m[7] +
			handState.gripFromWorld.m[4] * right +
			handState.gripFromWorld.m[5] * up -
			handState.gripFromWorld.m[6] * distance;
		matrix.m[11] =
			handState.gripFromWorld.m[11] +
			handState.gripFromWorld.m[8] * right +
			handState.gripFromWorld.m[9] * up -
			handState.gripFromWorld.m[10] * distance;
		return matrix;
	}

	SwgVrPhysics::Matrix4x4 pullTowardHandMatrix(ClientObject const &object, SwgVrPhysics::Matrix4x4 const &targetFromWorld, uint32_t hand, double sampleTimeSeconds)
	{
		SwgVrPhysics::Matrix4x4 currentFromWorld = swgWorldToVrLocalMatrix(object.getTransform_o2w());

		double const previousSampleTimeSeconds = s_activePullLastUpdateSeconds[hand];
		double deltaSeconds = previousSampleTimeSeconds > 0.0 ? sampleTimeSeconds - previousSampleTimeSeconds : (1.0 / 60.0);
		if (deltaSeconds <= 0.0 || deltaSeconds > 0.10)
			deltaSeconds = 1.0 / 60.0;
		s_activePullLastUpdateSeconds[hand] = sampleTimeSeconds;

		Vector const current(currentFromWorld.m[3], currentFromWorld.m[7], currentFromWorld.m[11]);
		Vector const target(targetFromWorld.m[3], targetFromWorld.m[7], targetFromWorld.m[11]);
		Vector const delta = target - current;
		float const distance = delta.magnitude();
		float const snapDistance = getEnvironmentFloat("SWG_OG_VR_PHYSICS_OPEN_PULL_SNAP_DISTANCE", 0.12f);

		SwgVrPhysics::Matrix4x4 result = targetFromWorld;
		if (distance > snapDistance)
		{
			float const speed = getEnvironmentFloat("SWG_OG_VR_PHYSICS_OPEN_PULL_SPEED", 5.5f);
			float const step = clamp(0.0f, speed * static_cast<float>(deltaSeconds), distance);
			Vector const pulled = current + delta * (step / distance);
			result.m[3] = pulled.x;
			result.m[7] = pulled.y;
			result.m[11] = pulled.z;
		}

		return result;
	}

	bool updateForcePullObject(ClientObject &object, BridgeApi const &api)
	{
		if (!api.getHandState || !api.beginGrab)
			return false;

		uint64_t const id = objectBridgeId(object);
		if (id == 0)
			return false;
		for (uint32_t hand = 0; hand != SwgVrPhysics::Hand_Count; ++hand)
		{
			if (s_activePullNetworkId[hand] != id)
				continue;

			SwgVrPhysics::HandState handState;
			ZeroMemory(&handState, sizeof(handState));
			if (!api.getHandState(hand, &handState))
			{
				s_activePullNetworkId[hand] = 0;
				s_activePullLastUpdateSeconds[hand] = 0.0;
				return false;
			}

			SwgVrPhysics::Matrix4x4 const targetFromWorld = snapToHandMatrix(handState, manipulationRadius(object));
			if ((handState.flags & SwgVrPhysics::MatrixFlagGripHeld) != 0)
			{
				SwgVrPhysics::Vector3 const halfExtents = halfExtentsForObject(object);
				if (api.beginGrab(id, hand, &targetFromWorld, &halfExtents))
				{
					s_activeGrabNetworkId[hand] = id;
					s_activePullNetworkId[hand] = 0;
					s_activePullLastUpdateSeconds[hand] = 0.0;
					object.setTransform_o2w(vrLocalToSwgWorldTransform(targetFromWorld));
					traceGrabCandidate(object, hand, "begin", "open-pull-caught", 0.0f, 0.0f, manipulationRadius(object));
					return true;
				}
			}

			if (!isOpenPullGesture(handState))
			{
				s_activePullNetworkId[hand] = 0;
				s_activePullLastUpdateSeconds[hand] = 0.0;
				return false;
			}

			SwgVrPhysics::Matrix4x4 const pulledFromWorld = pullTowardHandMatrix(object, targetFromWorld, hand, handState.sampleTimeSeconds);
			object.setTransform_o2w(vrLocalToSwgWorldTransform(pulledFromWorld));
			setHoverHighlight(object, true);
			return true;
		}

		return false;
	}

	void releaseInactiveHandGrabs(BridgeApi const &api)
	{
		if (!api.getHandState || !api.getGrabbedObjectState || !api.endGrab)
			return;

		for (uint32_t hand = 0; hand != SwgVrPhysics::Hand_Count; ++hand)
		{
			uint64_t const activeNetworkId = s_activeGrabNetworkId[hand];
			if (activeNetworkId == 0)
				continue;

			SwgVrPhysics::GrabbedObjectState grabbed;
			ZeroMemory(&grabbed, sizeof(grabbed));
			if (!api.getGrabbedObjectState(activeNetworkId, &grabbed) || (grabbed.flags & SwgVrPhysics::ObjectFlagGrabbed) == 0)
			{
				s_activeGrabNetworkId[hand] = 0;
				continue;
			}

			SwgVrPhysics::HandState handState;
			ZeroMemory(&handState, sizeof(handState));
			if (!api.getHandState(hand, &handState) || (handState.flags & SwgVrPhysics::MatrixFlagGripHeld) == 0)
			{
				SwgVrPhysics::ReleasedObjectState released;
				ZeroMemory(&released, sizeof(released));
				(void)api.endGrab(activeNetworkId, &released);
				s_activeGrabNetworkId[hand] = 0;
				clearOpenPullCandidate(hand);
			}
		}
	}

	void releaseObjectGrabIfActive(ClientObject const &object, BridgeApi const &api, SwgVrPhysics::GrabbedObjectState const &grabbed)
	{
		if (!api.endGrab || (grabbed.flags & SwgVrPhysics::ObjectFlagGrabbed) == 0 || grabbed.grabbingHand >= SwgVrPhysics::Hand_Count)
			return;

		SwgVrPhysics::ReleasedObjectState released;
		ZeroMemory(&released, sizeof(released));
		uint64_t const id = objectBridgeId(object);
		if (id != 0 && api.endGrab(id, &released))
			s_activeGrabNetworkId[grabbed.grabbingHand] = 0;
	}

	bool updateHoverOrStartGrab(ClientObject &object, BridgeApi const &api, bool trace)
	{
		if (!api.getHandState || !api.getAimRay || !api.beginGrab)
			return false;

		char const * const rejectReason = eligibilityRejectReason(object);
		uint64_t const id = objectBridgeId(object);
		if (id == 0)
			return false;
		bool hover = false;
		for (uint32_t hand = 0; hand != SwgVrPhysics::Hand_Count; ++hand)
		{
			if (s_activeGrabNetworkId[hand] != 0)
				continue;

			SwgVrPhysics::HandState handState;
			ZeroMemory(&handState, sizeof(handState));
			if (!api.getHandState(hand, &handState))
				continue;
			SwgVrPhysics::HandState const handStateSwg = vrLocalHandStateToSwgWorld(handState);

			SwgVrPhysics::Ray ray;
			ZeroMemory(&ray, sizeof(ray));
			float alongRay = 0.0f;
			float missDistanceSquared = 0.0f;
			float radius = 0.0f;
			bool const gripHeld = (handState.flags & SwgVrPhysics::MatrixFlagGripHeld) != 0;
			bool const rayHit = api.getAimRay(hand, &ray) && rayCanSelectObject(object, vrLocalRayToSwgWorldRay(ray), &alongRay, &missDistanceSquared, &radius);
			bool const handContactHit = gripHeld && handContactCanSelectObject(object, handStateSwg, &missDistanceSquared, &radius);
			if (!rayHit && !handContactHit)
				continue;
			if (rayHit && !handContactHit && (handState.flags & SwgVrPhysics::MatrixFlagUiOccluded) != 0 && alongRay >= handState.uiOcclusionDistanceMeters - 0.05f)
			{
				if (trace)
					traceGrabCandidate(object, hand, "reject", "ui-occluded", alongRay, missDistanceSquared, radius);
				continue;
			}

			if (rejectReason)
			{
				if (trace)
					traceGrabCandidate(object, hand, "reject", rejectReason, alongRay, missDistanceSquared, radius);
				clearOpenPullCandidate(hand);
				continue;
			}

			hover = true;
			bool const openPull = isOpenPullGesture(handState);
			if (rayHit && !gripHeld && openPull)
			{
				double const sampleTimeSeconds = handState.sampleTimeSeconds > 0.0 ? handState.sampleTimeSeconds : static_cast<double>(GetTickCount()) * 0.001;
				if (s_openPullCandidateNetworkId[hand] != id)
				{
					s_openPullCandidateNetworkId[hand] = id;
					s_openPullCandidateStartSeconds[hand] = sampleTimeSeconds;
				}

				float const dwellSeconds = getEnvironmentFloat("SWG_OG_VR_PHYSICS_OPEN_PULL_DWELL_SECONDS", 0.18f);
				if ((sampleTimeSeconds - s_openPullCandidateStartSeconds[hand]) >= dwellSeconds)
				{
					s_activePullNetworkId[hand] = id;
					s_activePullLastUpdateSeconds[hand] = sampleTimeSeconds;
					if (trace)
						traceGrabCandidate(object, hand, "pull", "open-hand", alongRay, missDistanceSquared, radius);

					SwgVrPhysics::Matrix4x4 const targetFromWorld = snapToHandMatrix(handState, manipulationRadius(object));
					object.setTransform_o2w(vrLocalToSwgWorldTransform(pullTowardHandMatrix(object, targetFromWorld, hand, sampleTimeSeconds)));
					setHoverHighlight(object, true);
					return true;
				}
				continue;
			}

			if (!gripHeld)
			{
				clearOpenPullCandidate(hand);
				continue;
			}

			clearOpenPullCandidate(hand);
			SwgVrPhysics::Matrix4x4 const objectFromWorld = snapToHandMatrix(handState, manipulationRadius(object));
			SwgVrPhysics::Vector3 const halfExtents = halfExtentsForObject(object);
			if (api.beginGrab(id, hand, &objectFromWorld, &halfExtents))
			{
				if (trace)
					traceGrabCandidate(object, hand, "begin", handContactHit ? "hand-contact" : "eligible", alongRay, missDistanceSquared, radius);
				setHoverHighlight(object, false);
				s_activeGrabNetworkId[hand] = id;
				object.setTransform_o2w(vrLocalToSwgWorldTransform(objectFromWorld));
				return true;
			}
		}

		setHoverHighlight(object, hover);
		return false;
	}
}

bool SwgVrPhysics_isClientObjectEligibleForGrab(ClientObject const &object)
{
	return eligibilityRejectReason(object) == 0;
}

bool SwgVrPhysics_applyClientObjectAlterOverride(ClientObject &object)
{
	uint64_t const id = objectBridgeId(object);
	if (id == 0)
		return false;

	SwgVrPhysics::GrabbedObjectState grabbed;
	ZeroMemory(&grabbed, sizeof(grabbed));
	BridgeApi const &api = getBridgeApi();
	releaseInactiveHandGrabs(api);
	if (!api.getGrabbedObjectState || !api.getGrabbedObjectState(id, &grabbed))
	{
		if (updateForcePullObject(object, api))
			return true;

		if (traceEnabled())
			return updateHoverOrStartGrab(object, api, true);

		return updateHoverOrStartGrab(object, api, false);
	}

	if ((grabbed.flags & (SwgVrPhysics::ObjectFlagGrabbed | SwgVrPhysics::ObjectFlagPhysicsActive | SwgVrPhysics::ObjectFlagSleeping)) == 0)
		return false;

	if (!SwgVrPhysics_isClientObjectEligibleForGrab(object))
	{
		releaseObjectGrabIfActive(object, api, grabbed);
		setHoverHighlight(object, false);
		return false;
	}

	if ((grabbed.flags & SwgVrPhysics::ObjectFlagGrabbed) != 0 && grabbed.grabbingHand < SwgVrPhysics::Hand_Count)
	{
		SwgVrPhysics::HandState handState;
		ZeroMemory(&handState, sizeof(handState));
		if (api.getHandState && api.endGrab && api.getHandState(grabbed.grabbingHand, &handState) && (handState.flags & SwgVrPhysics::MatrixFlagGripHeld) == 0)
		{
			SwgVrPhysics::ReleasedObjectState released;
			ZeroMemory(&released, sizeof(released));
			if (api.endGrab(id, &released))
			{
				traceGrabCandidate(object, grabbed.grabbingHand, "release", "grip-up", 0.0f, 0.0f, manipulationRadius(object));
				setHoverHighlight(object, false);
				s_activeGrabNetworkId[grabbed.grabbingHand] = 0;
			}
			return false;
		}
	}

	object.setTransform_o2w(vrLocalToSwgWorldTransform(grabbed.objectFromWorld));
	return true;
}

#endif
