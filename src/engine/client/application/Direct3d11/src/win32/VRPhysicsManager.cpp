// ======================================================================
//
// VRPhysicsManager.cpp
//
// Runs local client-only rigid body simulation on a fixed 60 Hz thread.
// Define SWG_VR_PHYSICS_USE_BULLET=1 and provide Bullet3 include/lib paths
// to use btDiscreteDynamicsWorld. Without Bullet, a small deterministic
// fallback keeps the bridge and ClientObject hook testable.
//
// ======================================================================

#include "VRPhysicsManager.h"

#include <algorithm>
#include <cmath>
#include <vector>

#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
#	include <btBulletDynamicsCommon.h>
#endif

namespace
{
	float const FixedDeltaSeconds = 1.0f / 60.0f;
	float const SleepLinearThreshold = 0.04f;
	float const SleepAngularThreshold = 0.03f;
	float const SleepHoldSeconds = 0.50f;

	struct ScopedCriticalSection
	{
		explicit ScopedCriticalSection(CRITICAL_SECTION &criticalSection) :
			m_criticalSection(criticalSection)
		{
			EnterCriticalSection(&m_criticalSection);
		}

		~ScopedCriticalSection()
		{
			LeaveCriticalSection(&m_criticalSection);
		}

	private:
		CRITICAL_SECTION &m_criticalSection;

		ScopedCriticalSection(ScopedCriticalSection const &);
		ScopedCriticalSection &operator=(ScopedCriticalSection const &);
	};

	SwgVrPhysics::Vector3 vector3(float x, float y, float z)
	{
		SwgVrPhysics::Vector3 value = {x, y, z};
		return value;
	}

	SwgVrPhysics::Vector3 positionOf(SwgVrPhysics::Matrix4x4 const &matrix)
	{
		return vector3(matrix.m[3], matrix.m[7], matrix.m[11]);
	}

	void setPosition(SwgVrPhysics::Matrix4x4 &matrix, SwgVrPhysics::Vector3 const &position)
	{
		matrix.m[3] = position.x;
		matrix.m[7] = position.y;
		matrix.m[11] = position.z;
	}

	SwgVrPhysics::Vector3 subtract(SwgVrPhysics::Vector3 const &lhs, SwgVrPhysics::Vector3 const &rhs)
	{
		return vector3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
	}

	SwgVrPhysics::Vector3 scale(SwgVrPhysics::Vector3 const &value, float scalar)
	{
		return vector3(value.x * scalar, value.y * scalar, value.z * scalar);
	}

	SwgVrPhysics::Vector3 add(SwgVrPhysics::Vector3 const &lhs, SwgVrPhysics::Vector3 const &rhs)
	{
		return vector3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
	}

	float length(SwgVrPhysics::Vector3 const &value)
	{
		return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
	}

	float clampFloat(float value, float minValue, float maxValue)
	{
		if (value < minValue)
			return minValue;
		if (value > maxValue)
			return maxValue;
		return value;
	}

	SwgVrPhysics::Quaternion identityQuaternion()
	{
		SwgVrPhysics::Quaternion quaternion = {0.0f, 0.0f, 0.0f, 1.0f};
		return quaternion;
	}

#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
	btVector3 toBt(SwgVrPhysics::Vector3 const &value)
	{
		return btVector3(value.x, value.y, value.z);
	}

	SwgVrPhysics::Vector3 fromBt(btVector3 const &value)
	{
		return vector3(value.x(), value.y(), value.z());
	}

	btTransform toBt(SwgVrPhysics::Matrix4x4 const &matrix)
	{
		btMatrix3x3 basis(
			matrix.m[0], matrix.m[1], matrix.m[2],
			matrix.m[4], matrix.m[5], matrix.m[6],
			matrix.m[8], matrix.m[9], matrix.m[10]);
		return btTransform(basis, btVector3(matrix.m[3], matrix.m[7], matrix.m[11]));
	}

