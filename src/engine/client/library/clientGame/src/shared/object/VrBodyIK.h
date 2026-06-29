// ======================================================================
//
// VrBodyIK.h
//
// Full-body VR inverse kinematics solver for SWG.
// Implements VRIK-style embodiment using open algorithms (Two-Bone IK for
// arms, lean-blend for spine/head) driven by HMD + 2-controller OpenXR data.
//
// Only compiled when SWG_VR_PHYSICS_CLIENTOBJECT_HOOK is defined.
//
// ======================================================================

#ifndef INCLUDED_VrBodyIK_H
#define INCLUDED_VrBodyIK_H

// ======================================================================

#if defined(SWG_VR_PHYSICS_CLIENTOBJECT_HOOK)

// ======================================================================

class CreatureObject;
class CrcString;
class Transform;

// ======================================================================
//
// VrBodyIK
//
// Static (singleton-style) IK system.
//
// Call once per player-creature frame:
//   VrBodyIK::update(creature)
//
// TransformModifiers attached to the creature's SkeletalAppearance2 call
//   VrBodyIK::getSolvedTransform(boneName, l2o)
// to retrieve the pre-solved bone transform for this frame.
//
// Call VrBodyIK::reset() on logout / creature change.
//
// ======================================================================

class VrBodyIK
{
public:

	// Returns true when the body IK system is available.
	static bool isEnabled();

	// Main per-frame update.  Must be called before any modifier reads.
	// creature is the local player creature.
	static void update(CreatureObject &creature);

	// True if a solve ran successfully this frame and getSolvedTransform
	// will return valid results.
	static bool isActive();

	// Runtime arm IK blend weight. 1 = fully driven by VR hands, 0 = let the
	// normal locomotion animation own the arms.
	static float getArmIkWeight();

	// Retrieve the solved object-space transform for a named bone.
	// Returns false if the bone is not driven by the full-body IK this frame.
	static bool getSolvedTransform(CrcString const &boneName, Transform &l2o_out);

	// Attach per-bone TransformModifiers to the player creature's skeletal
	// appearance.  Safe to call every frame; no-ops after first successful attach.
	static void ensureModifiersAttached(CreatureObject &creature);

	// Mark cached modifier attachment state dirty after the owning appearance
	// clears its transform modifiers.
	static void markModifiersDirty();

	// Remove and reset all body-IK modifiers from the creature appearance.
	static void detachModifiers(CreatureObject &creature);

	// Trigger a recalibration snapshot from current HMD and controller poses.
	// Should be called when the player presses a recalibrate key.
	static void recalibrate(CreatureObject &creature);

	// Reset all runtime state (call on logout / creature destroy).
	static void reset();

private:

	// Not instantiable.
	VrBodyIK();
	~VrBodyIK();
	VrBodyIK(VrBodyIK const &);
	VrBodyIK &operator=(VrBodyIK const &);
};

// ======================================================================

#endif // SWG_VR_PHYSICS_CLIENTOBJECT_HOOK

// ======================================================================

#endif // INCLUDED_VrBodyIK_H
