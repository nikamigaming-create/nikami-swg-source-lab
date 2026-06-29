// ======================================================================
//
// SetupClientSkeletalAnimation.cpp
// Copyright 2001, 2002 Sony Online Entertainment, Inc.
// All Rights Reserved.
//
// ======================================================================

#include "clientSkeletalAnimation/FirstClientSkeletalAnimation.h"
#include "clientSkeletalAnimation/SetupClientSkeletalAnimation.h"

#include "clientSkeletalAnimation/ActionGeneratorSkeletalAnimationTemplate.h"
#include "clientSkeletalAnimation/AnimationEnvironment.h"
#include "clientSkeletalAnimation/AnimationHeldItemMapper.h"
#include "clientSkeletalAnimation/AnimationMessageActionTemplate.h"
#include "clientSkeletalAnimation/AnimationNotification.h"
#include "clientSkeletalAnimation/AnimationPostureMapper.h"
#include "clientSkeletalAnimation/AnimationPriorityMap.h"
#include "clientSkeletalAnimation/AnimationStateNameIdManager.h"
#include "clientSkeletalAnimation/AnimationStateHierarchyTemplateList.h"
#include "clientSkeletalAnimation/BasicSkeletonTemplate.h"
#include "clientSkeletalAnimation/CallbackAnimationNotification.h"
#include "clientSkeletalAnimation/CharacterLodManager.h"
#include "clientSkeletalAnimation/CompositeMesh.h"
#include "clientSkeletalAnimation/CompressedKeyframeAnimation.h"
#include "clientSkeletalAnimation/CompressedKeyframeAnimationTemplate.h"
#include "clientSkeletalAnimation/ConfigClientSkeletalAnimation.h"
#include "clientSkeletalAnimation/DirectionSkeletalAnimation.h"
#include "clientSkeletalAnimation/DirectionSkeletalAnimationTemplate.h"
#include "clientSkeletalAnimation/EditableAnimationState.h"
#include "clientSkeletalAnimation/EditableAnimationStateHierarchyTemplate.h"
#include "clientSkeletalAnimation/EditableBasicAnimationAction.h"
#include "clientSkeletalAnimation/EditableMovementAnimationAction.h"
#include "clientSkeletalAnimation/FullGeometrySkeletalAppearanceBatchRenderer.h"
#include "clientSkeletalAnimation/KeyframeSkeletalAnimation.h"
#include "clientSkeletalAnimation/KeyframeSkeletalAnimationTemplate.h"
#include "clientSkeletalAnimation/LodMeshGeneratorTemplate.h"
#include "clientSkeletalAnimation/LodSkeletonTemplate.h"
#include "clientSkeletalAnimation/LogicalAnimationTableTemplate.h"
#include "clientSkeletalAnimation/LogicalAnimationTableTemplateList.h"
#include "clientSkeletalAnimation/LookAtTransformModifier.h"
#include "clientSkeletalAnimation/MeshGeneratorTemplateList.h"
#include "clientSkeletalAnimation/OcclusionZoneSet.h"
#include "clientSkeletalAnimation/OwnerProxyShader.h"
#include "clientSkeletalAnimation/OwnerProxyShaderTemplate.h"
#include "clientSkeletalAnimation/PriorityBlendAnimation.h"
#include "clientSkeletalAnimation/PriorityBlendAnimationTemplate.h"
#include "clientSkeletalAnimation/ProxySkeletalAnimationTemplate.h"
#include "clientSkeletalAnimation/ShowAttachedObjectAction.h"
#include "clientSkeletalAnimation/ShowAttachedObjectActionTemplate.h"
#include "clientSkeletalAnimation/SinglePrioritySkeletalAnimation.h"
#include "clientSkeletalAnimation/SkeletalAnimation.h"
#include "clientSkeletalAnimation/SkeletalAnimationDebugging.h"
#include "clientSkeletalAnimation/SkeletalAnimationTemplate.h"
#include "clientSkeletalAnimation/SkeletalAnimationTemplateList.h"
#include "clientSkeletalAnimation/SkeletalAppearance2.h"
#include "clientSkeletalAnimation/SkeletalAppearanceTemplate.h"
#include "clientSkeletalAnimation/SkeletalMeshGenerator.h"
#include "clientSkeletalAnimation/SkeletalMeshGeneratorTemplate.h"
#include "clientSkeletalAnimation/Skeleton.h"
#include "clientSkeletalAnimation/SkeletonTemplateList.h"
#include "clientSkeletalAnimation/SpeedSkeletalAnimation.h"
#include "clientSkeletalAnimation/StateHierarchyAnimationController.h"
#include "clientSkeletalAnimation/StringSelectorSkeletalAnimationTemplate.h"
#include "clientSkeletalAnimation/SpeedSkeletalAnimationTemplate.h"
#include "clientSkeletalAnimation/SoftwareBlendSkeletalShaderPrimitive.h"
#include "clientSkeletalAnimation/TargetPitchTransformModifier.h"
#include "clientSkeletalAnimation/TimeScaleSkeletalAnimation.h"
#include "clientSkeletalAnimation/TimeScaleSkeletalAnimationTemplate.h"
#include "clientSkeletalAnimation/TrackAnimationController.h"
#include "clientSkeletalAnimation/TransformMaskList.h"
#include "clientSkeletalAnimation/YawSkeletalAnimationTemplate.h"
#include "sharedDebug/InstallTimer.h"