	SwgVrPhysics::Matrix4x4 fromBt(btTransform const &transform)
	{
		SwgVrPhysics::Matrix4x4 matrix = SwgVrPhysics::identityMatrix();
		btMatrix3x3 const basis = transform.getBasis();
		for (int row = 0; row != 3; ++row)
		{
			for (int column = 0; column != 3; ++column)
				matrix.m[row * 4 + column] = basis[row][column];
		}
		btVector3 const origin = transform.getOrigin();
		matrix.m[3] = origin.x();
		matrix.m[7] = origin.y();
		matrix.m[11] = origin.z();
		return matrix;
	}

	SwgVrPhysics::Quaternion orientationFromBt(btTransform const &transform)
	{
		btQuaternion const q = transform.getRotation();
		SwgVrPhysics::Quaternion result = {q.x(), q.y(), q.z(), q.w()};
		return result;
	}
#endif
}

struct VRPhysicsManager::WorldState
{
#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
	WorldState() :
		collisionConfiguration(0),
		dispatcher(0),
		broadphase(0),
		solver(0),
		dynamicsWorld(0),
		groundShape(0),
		groundMotionState(0),
		groundRigidBody(0)
	{
	}

	~WorldState()
	{
		delete groundRigidBody;
		delete groundMotionState;
		delete groundShape;
		delete dynamicsWorld;
		delete solver;
		delete broadphase;
		delete dispatcher;
		delete collisionConfiguration;
	}

	btDefaultCollisionConfiguration *collisionConfiguration;
	btCollisionDispatcher *dispatcher;
	btBroadphaseInterface *broadphase;
	btSequentialImpulseConstraintSolver *solver;
	btDiscreteDynamicsWorld *dynamicsWorld;
	btCollisionShape *groundShape;
	btDefaultMotionState *groundMotionState;
	btRigidBody *groundRigidBody;
#endif
};

struct VRPhysicsManager::BodyState
{
	BodyState();
	~BodyState();

	uint64_t networkId;
	SwgVrPhysics::Matrix4x4 objectFromWorld;
	SwgVrPhysics::Vector3 halfExtentsMeters;
	SwgVrPhysics::Vector3 linearVelocityMetersPerSecond;
	SwgVrPhysics::Vector3 angularVelocityRadiansPerSecond;
	float sleepAccumulatorSeconds;
	bool finalPlacementDispatched;

#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
	btCollisionShape *shape;
	btDefaultMotionState *motionState;
	btRigidBody *rigidBody;
#endif
};

VRPhysicsManager::ControllerHistory::ControllerHistory() :
	count(0),
	next(0)
{
	for (size_t i = 0; i != 5; ++i)
	{
		samples[i].gripFromWorld = SwgVrPhysics::identityMatrix();
		samples[i].sampleTimeSeconds = 0.0;
		samples[i].gripHeld = false;
	}
}

VRPhysicsManager::BodyState::BodyState() :
	networkId(0),
	objectFromWorld(SwgVrPhysics::identityMatrix()),
	halfExtentsMeters(vector3(0.25f, 0.25f, 0.25f)),
	linearVelocityMetersPerSecond(vector3(0.0f, 0.0f, 0.0f)),
	angularVelocityRadiansPerSecond(vector3(0.0f, 0.0f, 0.0f)),
	sleepAccumulatorSeconds(0.0f),
	finalPlacementDispatched(false)
#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
	,
	shape(0),
	motionState(0),
	rigidBody(0)
#endif
{
}

VRPhysicsManager::BodyState::~BodyState()
{
#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
	delete rigidBody;
	delete motionState;
	delete shape;
#endif
}

VRPhysicsManager::VRPhysicsManager() :
	m_running(0),
	m_thread(0),
	m_finalPlacementCallback(0),
	m_finalPlacementCallbackContext(0),
	m_bodies(),
	m_world(0)
{
	InitializeCriticalSection(&m_mutex);
}

VRPhysicsManager::~VRPhysicsManager()
{
	stop();
	shutdownWorld();
	DeleteCriticalSection(&m_mutex);
}

