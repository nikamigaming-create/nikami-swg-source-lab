// ======================================================================
//
// VrBodyTransformModifier.h
//
// Per-bone TransformModifier that reads pre-solved transforms from
// VrBodyIK and applies them to the player creature skeleton.
//
// One instance is attached per driven bone via
//   SkeletalAppearance2::addTransformModifierTakeOwnership.
// The modifier is stateless; all solved data lives in VrBodyIK.
//
// ======================================================================

#ifndef INCLUDED_VrBodyTransformModifier_H
#define INCLUDED_VrBodyTransformModifier_H

// ======================================================================

#if defined(SWG_VR_PHYSICS_CLIENTOBJECT_HOOK)

#include "clientSkeletalAnimation/TransformModifier.h"
#include "sharedMath/Vector.h"

// ======================================================================

class VrBodyTransformModifier : public TransformModifier
{
public:

	enum Mode
	{
		M_alignBoneAxis,
		M_matchSolvedOrientation,
		M_applyLocalDelta,
		M_alignBoneAxisAndPosition,
		M_matchSolvedPosition,
		M_matchSolvedTransform
	};

	explicit VrBodyTransformModifier(Vector const &alignedBoneAxis_l, Mode mode = M_alignBoneAxis);
	virtual ~VrBodyTransformModifier();

	// TransformModifier interface.
	// If VrBodyIK has a solved result for transformName, overwrite
	// transform_l2o with it and return true.
	// Otherwise fall through and return false (engine keeps current transform).
	virtual bool modifyTransform(
		float             elapsedTime,
		Skeleton const   &skeleton,
		CrcString const  &transformName,
		Transform const  &transform_p2o,
		Transform const  &transform_l2p,
		Transform        &transform_l2o);

private:

	Vector m_alignedBoneAxis_l;
	Mode   m_mode;

	// Disabled.
	VrBodyTransformModifier();
	VrBodyTransformModifier(VrBodyTransformModifier const &);
	VrBodyTransformModifier &operator=(VrBodyTransformModifier const &);
};

// ======================================================================

#endif // SWG_VR_PHYSICS_CLIENTOBJECT_HOOK

// ======================================================================

#endif // INCLUDED_VrBodyTransformModifier_H
