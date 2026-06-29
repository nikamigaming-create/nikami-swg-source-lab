// ======================================================================
//
// VrDetachedHands.h
//
// Detached first-person VR hand rig.  This never modifies the live player
// skeleton; it renders a separate hand-only SWG asset driven by OpenXR
// controller poses from the D3D11 bridge.
//
// ======================================================================

#ifndef INCLUDED_VrDetachedHands_H
#define INCLUDED_VrDetachedHands_H

// ======================================================================

class CreatureObject;
class CellProperty;
class CrcLowerString;
class Vector;

// ======================================================================

class VrDetachedHands
{
public:
	static void setMenuPreviewCreature(CreatureObject *creature);
	static void update(CreatureObject &creature);
	static void updateMenuPreview();
	static void renderMenuPreviewStereo();
	static bool getRightWeaponMuzzlePosition_w(CrcLowerString const &hardpointName, Vector &position_w, CellProperty const *&cellProperty);
	static void reset();
};

// ======================================================================

#endif