void VRPhysicsManager::start()
{
	if (InterlockedCompareExchange(&m_running, 1, 0) != 0)
		return;

	initializeWorld();
	m_thread = CreateThread(0, 0, &VRPhysicsManager::threadProc, this, 0, 0);
	if (!m_thread)
	{
		InterlockedExchange(&m_running, 0);
		shutdownWorld();
	}
}

void VRPhysicsManager::stop()
{
	if (InterlockedCompareExchange(&m_running, 0, 1) != 1)
		return;

	if (m_thread)
	{
		WaitForSingleObject(m_thread, INFINITE);
		CloseHandle(m_thread);
		m_thread = 0;
	}

	shutdownWorld();
}

bool VRPhysicsManager::isRunning() const
{
	return m_running != 0;
}

void VRPhysicsManager::setFinalPlacementCallback(FinalPlacementCallback callback, void *context)
{
	ScopedCriticalSection lock(m_mutex);
	m_finalPlacementCallback = callback;
	m_finalPlacementCallbackContext = context;
}

void VRPhysicsManager::recordControllerSample(uint32_t hand, SwgVrPhysics::Matrix4x4 const &gripFromWorld, double sampleTimeSeconds, bool gripHeld)
{
	if (hand >= SwgVrPhysics::Hand_Count)
		return;

	ScopedCriticalSection lock(m_mutex);
	ControllerHistory &history = m_controllerHistory[hand];
	history.samples[history.next].gripFromWorld = gripFromWorld;
	history.samples[history.next].sampleTimeSeconds = sampleTimeSeconds;
	history.samples[history.next].gripHeld = gripHeld;
	history.next = (history.next + 1) % 5;
	history.count = std::min<size_t>(history.count + 1, 5);
}

bool VRPhysicsManager::spawnReleasedItem(SwgVrPhysics::ReleasedObjectState const &released)
{
	if (released.networkId == 0)
		return false;

	BodyState *body = new BodyState();
	body->networkId = released.networkId;
	body->objectFromWorld = released.objectFromWorld;
	body->halfExtentsMeters = released.halfExtentsMeters;
	body->linearVelocityMetersPerSecond = estimateLinearVelocity(released.sourceHand, released.linearVelocityMetersPerSecond);
	body->angularVelocityRadiansPerSecond = estimateAngularVelocity(released.sourceHand, released.angularVelocityRadiansPerSecond);

	ScopedCriticalSection lock(m_mutex);

#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
	if (m_world && m_world->dynamicsWorld)
	{
		body->shape = new btBoxShape(toBt(body->halfExtentsMeters));
		body->motionState = new btDefaultMotionState(toBt(body->objectFromWorld));

		btVector3 inertia(0.0f, 0.0f, 0.0f);
		float const massKg = 1.0f;
		body->shape->calculateLocalInertia(massKg, inertia);
		btRigidBody::btRigidBodyConstructionInfo info(massKg, body->motionState, body->shape, inertia);
		body->rigidBody = new btRigidBody(info);
		body->rigidBody->setFriction(0.85f);
		body->rigidBody->setRestitution(0.22f);
		body->rigidBody->setLinearVelocity(toBt(body->linearVelocityMetersPerSecond));
		body->rigidBody->setAngularVelocity(toBt(body->angularVelocityRadiansPerSecond));
		body->rigidBody->setSleepingThresholds(SleepLinearThreshold, SleepAngularThreshold);
		body->rigidBody->activate(true);
		m_world->dynamicsWorld->addRigidBody(body->rigidBody);
	}
#endif

	BodyMap::iterator existing = m_bodies.find(released.networkId);
	if (existing != m_bodies.end())
	{
#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
		if (m_world && m_world->dynamicsWorld && existing->second && existing->second->rigidBody)
			m_world->dynamicsWorld->removeRigidBody(existing->second->rigidBody);
#endif
		delete existing->second;
		existing->second = body;
	}
	else
	{
		m_bodies[released.networkId] = body;
	}

	return true;
}

DWORD WINAPI VRPhysicsManager::threadProc(void *context)
{
	VRPhysicsManager *manager = static_cast<VRPhysicsManager *>(context);
	if (manager)
		manager->runLoop();
	return 0;
}