#include <stdio.h>
#include <stdlib.h>

// ======================================================================

const char *const SetupClientSkeletalAnimation::cms_defaultPriorityMapFileName = "animation/priority_map.iff";

namespace
{
	void writeStartupTrace(char const *stage)
	{
		char const *path = getenv("SWG_STARTUP_TRACE_FILE");
		if (!path || !*path)
			return;

		FILE *file = fopen(path, "ab");
		if (!file)
			return;

		fprintf(file, "SetupClientSkeletalAnimation:%s\n", stage);
		fclose(file);
	}
}

// ======================================================================

SetupClientSkeletalAnimation::Data::Data() :
	allowLod0Skipping(true),
	stitchedSkinInheritsFromSelf(false)
{
}

// ======================================================================

void SetupClientSkeletalAnimation::setupGameData(Data &data)
{
	data.allowLod0Skipping = true;
}

// ----------------------------------------------------------------------

void SetupClientSkeletalAnimation::setupToolData(Data &data)
{
	data.allowLod0Skipping = false;
}

// ----------------------------------------------------------------------

void SetupClientSkeletalAnimation::setupViewerData(Data &data)
{
	data.allowLod0Skipping = false;
	data.stitchedSkinInheritsFromSelf = true;
}

// ----------------------------------------------------------------------