void VRPhysicsManager::initializeWorld()
{
	ScopedCriticalSection lock(m_mutex);

	if (!m_world)
		m_world = new WorldState();

#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
	if (m_world->dynamicsWorld)
		return;

	m_world->collisionConfiguration = new btDefaultCollisionConfiguration();
	m_world->dispatcher = new btCollisionDispatcher(m_world->collisionConfiguration);
	m_world->broadphase = new btDbvtBroadphase();
	m_world->solver = new btSequentialImpulseConstraintSolver();
	m_world->dynamicsWorld = new btDiscreteDynamicsWorld(
		m_world->dispatcher,
		m_world->broadphase,
		m_world->solver,
		m_world->collisionConfiguration);
	m_world->dynamicsWorld->setGravity(btVector3(0.0f, -9.81f, 0.0f));

	m_world->groundShape = new btStaticPlaneShape(btVector3(0.0f, 1.0f, 0.0f), 0.0f);
	m_world->groundMotionState = new btDefaultMotionState(btTransform(btQuaternion(0.0f, 0.0f, 0.0f, 1.0f), btVector3(0.0f, 0.0f, 0.0f)));
	btRigidBody::btRigidBodyConstructionInfo groundInfo(0.0f, m_world->groundMotionState, m_world->groundShape, btVector3(0.0f, 0.0f, 0.0f));
	m_world->groundRigidBody = new btRigidBody(groundInfo);
	m_world->groundRigidBody->setFriction(0.95f);
	m_world->dynamicsWorld->addRigidBody(m_world->groundRigidBody);
#endif
}

void VRPhysicsManager::shutdownWorld()
{
	ScopedCriticalSection lock(m_mutex);

#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
	if (m_world && m_world->dynamicsWorld)
	{
		for (BodyMap::iterator it = m_bodies.begin(); it != m_bodies.end(); ++it)
		{
			BodyState *body = it->second;
			if (body && body->rigidBody)
				m_world->dynamicsWorld->removeRigidBody(body->rigidBody);
		}

		if (m_world->groundRigidBody)
			m_world->dynamicsWorld->removeRigidBody(m_world->groundRigidBody);
	}
#endif

	clearBodiesUnlocked();
	delete m_world;
	m_world = 0;
}

void VRPhysicsManager::clearBodiesUnlocked()
{
	for (BodyMap::iterator it = m_bodies.begin(); it != m_bodies.end(); ++it)
		delete it->second;
	m_bodies.clear();
}

void VRPhysicsManager::runLoop()
{
	DWORD nextTick = GetTickCount();

	while (m_running != 0)
	{
		nextTick += 16;
		step(FixedDeltaSeconds);

		DWORD const now = GetTickCount();
		if (nextTick > now)
			::Sleep(nextTick - now);
		else
			nextTick = now;
	}
}

void VRPhysicsManager::step(float fixedDeltaSeconds)
{
	std::vector<SwgVrPhysics::GrabbedObjectState> activeStates;
	std::vector<SwgVrPhysics::FinalPlacementState> completed;

	{
		ScopedCriticalSection lock(m_mutex);

#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
		if (m_world && m_world->dynamicsWorld)
			m_world->dynamicsWorld->stepSimulation(fixedDeltaSeconds, 0, fixedDeltaSeconds);
#endif

		for (BodyMap::iterator it = m_bodies.begin(); it != m_bodies.end();)
		{
			BodyState *body = it->second;
			if (!body)
			{
				BodyMap::iterator eraseIt = it++;
				m_bodies.erase(eraseIt);
				continue;
			}

#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
			if (body->rigidBody)
			{
				btTransform transform;
				body->rigidBody->getMotionState()->getWorldTransform(transform);
				body->objectFromWorld = fromBt(transform);
				body->linearVelocityMetersPerSecond = fromBt(body->rigidBody->getLinearVelocity());
				body->angularVelocityRadiansPerSecond = fromBt(body->rigidBody->getAngularVelocity());

				if (body->rigidBody->getActivationState() == ISLAND_SLEEPING ||
					(length(body->linearVelocityMetersPerSecond) < SleepLinearThreshold && length(body->angularVelocityRadiansPerSecond) < SleepAngularThreshold))
					body->sleepAccumulatorSeconds += fixedDeltaSeconds;
				else
					body->sleepAccumulatorSeconds = 0.0f;
			}
#else
			body->linearVelocityMetersPerSecond.y -= 9.81f * fixedDeltaSeconds;
			SwgVrPhysics::Vector3 position = positionOf(body->objectFromWorld);
			position = add(position, scale(body->linearVelocityMetersPerSecond, fixedDeltaSeconds));

			float const floorHeight = body->halfExtentsMeters.y;
			if (position.y <= floorHeight)
			{
				position.y = floorHeight;
				body->linearVelocityMetersPerSecond.x *= 0.86f;
				body->linearVelocityMetersPerSecond.z *= 0.86f;
				body->linearVelocityMetersPerSecond.y = std::fabs(body->linearVelocityMetersPerSecond.y) * 0.22f;
				body->angularVelocityRadiansPerSecond = scale(body->angularVelocityRadiansPerSecond, 0.72f);
			}

			setPosition(body->objectFromWorld, position);
			if (length(body->linearVelocityMetersPerSecond) < SleepLinearThreshold && length(body->angularVelocityRadiansPerSecond) < SleepAngularThreshold)
				body->sleepAccumulatorSeconds += fixedDeltaSeconds;
			else
				body->sleepAccumulatorSeconds = 0.0f;
#endif

			bool const completedNow = !body->finalPlacementDispatched && body->sleepAccumulatorSeconds >= SleepHoldSeconds;
			SwgVrPhysics::GrabbedObjectState activeState;
			activeState.networkId = body->networkId;
			activeState.flags = SwgVrPhysics::ObjectFlagPhysicsActive | (completedNow ? SwgVrPhysics::ObjectFlagSleeping : 0);
			activeState.grabbingHand = 0;
			activeState.lastUpdateSeconds = 0.0;
			activeState.objectFromWorld = body->objectFromWorld;
			activeState.halfExtentsMeters = body->halfExtentsMeters;
			activeState.linearVelocityMetersPerSecond = body->linearVelocityMetersPerSecond;
			activeState.angularVelocityRadiansPerSecond = body->angularVelocityRadiansPerSecond;
			activeStates.push_back(activeState);

			if (completedNow)
			{
				body->finalPlacementDispatched = true;
				SwgVrPhysics::Vector3 const position = positionOf(body->objectFromWorld);
				SwgVrPhysics::FinalPlacementState placement;
				placement.networkId = body->networkId;
				placement.sleepTimeSeconds = 0.0;
				placement.objectFromWorld = body->objectFromWorld;
				placement.positionMeters = position;
#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
				placement.orientation = body->rigidBody ? orientationFromBt(body->rigidBody->getWorldTransform()) : identityQuaternion();
#else
				placement.orientation = identityQuaternion();
#endif
				completed.push_back(placement);

#if defined(SWG_VR_PHYSICS_USE_BULLET) && SWG_VR_PHYSICS_USE_BULLET
				if (m_world && m_world->dynamicsWorld && body->rigidBody)
					m_world->dynamicsWorld->removeRigidBody(body->rigidBody);
#endif

				BodyMap::iterator eraseIt = it++;
				delete body;
				m_bodies.erase(eraseIt);
			}
			else
			{
				++it;
			}
		}
	}

	for (std::vector<SwgVrPhysics::GrabbedObjectState>::const_iterator it = activeStates.begin(); it != activeStates.end(); ++it)
		SWGVRPhysics_PublishSimulatedObjectState(&(*it));

	for (std::vector<SwgVrPhysics::FinalPlacementState>::const_iterator it = completed.begin(); it != completed.end(); ++it)
		dispatchFinalPlacement(*it);
}

void VRPhysicsManager::dispatchFinalPlacement(SwgVrPhysics::FinalPlacementState const &placement)
{
	FinalPlacementCallback callback = 0;
	void *context = 0;
	{
		ScopedCriticalSection lock(m_mutex);
		callback = m_finalPlacementCallback;
		context = m_finalPlacementCallbackContext;
	}

	if (callback)
		callback(placement, context);
	else
		SWGVRPhysics_PublishFinalPlacement(&placement);
}