void SetupClientSkeletalAnimation::install(Data const &data)
{
	InstallTimer const installTimer("SetupClientSkeletalAnimation::install");
	writeStartupTrace("install-entry");

	ConfigClientSkeletalAnimation::install();
	writeStartupTrace("after-config");

	SkeletalAnimationDebugging::install();
	writeStartupTrace("after-debugging");

	AnimationPriorityMap::install(cms_defaultPriorityMapFileName);
	writeStartupTrace("after-priority-map");
	TrackAnimationController::install();
	writeStartupTrace("after-track-animation-controller");
	StateHierarchyAnimationController::install();
	writeStartupTrace("after-state-hierarchy-controller");
	AnimationStateHierarchyTemplateList::install();
	writeStartupTrace("after-state-hierarchy-template-list");
	LogicalAnimationTableTemplate::install();
	writeStartupTrace("after-logical-animation-table-template");
	LogicalAnimationTableTemplateList::install();
	writeStartupTrace("after-logical-animation-table-template-list");
	AnimationEnvironment::install();
	writeStartupTrace("after-animation-environment");

	Skeleton::install();
	writeStartupTrace("after-skeleton");
	SkeletonTemplateList::install();
	writeStartupTrace("after-skeleton-template-list");
	BasicSkeletonTemplate::install();
	writeStartupTrace("after-basic-skeleton-template");
	LodSkeletonTemplate::install(data.allowLod0Skipping);
	writeStartupTrace("after-lod-skeleton-template");

	TransformMaskList::install();
	writeStartupTrace("after-transform-mask-list");

	AnimationNotification::install();
	writeStartupTrace("after-animation-notification");
	CallbackAnimationNotification::install();
	writeStartupTrace("after-callback-animation-notification");
	SkeletalAnimationTemplateList::install();
	writeStartupTrace("after-skeletal-animation-template-list");
	SkeletalAnimationTemplate::install();
	writeStartupTrace("after-skeletal-animation-template");
	SkeletalAnimation::install();
	writeStartupTrace("after-skeletal-animation");
	BasePriorityBlendAnimation::install();
	writeStartupTrace("after-base-priority-blend-animation");
	SpeedSkeletalAnimation::install();
	writeStartupTrace("after-speed-skeletal-animation");
	CompressedKeyframeAnimationTemplate::install();
	writeStartupTrace("after-compressed-keyframe-animation-template");
	CompressedKeyframeAnimation::install();
	writeStartupTrace("after-compressed-keyframe-animation");
	KeyframeSkeletalAnimationTemplate::install();
	writeStartupTrace("after-keyframe-skeletal-animation-template");
	KeyframeSkeletalAnimation::install();
	writeStartupTrace("after-keyframe-skeletal-animation");
	ProxySkeletalAnimationTemplate::install();
	writeStartupTrace("after-proxy-skeletal-animation-template");
	DirectionSkeletalAnimationTemplate::install();
	writeStartupTrace("after-direction-skeletal-animation-template");
	DirectionSkeletalAnimation::install();
	writeStartupTrace("after-direction-skeletal-animation");
	PriorityBlendAnimationTemplate::install();
	writeStartupTrace("after-priority-blend-animation-template");
	PriorityBlendAnimation::install();
	writeStartupTrace("after-priority-blend-animation");
	SinglePrioritySkeletalAnimation::install();
	writeStartupTrace("after-single-priority-skeletal-animation");
	SpeedSkeletalAnimationTemplate::install();
	writeStartupTrace("after-speed-skeletal-animation-template");
	StringSelectorSkeletalAnimationTemplate::install();
	writeStartupTrace("after-string-selector-skeletal-animation-template");
	TimeScaleSkeletalAnimationTemplate::install();
	writeStartupTrace("after-time-scale-skeletal-animation-template");
	TimeScaleSkeletalAnimation::install();
	writeStartupTrace("after-time-scale-skeletal-animation");
	ActionGeneratorSkeletalAnimationTemplate::install();
	writeStartupTrace("after-action-generator-skeletal-animation-template");
	YawSkeletalAnimationTemplate::install();
	writeStartupTrace("after-yaw-skeletal-animation-template");

	OcclusionZoneSet::install();
	writeStartupTrace("after-occlusion-zone-set");
	CompositeMesh::install();
	writeStartupTrace("after-composite-mesh");
	SkeletalAppearanceTemplate::install();
	writeStartupTrace("after-skeletal-appearance-template");
	SkeletalAppearance2::install();
	writeStartupTrace("after-skeletal-appearance2");
	SoftwareBlendSkeletalShaderPrimitive::install();
	writeStartupTrace("after-software-blend-skeletal-shader-primitive");

	MeshGeneratorTemplateList::install();
	writeStartupTrace("after-mesh-generator-template-list");
	SkeletalMeshGenerator::install();
	writeStartupTrace("after-skeletal-mesh-generator");
	SkeletalMeshGeneratorTemplate::install();
	writeStartupTrace("after-skeletal-mesh-generator-template");
	LodMeshGeneratorTemplate::install(data.allowLod0Skipping);
	writeStartupTrace("after-lod-mesh-generator-template");

	AnimationMessageActionTemplate::install();
	writeStartupTrace("after-animation-message-action-template");
	ShowAttachedObjectAction::install();
	writeStartupTrace("after-show-attached-object-action");
	ShowAttachedObjectActionTemplate::install();
	writeStartupTrace("after-show-attached-object-action-template");

	AnimationStateNameIdManager::install();
	writeStartupTrace("after-animation-state-name-id-manager");

	// @todo -TRF- Someday make non-editable, more efficient versions and only use these editable ones in the AnimationEditor.
	EditableBasicAnimationAction::install();
	writeStartupTrace("after-editable-basic-animation-action");
	EditableMovementAnimationAction::install();
	writeStartupTrace("after-editable-movement-animation-action");
	EditableAnimationState::install();
	writeStartupTrace("after-editable-animation-state");
	EditableAnimationStateHierarchyTemplate::install();
	writeStartupTrace("after-editable-animation-state-hierarchy-template");

	AnimationHeldItemMapper::install("animation/held_item_map.iff");
	writeStartupTrace("after-held-item-mapper");
	AnimationPostureMapper::install("animation/posture_map.iff");
	writeStartupTrace("after-posture-mapper");

	OwnerProxyShaderTemplate::install();
	writeStartupTrace("after-owner-proxy-shader-template");
	OwnerProxyShader::install(data.stitchedSkinInheritsFromSelf);
	writeStartupTrace("after-owner-proxy-shader");

	FullGeometrySkeletalAppearanceBatchRenderer::install();
	writeStartupTrace("after-full-geometry-batch-renderer");

	LookAtTransformModifier::install();
	writeStartupTrace("after-look-at-transform-modifier");
	TargetPitchTransformModifier::install();
	writeStartupTrace("after-target-pitch-transform-modifier");

	CharacterLodManager::install();
	writeStartupTrace("after-character-lod-manager");
	writeStartupTrace("install-exit");
}

// ======================================================================