SwgVrPhysics::Vector3 VRPhysicsManager::estimateLinearVelocity(uint32_t hand, SwgVrPhysics::Vector3 fallbackVelocity) const
{
	if (hand >= SwgVrPhysics::Hand_Count)
		return fallbackVelocity;

	ScopedCriticalSection lock(m_mutex);
	ControllerHistory const &history = m_controllerHistory[hand];
	if (history.count < 2)
		return fallbackVelocity;

	size_t const newestIndex = (history.next + 5 - 1) % 5;
	size_t const oldestIndex = (history.next + 5 - history.count) % 5;
	ControllerSample const &newest = history.samples[newestIndex];
	ControllerSample const &oldest = history.samples[oldestIndex];
	double const deltaSeconds = newest.sampleTimeSeconds - oldest.sampleTimeSeconds;
	if (deltaSeconds <= 0.0001)
		return fallbackVelocity;

	return scale(subtract(positionOf(newest.gripFromWorld), positionOf(oldest.gripFromWorld)), static_cast<float>(1.0 / deltaSeconds));
}

SwgVrPhysics::Vector3 VRPhysicsManager::estimateAngularVelocity(uint32_t hand, SwgVrPhysics::Vector3 fallbackVelocity) const
{
	if (hand >= SwgVrPhysics::Hand_Count)
		return fallbackVelocity;

	ScopedCriticalSection lock(m_mutex);
	ControllerHistory const &history = m_controllerHistory[hand];
	if (history.count < 2)
		return fallbackVelocity;

	size_t const newestIndex = (history.next + 5 - 1) % 5;
	size_t const oldestIndex = (history.next + 5 - history.count) % 5;
	ControllerSample const &newest = history.samples[newestIndex];
	ControllerSample const &oldest = history.samples[oldestIndex];
	double const deltaSeconds = newest.sampleTimeSeconds - oldest.sampleTimeSeconds;
	if (deltaSeconds <= 0.0001)
		return fallbackVelocity;

	SwgVrPhysics::Matrix4x4 const &a = newest.gripFromWorld;
	SwgVrPhysics::Matrix4x4 const &b = oldest.gripFromWorld;

	float const r00 = a.m[0] * b.m[0] + a.m[1] * b.m[1] + a.m[2] * b.m[2];
	float const r11 = a.m[4] * b.m[4] + a.m[5] * b.m[5] + a.m[6] * b.m[6];
	float const r22 = a.m[8] * b.m[8] + a.m[9] * b.m[9] + a.m[10] * b.m[10];
	float const trace = r00 + r11 + r22;
	float const cosAngle = clampFloat((trace - 1.0f) * 0.5f, -1.0f, 1.0f);
	float const angle = std::acos(cosAngle);
	if (angle < 0.0001f)
		return fallbackVelocity;

	float const r21 = a.m[8] * b.m[4] + a.m[9] * b.m[5] + a.m[10] * b.m[6];
	float const r12 = a.m[4] * b.m[8] + a.m[5] * b.m[9] + a.m[6] * b.m[10];
	float const r02 = a.m[0] * b.m[8] + a.m[1] * b.m[9] + a.m[2] * b.m[10];
	float const r20 = a.m[8] * b.m[0] + a.m[9] * b.m[1] + a.m[10] * b.m[2];
	float const r10 = a.m[4] * b.m[0] + a.m[5] * b.m[1] + a.m[6] * b.m[2];
	float const r01 = a.m[0] * b.m[4] + a.m[1] * b.m[5] + a.m[2] * b.m[6];

	SwgVrPhysics::Vector3 axis = vector3(r21 - r12, r02 - r20, r10 - r01);
	float const axisLength = length(axis);
	if (axisLength < 0.0001f)
		return fallbackVelocity;

	float const scalar = static_cast<float>(angle / (axisLength * deltaSeconds));
	return scale(axis, scalar);
}
