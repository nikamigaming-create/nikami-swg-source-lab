// ======================================================================
//
// Direct3d11_VrBridge.cpp
//
// Gated OG D3D11 VR bridge shell. This module is intentionally inert unless
// the VR environment flag is set; flat DX11 rendering must remain unchanged.
//
// ======================================================================

#include "FirstDirect3d11.h"
#include "Direct3d11_VrBridge.h"
#include "VRPhysicsBridge.h"
#include "VRPhysicsManager.h"

#include <dxgi.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <windows.h>

// ======================================================================

namespace
{
	float clampFloat(float value, float low, float high);

	typedef unsigned int XrBool32;
	typedef unsigned long long XrFlags64;
	typedef unsigned long long XrInstance;
	typedef unsigned long long XrSystemId;
	typedef unsigned long long XrVersion;
	typedef unsigned long long XrSession;
	typedef unsigned long long XrSpace;
	typedef unsigned long long XrSwapchain;
	typedef unsigned long long XrPath;
	typedef unsigned long long XrActionSet;
	typedef unsigned long long XrAction;
	typedef unsigned long long XrHandTrackerEXT;
	typedef long long XrTime;
	typedef long long XrDuration;
	typedef unsigned long long XrSwapchainUsageFlags;
	typedef unsigned long long XrCompositionLayerFlags;
	typedef int XrResult;
	typedef int XrStructureType;
	typedef int XrFormFactor;
	typedef int XrViewConfigurationType;
	typedef int XrEnvironmentBlendMode;
	typedef int XrReferenceSpaceType;
	typedef int XrEyeVisibility;
	typedef int XrActionType;
	typedef int XrHandEXT;
	typedef int XrHandJointEXT;
	typedef int XrHandJointSetEXT;
	typedef XrFlags64 XrViewStateFlags;
	typedef XrFlags64 XrSpaceLocationFlags;
	typedef void (*PFN_xrVoidFunction)(void);

	XrInstance const XR_NULL_HANDLE_LOCAL = 0;
	XrSystemId const XR_NULL_SYSTEM_ID_LOCAL = 0;
	XrPath const XR_NULL_PATH_LOCAL = 0;
	XrResult const XR_SUCCESS_LOCAL = 0;
	XrResult const XR_EVENT_UNAVAILABLE_LOCAL = 4;
	XrStructureType const XR_TYPE_EXTENSION_PROPERTIES_LOCAL = 2;
	XrStructureType const XR_TYPE_INSTANCE_CREATE_INFO_LOCAL = 3;
	XrStructureType const XR_TYPE_SYSTEM_GET_INFO_LOCAL = 4;
	XrStructureType const XR_TYPE_SYSTEM_PROPERTIES_LOCAL = 5;
	XrStructureType const XR_TYPE_SESSION_BEGIN_INFO_LOCAL = 10;
	XrStructureType const XR_TYPE_FRAME_END_INFO_LOCAL = 12;
	XrStructureType const XR_TYPE_EVENT_DATA_BUFFER_LOCAL = 16;
	XrStructureType const XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED_LOCAL = 18;
	XrStructureType const XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING_LOCAL = 40;
	XrStructureType const XR_TYPE_ACTION_STATE_BOOLEAN_LOCAL = 23;
	XrStructureType const XR_TYPE_ACTION_STATE_FLOAT_LOCAL = 24;
	XrStructureType const XR_TYPE_ACTION_STATE_VECTOR2F_LOCAL = 25;
	XrStructureType const XR_TYPE_ACTION_STATE_POSE_LOCAL = 27;
	XrStructureType const XR_TYPE_ACTION_SET_CREATE_INFO_LOCAL = 28;
	XrStructureType const XR_TYPE_ACTION_CREATE_INFO_LOCAL = 29;
	XrStructureType const XR_TYPE_SESSION_CREATE_INFO_LOCAL = 8;
	XrStructureType const XR_TYPE_SWAPCHAIN_CREATE_INFO_LOCAL = 9;
	XrStructureType const XR_TYPE_INSTANCE_PROPERTIES_LOCAL = 32;
	XrStructureType const XR_TYPE_VIEW_LOCATE_INFO_LOCAL = 6;
	XrStructureType const XR_TYPE_VIEW_LOCAL = 7;
	XrStructureType const XR_TYPE_FRAME_WAIT_INFO_LOCAL = 33;
	XrStructureType const XR_TYPE_COMPOSITION_LAYER_PROJECTION_LOCAL = 35;
	XrStructureType const XR_TYPE_COMPOSITION_LAYER_QUAD_LOCAL = 36;
	XrStructureType const XR_TYPE_REFERENCE_SPACE_CREATE_INFO_LOCAL = 37;
	XrStructureType const XR_TYPE_ACTION_SPACE_CREATE_INFO_LOCAL = 38;
	XrStructureType const XR_TYPE_SPACE_LOCATION_LOCAL = 42;
	XrStructureType const XR_TYPE_VIEW_STATE_LOCAL = 11;
	XrStructureType const XR_TYPE_FRAME_STATE_LOCAL = 44;
	XrStructureType const XR_TYPE_FRAME_BEGIN_INFO_LOCAL = 46;
	XrStructureType const XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW_LOCAL = 48;
	XrStructureType const XR_TYPE_VIEW_CONFIGURATION_VIEW_LOCAL = 41;
	XrStructureType const XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING_LOCAL = 51;
	XrStructureType const XR_TYPE_INTERACTION_PROFILE_STATE_LOCAL = 53;
	XrStructureType const XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL = 55;
	XrStructureType const XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL = 56;
	XrStructureType const XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL = 57;
	XrStructureType const XR_TYPE_ACTION_STATE_GET_INFO_LOCAL = 58;
	XrStructureType const XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO_LOCAL = 60;
	XrStructureType const XR_TYPE_ACTIONS_SYNC_INFO_LOCAL = 61;
	XrStructureType const XR_TYPE_GRAPHICS_BINDING_D3D11_KHR_LOCAL = 1000027000;
	XrStructureType const XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR_LOCAL = 1000027001;
	XrStructureType const XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR_LOCAL = 1000027002;
	XrStructureType const XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT_LOCAL = 1000051000;
	XrStructureType const XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT_LOCAL = 1000051001;
	XrStructureType const XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT_LOCAL = 1000051002;
	XrStructureType const XR_TYPE_HAND_JOINT_LOCATIONS_EXT_LOCAL = 1000051003;
	XrActionType const XR_ACTION_TYPE_BOOLEAN_INPUT_LOCAL = 1;
	XrActionType const XR_ACTION_TYPE_FLOAT_INPUT_LOCAL = 2;
	XrActionType const XR_ACTION_TYPE_VECTOR2F_INPUT_LOCAL = 3;
	XrActionType const XR_ACTION_TYPE_POSE_INPUT_LOCAL = 4;
	XrHandEXT const XR_HAND_LEFT_EXT_LOCAL = 1;
	XrHandEXT const XR_HAND_RIGHT_EXT_LOCAL = 2;
	XrHandJointSetEXT const XR_HAND_JOINT_SET_DEFAULT_EXT_LOCAL = 0;
	XrHandJointEXT const XR_HAND_JOINT_PALM_EXT_LOCAL = 0;
	XrHandJointEXT const XR_HAND_JOINT_WRIST_EXT_LOCAL = 1;
	XrHandJointEXT const XR_HAND_JOINT_INDEX_PROXIMAL_EXT_LOCAL = 7;
	XrHandJointEXT const XR_HAND_JOINT_INDEX_TIP_EXT_LOCAL = 10;
	unsigned int const XR_HAND_JOINT_COUNT_EXT_LOCAL = 26;
	XrFormFactor const XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY_LOCAL = 1;
	XrViewConfigurationType const XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_LOCAL = 2;
	XrReferenceSpaceType const XR_REFERENCE_SPACE_TYPE_LOCAL_LOCAL = 1;
	XrReferenceSpaceType const XR_REFERENCE_SPACE_TYPE_STAGE_LOCAL = 3;
	XrEnvironmentBlendMode const XR_ENVIRONMENT_BLEND_MODE_OPAQUE_LOCAL = 1;
	XrEyeVisibility const XR_EYE_VISIBILITY_BOTH_LOCAL = 0;
	XrSwapchainUsageFlags const XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT_LOCAL = 0x00000001;
	XrSwapchainUsageFlags const XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT_LOCAL = 0x00000010;
	XrSwapchainUsageFlags const XR_SWAPCHAIN_USAGE_SAMPLED_BIT_LOCAL = 0x00000020;
	int const XR_SESSION_STATE_READY_LOCAL = 2;
	int const XR_SESSION_STATE_STOPPING_LOCAL = 6;
	int const XR_SESSION_STATE_EXITING_LOCAL = 8;
	XrSpaceLocationFlags const XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL = 0x00000001;
	XrSpaceLocationFlags const XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL = 0x00000002;
	XrSpaceLocationFlags const XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT_LOCAL = 0x00000004;
	XrSpaceLocationFlags const XR_SPACE_LOCATION_POSITION_TRACKED_BIT_LOCAL = 0x00000008;
	XrViewStateFlags const XR_VIEW_STATE_ORIENTATION_VALID_BIT_LOCAL = 0x00000001;
	XrViewStateFlags const XR_VIEW_STATE_POSITION_VALID_BIT_LOCAL = 0x00000002;
	XrDuration const XR_INFINITE_DURATION_LOCAL = 0x7fffffffffffffffLL;
	int const XR_MAX_ACTION_SET_NAME_SIZE_LOCAL = 64;
	int const XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE_LOCAL = 128;
	int const XR_MAX_ACTION_NAME_SIZE_LOCAL = 64;
	int const XR_MAX_LOCALIZED_ACTION_NAME_SIZE_LOCAL = 128;

	ID3D11Device *ms_device;
	ID3D11DeviceContext *ms_context;
	HWND ms_window;
	Direct3d11_VrBridge::VrUiInputFunction ms_vrUiInput;
	Direct3d11_VrBridge::VrMouseInputFunction ms_vrMouseInput;
	Direct3d11_VrBridge::VrControllerInputFunction ms_vrControllerInput;
	Direct3d11_VrBridge::VrMenuInputFunction ms_vrMenuInput;
	bool ms_installed;
	bool ms_vrTvModeEnabled = true;
	bool ms_lastSubmittedTvModeEnabled = true;
	int ms_frameCount;
	int ms_backBufferSubmitCount;
	int ms_pointerRaySubmitCount;
	HMODULE ms_openXrLoader;
	bool ms_openXrProbeReady;
	bool ms_openXrFrameSubmitReady;
	XrInstance ms_xrInstance;
	XrSystemId ms_xrSystemId;
	XrSession ms_xrSession;
	XrSpace ms_xrLocalSpace;
	XrSpace ms_xrWorldRenderSpace;
	XrReferenceSpaceType ms_xrReferenceSpaceType;
	XrSwapchain ms_xrQuadSwapchain;
	unsigned int ms_xrQuadSwapchainWidth;
	unsigned int ms_xrQuadSwapchainHeight;
	long long ms_xrQuadSwapchainFormat;
	std::vector<ID3D11RenderTargetView *> ms_xrQuadRenderTargetViews;
	XrSwapchain ms_xrProjectionSwapchain;
	unsigned int ms_xrProjectionSwapchainWidth;
	unsigned int ms_xrProjectionSwapchainHeight;
	unsigned int ms_xrRecommendedEyeWidth;
	unsigned int ms_xrRecommendedEyeHeight;
	unsigned int ms_xrMaxSwapchainImageWidth;
	unsigned int ms_xrMaxSwapchainImageHeight;
	unsigned int ms_xrMaxLayerCount;
	bool ms_xrProjectionLayerLogged;
	bool ms_xrWorldFrameBegun;
	bool ms_xrWorldFrameSubmitted;
	bool ms_xrWorldEyeRendered[2];
	bool ms_xrProjectionImageReady;
	unsigned int ms_xrProjectionImageIndex;
	int ms_xrWorldHeadOriginFrameCount;
	bool ms_xrWorldHeadOriginSettled;
	bool ms_xrWorldBaseSpaceJustRecentered;
	bool ms_xrWorldYawCaptured;
	float ms_xrWorldYawCos;
	float ms_xrWorldYawSin;
	float ms_xrWorldYawHalfCos;
	float ms_xrWorldYawHalfSin;
	bool ms_xrSessionBegun;
	bool ms_xrSessionRunning;
	int ms_xrSessionState;
	bool ms_xrIdleRecycleAttempted;
	int ms_xrQuadSubmitCount;
	bool ms_xrQuadPoseCaptured;
	bool ms_xrQuadHeadPoseCaptured;
	bool ms_xrLoadingHeadPoseCaptured;
	bool ms_xrQuadAnchorFailureLogged;
	int ms_xrSuppressControllerInputFrames;
	bool ms_xrUiQuadReady;
	float ms_xrUiQuadWidthMeters;
	float ms_xrUiQuadHeightMeters;
	bool ms_xrActionFunctionsReady;
	bool ms_xrControllerActionsReady;
	bool ms_xrHandTrackingExtensionSupported;
	bool ms_xrHandTrackingSystemSupported;
	bool ms_xrHandTrackingFunctionsReady;
	bool ms_xrHandTrackingReady;
	int ms_xrControllerSnapshotCount;
	int ms_xrHandTrackingSnapshotCount;
	bool ms_xrMenuButtonDown;
	bool ms_xrWristMenuButtonDown;
	bool ms_xrTargetNextButtonDown;
	bool ms_xrTargetPreviousButtonDown;
	bool ms_xrRightTriggerDown;
	int ms_xrRightTriggerClientX;
	int ms_xrRightTriggerClientY;
	int ms_xrRightTriggerLastClientX;
	int ms_xrRightTriggerLastClientY;
	int ms_xrUiTriggerHandIndex;
	bool ms_xrGripMouseDown;
	int ms_xrGripMouseClientX;
	int ms_xrGripMouseClientY;
	int ms_xrGripMouseHandIndex;
	double ms_xrGripFallbackStartSeconds[2];
	bool ms_vrPhysicsPreviousHandValid[2];
	bool ms_vrPhysicsPreviousHandOrientationValid[2];
	double ms_vrPhysicsPreviousHandPositionTimeSeconds[2];
	double ms_vrPhysicsPreviousHandOrientationTimeSeconds[2];
	SwgVrPhysics::Vector3 ms_vrPhysicsPreviousHandPosition[2];
	SwgVrPhysics::Quaternion ms_vrPhysicsPreviousHandOrientation[2];
	VRPhysicsManager *ms_vrPhysicsManager;

	struct XrApplicationInfoLocal
	{
		char applicationName[128];
		unsigned int applicationVersion;
		char engineName[128];
		unsigned int engineVersion;
		XrVersion apiVersion;
	};

	struct XrInstanceCreateInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrFlags64 createFlags;
		XrApplicationInfoLocal applicationInfo;
		unsigned int enabledApiLayerCount;
		char const *const *enabledApiLayerNames;
		unsigned int enabledExtensionCount;
		char const *const *enabledExtensionNames;
	};

	struct XrExtensionPropertiesLocal
	{
		XrStructureType type;
		void *next;
		char extensionName[128];
		unsigned int extensionVersion;
	};

	struct XrInstancePropertiesLocal
	{
		XrStructureType type;
		void *next;
		XrVersion runtimeVersion;
		char runtimeName[128];
	};

	struct XrSystemGetInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrFormFactor formFactor;
	};

	struct XrSystemGraphicsPropertiesLocal
	{
		unsigned int maxSwapchainImageHeight;
		unsigned int maxSwapchainImageWidth;
		unsigned int maxLayerCount;
	};

	struct XrSystemTrackingPropertiesLocal
	{
		XrBool32 orientationTracking;
		XrBool32 positionTracking;
	};

	struct XrSystemPropertiesLocal
	{
		XrStructureType type;
		void *next;
		XrSystemId systemId;
		unsigned int vendorId;
		char systemName[256];
		XrSystemGraphicsPropertiesLocal graphicsProperties;
		XrSystemTrackingPropertiesLocal trackingProperties;
	};

	struct XrSystemHandTrackingPropertiesEXTLocal
	{
		XrStructureType type;
		void *next;
		XrBool32 supportsHandTracking;
	};

	struct XrViewConfigurationViewLocal
	{
		XrStructureType type;
		void *next;
		unsigned int recommendedImageRectWidth;
		unsigned int maxImageRectWidth;
		unsigned int recommendedImageRectHeight;
		unsigned int maxImageRectHeight;
		unsigned int recommendedSwapchainSampleCount;
		unsigned int maxSwapchainSampleCount;
	};

	struct XrGraphicsBindingD3D11KHRLocal
	{
		XrStructureType type;
		void const *next;
		ID3D11Device *device;
	};

	struct XrGraphicsRequirementsD3D11KHRLocal
	{
		XrStructureType type;
		void *next;
		LUID adapterLuid;
		D3D_FEATURE_LEVEL minFeatureLevel;
	};

	struct XrSessionCreateInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrFlags64 createFlags;
		XrSystemId systemId;
	};

	struct XrSwapchainCreateInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrFlags64 createFlags;
		XrSwapchainUsageFlags usageFlags;
		long long format;
		unsigned int sampleCount;
		unsigned int width;
		unsigned int height;
		unsigned int faceCount;
		unsigned int arraySize;
		unsigned int mipCount;
	};

	struct XrSwapchainImageD3D11KHRLocal
	{
		XrStructureType type;
		void *next;
		ID3D11Texture2D *texture;
	};

	struct XrQuaternionfLocal
	{
		float x;
		float y;
		float z;
		float w;
	};

	struct XrVector3fLocal
	{
		float x;
		float y;
		float z;
	};

	struct XrPosefLocal
	{
		XrQuaternionfLocal orientation;
		XrVector3fLocal position;
	};

	struct XrVector2fLocal
	{
		float x;
		float y;
	};

	XrPosefLocal ms_xrQuadPose;
	XrPosefLocal ms_xrQuadHeadPose;
	XrPosefLocal ms_xrLoadingHeadPose;
	XrPosefLocal ms_xrUiQuadPose;

	struct XrActionSetCreateInfoLocal
	{
		XrStructureType type;
		void const *next;
		char actionSetName[XR_MAX_ACTION_SET_NAME_SIZE_LOCAL];
		char localizedActionSetName[XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE_LOCAL];
		unsigned int priority;
	};

	struct XrActionCreateInfoLocal
	{
		XrStructureType type;
		void const *next;
		char actionName[XR_MAX_ACTION_NAME_SIZE_LOCAL];
		XrActionType actionType;
		unsigned int countSubactionPaths;
		XrPath const *subactionPaths;
		char localizedActionName[XR_MAX_LOCALIZED_ACTION_NAME_SIZE_LOCAL];
	};

	struct XrActionSuggestedBindingLocal
	{
		XrAction action;
		XrPath binding;
	};

	struct XrInteractionProfileSuggestedBindingLocal
	{
		XrStructureType type;
		void const *next;
		XrPath interactionProfile;
		unsigned int countSuggestedBindings;
		XrActionSuggestedBindingLocal const *suggestedBindings;
	};

	struct XrInteractionProfileStateLocal
	{
		XrStructureType type;
		void *next;
		XrPath interactionProfile;
	};

	struct XrSessionActionSetsAttachInfoLocal
	{
		XrStructureType type;
		void const *next;
		unsigned int countActionSets;
		XrActionSet const *actionSets;
	};

	struct XrActionStateGetInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrAction action;
		XrPath subactionPath;
	};

	struct XrActionStateFloatLocal
	{
		XrStructureType type;
		void *next;
		float currentState;
		XrBool32 changedSinceLastSync;
		XrTime lastChangeTime;
		XrBool32 isActive;
	};

	struct XrActionStateBooleanLocal
	{
		XrStructureType type;
		void *next;
		XrBool32 currentState;
		XrBool32 changedSinceLastSync;
		XrTime lastChangeTime;
		XrBool32 isActive;
	};

	struct XrActionStateVector2fLocal
	{
		XrStructureType type;
		void *next;
		XrVector2fLocal currentState;
		XrBool32 changedSinceLastSync;
		XrTime lastChangeTime;
		XrBool32 isActive;
	};

	struct XrActionStatePoseLocal
	{
		XrStructureType type;
		void *next;
		XrBool32 isActive;
	};

	struct XrActiveActionSetLocal
	{
		XrActionSet actionSet;
		XrPath subactionPath;
	};

	struct XrActionsSyncInfoLocal
	{
		XrStructureType type;
		void const *next;
		unsigned int countActiveActionSets;
		XrActiveActionSetLocal const *activeActionSets;
	};

	struct XrActionSpaceCreateInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrAction action;
		XrPath subactionPath;
		XrPosefLocal poseInActionSpace;
	};

	struct XrHandTrackerCreateInfoEXTLocal
	{
		XrStructureType type;
		void const *next;
		XrHandEXT hand;
		XrHandJointSetEXT handJointSet;
	};

	struct XrHandJointsLocateInfoEXTLocal
	{
		XrStructureType type;
		void const *next;
		XrSpace baseSpace;
		XrTime time;
	};

	struct XrHandJointLocationEXTLocal
	{
		XrSpaceLocationFlags locationFlags;
		XrPosefLocal pose;
		float radius;
	};

	struct XrHandJointLocationsEXTLocal
	{
		XrStructureType type;
		void *next;
		XrBool32 isActive;
		unsigned int jointCount;
		XrHandJointLocationEXTLocal *jointLocations;
	};

	struct XrSpaceLocationLocal
	{
		XrStructureType type;
		void *next;
		XrSpaceLocationFlags locationFlags;
		XrPosefLocal pose;
	};

	struct XrReferenceSpaceCreateInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrReferenceSpaceType referenceSpaceType;
		XrPosefLocal poseInReferenceSpace;
	};

	struct XrEventDataBufferLocal
	{
		XrStructureType type;
		void const *next;
		char varying[4000];
	};

	struct XrEventDataSessionStateChangedLocal
	{
		XrStructureType type;
		void const *next;
		XrSession session;
		int state;
		XrTime time;
	};

	struct XrEventDataReferenceSpaceChangePendingLocal
	{
		XrStructureType type;
		void const *next;
		XrSession session;
		XrReferenceSpaceType referenceSpaceType;
		XrTime changeTime;
		XrBool32 poseValid;
		XrPosefLocal poseInPreviousSpace;
	};

	struct XrSessionBeginInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrViewConfigurationType primaryViewConfigurationType;
	};

	struct XrFrameWaitInfoLocal
	{
		XrStructureType type;
		void const *next;
	};

	struct XrFrameStateLocal
	{
		XrStructureType type;
		void *next;
		XrTime predictedDisplayTime;
		XrDuration predictedDisplayPeriod;
		XrBool32 shouldRender;
	};

	struct XrFrameBeginInfoLocal
	{
		XrStructureType type;
		void const *next;
	};

	struct XrFovfLocal
	{
		float angleLeft;
		float angleRight;
		float angleUp;
		float angleDown;
	};

	struct XrViewLocal
	{
		XrStructureType type;
		void *next;
		XrPosefLocal pose;
		XrFovfLocal fov;
	};

	struct XrViewLocateInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrViewConfigurationType viewConfigurationType;
		XrTime displayTime;
		XrSpace space;
	};

	struct XrViewStateLocal
	{
		XrStructureType type;
		void *next;
		XrViewStateFlags viewStateFlags;
	};

	struct XrOffset2DiLocal
	{
		int x;
		int y;
	};

	struct XrExtent2DiLocal
	{
		int width;
		int height;
	};

	struct XrRect2DiLocal
	{
		XrOffset2DiLocal offset;
		XrExtent2DiLocal extent;
	};

	struct XrSwapchainSubImageLocal
	{
		XrSwapchain swapchain;
		XrRect2DiLocal imageRect;
		unsigned int imageArrayIndex;
	};

	struct XrExtent2DfLocal
	{
		float width;
		float height;
	};

	struct XrCompositionLayerBaseHeaderLocal
	{
		XrStructureType type;
		void const *next;
		XrCompositionLayerFlags layerFlags;
		XrSpace space;
	};

	struct XrCompositionLayerProjectionViewLocal
	{
		XrStructureType type;
		void const *next;
		XrPosefLocal pose;
		XrFovfLocal fov;
		XrSwapchainSubImageLocal subImage;
	};

	struct XrCompositionLayerProjectionLocal
	{
		XrStructureType type;
		void const *next;
		XrCompositionLayerFlags layerFlags;
		XrSpace space;
		unsigned int viewCount;
		XrCompositionLayerProjectionViewLocal const *views;
	};

	struct XrCompositionLayerQuadLocal
	{
		XrStructureType type;
		void const *next;
		XrCompositionLayerFlags layerFlags;
		XrSpace space;
		XrEyeVisibility eyeVisibility;
		XrSwapchainSubImageLocal subImage;
		XrPosefLocal pose;
		XrExtent2DfLocal size;
	};

	struct XrFrameEndInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrTime displayTime;
		XrEnvironmentBlendMode environmentBlendMode;
		unsigned int layerCount;
		XrCompositionLayerBaseHeaderLocal const *const *layers;
	};

	struct XrSwapchainImageAcquireInfoLocal
	{
		XrStructureType type;
		void const *next;
	};

	struct XrSwapchainImageWaitInfoLocal
	{
		XrStructureType type;
		void const *next;
		XrDuration timeout;
	};

	struct XrSwapchainImageReleaseInfoLocal
	{
		XrStructureType type;
		void const *next;
	};

	typedef XrResult (*PFN_xrGetInstanceProcAddrLocal)(XrInstance, char const *, PFN_xrVoidFunction *);
	typedef XrResult (*PFN_xrCreateInstanceLocal)(XrInstanceCreateInfoLocal const *, XrInstance *);
	typedef XrResult (*PFN_xrDestroyInstanceLocal)(XrInstance);
	typedef XrResult (*PFN_xrEnumerateInstanceExtensionPropertiesLocal)(char const *, unsigned int, unsigned int *, XrExtensionPropertiesLocal *);
	typedef XrResult (*PFN_xrGetInstancePropertiesLocal)(XrInstance, XrInstancePropertiesLocal *);
	typedef XrResult (*PFN_xrGetSystemLocal)(XrInstance, XrSystemGetInfoLocal const *, XrSystemId *);
	typedef XrResult (*PFN_xrGetSystemPropertiesLocal)(XrInstance, XrSystemId, XrSystemPropertiesLocal *);
	typedef XrResult (*PFN_xrEnumerateViewConfigurationsLocal)(XrInstance, XrSystemId, unsigned int, unsigned int *, XrViewConfigurationType *);
	typedef XrResult (*PFN_xrEnumerateViewConfigurationViewsLocal)(XrInstance, XrSystemId, XrViewConfigurationType, unsigned int, unsigned int *, XrViewConfigurationViewLocal *);
	typedef XrResult (*PFN_xrGetD3D11GraphicsRequirementsKHRLocal)(XrInstance, XrSystemId, XrGraphicsRequirementsD3D11KHRLocal *);
	typedef XrResult (*PFN_xrCreateSessionLocal)(XrInstance, XrSessionCreateInfoLocal const *, XrSession *);
	typedef XrResult (*PFN_xrDestroySessionLocal)(XrSession);
	typedef XrResult (*PFN_xrEnumerateSwapchainFormatsLocal)(XrSession, unsigned int, unsigned int *, long long *);
	typedef XrResult (*PFN_xrCreateSwapchainLocal)(XrSession, XrSwapchainCreateInfoLocal const *, XrSwapchain *);
	typedef XrResult (*PFN_xrDestroySwapchainLocal)(XrSwapchain);
	typedef XrResult (*PFN_xrEnumerateSwapchainImagesLocal)(XrSwapchain, unsigned int, unsigned int *, XrSwapchainImageD3D11KHRLocal *);
	typedef XrResult (*PFN_xrPollEventLocal)(XrInstance, XrEventDataBufferLocal *);
	typedef XrResult (*PFN_xrCreateReferenceSpaceLocal)(XrSession, XrReferenceSpaceCreateInfoLocal const *, XrSpace *);
	typedef XrResult (*PFN_xrDestroySpaceLocal)(XrSpace);
	typedef XrResult (*PFN_xrBeginSessionLocal)(XrSession, XrSessionBeginInfoLocal const *);
	typedef XrResult (*PFN_xrEndSessionLocal)(XrSession);
	typedef XrResult (*PFN_xrWaitFrameLocal)(XrSession, XrFrameWaitInfoLocal const *, XrFrameStateLocal *);
	typedef XrResult (*PFN_xrBeginFrameLocal)(XrSession, XrFrameBeginInfoLocal const *);
	typedef XrResult (*PFN_xrEndFrameLocal)(XrSession, XrFrameEndInfoLocal const *);
	typedef XrResult (*PFN_xrLocateViewsLocal)(XrSession, XrViewLocateInfoLocal const *, XrViewStateLocal *, unsigned int, unsigned int *, XrViewLocal *);
	typedef XrResult (*PFN_xrAcquireSwapchainImageLocal)(XrSwapchain, XrSwapchainImageAcquireInfoLocal const *, unsigned int *);
	typedef XrResult (*PFN_xrWaitSwapchainImageLocal)(XrSwapchain, XrSwapchainImageWaitInfoLocal const *);
	typedef XrResult (*PFN_xrReleaseSwapchainImageLocal)(XrSwapchain, XrSwapchainImageReleaseInfoLocal const *);
	typedef XrResult (*PFN_xrStringToPathLocal)(XrInstance, char const *, XrPath *);
	typedef XrResult (*PFN_xrPathToStringLocal)(XrInstance, XrPath, unsigned int, unsigned int *, char *);
	typedef XrResult (*PFN_xrCreateActionSetLocal)(XrInstance, XrActionSetCreateInfoLocal const *, XrActionSet *);
	typedef XrResult (*PFN_xrDestroyActionSetLocal)(XrActionSet);
	typedef XrResult (*PFN_xrCreateActionLocal)(XrActionSet, XrActionCreateInfoLocal const *, XrAction *);
	typedef XrResult (*PFN_xrDestroyActionLocal)(XrAction);
	typedef XrResult (*PFN_xrSuggestInteractionProfileBindingsLocal)(XrInstance, XrInteractionProfileSuggestedBindingLocal const *);
	typedef XrResult (*PFN_xrGetCurrentInteractionProfileLocal)(XrSession, XrPath, XrInteractionProfileStateLocal *);
	typedef XrResult (*PFN_xrAttachSessionActionSetsLocal)(XrSession, XrSessionActionSetsAttachInfoLocal const *);
	typedef XrResult (*PFN_xrSyncActionsLocal)(XrSession, XrActionsSyncInfoLocal const *);
	typedef XrResult (*PFN_xrGetActionStateBooleanLocal)(XrSession, XrActionStateGetInfoLocal const *, XrActionStateBooleanLocal *);
	typedef XrResult (*PFN_xrGetActionStateFloatLocal)(XrSession, XrActionStateGetInfoLocal const *, XrActionStateFloatLocal *);
	typedef XrResult (*PFN_xrGetActionStateVector2fLocal)(XrSession, XrActionStateGetInfoLocal const *, XrActionStateVector2fLocal *);
	typedef XrResult (*PFN_xrGetActionStatePoseLocal)(XrSession, XrActionStateGetInfoLocal const *, XrActionStatePoseLocal *);
	typedef XrResult (*PFN_xrCreateActionSpaceLocal)(XrSession, XrActionSpaceCreateInfoLocal const *, XrSpace *);
	typedef XrResult (*PFN_xrLocateSpaceLocal)(XrSpace, XrSpace, XrTime, XrSpaceLocationLocal *);
	typedef XrResult (*PFN_xrCreateHandTrackerEXTLocal)(XrSession, XrHandTrackerCreateInfoEXTLocal const *, XrHandTrackerEXT *);
	typedef XrResult (*PFN_xrDestroyHandTrackerEXTLocal)(XrHandTrackerEXT);
	typedef XrResult (*PFN_xrLocateHandJointsEXTLocal)(XrHandTrackerEXT, XrHandJointsLocateInfoEXTLocal const *, XrHandJointLocationsEXTLocal *);

	bool ensurePointerSwapchain();
	bool ensureWandSwapchain();
	bool ensureReticleSwapchain();

	PFN_xrDestroyInstanceLocal ms_xrDestroyInstance;
	PFN_xrDestroySessionLocal ms_xrDestroySession;
	PFN_xrEnumerateSwapchainFormatsLocal ms_xrEnumerateSwapchainFormats;
	PFN_xrCreateSwapchainLocal ms_xrCreateSwapchain;
	PFN_xrDestroySwapchainLocal ms_xrDestroySwapchain;
	PFN_xrEnumerateSwapchainImagesLocal ms_xrEnumerateSwapchainImages;
	PFN_xrPollEventLocal ms_xrPollEvent;
	PFN_xrCreateReferenceSpaceLocal ms_xrCreateReferenceSpace;
	PFN_xrDestroySpaceLocal ms_xrDestroySpace;
	PFN_xrBeginSessionLocal ms_xrBeginSession;
	PFN_xrEndSessionLocal ms_xrEndSession;
	PFN_xrWaitFrameLocal ms_xrWaitFrame;
	PFN_xrBeginFrameLocal ms_xrBeginFrame;
	PFN_xrEndFrameLocal ms_xrEndFrame;
	PFN_xrLocateViewsLocal ms_xrLocateViews;
	PFN_xrAcquireSwapchainImageLocal ms_xrAcquireSwapchainImage;
	PFN_xrWaitSwapchainImageLocal ms_xrWaitSwapchainImage;
	PFN_xrReleaseSwapchainImageLocal ms_xrReleaseSwapchainImage;
	PFN_xrStringToPathLocal ms_xrStringToPath;
	PFN_xrPathToStringLocal ms_xrPathToString;
	PFN_xrCreateActionSetLocal ms_xrCreateActionSet;
	PFN_xrDestroyActionSetLocal ms_xrDestroyActionSet;
	PFN_xrCreateActionLocal ms_xrCreateAction;
	PFN_xrDestroyActionLocal ms_xrDestroyAction;
	PFN_xrSuggestInteractionProfileBindingsLocal ms_xrSuggestInteractionProfileBindings;
	PFN_xrGetCurrentInteractionProfileLocal ms_xrGetCurrentInteractionProfile;
	PFN_xrAttachSessionActionSetsLocal ms_xrAttachSessionActionSets;
	PFN_xrSyncActionsLocal ms_xrSyncActions;
	PFN_xrGetActionStateBooleanLocal ms_xrGetActionStateBoolean;
	PFN_xrGetActionStateFloatLocal ms_xrGetActionStateFloat;
	PFN_xrGetActionStateVector2fLocal ms_xrGetActionStateVector2f;
	PFN_xrGetActionStatePoseLocal ms_xrGetActionStatePose;
	PFN_xrCreateActionSpaceLocal ms_xrCreateActionSpace;
	PFN_xrLocateSpaceLocal ms_xrLocateSpace;
	PFN_xrCreateHandTrackerEXTLocal ms_xrCreateHandTrackerEXT;
	PFN_xrDestroyHandTrackerEXTLocal ms_xrDestroyHandTrackerEXT;
	PFN_xrLocateHandJointsEXTLocal ms_xrLocateHandJointsEXT;
	std::vector<XrSwapchainImageD3D11KHRLocal> ms_xrQuadImages;
	std::vector<XrSwapchainImageD3D11KHRLocal> ms_xrProjectionImages;
	XrFrameStateLocal ms_xrWorldFrameState;
	XrViewLocal ms_xrWorldViews[2];
	bool ms_xrWorldHeadOriginCaptured;
	XrVector3fLocal ms_xrWorldHeadOrigin;
	XrPosefLocal ms_xrBasePoseInReferenceSpace;
	XrPosefLocal ms_xrHandHeadPose;
	bool ms_xrHandHeadPoseCaptured;
	XrActionSet ms_xrControllerActionSet;
	XrPath ms_xrHandSubactionPaths[2];
	XrAction ms_xrAimPoseAction;
	XrAction ms_xrGripPoseAction;
	XrAction ms_xrTriggerValueAction;
	XrAction ms_xrSqueezeValueAction;
	XrAction ms_xrThumbstickAction;
	XrAction ms_xrMenuClickAction;
	XrAction ms_xrTurnModeClickAction;
	XrAction ms_xrSelectClickAction;
	XrAction ms_xrTargetNextClickAction;
	XrAction ms_xrTargetPreviousClickAction;
	XrSpace ms_xrAimSpaces[2];
	XrSpace ms_xrGripSpaces[2];
	XrHandTrackerEXT ms_xrHandTrackers[2];
	XrPath ms_xrLastInteractionProfiles[2];
	DWORD ms_xrLastInteractionProfileLogMilliseconds;
	bool ms_xrInteractionProfileQueryMissingLogged;
	XrSwapchain ms_xrPointerSwapchain;
	std::vector<XrSwapchainImageD3D11KHRLocal> ms_xrPointerImages;
	unsigned int ms_xrPointerSwapchainWidth;
	unsigned int ms_xrPointerSwapchainHeight;
	XrSwapchain ms_xrWandSwapchain;
	std::vector<XrSwapchainImageD3D11KHRLocal> ms_xrWandImages;
	unsigned int ms_xrWandSwapchainWidth;
	unsigned int ms_xrWandSwapchainHeight;
	XrSwapchain ms_xrReticleSwapchain;
	std::vector<XrSwapchainImageD3D11KHRLocal> ms_xrReticleImages;
	unsigned int ms_xrReticleSwapchainWidth;
	unsigned int ms_xrReticleSwapchainHeight;
	XrSwapchain ms_xrObjectContextSwapchain;
	std::vector<XrSwapchainImageD3D11KHRLocal> ms_xrObjectContextImages;
	std::vector<ID3D11RenderTargetView *> ms_xrObjectContextRenderTargetViews;
	unsigned int ms_xrObjectContextSwapchainWidth;
	unsigned int ms_xrObjectContextSwapchainHeight;
	unsigned int ms_xrObjectContextCaptureImageIndex;
	bool ms_xrObjectContextCaptureImageAcquired;
	int ms_xrObjectContextCaptureFrame;
	XrSwapchain ms_xrHoverTargetSwapchain;
	std::vector<XrSwapchainImageD3D11KHRLocal> ms_xrHoverTargetImages;
	unsigned int ms_xrHoverTargetSwapchainWidth;
	unsigned int ms_xrHoverTargetSwapchainHeight;
	XrSwapchain ms_xrWristDashboardSwapchain;
	std::vector<XrSwapchainImageD3D11KHRLocal> ms_xrWristDashboardImages;
	ID3D11Texture2D *ms_xrWristDashboardCaptureTexture;
	ID3D11RenderTargetView *ms_xrWristDashboardCaptureRenderTargetView;
	unsigned int ms_xrWristDashboardSwapchainWidth;
	unsigned int ms_xrWristDashboardSwapchainHeight;
	unsigned int ms_xrWristDashboardCaptureImageIndex;
	bool ms_xrWristDashboardCaptureImageAcquired;
	int ms_xrWristDashboardCaptureFrame;
	char ms_objectContextTargetName[128];
	int ms_objectContextHealth;
	int ms_objectContextHealthMax;
	int ms_objectContextAction;
	int ms_objectContextActionMax;
	int ms_objectContextMind;
	int ms_objectContextMindMax;
	bool ms_objectContextAttackable;
	bool ms_objectContextValid;
	int ms_objectContextFrame;
	struct ObjectContextInputRegion
	{
		bool active;
		int textureLeft;
		int textureTop;
		int textureRight;
		int textureBottom;
		int clientLeft;
		int clientTop;
		int clientRight;
		int clientBottom;
	};
	ObjectContextInputRegion ms_objectContextInputRegions[4];
	char ms_hoverTargetName[128];
	int ms_hoverTargetHealth;
	int ms_hoverTargetHealthMax;
	int ms_hoverTargetAction;
	int ms_hoverTargetActionMax;
	int ms_hoverTargetMind;
	int ms_hoverTargetMindMax;
	bool ms_hoverTargetAttackable;
	bool ms_hoverTargetValid;
	int ms_hoverTargetFrame;
	float ms_wristDashboardPlayerX;
	float ms_wristDashboardPlayerZ;
	float ms_wristDashboardHeadingRadians;
	int ms_wristDashboardHealth;
	int ms_wristDashboardHealthMax;
	int ms_wristDashboardAction;
	int ms_wristDashboardActionMax;
	int ms_wristDashboardMind;
	int ms_wristDashboardMindMax;
	bool ms_wristDashboardValid;
	int ms_wristDashboardFrame;
	int ms_xrPointerSubmitCount;
	int ms_xrWandSubmitCount;
	int ms_xrHandSubmitCount;
	int ms_xrWristMenuButtonSubmitCount;
	int ms_xrObjectContextSubmitCount;
	int ms_xrWristDashboardSubmitCount;
	bool ms_xrObjectContextPointerInside[2];
	int ms_xrObjectContextPointerFrame[2];
	bool ms_xrObjectContextLeftClickConsumed;
	bool ms_xrObjectContextRightClickConsumed;
	XrCompositionLayerFlags const XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT_LOCAL = 0x00000002;
	XrCompositionLayerFlags const XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT_LOCAL = 0x00000001;

	bool isTruthy(char const *text)
	{
		return text && text[0] && text[0] != '0';
	}

	bool getEnvironmentFlag(char const *name)
	{
		char value[32];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		return length > 0 && length < sizeof(value) && isTruthy(value);
	}

	bool getEnvironmentFlagDefault(char const *name, bool defaultValue)
	{
		char value[32];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return defaultValue;
		if (_stricmp(value, "false") == 0 || _stricmp(value, "off") == 0 || _stricmp(value, "no") == 0)
			return false;
		return isTruthy(value);
	}

	bool isTvModeEnabled()
	{
		char value[32];
		DWORD const length = GetEnvironmentVariableA("SWG_OG_VR_TV_MODE", value, sizeof(value));
		if (length > 0 && length < sizeof(value))
			return atoi(value) != 0 && _stricmp(value, "false") != 0 && _stricmp(value, "off") != 0 && _stricmp(value, "no") != 0;

		return ms_vrTvModeEnabled;
	}

	bool arePointerVisualLayersEnabled()
	{
		return getEnvironmentFlagDefault("SWG_OG_VR_POINTER_VISUALS", true);
	}

	char const *const ms_vrRendererHandLayerBuildMarker = "rendererHandLayersHardDisabled_noFallback";

	bool areHandVisualLayersEnabled()
	{
		return false;
	}

	bool isWristMenuButtonEnabled()
	{
		return false;
	}

	char const *getProofPath()
	{
		static bool initialized = false;
		static char path[MAX_PATH];
		if (!initialized)
		{
			DWORD const length = GetEnvironmentVariableA("SWG_OG_VR_PROOF", path, sizeof(path));
			if (length == 0 || length >= sizeof(path))
				path[0] = '\0';
			initialized = true;
		}

		return path[0] ? path : 0;
	}

	void appendProofLine(char const *text)
	{
		char const *const path = getProofPath();
		if (!path || !text)
			return;

		FILE *file = fopen(path, "ab");
		if (!file)
			return;

		fputs(text, file);
		fputs("\r\n", file);
		fclose(file);
	}

	bool bridgeDebugOutputEnabled()
	{
		static bool initialized = false;
		static bool enabled = false;
		if (!initialized)
		{
			enabled = getEnvironmentFlagDefault("SWG_OG_VR_DEBUG_OUTPUT", false);
			initialized = true;
		}
		return enabled;
	}

	void bridgeLog(char const *format, ...)
	{
		char const *const proofPath = getProofPath();
		bool const debugOutput = bridgeDebugOutputEnabled();
		if (!proofPath && !debugOutput)
			return;

		char buffer[2048];
		va_list arguments;
		va_start(arguments, format);
		_vsnprintf(buffer, sizeof(buffer) - 1, format, arguments);
		va_end(arguments);
		buffer[sizeof(buffer) - 1] = '\0';

		if (debugOutput)
		{
			OutputDebugStringA("Direct3d11_VrBridge: ");
			OutputDebugStringA(buffer);
			OutputDebugStringA("\n");
		}
		if (proofPath)
			appendProofLine(buffer);
	}

	XrVersion xrMakeVersion(unsigned short major, unsigned short minor, unsigned int patch)
	{
		return (static_cast<XrVersion>(major) << 48) | (static_cast<XrVersion>(minor) << 32) | static_cast<XrVersion>(patch);
	}

	template <typename T>
	bool getXrProc(PFN_xrGetInstanceProcAddrLocal getProc, XrInstance instance, char const *name, T &out)
	{
		PFN_xrVoidFunction function = 0;
		if (!getProc || getProc(instance, name, &function) != XR_SUCCESS_LOCAL || !function)
			return false;

		out = reinterpret_cast<T>(function);
		return true;
	}

	void addLoaderCandidate(char candidates[][MAX_PATH], int &candidateCount, char const *candidate)
	{
		if (!candidate || !candidate[0] || candidateCount >= 8)
			return;

		for (int i = 0; i < candidateCount; ++i)
		{
			if (_stricmp(candidates[i], candidate) == 0)
				return;
		}

		strncpy(candidates[candidateCount], candidate, MAX_PATH - 1);
		candidates[candidateCount][MAX_PATH - 1] = '\0';
		++candidateCount;
	}

	void addProgramFilesLoaderCandidate(char candidates[][MAX_PATH], int &candidateCount, char const *environmentName, char const *relativePath)
	{
		char root[MAX_PATH];
		DWORD const rootLength = GetEnvironmentVariableA(environmentName, root, sizeof(root));
		if (rootLength == 0 || rootLength >= sizeof(root))
			return;

		char path[MAX_PATH];
		_snprintf(path, sizeof(path) - 1, "%s\\%s", root, relativePath);
		path[sizeof(path) - 1] = '\0';
		if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
			addLoaderCandidate(candidates, candidateCount, path);
	}

	HMODULE loadOpenXrLoader(char *loaderPath, int loaderPathSize)
	{
		char candidates[8][MAX_PATH];
		int candidateCount = 0;
		char explicitLoader[MAX_PATH];
		DWORD const explicitLength = GetEnvironmentVariableA("SWG_OPENXR_LOADER", explicitLoader, sizeof(explicitLoader));
		if (explicitLength > 0 && explicitLength < sizeof(explicitLoader))
			addLoaderCandidate(candidates, candidateCount, explicitLoader);

		addLoaderCandidate(candidates, candidateCount, "C:\\Windows\\System32\\openxr_loader.dll");
		addLoaderCandidate(candidates, candidateCount, "openxr_loader.dll");
		addProgramFilesLoaderCandidate(candidates, candidateCount, "ProgramFiles(x86)", "Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll");
		addProgramFilesLoaderCandidate(candidates, candidateCount, "ProgramFiles", "Epic Games\\UE_5.7\\Engine\\Binaries\\ThirdParty\\OpenXR\\win64\\openxr_loader.dll");

		for (int i = 0; i < candidateCount; ++i)
		{
			HMODULE const loader = LoadLibraryA(candidates[i]);
			if (loader)
			{
				strncpy(loaderPath, candidates[i], loaderPathSize - 1);
				loaderPath[loaderPathSize - 1] = '\0';
				return loader;
			}
		}

		if (loaderPathSize > 0)
			loaderPath[0] = '\0';
		return 0;
	}

	bool d3dDeviceAdapterLuid(ID3D11Device *device, LUID &outLuid)
	{
		if (!device)
			return false;

		IDXGIDevice *dxgiDevice = 0;
		if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgiDevice))) || !dxgiDevice)
			return false;

		IDXGIAdapter *adapter = 0;
		HRESULT const adapterResult = dxgiDevice->GetAdapter(&adapter);
		dxgiDevice->Release();
		if (FAILED(adapterResult) || !adapter)
			return false;

		DXGI_ADAPTER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		HRESULT const descResult = adapter->GetDesc(&desc);
		adapter->Release();
		if (FAILED(descResult))
			return false;

		outLuid = desc.AdapterLuid;
		return true;
	}

	bool sameLuid(LUID const &lhs, LUID const &rhs)
	{
		return lhs.HighPart == rhs.HighPart && lhs.LowPart == rhs.LowPart;
	}

	XrPosefLocal identityPose()
	{
		XrPosefLocal pose;
		ZeroMemory(&pose, sizeof(pose));
		pose.orientation.w = 1.0f;
		return pose;
	}

	SwgVrPhysics::Matrix4x4 matrixFromPose(XrPosefLocal const &pose)
	{
		SwgVrPhysics::Matrix4x4 matrix = SwgVrPhysics::identityMatrix();
		float x = pose.orientation.x;
		float y = pose.orientation.y;
		float z = pose.orientation.z;
		float w = pose.orientation.w;
		float const lengthSquared = x * x + y * y + z * z + w * w;
		if (lengthSquared > 0.000001f)
		{
			float const inverseLength = 1.0f / sqrtf(lengthSquared);
			x *= inverseLength;
			y *= inverseLength;
			z *= inverseLength;
			w *= inverseLength;
		}
		else
		{
			x = 0.0f;
			y = 0.0f;
			z = 0.0f;
			w = 1.0f;
		}
		float const xx = x * x;
		float const yy = y * y;
		float const zz = z * z;
		float const xy = x * y;
		float const xz = x * z;
		float const yz = y * z;
		float const wx = w * x;
		float const wy = w * y;
		float const wz = w * z;

		matrix.m[0] = 1.0f - 2.0f * (yy + zz);
		matrix.m[1] = 2.0f * (xy - wz);
		matrix.m[2] = 2.0f * (xz + wy);
		matrix.m[3] = pose.position.x;
		matrix.m[4] = 2.0f * (xy + wz);
		matrix.m[5] = 1.0f - 2.0f * (xx + zz);
		matrix.m[6] = 2.0f * (yz - wx);
		matrix.m[7] = pose.position.y;
		matrix.m[8] = 2.0f * (xz - wy);
		matrix.m[9] = 2.0f * (yz + wx);
		matrix.m[10] = 1.0f - 2.0f * (xx + yy);
		matrix.m[11] = pose.position.z;
		return matrix;
	}

	XrQuaternionfLocal normalizedQuaternion(XrQuaternionfLocal const &q)
	{
		XrQuaternionfLocal result = q;
		float const lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
		if (lengthSquared > 0.000001f)
		{
			float const inverseLength = 1.0f / sqrtf(lengthSquared);
			result.x *= inverseLength;
			result.y *= inverseLength;
			result.z *= inverseLength;
			result.w *= inverseLength;
		}
		else
		{
			result.x = 0.0f;
			result.y = 0.0f;
			result.z = 0.0f;
			result.w = 1.0f;
		}
		return result;
	}

	bool finiteFloat(float value)
	{
		return value == value && value > -3.402823466e+38f && value < 3.402823466e+38f;
	}

	bool validVector(XrVector3fLocal const &value)
	{
		return finiteFloat(value.x) && finiteFloat(value.y) && finiteFloat(value.z);
	}

	bool validQuaternion(XrQuaternionfLocal const &q)
	{
		if (!finiteFloat(q.x) || !finiteFloat(q.y) || !finiteFloat(q.z) || !finiteFloat(q.w))
			return false;

		float const lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
		return lengthSquared > 0.250f && lengthSquared < 4.000f;
	}

	bool validPose(XrPosefLocal const &pose)
	{
		return validVector(pose.position) && validQuaternion(pose.orientation);
	}

	XrQuaternionfLocal inverseQuaternion(XrQuaternionfLocal const &q)
	{
		XrQuaternionfLocal result = normalizedQuaternion(q);
		result.x = -result.x;
		result.y = -result.y;
		result.z = -result.z;
		return result;
	}

	SwgVrPhysics::Vector3 velocityFromPose(int handIndex, XrPosefLocal const &pose, double sampleTimeSeconds)
	{
		SwgVrPhysics::Vector3 const position{pose.position.x, pose.position.y, pose.position.z};
		SwgVrPhysics::Vector3 velocity{0.0f, 0.0f, 0.0f};

		if (handIndex >= 0 && handIndex < 2 && ms_vrPhysicsPreviousHandValid[handIndex])
		{
			double const deltaSeconds = sampleTimeSeconds - ms_vrPhysicsPreviousHandPositionTimeSeconds[handIndex];
			if (deltaSeconds > 0.0001)
			{
				float const invDelta = static_cast<float>(1.0 / deltaSeconds);
				SwgVrPhysics::Vector3 const previous = ms_vrPhysicsPreviousHandPosition[handIndex];
				velocity.x = (position.x - previous.x) * invDelta;
				velocity.y = (position.y - previous.y) * invDelta;
				velocity.z = (position.z - previous.z) * invDelta;
			}
		}

		if (handIndex >= 0 && handIndex < 2)
		{
			ms_vrPhysicsPreviousHandValid[handIndex] = true;
			ms_vrPhysicsPreviousHandPositionTimeSeconds[handIndex] = sampleTimeSeconds;
			ms_vrPhysicsPreviousHandPosition[handIndex] = position;
		}

		return velocity;
	}

	SwgVrPhysics::Vector3 angularVelocityFromPose(int handIndex, XrPosefLocal const &pose, double sampleTimeSeconds)
	{
		SwgVrPhysics::Quaternion const current{pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
		SwgVrPhysics::Vector3 angularVelocity{0.0f, 0.0f, 0.0f};

		if (handIndex >= 0 && handIndex < 2 && ms_vrPhysicsPreviousHandOrientationValid[handIndex])
		{
			double const deltaSeconds = sampleTimeSeconds - ms_vrPhysicsPreviousHandOrientationTimeSeconds[handIndex];
			if (deltaSeconds > 0.0001)
			{
				SwgVrPhysics::Quaternion const previous = ms_vrPhysicsPreviousHandOrientation[handIndex];
				SwgVrPhysics::Quaternion delta{
					current.w * -previous.x + current.x * previous.w + current.y * -previous.z - current.z * -previous.y,
					current.w * -previous.y - current.x * -previous.z + current.y * previous.w + current.z * -previous.x,
					current.w * -previous.z + current.x * -previous.y - current.y * -previous.x + current.z * previous.w,
					current.w * previous.w - current.x * -previous.x - current.y * -previous.y - current.z * -previous.z};

				if (delta.w < 0.0f)
				{
					delta.x = -delta.x;
					delta.y = -delta.y;
					delta.z = -delta.z;
					delta.w = -delta.w;
				}

				float const vectorLength = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
				if (vectorLength > 0.00001f)
				{
					float const angle = 2.0f * atan2f(vectorLength, delta.w);
					float const scale = static_cast<float>(angle / (vectorLength * deltaSeconds));
					angularVelocity.x = delta.x * scale;
					angularVelocity.y = delta.y * scale;
					angularVelocity.z = delta.z * scale;
				}
			}
		}

		if (handIndex >= 0 && handIndex < 2)
		{
			ms_vrPhysicsPreviousHandOrientationValid[handIndex] = true;
			ms_vrPhysicsPreviousHandOrientationTimeSeconds[handIndex] = sampleTimeSeconds;
			ms_vrPhysicsPreviousHandOrientation[handIndex] = current;
		}

		return angularVelocity;
	}

	void resetVrPhysicsTracking()
	{
		for (int hand = 0; hand != 2; ++hand)
		{
			ms_vrPhysicsPreviousHandValid[hand] = false;
			ms_vrPhysicsPreviousHandOrientationValid[hand] = false;
			ms_vrPhysicsPreviousHandPositionTimeSeconds[hand] = 0.0;
			ms_vrPhysicsPreviousHandOrientationTimeSeconds[hand] = 0.0;
			ms_vrPhysicsPreviousHandPosition[hand] = SwgVrPhysics::Vector3{0.0f, 0.0f, 0.0f};
			ms_vrPhysicsPreviousHandOrientation[hand] = SwgVrPhysics::Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
		}

		SWGVRPhysics_ResetBridge();
	}

	void pumpVrPhysicsReleases()
	{
		if (!ms_vrPhysicsManager)
			return;

		SwgVrPhysics::ReleasedObjectState released{};
		while (SWGVRPhysics_TryConsumeReleasedObject(&released))
			(void)ms_vrPhysicsManager->spawnReleasedItem(released);
	}

	float getEnvironmentFloat(char const *name, float defaultValue)
	{
		char value[64];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		if (length == 0 || length >= sizeof(value))
			return defaultValue;

		return static_cast<float>(atof(value));
	}

	bool getEnvironmentEquals(char const *name, char const *expectedValue)
	{
		char value[64];
		DWORD const length = GetEnvironmentVariableA(name, value, sizeof(value));
		return length > 0 && length < sizeof(value) && _stricmp(value, expectedValue) == 0;
	}

	float vectorLength2(float x, float z)
	{
		return sqrtf(x * x + z * z);
	}

	XrVector3fLocal headsetForward(XrQuaternionfLocal const &q)
	{
		XrVector3fLocal forward;
		forward.x = -2.0f * (q.x * q.z + q.w * q.y);
		forward.y = 0.0f;
		forward.z = -1.0f + 2.0f * (q.x * q.x + q.y * q.y);

		float const length = vectorLength2(forward.x, forward.z);
		if (length > 0.0001f)
		{
			forward.x /= length;
			forward.z /= length;
		}
		else
		{
			forward.x = 0.0f;
			forward.z = -1.0f;
		}

		return forward;
	}

	XrVector3fLocal quaternionForward(XrQuaternionfLocal const &q)
	{
		XrVector3fLocal forward;
		forward.x = -2.0f * (q.x * q.z + q.w * q.y);
		forward.y = 2.0f * (q.w * q.x - q.y * q.z);
		forward.z = -1.0f + 2.0f * (q.x * q.x + q.y * q.y);
		float const length = sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
		if (length > 0.0001f)
		{
			forward.x /= length;
			forward.y /= length;
			forward.z /= length;
		}
		else
		{
			forward.x = 0.0f;
			forward.y = 0.0f;
			forward.z = -1.0f;
		}

		return forward;
	}

	void resetWorldHeadRecenter()
	{
		if (ms_xrWorldRenderSpace && ms_xrDestroySpace)
			ms_xrDestroySpace(ms_xrWorldRenderSpace);
		ms_xrWorldRenderSpace = 0;
		ms_xrWorldHeadOriginCaptured = false;
		ms_xrWorldHeadOriginFrameCount = 0;
		ms_xrWorldHeadOriginSettled = false;
		ms_xrWorldBaseSpaceJustRecentered = false;
		ms_xrWorldYawCaptured = false;
		ms_xrWorldYawCos = 1.0f;
		ms_xrWorldYawSin = 0.0f;
		ms_xrWorldYawHalfCos = 1.0f;
		ms_xrWorldYawHalfSin = 0.0f;
		ZeroMemory(&ms_xrWorldHeadOrigin, sizeof(ms_xrWorldHeadOrigin));
	}

	void setHandHeadPose(XrPosefLocal const &headPose)
	{
		ms_xrHandHeadPose = headPose;
		ms_xrHandHeadPose.orientation = normalizedQuaternion(ms_xrHandHeadPose.orientation);
		ms_xrHandHeadPoseCaptured = true;
	}

	void resetWorldHeadRecenterLogged(char const *reason)
	{
		resetWorldHeadRecenter();
		bridgeLog("{\"event\":\"openxrWorldHeadRecenterReset\",\"reason\":\"%s\",\"frame\":%d}", reason ? reason : "", ms_frameCount);
	}

	XrVector3fLocal captureWorldYawCorrection(XrQuaternionfLocal const &orientation, float &capturedYawDegrees)
	{
		XrVector3fLocal const forward = headsetForward(orientation);
		float const capturedYaw = atan2f(-forward.x, -forward.z);
		float const correctionYaw = -capturedYaw;
		ms_xrWorldYawCos = cosf(correctionYaw);
		ms_xrWorldYawSin = sinf(correctionYaw);
		ms_xrWorldYawHalfCos = cosf(correctionYaw * 0.5f);
		ms_xrWorldYawHalfSin = sinf(correctionYaw * 0.5f);
		ms_xrWorldYawCaptured = true;
		capturedYawDegrees = capturedYaw * 57.2957795f;
		return forward;
	}

	XrPosefLocal recenteredWorldPose(XrPosefLocal const &pose)
	{
		if (!ms_xrWorldHeadOriginCaptured || !ms_xrWorldYawCaptured)
			return pose;

		XrPosefLocal result = pose;
		float const localX = pose.position.x - ms_xrWorldHeadOrigin.x;
		float const localY = pose.position.y - ms_xrWorldHeadOrigin.y;
		float const localZ = pose.position.z - ms_xrWorldHeadOrigin.z;
		result.position.x = ms_xrWorldYawCos * localX - ms_xrWorldYawSin * localZ;
		result.position.y = localY;
		result.position.z = ms_xrWorldYawSin * localX + ms_xrWorldYawCos * localZ;

		result.orientation.x = ms_xrWorldYawHalfCos * pose.orientation.x + ms_xrWorldYawHalfSin * pose.orientation.z;
		result.orientation.y = ms_xrWorldYawHalfCos * pose.orientation.y + ms_xrWorldYawHalfSin * pose.orientation.w;
		result.orientation.z = ms_xrWorldYawHalfCos * pose.orientation.z - ms_xrWorldYawHalfSin * pose.orientation.x;
		result.orientation.w = ms_xrWorldYawHalfCos * pose.orientation.w - ms_xrWorldYawHalfSin * pose.orientation.y;
		return result;
	}

	float worldHeadPositionScale()
	{
		return clampFloat(getEnvironmentFloat("SWG_OG_VR_HEAD_POSITION_SCALE", 1.0f), 0.0f, 4.0f);
	}

	float worldStereoScale()
	{
		return clampFloat(getEnvironmentFloat("SWG_OG_VR_STEREO_SCALE", 1.0f), 0.0f, 2.0f);
	}

	bool buildWorldRenderEyePose(int eye, XrPosefLocal &pose)
	{
		if (eye < 0 || eye >= 2 || !ms_xrWorldHeadOriginSettled || !ms_xrWorldHeadOriginCaptured || !ms_xrWorldYawCaptured)
			return false;
		if (ms_xrWorldViews[0].type != XR_TYPE_VIEW_LOCAL || ms_xrWorldViews[1].type != XR_TYPE_VIEW_LOCAL)
			return false;

		XrVector3fLocal const rawCenter = {
			(ms_xrWorldViews[0].pose.position.x + ms_xrWorldViews[1].pose.position.x) * 0.5f,
			(ms_xrWorldViews[0].pose.position.y + ms_xrWorldViews[1].pose.position.y) * 0.5f,
			(ms_xrWorldViews[0].pose.position.z + ms_xrWorldViews[1].pose.position.z) * 0.5f
		};
		float const centerLocalX = rawCenter.x - ms_xrWorldHeadOrigin.x;
		float const centerLocalY = rawCenter.y - ms_xrWorldHeadOrigin.y;
		float const centerLocalZ = rawCenter.z - ms_xrWorldHeadOrigin.z;
		float const eyeLocalX = ms_xrWorldViews[eye].pose.position.x - rawCenter.x;
		float const eyeLocalY = ms_xrWorldViews[eye].pose.position.y - rawCenter.y;
		float const eyeLocalZ = ms_xrWorldViews[eye].pose.position.z - rawCenter.z;
		float const centerX = ms_xrWorldYawCos * centerLocalX - ms_xrWorldYawSin * centerLocalZ;
		float const centerY = centerLocalY;
		float const centerZ = ms_xrWorldYawSin * centerLocalX + ms_xrWorldYawCos * centerLocalZ;
		float const stereoX = ms_xrWorldYawCos * eyeLocalX - ms_xrWorldYawSin * eyeLocalZ;
		float const stereoY = eyeLocalY;
		float const stereoZ = ms_xrWorldYawSin * eyeLocalX + ms_xrWorldYawCos * eyeLocalZ;
		float const headScale = worldHeadPositionScale();
		float const stereoScale = worldStereoScale();

		pose = recenteredWorldPose(ms_xrWorldViews[eye].pose);
		pose.position.x = centerX * headScale + stereoX * stereoScale;
		pose.position.y = centerY * headScale + stereoY * stereoScale;
		pose.position.z = centerZ * headScale + stereoZ * stereoScale;
		pose.orientation = normalizedQuaternion(pose.orientation);
		return true;
	}

	bool menuHandSpaceReady()
	{
		return ms_xrQuadHeadPoseCaptured || ms_xrLoadingHeadPoseCaptured || ms_xrHandHeadPoseCaptured;
	}

	XrPosefLocal menuHandHeadPose()
	{
		if (ms_xrQuadHeadPoseCaptured)
			return ms_xrQuadHeadPose;
		if (ms_xrLoadingHeadPoseCaptured)
			return ms_xrLoadingHeadPose;
		if (ms_xrHandHeadPoseCaptured)
			return ms_xrHandHeadPose;
		return identityPose();
	}

	XrQuaternionfLocal quadOrientationFacingHead(XrVector3fLocal const &forward)
	{
		float const yaw = atan2f(-forward.x, -forward.z);
		XrQuaternionfLocal orientation;
		ZeroMemory(&orientation, sizeof(orientation));
		orientation.y = sinf(yaw * 0.5f);
		orientation.w = cosf(yaw * 0.5f);
		return orientation;
	}

	XrQuaternionfLocal yawOrientation(float yawRadians)
	{
		XrQuaternionfLocal orientation;
		ZeroMemory(&orientation, sizeof(orientation));
		orientation.y = sinf(yawRadians * 0.5f);
		orientation.w = cosf(yawRadians * 0.5f);
		return orientation;
	}

	XrVector3fLocal rotateForwardByYaw(XrVector3fLocal const &forward, float yawRadians)
	{
		float const cosYaw = cosf(yawRadians);
		float const sinYaw = sinf(yawRadians);
		XrVector3fLocal rotated;
		rotated.x = forward.x * cosYaw + forward.z * sinYaw;
		rotated.y = 0.0f;
		rotated.z = -forward.x * sinYaw + forward.z * cosYaw;
		float const length = sqrtf(rotated.x * rotated.x + rotated.z * rotated.z);
		if (length > 0.00001f)
		{
			rotated.x /= length;
			rotated.z /= length;
		}
		else
		{
			rotated.x = 0.0f;
			rotated.z = -1.0f;
		}
		return rotated;
	}

	XrVector3fLocal rightFromForward(XrVector3fLocal const &forward)
	{
		XrVector3fLocal right;
		right.x = -forward.z;
		right.y = 0.0f;
		right.z = forward.x;
		return right;
	}

	float dotVector(XrVector3fLocal const &lhs, XrVector3fLocal const &rhs)
	{
		return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
	}

	XrVector3fLocal addVector(XrVector3fLocal const &lhs, XrVector3fLocal const &rhs)
	{
		XrVector3fLocal result;
		result.x = lhs.x + rhs.x;
		result.y = lhs.y + rhs.y;
		result.z = lhs.z + rhs.z;
		return result;
	}

	XrVector3fLocal subtractVector(XrVector3fLocal const &lhs, XrVector3fLocal const &rhs)
	{
		XrVector3fLocal result;
		result.x = lhs.x - rhs.x;
		result.y = lhs.y - rhs.y;
		result.z = lhs.z - rhs.z;
		return result;
	}

	XrVector3fLocal scaleVector(XrVector3fLocal const &value, float scale)
	{
		XrVector3fLocal result;
		result.x = value.x * scale;
		result.y = value.y * scale;
		result.z = value.z * scale;
		return result;
	}

	XrVector3fLocal crossVector(XrVector3fLocal const &lhs, XrVector3fLocal const &rhs)
	{
		XrVector3fLocal result;
		result.x = lhs.y * rhs.z - lhs.z * rhs.y;
		result.y = lhs.z * rhs.x - lhs.x * rhs.z;
		result.z = lhs.x * rhs.y - lhs.y * rhs.x;
		return result;
	}

	bool normalizeVector(XrVector3fLocal &value)
	{
		float const length = sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
		if (length < 0.0001f)
			return false;

		value.x /= length;
		value.y /= length;
		value.z /= length;
		return true;
	}

	XrQuaternionfLocal multiplyQuaternion(XrQuaternionfLocal const &lhs, XrQuaternionfLocal const &rhs)
	{
		XrQuaternionfLocal result;
		result.x = lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y;
		result.y = lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x;
		result.z = lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w;
		result.w = lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z;
		return result;
	}

	XrVector3fLocal rotateVector(XrQuaternionfLocal const &orientation, XrVector3fLocal const &value)
	{
		XrVector3fLocal qv;
		qv.x = orientation.x;
		qv.y = orientation.y;
		qv.z = orientation.z;
		XrVector3fLocal const uv = crossVector(qv, value);
		XrVector3fLocal const uuv = crossVector(qv, uv);
		return addVector(value, addVector(scaleVector(uv, 2.0f * orientation.w), scaleVector(uuv, 2.0f)));
	}

	XrPosefLocal composePose(XrPosefLocal const &parent, XrPosefLocal const &local)
	{
		XrPosefLocal result = identityPose();
		result.orientation = normalizedQuaternion(multiplyQuaternion(parent.orientation, local.orientation));
		result.position = addVector(parent.position, rotateVector(parent.orientation, local.position));
		return result;
	}

	XrSpace worldCompositionLayerSpace()
	{
		return ms_xrLocalSpace;
	}

	XrPosefLocal worldCompositionLayerPose(XrPosefLocal const &referenceSpacePose)
	{
		return referenceSpacePose;
	}

	XrPosefLocal aimRayOriginPose(XrPosefLocal const &pose)
	{
		XrPosefLocal result = pose;
		XrVector3fLocal offset;
		offset.x = getEnvironmentFloat("SWG_OG_VR_AIM_RAY_OFFSET_X_METERS", 0.0f);
		offset.y = getEnvironmentFloat("SWG_OG_VR_AIM_RAY_OFFSET_Y_METERS", -0.10f);
		offset.z = getEnvironmentFloat("SWG_OG_VR_AIM_RAY_OFFSET_Z_METERS", 0.0f);
		result.position = addVector(result.position, rotateVector(result.orientation, offset));
		return result;
	}

	float handIkOffsetMeters(int handIndex, char axis, float defaultValue)
	{
		char const * const axisName =
			axis == 'X' ? "X" :
			axis == 'Y' ? "Y" :
			"Z";
		char globalName[96];
		_snprintf(globalName, sizeof(globalName) - 1, "SWG_OG_VR_HAND_IK_OFFSET_%s_METERS", axisName);
		globalName[sizeof(globalName) - 1] = '\0';
		float value = getEnvironmentFloat(globalName, defaultValue);

		char sideName[112];
		_snprintf(sideName, sizeof(sideName) - 1, "SWG_OG_VR_%s_HAND_IK_OFFSET_%s_METERS", handIndex == 0 ? "LEFT" : "RIGHT", axisName);
		sideName[sizeof(sideName) - 1] = '\0';
		return getEnvironmentFloat(sideName, value);
	}

	XrPosefLocal handIkPose(XrPosefLocal const &pose, int handIndex)
	{
		XrPosefLocal result = pose;
		result.position.x += handIkOffsetMeters(handIndex, 'X', 0.0f);
		result.position.y += handIkOffsetMeters(handIndex, 'Y', 0.10f);
		result.position.z += handIkOffsetMeters(handIndex, 'Z', 0.0f);
		return result;
	}

	XrPosefLocal poseRelativeToHead(XrPosefLocal const &pose, XrPosefLocal const &headPose)
	{
		XrQuaternionfLocal const headInverse = inverseQuaternion(headPose.orientation);
		XrPosefLocal result = identityPose();
		result.orientation = normalizedQuaternion(multiplyQuaternion(headInverse, pose.orientation));
		result.position = rotateVector(headInverse, subtractVector(pose.position, headPose.position));
		return result;
	}

	XrQuaternionfLocal quaternionFromBasis(XrVector3fLocal const &xAxis, XrVector3fLocal const &yAxis, XrVector3fLocal const &zAxis)
	{
		float const m00 = xAxis.x;
		float const m01 = yAxis.x;
		float const m02 = zAxis.x;
		float const m10 = xAxis.y;
		float const m11 = yAxis.y;
		float const m12 = zAxis.y;
		float const m20 = xAxis.z;
		float const m21 = yAxis.z;
		float const m22 = zAxis.z;
		float const trace = m00 + m11 + m22;
		XrQuaternionfLocal q;
		ZeroMemory(&q, sizeof(q));

		if (trace > 0.0f)
		{
			float const s = sqrtf(trace + 1.0f) * 2.0f;
			q.w = 0.25f * s;
			q.x = (m21 - m12) / s;
			q.y = (m02 - m20) / s;
			q.z = (m10 - m01) / s;
		}
		else if (m00 > m11 && m00 > m22)
		{
			float const s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
			q.w = (m21 - m12) / s;
			q.x = 0.25f * s;
			q.y = (m01 + m10) / s;
			q.z = (m02 + m20) / s;
		}
		else if (m11 > m22)
		{
			float const s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
			q.w = (m02 - m20) / s;
			q.x = (m01 + m10) / s;
			q.y = 0.25f * s;
			q.z = (m12 + m21) / s;
		}
		else
		{
			float const s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
			q.w = (m10 - m01) / s;
			q.x = (m02 + m20) / s;
			q.y = (m12 + m21) / s;
			q.z = 0.25f * s;
		}

		return q;
	}

	XrQuaternionfLocal quadOrientationFacingHeadWithRoll(XrVector3fLocal const &forward, float rollRadians)
	{
		XrVector3fLocal normalizedForward = forward;
		if (!normalizeVector(normalizedForward))
			return identityPose().orientation;

		XrVector3fLocal right = rightFromForward(normalizedForward);
		if (!normalizeVector(right))
		{
			right.x = 1.0f;
			right.y = 0.0f;
			right.z = 0.0f;
		}

		XrVector3fLocal up;
		up.x = 0.0f;
		up.y = 1.0f;
		up.z = 0.0f;

		if (fabsf(rollRadians) > 0.0001f)
		{
			float const cosRoll = cosf(rollRadians);
			float const sinRoll = sinf(rollRadians);
			XrVector3fLocal const rolledRight = addVector(scaleVector(right, cosRoll), scaleVector(up, sinRoll));
			XrVector3fLocal const rolledUp = addVector(scaleVector(up, cosRoll), scaleVector(right, -sinRoll));
			right = rolledRight;
			up = rolledUp;
		}

		XrVector3fLocal const localZ = scaleVector(normalizedForward, -1.0f);
		return quaternionFromBasis(right, up, localZ);
	}

	XrQuaternionfLocal wandOrientationFromRay(XrVector3fLocal const &rayDirection, XrVector3fLocal preferredNormal, float rollRadians)
	{
		XrVector3fLocal yAxis = rayDirection;
		if (!normalizeVector(yAxis))
			return identityPose().orientation;

		if (!normalizeVector(preferredNormal))
		{
			preferredNormal.x = 0.0f;
			preferredNormal.y = 0.0f;
			preferredNormal.z = 1.0f;
		}

		XrVector3fLocal zAxis = subtractVector(preferredNormal, scaleVector(yAxis, dotVector(preferredNormal, yAxis)));
		normalizeVector(zAxis);
		XrVector3fLocal xAxis = crossVector(yAxis, zAxis);
		if (!normalizeVector(xAxis))
		{
			XrVector3fLocal worldUp;
			worldUp.x = 0.0f;
			worldUp.y = 1.0f;
			worldUp.z = 0.0f;
			xAxis = crossVector(worldUp, yAxis);
			normalizeVector(xAxis);
			zAxis = crossVector(xAxis, yAxis);
			normalizeVector(zAxis);
		}
		if (fabsf(rollRadians) > 0.0001f)
		{
			float const cosRoll = cosf(rollRadians);
			float const sinRoll = sinf(rollRadians);
			XrVector3fLocal const rolledXAxis = addVector(scaleVector(xAxis, cosRoll), scaleVector(zAxis, sinRoll));
			XrVector3fLocal const rolledZAxis = addVector(scaleVector(zAxis, cosRoll), scaleVector(xAxis, -sinRoll));
			return quaternionFromBasis(rolledXAxis, yAxis, rolledZAxis);
		}
		return quaternionFromBasis(xAxis, yAxis, zAxis);
	}

	float clampFloat(float value, float low, float high)
	{
		if (value < low)
			return low;
		if (value > high)
			return high;
		return value;
	}

	float maxFloat(float left, float right)
	{
		return left > right ? left : right;
	}

	int clampInt(int value, int low, int high)
	{
		if (value < low)
			return low;
		if (value > high)
			return high;
		return value;
	}

	char ms_contextPanelAnchorName[96];
	bool ms_contextPanelAnchorValid;
	int ms_contextPanelAnchorFrame;
	XrVector3fLocal ms_contextPanelAnchorPosition;
	float ms_contextPanelAnchorRadius;
	bool ms_hoverTargetAnchorValid;
	int ms_hoverTargetAnchorFrame;
	XrVector3fLocal ms_hoverTargetAnchorPosition;
	float ms_hoverTargetAnchorRadius;

	bool isObjectContextPanelName(char const *name)
	{
		return name &&
			(_stricmp(name, "AllTargets") == 0 ||
			_stricmp(name, "TargetsPage") == 0 ||
			_stricmp(name, "SecondaryTargetsPage") == 0);
	}

	bool hasFreshContextPanelAnchor()
	{
		return ms_contextPanelAnchorValid && (ms_frameCount - ms_contextPanelAnchorFrame) <= 5;
	}

	bool hasFreshHoverTargetAnchor()
	{
		return getEnvironmentFlagDefault("SWG_OG_VR_HOVER_TARGET_FRAME", true) &&
			ms_hoverTargetAnchorValid &&
			(ms_frameCount - ms_hoverTargetAnchorFrame) <= 5;
	}

	bool isObjectContextEnabled()
	{
		return getEnvironmentFlagDefault("SWG_OG_VR_OBJECT_CONTEXT_QUADS", true) &&
			getEnvironmentFlagDefault("SWG_OG_VR_TARGET_WINDOW_QUADS", true);
	}

	bool hasFreshObjectContext()
	{
		return isObjectContextEnabled() && ms_objectContextValid && hasFreshContextPanelAnchor() && (ms_frameCount - ms_objectContextFrame) <= 30;
	}

	bool hasFreshObjectContextCapture()
	{
		return ms_xrObjectContextCaptureFrame > 0;
	}

	bool hasActiveObjectContextInputRegions()
	{
		for (int i = 0; i != static_cast<int>(sizeof(ms_objectContextInputRegions) / sizeof(ms_objectContextInputRegions[0])); ++i)
		{
			if (ms_objectContextInputRegions[i].active)
				return true;
		}
		return false;
	}

	bool hasFreshWristDashboard()
	{
		return getEnvironmentFlagDefault("SWG_OG_VR_WRIST_DASHBOARD", true) &&
			ms_wristDashboardValid &&
			(ms_frameCount - ms_wristDashboardFrame) <= 90 &&
			ms_xrWristDashboardCaptureFrame > 0;
	}

	void objectContextLayerSize(float &widthMeters, float &heightMeters)
	{
		float const scale = clampFloat(getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_SCALE", 1.0f), 0.50f, 3.00f);
		widthMeters = clampFloat(getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_WIDTH_METERS", 1.25f) * scale, 0.65f, 8.00f);
		heightMeters = clampFloat(getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_HEIGHT_METERS", 0.62f) * scale, 0.35f, 4.00f);
	}

	XrPosefLocal objectContextPose(char const *panelName, float offsetRightMeters, float offsetUpMeters)
	{
		XrPosefLocal pose = identityPose();
		if (getEnvironmentFlagDefault("SWG_OG_VR_OBJECT_CONTEXT_ANCHOR_CAMERA_LOCAL", false) && ms_xrHandHeadPoseCaptured)
		{
			pose.position = addVector(ms_xrHandHeadPose.position, rotateVector(ms_xrHandHeadPose.orientation, ms_contextPanelAnchorPosition));
		}
		else
		{
			pose.position = ms_contextPanelAnchorPosition;
		}
		float const radius = clampFloat(ms_contextPanelAnchorRadius, 0.15f, 4.0f);
		pose.position.y += clampFloat(radius * getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_RADIUS_UP_SCALE", 0.90f), 0.35f, 2.20f);
		pose.position.y += getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_UP_METERS", 0.22f);
		if (panelName && _stricmp(panelName, "TargetsPage") == 0)
			pose.position.x -= getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_SIDE_METERS", 0.42f);
		else if (panelName && _stricmp(panelName, "SecondaryTargetsPage") == 0)
			pose.position.x += getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_SIDE_METERS", 0.42f);

		XrVector3fLocal headPosition;
		headPosition.x = 0.0f;
		headPosition.y = 0.0f;
		headPosition.z = 0.0f;
		if (ms_xrHandHeadPoseCaptured)
			headPosition = ms_xrHandHeadPose.position;
		XrVector3fLocal forward = subtractVector(pose.position, headPosition);
		forward.y = 0.0f;
		if (!normalizeVector(forward))
		{
			forward.x = 0.0f;
			forward.y = 0.0f;
			forward.z = -1.0f;
		}
		XrVector3fLocal const right = rightFromForward(forward);
		XrVector3fLocal up;
		up.x = 0.0f;
		up.y = 1.0f;
		up.z = 0.0f;
		float const headwardMeters = getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_HEADWARD_METERS", 0.0f);
		XrVector3fLocal const towardHead = scaleVector(forward, -headwardMeters);
		pose.position = addVector(pose.position, addVector(addVector(scaleVector(right, offsetRightMeters), scaleVector(up, offsetUpMeters)), towardHead));
		pose.orientation = quadOrientationFacingHeadWithRoll(forward, 0.0f);
		return pose;
	}

	XrPosefLocal hoverTargetPose()
	{
		XrPosefLocal pose = identityPose();
		pose.position = ms_hoverTargetAnchorPosition;
		float const radius = clampFloat(ms_hoverTargetAnchorRadius, 0.15f, 4.0f);
		pose.position.y += clampFloat(radius * getEnvironmentFloat("SWG_OG_VR_HOVER_TARGET_FRAME_RADIUS_UP_SCALE", getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_RADIUS_UP_SCALE", 0.90f)), 0.35f, 2.20f);
		pose.position.y += getEnvironmentFloat("SWG_OG_VR_HOVER_TARGET_FRAME_UP_METERS", getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_UP_METERS", 0.22f));

		XrVector3fLocal headPosition;
		headPosition.x = 0.0f;
		headPosition.y = 0.0f;
		headPosition.z = 0.0f;
		if (ms_xrHandHeadPoseCaptured)
			headPosition = ms_xrHandHeadPose.position;
		XrVector3fLocal forward = subtractVector(pose.position, headPosition);
		forward.y = 0.0f;
		if (!normalizeVector(forward))
		{
			forward.x = 0.0f;
			forward.y = 0.0f;
			forward.z = -1.0f;
		}
		pose.orientation = quadOrientationFacingHeadWithRoll(forward, 0.0f);
		return pose;
	}

	XrActionStateFloatLocal getFloatActionState(XrAction action, XrPath subactionPath);
	bool createWorldRenderSpace(float capturedYawDegrees, char const *phase);

	char const *referenceSpaceName(XrReferenceSpaceType referenceSpaceType)
	{
		if (referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE_LOCAL)
			return "stage";
		if (referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL_LOCAL)
			return "local";
		return "unknown";
	}

	void captureWorldHeadOriginFromPose(XrPosefLocal const &headPose, char const *phase, int recenterDelayFrames)
	{
		ms_xrWorldHeadOrigin.x = headPose.position.x;
		ms_xrWorldHeadOrigin.y = headPose.position.y;
		ms_xrWorldHeadOrigin.z = headPose.position.z;
		float capturedYawDegrees = 0.0f;
		XrVector3fLocal const forward = captureWorldYawCorrection(headPose.orientation, capturedYawDegrees);
		ms_xrWorldHeadOriginCaptured = true;
		ms_xrWorldHeadOriginSettled = createWorldRenderSpace(capturedYawDegrees, phase);
		setHandHeadPose(recenteredWorldPose(headPose));
		bridgeLog("{\"event\":\"openxrWorldHeadOrigin\",\"captured\":true,\"phase\":\"%s\",\"settled\":%s,\"frameCount\":%d,\"recenterDelayFrames\":%d,\"space\":\"%s\",\"origin\":[%0.3f,%0.3f,%0.3f],\"forward\":[%0.3f,%0.3f,%0.3f],\"capturedYawDegrees\":%0.3f}",
			phase ? phase : "",
			ms_xrWorldHeadOriginSettled ? "true" : "false",
			ms_xrWorldHeadOriginFrameCount,
			recenterDelayFrames,
			referenceSpaceName(ms_xrReferenceSpaceType),
			ms_xrWorldHeadOrigin.x,
			ms_xrWorldHeadOrigin.y,
			ms_xrWorldHeadOrigin.z,
			forward.x,
			forward.y,
			forward.z,
			capturedYawDegrees);
	}

	int controllerSnapshotLogInterval()
	{
		float const interval = getEnvironmentFloat("SWG_OG_VR_CONTROLLER_LOG_INTERVAL", 30.0f);
		if (interval < 1.0f)
			return 0;
		return static_cast<int>(interval);
	}

	bool openXrBoolString(XrBool32 value)
	{
		return value != 0;
	}

	unsigned int pointerPixel(int x, int y, int width, int height)
	{
		float const centerX = (static_cast<float>(width) - 1.0f) * 0.5f;
		float const centerY = (static_cast<float>(height) - 1.0f) * 0.5f;
		float const dx = fabsf(static_cast<float>(x) - centerX) / (centerX > 0.0f ? centerX : 1.0f);
		float const dy = fabsf(static_cast<float>(y) - centerY) / (centerY > 0.0f ? centerY : 1.0f);
		float alpha = 0.0f;
		if (dx < 0.18f)
			alpha = 245.0f;
		else if (dx < 0.70f)
			alpha = (0.70f - dx) / 0.52f * 105.0f;
		if (dy > 0.92f)
			alpha *= (1.0f - dy) / 0.08f;
		alpha = clampFloat(alpha, 0.0f, 255.0f);
		if (alpha <= 0.0f)
			return 0x00000000;

		unsigned int const red = dx < 0.20f ? 245 : 60;
		unsigned int const green = dx < 0.20f ? 255 : 225;
		unsigned int const blue = 255;
		return (static_cast<unsigned int>(alpha) << 24) | (red << 16) | (green << 8) | blue;
	}

	unsigned int reticlePixel(int x, int y, int width, int height)
	{
		float const centerX = (static_cast<float>(width) - 1.0f) * 0.5f;
		float const centerY = (static_cast<float>(height) - 1.0f) * 0.5f;
		float const dx = (static_cast<float>(x) - centerX) / (centerX > 0.0f ? centerX : 1.0f);
		float const dy = (static_cast<float>(y) - centerY) / (centerY > 0.0f ? centerY : 1.0f);
		float const radius = sqrtf(dx * dx + dy * dy);
		float alpha = 0.0f;
		if (radius < 0.20f)
			alpha = 255.0f;
		else if (radius < 0.48f)
			alpha = (0.48f - radius) / 0.28f * 190.0f;
		else if ((fabsf(dx) < 0.035f || fabsf(dy) < 0.035f) && radius < 0.78f)
			alpha = 130.0f;
		alpha = clampFloat(alpha, 0.0f, 255.0f);
		if (alpha <= 0.0f)
			return 0x00000000;

		unsigned int const red = radius < 0.24f ? 255 : 60;
		unsigned int const green = radius < 0.24f ? 255 : 230;
		unsigned int const blue = 255;
		return (static_cast<unsigned int>(alpha) << 24) | (red << 16) | (green << 8) | blue;
	}

	float ellipseCoverage(float x, float y, float centerX, float centerY, float radiusX, float radiusY)
	{
		if (radiusX <= 0.0f || radiusY <= 0.0f)
			return 0.0f;

		float const dx = (x - centerX) / radiusX;
		float const dy = (y - centerY) / radiusY;
		float const distance = dx * dx + dy * dy;
		if (distance >= 1.0f)
			return 0.0f;

		return clampFloat((1.0f - distance) * 3.0f, 0.0f, 1.0f);
	}

	unsigned int handPixel(int x, int y, int width, int height, bool closed, bool leftHand)
	{
		float const centerX = (static_cast<float>(width) - 1.0f) * 0.5f;
		float const normalizedX = (static_cast<float>(x) - centerX) / (centerX > 0.0f ? centerX : 1.0f);
		float const normalizedY = static_cast<float>(y) / static_cast<float>(height > 1 ? height - 1 : 1);
		float const mirror = leftHand ? -1.0f : 1.0f;
		float alpha = 0.0f;

		if (closed)
		{
			alpha = ellipseCoverage(normalizedX, normalizedY, 0.0f, 0.48f, 0.56f, 0.30f);
			alpha = maxFloat(alpha, ellipseCoverage(normalizedX, normalizedY, mirror * 0.44f, 0.58f, 0.24f, 0.16f));
		}
		else
		{
			alpha = ellipseCoverage(normalizedX, normalizedY, 0.0f, 0.62f, 0.52f, 0.25f);
			alpha = maxFloat(alpha, ellipseCoverage(normalizedX, normalizedY, mirror * 0.47f, 0.55f, 0.22f, 0.13f));
			alpha = maxFloat(alpha, ellipseCoverage(normalizedX, normalizedY, mirror * 0.56f, 0.45f, 0.15f, 0.10f));
			float const fingerCenters[4] = { -0.30f, -0.10f, 0.10f, 0.30f };
			for (int i = 0; i != 4; ++i)
				alpha = maxFloat(alpha, ellipseCoverage(normalizedX, normalizedY, fingerCenters[i], 0.24f, 0.105f, 0.23f));
			alpha = maxFloat(alpha, ellipseCoverage(normalizedX, normalizedY, 0.0f, 0.36f, 0.42f, 0.12f));
		}

		if (alpha <= 0.0f)
			return 0x00000000;

		unsigned int red = 192;
		unsigned int green = 156;
		unsigned int blue = 124;
		if (normalizedY < 0.36f)
		{
			red = 214;
			green = 176;
			blue = 138;
		}
		if ((leftHand && normalizedX < -0.34f) || (!leftHand && normalizedX > 0.34f))
		{
			red = 204;
			green = 166;
			blue = 128;
		}

		unsigned int const alphaByte = static_cast<unsigned int>(clampFloat(alpha * 245.0f, 0.0f, 245.0f));
		return (alphaByte << 24) | (red << 16) | (green << 8) | blue;
	}

	void fillPointerTexture(ID3D11Texture2D *texture, unsigned int width, unsigned int height)
	{
		if (!texture || !ms_context || width == 0 || height == 0)
			return;

		std::vector<unsigned int> pixels(width * height);
		for (unsigned int y = 0; y < height; ++y)
		{
			for (unsigned int x = 0; x < width; ++x)
				pixels[y * width + x] = pointerPixel(static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height));
		}

		ms_context->UpdateSubresource(texture, 0, 0, &pixels[0], width * sizeof(unsigned int), 0);
	}

	void fillReticleTexture(ID3D11Texture2D *texture, unsigned int width, unsigned int height)
	{
		if (!texture || !ms_context || width == 0 || height == 0)
			return;

		std::vector<unsigned int> pixels(width * height);
		for (unsigned int y = 0; y < height; ++y)
		{
			for (unsigned int x = 0; x < width; ++x)
				pixels[y * width + x] = reticlePixel(static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height));
		}

		ms_context->UpdateSubresource(texture, 0, 0, &pixels[0], width * sizeof(unsigned int), 0);
	}

	void fillHandTexture(ID3D11Texture2D *texture, unsigned int width, unsigned int height, bool closed)
	{
		if (!texture || !ms_context || width == 0 || height == 0)
			return;

		std::vector<unsigned int> pixels(width * height);
		unsigned int const halfWidth = width > 1 ? width / 2 : width;
		for (unsigned int y = 0; y < height; ++y)
		{
			for (unsigned int x = 0; x < width; ++x)
			{
				bool const leftHand = x < halfWidth;
				unsigned int const localX = leftHand ? x : x - halfWidth;
				pixels[y * width + x] = handPixel(static_cast<int>(localX), static_cast<int>(y), static_cast<int>(halfWidth), static_cast<int>(height), closed, leftHand);
			}
		}

		ms_context->UpdateSubresource(texture, 0, 0, &pixels[0], width * sizeof(unsigned int), 0);
	}

	int percentValue(int value, int maxValue)
	{
		if (maxValue <= 0)
			return 0;
		return clampInt(static_cast<int>((static_cast<float>(value) / static_cast<float>(maxValue)) * 100.0f + 0.5f), 0, 100);
	}

	void drawGdiText(HDC dc, RECT const &rect, char const *text, int height, COLORREF color, UINT format)
	{
		HFONT font = CreateFontA(height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
		HGDIOBJ oldFont = font ? SelectObject(dc, font) : 0;
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, color);
		RECT drawRect = rect;
		DrawTextA(dc, text ? text : "", -1, &drawRect, format);
		if (oldFont)
			SelectObject(dc, oldFont);
		if (font)
			DeleteObject(font);
	}

	void fillTargetContextTexture(ID3D11Texture2D *texture, unsigned int width, unsigned int height, char const *targetName, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool attackable)
	{
		if (!texture || !ms_context || width == 0 || height == 0)
			return;

		std::vector<unsigned int> pixels(width * height, 0xd8141820);
		BITMAPINFO bitmapInfo;
		ZeroMemory(&bitmapInfo, sizeof(bitmapInfo));
		bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width);
		bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biBitCount = 32;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;

		void *bits = 0;
		HDC dc = CreateCompatibleDC(0);
		HBITMAP bitmap = dc ? CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS, &bits, 0, 0) : 0;
		if (dc && bitmap && bits)
		{
			memcpy(bits, &pixels[0], pixels.size() * sizeof(unsigned int));
			HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
			HBRUSH backgroundBrush = CreateSolidBrush(RGB(20, 24, 32));
			RECT fullRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			FillRect(dc, &fullRect, backgroundBrush);
			DeleteObject(backgroundBrush);

			HBRUSH topBrush = CreateSolidBrush(attackable ? RGB(104, 31, 31) : RGB(31, 72, 68));
			RECT topRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height / 5) };
			FillRect(dc, &topRect, topBrush);
			DeleteObject(topBrush);

			RECT titleRect = { 28, 14, static_cast<LONG>(width - 28), static_cast<LONG>(height / 5 - 8) };
			drawGdiText(dc, titleRect, targetName && targetName[0] ? targetName : "Target", static_cast<int>(height / 12), RGB(244, 246, 248), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

			int const left = static_cast<int>(width / 12);
			int const right = static_cast<int>(width - width / 12);
			int const barWidth = right - left;
			int const barHeight = static_cast<int>(height / 12);
			int const firstBarY = static_cast<int>(height / 3);
			char label[96];
			struct BarSpec
			{
				char const *name;
				int value;
				int maxValue;
				COLORREF color;
			};
			BarSpec const bars[3] =
			{
				{ "Health", health, healthMax, RGB(206, 62, 64) },
				{ "Action", action, actionMax, RGB(72, 174, 88) },
				{ "Mind", mind, mindMax, RGB(76, 139, 218) }
			};

			for (int i = 0; i != 3; ++i)
			{
				int const y = firstBarY + i * static_cast<int>(height / 5);
				_snprintf(label, sizeof(label) - 1, "%s  %d / %d", bars[i].name, bars[i].value, bars[i].maxValue);
				label[sizeof(label) - 1] = '\0';
				RECT labelRect = { left, y - static_cast<int>(height / 14), right, y - 2 };
				drawGdiText(dc, labelRect, label, static_cast<int>(height / 17), RGB(220, 224, 230), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

				RECT railRect = { left, y, right, y + barHeight };
				HBRUSH railBrush = CreateSolidBrush(RGB(55, 60, 70));
				FillRect(dc, &railRect, railBrush);
				DeleteObject(railBrush);

				int const fillRight = left + (barWidth * percentValue(bars[i].value, bars[i].maxValue)) / 100;
				RECT fillRect = { left, y, fillRight, y + barHeight };
				HBRUSH fillBrush = CreateSolidBrush(bars[i].color);
				FillRect(dc, &fillRect, fillBrush);
				DeleteObject(fillBrush);
			}

			memcpy(&pixels[0], bits, pixels.size() * sizeof(unsigned int));
			for (size_t i = 0; i < pixels.size(); ++i)
				pixels[i] = (pixels[i] & 0x00ffffff) | 0xe8000000;

			SelectObject(dc, oldBitmap);
		}

		if (bitmap)
			DeleteObject(bitmap);
		if (dc)
			DeleteDC(dc);

		ms_context->UpdateSubresource(texture, 0, 0, &pixels[0], width * sizeof(unsigned int), 0);
	}

	void fillObjectContextTexture(ID3D11Texture2D *texture, unsigned int width, unsigned int height)
	{
		fillTargetContextTexture(texture, width, height, ms_objectContextTargetName, ms_objectContextHealth, ms_objectContextHealthMax, ms_objectContextAction, ms_objectContextActionMax, ms_objectContextMind, ms_objectContextMindMax, ms_objectContextAttackable);
	}

	void fillHoverTargetTexture(ID3D11Texture2D *texture, unsigned int width, unsigned int height)
	{
		fillTargetContextTexture(texture, width, height, ms_hoverTargetName, ms_hoverTargetHealth, ms_hoverTargetHealthMax, ms_hoverTargetAction, ms_hoverTargetActionMax, ms_hoverTargetMind, ms_hoverTargetMindMax, ms_hoverTargetAttackable);
	}

	bool stringToPath(char const *text, XrPath &path)
	{
		path = XR_NULL_PATH_LOCAL;
		if (!ms_xrStringToPath || !ms_xrInstance)
			return false;

		XrResult const result = ms_xrStringToPath(ms_xrInstance, text, &path);
		if (result != XR_SUCCESS_LOCAL || path == XR_NULL_PATH_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrControllerActions\",\"ready\":false,\"failure\":\"stringToPath\",\"path\":\"%s\",\"result\":%d}", text ? text : "", result);
			return false;
		}

		return true;
	}

	void releaseMenuButtonState(char const *reason);
	void releaseWristMenuButtonState(char const *reason);
	bool ensureObjectContextSwapchain();
	bool ensureHoverTargetSwapchain();
	bool ensureWristDashboardSwapchain();

	void destroyHandTrackers()
	{
		for (int hand = 0; hand != 2; ++hand)
		{
			if (ms_xrHandTrackers[hand] && ms_xrDestroyHandTrackerEXT)
				ms_xrDestroyHandTrackerEXT(ms_xrHandTrackers[hand]);
			ms_xrHandTrackers[hand] = 0;
		}

		ms_xrHandTrackingReady = false;
		ms_xrHandTrackingSnapshotCount = 0;
	}

	void destroyControllerActions()
	{
		for (int i = 0; i < 2; ++i)
		{
			if (ms_xrAimSpaces[i] && ms_xrDestroySpace)
				ms_xrDestroySpace(ms_xrAimSpaces[i]);
			ms_xrAimSpaces[i] = 0;
			if (ms_xrGripSpaces[i] && ms_xrDestroySpace)
				ms_xrDestroySpace(ms_xrGripSpaces[i]);
			ms_xrGripSpaces[i] = 0;
		}

		if (ms_xrSelectClickAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrSelectClickAction);
		ms_xrSelectClickAction = 0;
		if (ms_xrTargetPreviousClickAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrTargetPreviousClickAction);
		ms_xrTargetPreviousClickAction = 0;
		if (ms_xrTargetNextClickAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrTargetNextClickAction);
		ms_xrTargetNextClickAction = 0;
		if (ms_xrTurnModeClickAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrTurnModeClickAction);
		ms_xrTurnModeClickAction = 0;
		if (ms_xrMenuClickAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrMenuClickAction);
		ms_xrMenuClickAction = 0;
		if (ms_xrThumbstickAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrThumbstickAction);
		ms_xrThumbstickAction = 0;
		if (ms_xrSqueezeValueAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrSqueezeValueAction);
		ms_xrSqueezeValueAction = 0;
		if (ms_xrTriggerValueAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrTriggerValueAction);
		ms_xrTriggerValueAction = 0;
		if (ms_xrGripPoseAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrGripPoseAction);
		ms_xrGripPoseAction = 0;
		if (ms_xrAimPoseAction && ms_xrDestroyAction)
			ms_xrDestroyAction(ms_xrAimPoseAction);
		ms_xrAimPoseAction = 0;

		if (ms_xrControllerActionSet && ms_xrDestroyActionSet)
			ms_xrDestroyActionSet(ms_xrControllerActionSet);
		ms_xrControllerActionSet = 0;
		ms_xrHandSubactionPaths[0] = XR_NULL_PATH_LOCAL;
		ms_xrHandSubactionPaths[1] = XR_NULL_PATH_LOCAL;
		ms_xrLastInteractionProfiles[0] = XR_NULL_PATH_LOCAL;
		ms_xrLastInteractionProfiles[1] = XR_NULL_PATH_LOCAL;
		ms_xrLastInteractionProfileLogMilliseconds = 0;
		ms_xrInteractionProfileQueryMissingLogged = false;
		ms_xrControllerActionsReady = false;
		ms_xrControllerSnapshotCount = 0;
		ms_xrTargetNextButtonDown = false;
		ms_xrTargetPreviousButtonDown = false;
		releaseMenuButtonState("destroy-actions");
		releaseWristMenuButtonState("destroy-actions");
	}

	void destroyOpenXrObjects()
	{
		destroyHandTrackers();
		destroyControllerActions();

		if (ms_xrPointerSwapchain && ms_xrDestroySwapchain)
			ms_xrDestroySwapchain(ms_xrPointerSwapchain);
		ms_xrPointerSwapchain = 0;
		ms_xrPointerImages.clear();
		ms_xrPointerSwapchainWidth = 0;
		ms_xrPointerSwapchainHeight = 0;
		if (ms_xrWandSwapchain && ms_xrDestroySwapchain)
			ms_xrDestroySwapchain(ms_xrWandSwapchain);
		ms_xrWandSwapchain = 0;
		ms_xrWandImages.clear();
		ms_xrWandSwapchainWidth = 0;
		ms_xrWandSwapchainHeight = 0;
		if (ms_xrReticleSwapchain && ms_xrDestroySwapchain)
			ms_xrDestroySwapchain(ms_xrReticleSwapchain);
		ms_xrReticleSwapchain = 0;
		ms_xrReticleImages.clear();
		ms_xrReticleSwapchainWidth = 0;
		ms_xrReticleSwapchainHeight = 0;
		if (ms_xrObjectContextSwapchain && ms_xrDestroySwapchain)
			ms_xrDestroySwapchain(ms_xrObjectContextSwapchain);
		ms_xrObjectContextSwapchain = 0;
		ms_xrObjectContextImages.clear();
		for (std::vector<ID3D11RenderTargetView *>::iterator i = ms_xrObjectContextRenderTargetViews.begin(); i != ms_xrObjectContextRenderTargetViews.end(); ++i)
		{
			if (*i)
			{
				(*i)->Release();
				*i = 0;
			}
		}
		ms_xrObjectContextRenderTargetViews.clear();
		ms_xrObjectContextSwapchainWidth = 0;
		ms_xrObjectContextSwapchainHeight = 0;
		ms_xrObjectContextCaptureImageIndex = 0;
		ms_xrObjectContextCaptureImageAcquired = false;
		ms_xrObjectContextCaptureFrame = 0;
		ms_objectContextValid = false;
		ms_objectContextFrame = 0;
		for (int inputRegionIndex = 0; inputRegionIndex != static_cast<int>(sizeof(ms_objectContextInputRegions) / sizeof(ms_objectContextInputRegions[0])); ++inputRegionIndex)
			ms_objectContextInputRegions[inputRegionIndex].active = false;
		if (ms_xrHoverTargetSwapchain && ms_xrDestroySwapchain)
			ms_xrDestroySwapchain(ms_xrHoverTargetSwapchain);
		ms_xrHoverTargetSwapchain = 0;
		ms_xrHoverTargetImages.clear();
		ms_xrHoverTargetSwapchainWidth = 0;
		ms_xrHoverTargetSwapchainHeight = 0;
		ms_hoverTargetValid = false;
		ms_hoverTargetFrame = 0;
		if (ms_xrWristDashboardSwapchain && ms_xrDestroySwapchain)
			ms_xrDestroySwapchain(ms_xrWristDashboardSwapchain);
		ms_xrWristDashboardSwapchain = 0;
		ms_xrWristDashboardImages.clear();
		if (ms_xrWristDashboardCaptureRenderTargetView)
		{
			ms_xrWristDashboardCaptureRenderTargetView->Release();
			ms_xrWristDashboardCaptureRenderTargetView = 0;
		}
		if (ms_xrWristDashboardCaptureTexture)
		{
			ms_xrWristDashboardCaptureTexture->Release();
			ms_xrWristDashboardCaptureTexture = 0;
		}
		ms_xrWristDashboardSwapchainWidth = 0;
		ms_xrWristDashboardSwapchainHeight = 0;
		ms_xrWristDashboardCaptureImageIndex = 0;
		ms_xrWristDashboardCaptureImageAcquired = false;
		ms_xrWristDashboardCaptureFrame = 0;
		ms_wristDashboardValid = false;
		ms_wristDashboardFrame = 0;
		ms_xrPointerSubmitCount = 0;
		ms_xrWandSubmitCount = 0;
		ms_xrHandSubmitCount = 0;
		ms_xrWristMenuButtonSubmitCount = 0;
		ms_xrObjectContextSubmitCount = 0;
		ms_xrWristDashboardSubmitCount = 0;

		for (std::vector<ID3D11RenderTargetView *>::iterator i = ms_xrQuadRenderTargetViews.begin(); i != ms_xrQuadRenderTargetViews.end(); ++i)
		{
			if (*i)
			{
				(*i)->Release();
				*i = 0;
			}
		}
		ms_xrQuadRenderTargetViews.clear();
		if (ms_xrQuadSwapchain && ms_xrDestroySwapchain)
			ms_xrDestroySwapchain(ms_xrQuadSwapchain);
		ms_xrQuadSwapchain = 0;
		ms_xrQuadImages.clear();
		ms_xrQuadSwapchainWidth = 0;
		ms_xrQuadSwapchainHeight = 0;
		ms_xrQuadSwapchainFormat = 0;
		if (ms_xrProjectionSwapchain && ms_xrDestroySwapchain)
			ms_xrDestroySwapchain(ms_xrProjectionSwapchain);
		ms_xrProjectionSwapchain = 0;
		ms_xrProjectionImages.clear();
		ms_xrProjectionSwapchainWidth = 0;
		ms_xrProjectionSwapchainHeight = 0;
		ms_xrMaxSwapchainImageWidth = 0;
		ms_xrMaxSwapchainImageHeight = 0;
		ms_xrMaxLayerCount = 0;
		ms_xrProjectionImageReady = false;
		ms_xrProjectionImageIndex = 0;
		ms_xrWorldFrameBegun = false;
		ms_xrWorldFrameSubmitted = false;
		resetWorldHeadRecenter();

		if (ms_xrLocalSpace && ms_xrDestroySpace)
			ms_xrDestroySpace(ms_xrLocalSpace);
		ms_xrLocalSpace = 0;
		ms_xrReferenceSpaceType = 0;

		if (ms_xrSessionBegun && ms_xrEndSession)
			ms_xrEndSession(ms_xrSession);
		ms_xrSessionBegun = false;
		ms_xrSessionRunning = false;
		ms_xrSessionState = 0;

		if (ms_xrSession && ms_xrDestroySession)
			ms_xrDestroySession(ms_xrSession);
		ms_xrSession = 0;

		if (ms_xrInstance && ms_xrDestroyInstance)
			ms_xrDestroyInstance(ms_xrInstance);
		ms_xrInstance = 0;
		ms_xrSystemId = 0;
		ms_openXrFrameSubmitReady = false;
		ms_xrActionFunctionsReady = false;
		ms_xrHandTrackingFunctionsReady = false;
		ms_xrHandTrackingSystemSupported = false;
		ms_xrCreateHandTrackerEXT = 0;
		ms_xrDestroyHandTrackerEXT = 0;
		ms_xrLocateHandJointsEXT = 0;
		ms_xrQuadPoseCaptured = false;
		ms_xrQuadHeadPoseCaptured = false;
		ms_xrLoadingHeadPoseCaptured = false;
		ms_xrHandHeadPose = identityPose();
		ms_xrHandHeadPoseCaptured = false;
		ms_xrQuadAnchorFailureLogged = false;
		ms_xrUiQuadReady = false;
		ms_xrUiQuadPose = identityPose();
		ms_xrUiQuadWidthMeters = 0.0f;
		ms_xrUiQuadHeightMeters = 0.0f;
		ms_xrRightTriggerDown = false;
		ms_xrRightTriggerClientX = 0;
	ms_xrRightTriggerClientY = 0;
	ms_xrQuadPose = identityPose();
	ms_xrQuadHeadPose = identityPose();
	ms_xrLoadingHeadPose = identityPose();
}

	bool loadOpenXrFrameFunctions(PFN_xrGetInstanceProcAddrLocal getProc, XrInstance instance)
	{
		getXrProc(getProc, instance, "xrCreateSwapchain", ms_xrCreateSwapchain);
		getXrProc(getProc, instance, "xrDestroySwapchain", ms_xrDestroySwapchain);
		getXrProc(getProc, instance, "xrEnumerateSwapchainImages", ms_xrEnumerateSwapchainImages);
		getXrProc(getProc, instance, "xrPollEvent", ms_xrPollEvent);
		getXrProc(getProc, instance, "xrCreateReferenceSpace", ms_xrCreateReferenceSpace);
		getXrProc(getProc, instance, "xrDestroySpace", ms_xrDestroySpace);
		getXrProc(getProc, instance, "xrBeginSession", ms_xrBeginSession);
		getXrProc(getProc, instance, "xrEndSession", ms_xrEndSession);
		getXrProc(getProc, instance, "xrWaitFrame", ms_xrWaitFrame);
		getXrProc(getProc, instance, "xrBeginFrame", ms_xrBeginFrame);
		getXrProc(getProc, instance, "xrEndFrame", ms_xrEndFrame);
		getXrProc(getProc, instance, "xrLocateViews", ms_xrLocateViews);
		getXrProc(getProc, instance, "xrAcquireSwapchainImage", ms_xrAcquireSwapchainImage);
		getXrProc(getProc, instance, "xrWaitSwapchainImage", ms_xrWaitSwapchainImage);
		getXrProc(getProc, instance, "xrReleaseSwapchainImage", ms_xrReleaseSwapchainImage);
		getXrProc(getProc, instance, "xrStringToPath", ms_xrStringToPath);
		getXrProc(getProc, instance, "xrPathToString", ms_xrPathToString);
		getXrProc(getProc, instance, "xrCreateActionSet", ms_xrCreateActionSet);
		getXrProc(getProc, instance, "xrDestroyActionSet", ms_xrDestroyActionSet);
		getXrProc(getProc, instance, "xrCreateAction", ms_xrCreateAction);
		getXrProc(getProc, instance, "xrDestroyAction", ms_xrDestroyAction);
		getXrProc(getProc, instance, "xrSuggestInteractionProfileBindings", ms_xrSuggestInteractionProfileBindings);
		getXrProc(getProc, instance, "xrGetCurrentInteractionProfile", ms_xrGetCurrentInteractionProfile);
		getXrProc(getProc, instance, "xrAttachSessionActionSets", ms_xrAttachSessionActionSets);
		getXrProc(getProc, instance, "xrSyncActions", ms_xrSyncActions);
		getXrProc(getProc, instance, "xrGetActionStateBoolean", ms_xrGetActionStateBoolean);
		getXrProc(getProc, instance, "xrGetActionStateFloat", ms_xrGetActionStateFloat);
		getXrProc(getProc, instance, "xrGetActionStateVector2f", ms_xrGetActionStateVector2f);
		getXrProc(getProc, instance, "xrGetActionStatePose", ms_xrGetActionStatePose);
		getXrProc(getProc, instance, "xrCreateActionSpace", ms_xrCreateActionSpace);
		getXrProc(getProc, instance, "xrLocateSpace", ms_xrLocateSpace);
		if (ms_xrHandTrackingExtensionSupported)
		{
			getXrProc(getProc, instance, "xrCreateHandTrackerEXT", ms_xrCreateHandTrackerEXT);
			getXrProc(getProc, instance, "xrDestroyHandTrackerEXT", ms_xrDestroyHandTrackerEXT);
			getXrProc(getProc, instance, "xrLocateHandJointsEXT", ms_xrLocateHandJointsEXT);
		}
		ms_xrActionFunctionsReady = ms_xrStringToPath &&
			ms_xrCreateActionSet &&
			ms_xrDestroyActionSet &&
			ms_xrCreateAction &&
			ms_xrDestroyAction &&
			ms_xrSuggestInteractionProfileBindings &&
			ms_xrAttachSessionActionSets &&
			ms_xrSyncActions &&
			ms_xrGetActionStateBoolean &&
			ms_xrGetActionStateFloat &&
			ms_xrGetActionStateVector2f &&
			ms_xrGetActionStatePose &&
			ms_xrCreateActionSpace &&
			ms_xrLocateSpace;
		bridgeLog("{\"event\":\"openxrActionFunctions\",\"ready\":%s}", ms_xrActionFunctionsReady ? "true" : "false");
		ms_xrHandTrackingFunctionsReady = ms_xrHandTrackingExtensionSupported &&
			ms_xrCreateHandTrackerEXT &&
			ms_xrDestroyHandTrackerEXT &&
			ms_xrLocateHandJointsEXT;
		bridgeLog("{\"event\":\"openxrHandTrackingFunctions\",\"ready\":%s,\"extension\":%s,\"system\":%s}",
			ms_xrHandTrackingFunctionsReady ? "true" : "false",
			ms_xrHandTrackingExtensionSupported ? "true" : "false",
			ms_xrHandTrackingSystemSupported ? "true" : "false");

		return ms_xrCreateSwapchain &&
			ms_xrDestroySwapchain &&
			ms_xrEnumerateSwapchainImages &&
			ms_xrPollEvent &&
			ms_xrCreateReferenceSpace &&
			ms_xrDestroySpace &&
			ms_xrBeginSession &&
			ms_xrEndSession &&
			ms_xrWaitFrame &&
			ms_xrBeginFrame &&
			ms_xrEndFrame &&
			ms_xrLocateViews &&
			ms_xrAcquireSwapchainImage &&
			ms_xrWaitSwapchainImage &&
			ms_xrReleaseSwapchainImage;
	}

	void releaseMenuButtonState(char const *reason)
	{
		if (!ms_xrMenuButtonDown)
			return;

		ms_xrMenuButtonDown = false;
		if (ms_vrMenuInput)
			ms_vrMenuInput(false);
		bridgeLog("{\"event\":\"openxrMenuButton\",\"pressed\":false,\"reason\":\"%s\",\"frame\":%d,\"callback\":%s}", reason ? reason : "", ms_frameCount, ms_vrMenuInput ? "true" : "false");
	}

	void releaseWristMenuButtonState(char const *reason)
	{
		if (!ms_xrWristMenuButtonDown)
			return;

		ms_xrWristMenuButtonDown = false;
		if (ms_vrMenuInput)
			ms_vrMenuInput(false);
		bridgeLog("{\"event\":\"openxrWristMenuButtonInput\",\"pressed\":false,\"reason\":\"%s\",\"frame\":%d,\"callback\":%s}", reason ? reason : "", ms_frameCount, ms_vrMenuInput ? "true" : "false");
	}

	bool createControllerAction(char const *name, char const *localizedName, XrActionType actionType, XrAction &action)
	{
		action = 0;
		if (!ms_xrCreateAction || !ms_xrControllerActionSet)
			return false;

		XrActionCreateInfoLocal actionInfo;
		ZeroMemory(&actionInfo, sizeof(actionInfo));
		actionInfo.type = XR_TYPE_ACTION_CREATE_INFO_LOCAL;
		actionInfo.actionType = actionType;
		actionInfo.countSubactionPaths = 2;
		actionInfo.subactionPaths = ms_xrHandSubactionPaths;
		strncpy(actionInfo.actionName, name, sizeof(actionInfo.actionName) - 1);
		strncpy(actionInfo.localizedActionName, localizedName, sizeof(actionInfo.localizedActionName) - 1);
		XrResult const result = ms_xrCreateAction(ms_xrControllerActionSet, &actionInfo, &action);
		if (result != XR_SUCCESS_LOCAL || !action)
		{
			bridgeLog("{\"event\":\"openxrControllerActions\",\"ready\":false,\"failure\":\"createAction\",\"name\":\"%s\",\"result\":%d}", name ? name : "", result);
			return false;
		}

		return true;
	}

	bool addSuggestedBinding(XrActionSuggestedBindingLocal *bindings, unsigned int &bindingCount, unsigned int maxBindings, XrAction action, char const *bindingPath)
	{
		if (!bindings || bindingCount >= maxBindings || !action)
			return false;

		XrPath path = XR_NULL_PATH_LOCAL;
		if (!stringToPath(bindingPath, path))
			return false;

		bindings[bindingCount].action = action;
		bindings[bindingCount].binding = path;
		++bindingCount;
		return true;
	}

	bool suggestControllerBindings(char const *profileName, char const *profilePath, XrActionSuggestedBindingLocal const *bindings, unsigned int bindingCount, unsigned int &profileCount, unsigned int &totalBindingCount)
	{
		if (!ms_xrSuggestInteractionProfileBindings || !profilePath || !bindings || bindingCount == 0)
			return false;

		XrPath interactionProfile = XR_NULL_PATH_LOCAL;
		if (!stringToPath(profilePath, interactionProfile))
			return false;

		XrInteractionProfileSuggestedBindingLocal suggestedBinding;
		ZeroMemory(&suggestedBinding, sizeof(suggestedBinding));
		suggestedBinding.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING_LOCAL;
		suggestedBinding.interactionProfile = interactionProfile;
		suggestedBinding.countSuggestedBindings = bindingCount;
		suggestedBinding.suggestedBindings = bindings;

		XrResult const result = ms_xrSuggestInteractionProfileBindings(ms_xrInstance, &suggestedBinding);
		if (result != XR_SUCCESS_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrControllerBindings\",\"ready\":false,\"profile\":\"%s\",\"result\":%d,\"bindings\":%u}", profileName ? profileName : "", result, bindingCount);
			return false;
		}

		++profileCount;
		totalBindingCount += bindingCount;
		bridgeLog("{\"event\":\"openxrControllerBindings\",\"ready\":true,\"profile\":\"%s\",\"bindings\":%u}", profileName ? profileName : "", bindingCount);
		return true;
	}

	bool createActionSpace(XrAction action, XrPath subactionPath, XrSpace &space)
	{
		space = 0;
		if (!ms_xrCreateActionSpace || !ms_xrSession || !action || subactionPath == XR_NULL_PATH_LOCAL)
			return false;

		XrActionSpaceCreateInfoLocal spaceInfo;
		ZeroMemory(&spaceInfo, sizeof(spaceInfo));
		spaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO_LOCAL;
		spaceInfo.action = action;
		spaceInfo.subactionPath = subactionPath;
		spaceInfo.poseInActionSpace = identityPose();
		XrResult const result = ms_xrCreateActionSpace(ms_xrSession, &spaceInfo, &space);
		if (result != XR_SUCCESS_LOCAL || !space)
		{
			bridgeLog("{\"event\":\"openxrControllerActions\",\"ready\":false,\"failure\":\"createActionSpace\",\"result\":%d}", result);
			return false;
		}

		return true;
	}

	bool createHandTrackers()
	{
		destroyHandTrackers();
		if (!ms_xrHandTrackingExtensionSupported || !ms_xrHandTrackingSystemSupported)
		{
			bridgeLog("{\"event\":\"openxrHandTracking\",\"ready\":false,\"failure\":\"unsupported\",\"extension\":%s,\"system\":%s}",
				ms_xrHandTrackingExtensionSupported ? "true" : "false",
				ms_xrHandTrackingSystemSupported ? "true" : "false");
			return false;
		}
		if (!ms_xrHandTrackingFunctionsReady || !ms_xrSession)
		{
			bridgeLog("{\"event\":\"openxrHandTracking\",\"ready\":false,\"failure\":\"functions-or-session-missing\"}");
			return false;
		}

		bool anyReady = false;
		for (int hand = 0; hand != 2; ++hand)
		{
			XrHandTrackerCreateInfoEXTLocal createInfo;
			ZeroMemory(&createInfo, sizeof(createInfo));
			createInfo.type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT_LOCAL;
			createInfo.hand = hand == 0 ? XR_HAND_LEFT_EXT_LOCAL : XR_HAND_RIGHT_EXT_LOCAL;
			createInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT_LOCAL;

			XrResult const result = ms_xrCreateHandTrackerEXT(ms_xrSession, &createInfo, &ms_xrHandTrackers[hand]);
			bool const ready = result == XR_SUCCESS_LOCAL && ms_xrHandTrackers[hand] != 0;
			anyReady = anyReady || ready;
			bridgeLog("{\"event\":\"openxrHandTracker\",\"ready\":%s,\"hand\":\"%s\",\"result\":%d}",
				ready ? "true" : "false",
				hand == 0 ? "left" : "right",
				result);
		}

		ms_xrHandTrackingReady = anyReady;
		return anyReady;
	}

	bool createControllerActions()
	{
		destroyControllerActions();
		if (!ms_xrActionFunctionsReady || !ms_xrSession || !ms_xrInstance)
		{
			bridgeLog("{\"event\":\"openxrControllerActions\",\"ready\":false,\"failure\":\"action-functions-missing\"}");
			return false;
		}

		if (!stringToPath("/user/hand/left", ms_xrHandSubactionPaths[0]) || !stringToPath("/user/hand/right", ms_xrHandSubactionPaths[1]))
			return false;

		XrActionSetCreateInfoLocal actionSetInfo;
		ZeroMemory(&actionSetInfo, sizeof(actionSetInfo));
		actionSetInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO_LOCAL;
		strncpy(actionSetInfo.actionSetName, "swg_vr_controls", sizeof(actionSetInfo.actionSetName) - 1);
		strncpy(actionSetInfo.localizedActionSetName, "SWG VR Controls", sizeof(actionSetInfo.localizedActionSetName) - 1);
		actionSetInfo.priority = 0;
		XrResult result = ms_xrCreateActionSet(ms_xrInstance, &actionSetInfo, &ms_xrControllerActionSet);
		if (result != XR_SUCCESS_LOCAL || !ms_xrControllerActionSet)
		{
			bridgeLog("{\"event\":\"openxrControllerActions\",\"ready\":false,\"failure\":\"createActionSet\",\"result\":%d}", result);
			return false;
		}

		if (!createControllerAction("aim_pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT_LOCAL, ms_xrAimPoseAction) ||
			!createControllerAction("grip_pose", "Grip Pose", XR_ACTION_TYPE_POSE_INPUT_LOCAL, ms_xrGripPoseAction) ||
			!createControllerAction("trigger_value", "Trigger Value", XR_ACTION_TYPE_FLOAT_INPUT_LOCAL, ms_xrTriggerValueAction) ||
			!createControllerAction("squeeze_value", "Squeeze Value", XR_ACTION_TYPE_FLOAT_INPUT_LOCAL, ms_xrSqueezeValueAction) ||
			!createControllerAction("thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT_LOCAL, ms_xrThumbstickAction) ||
			!createControllerAction("menu_click", "Menu Click", XR_ACTION_TYPE_BOOLEAN_INPUT_LOCAL, ms_xrMenuClickAction) ||
			!createControllerAction("target_next_click", "Target Next", XR_ACTION_TYPE_BOOLEAN_INPUT_LOCAL, ms_xrTargetNextClickAction) ||
			!createControllerAction("target_previous_click", "Target Previous", XR_ACTION_TYPE_BOOLEAN_INPUT_LOCAL, ms_xrTargetPreviousClickAction) ||
			!createControllerAction("turn_mode_click", "Turn Mode Click", XR_ACTION_TYPE_BOOLEAN_INPUT_LOCAL, ms_xrTurnModeClickAction))
		{
			destroyControllerActions();
			return false;
		}

		static unsigned int const bindingCapacity = 32;
		unsigned int suggestedProfileCount = 0;
		unsigned int totalBindingCount = 0;
		bool const bindSystemMenuButton = getEnvironmentFlagDefault("SWG_OG_VR_BIND_SYSTEM_MENU_BUTTON", false);

		{
			XrActionSuggestedBindingLocal bindings[bindingCapacity];
			ZeroMemory(bindings, sizeof(bindings));
			unsigned int bindingCount = 0;
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/left/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/right/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/left/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/right/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTriggerValueAction, "/user/hand/left/input/trigger/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTriggerValueAction, "/user/hand/right/input/trigger/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrSqueezeValueAction, "/user/hand/left/input/squeeze/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrSqueezeValueAction, "/user/hand/right/input/squeeze/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrThumbstickAction, "/user/hand/left/input/thumbstick");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrThumbstickAction, "/user/hand/right/input/thumbstick");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTurnModeClickAction, "/user/hand/right/input/thumbstick/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTargetPreviousClickAction, "/user/hand/left/input/x/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTargetNextClickAction, "/user/hand/left/input/y/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/right/input/a/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/right/input/b/click");
			if (bindSystemMenuButton)
			{
				addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/left/input/menu/click");
				addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/right/input/menu/click");
			}
			IGNORE_RETURN(suggestControllerBindings("oculus_touch", "/interaction_profiles/oculus/touch_controller", bindings, bindingCount, suggestedProfileCount, totalBindingCount));
		}

		{
			XrActionSuggestedBindingLocal bindings[bindingCapacity];
			ZeroMemory(bindings, sizeof(bindings));
			unsigned int bindingCount = 0;
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/left/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/right/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/left/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/right/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTriggerValueAction, "/user/hand/left/input/trigger/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTriggerValueAction, "/user/hand/right/input/trigger/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrSqueezeValueAction, "/user/hand/left/input/squeeze/force");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrSqueezeValueAction, "/user/hand/right/input/squeeze/force");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrThumbstickAction, "/user/hand/left/input/thumbstick");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrThumbstickAction, "/user/hand/right/input/thumbstick");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTurnModeClickAction, "/user/hand/right/input/thumbstick/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/left/input/a/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/left/input/b/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/right/input/a/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/right/input/b/click");
			if (bindSystemMenuButton)
			{
				addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/left/input/system/click");
				addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/right/input/system/click");
			}
			IGNORE_RETURN(suggestControllerBindings("valve_index", "/interaction_profiles/valve/index_controller", bindings, bindingCount, suggestedProfileCount, totalBindingCount));
		}

		{
			XrActionSuggestedBindingLocal bindings[bindingCapacity];
			ZeroMemory(bindings, sizeof(bindings));
			unsigned int bindingCount = 0;
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/left/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/right/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/left/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/right/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTriggerValueAction, "/user/hand/left/input/trigger/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTriggerValueAction, "/user/hand/right/input/trigger/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrThumbstickAction, "/user/hand/left/input/trackpad");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrThumbstickAction, "/user/hand/right/input/trackpad");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTurnModeClickAction, "/user/hand/right/input/trackpad/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/left/input/menu/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/right/input/menu/click");
			IGNORE_RETURN(suggestControllerBindings("htc_vive", "/interaction_profiles/htc/vive_controller", bindings, bindingCount, suggestedProfileCount, totalBindingCount));
		}

		{
			XrActionSuggestedBindingLocal bindings[bindingCapacity];
			ZeroMemory(bindings, sizeof(bindings));
			unsigned int bindingCount = 0;
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/left/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/right/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/left/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/right/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTriggerValueAction, "/user/hand/left/input/trigger/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTriggerValueAction, "/user/hand/right/input/trigger/value");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrThumbstickAction, "/user/hand/left/input/thumbstick");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrThumbstickAction, "/user/hand/right/input/thumbstick");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrTurnModeClickAction, "/user/hand/right/input/thumbstick/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/left/input/menu/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/right/input/menu/click");
			IGNORE_RETURN(suggestControllerBindings("microsoft_motion", "/interaction_profiles/microsoft/motion_controller", bindings, bindingCount, suggestedProfileCount, totalBindingCount));
			IGNORE_RETURN(suggestControllerBindings("hp_mixed_reality", "/interaction_profiles/hp/mixed_reality_controller", bindings, bindingCount, suggestedProfileCount, totalBindingCount));
		}

		{
			XrActionSuggestedBindingLocal bindings[bindingCapacity];
			ZeroMemory(bindings, sizeof(bindings));
			unsigned int bindingCount = 0;
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/left/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrAimPoseAction, "/user/hand/right/input/aim/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/left/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrGripPoseAction, "/user/hand/right/input/grip/pose");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/left/input/menu/click");
			addSuggestedBinding(bindings, bindingCount, bindingCapacity, ms_xrMenuClickAction, "/user/hand/right/input/menu/click");
			IGNORE_RETURN(suggestControllerBindings("khr_simple", "/interaction_profiles/khr/simple_controller", bindings, bindingCount, suggestedProfileCount, totalBindingCount));
		}

		if (suggestedProfileCount == 0)
		{
			bridgeLog("{\"event\":\"openxrControllerActions\",\"ready\":false,\"failure\":\"suggestBindings\",\"profiles\":0}");
			destroyControllerActions();
			return false;
		}

		for (int hand = 0; hand < 2; ++hand)
		{
			if (!createActionSpace(ms_xrAimPoseAction, ms_xrHandSubactionPaths[hand], ms_xrAimSpaces[hand]) ||
				!createActionSpace(ms_xrGripPoseAction, ms_xrHandSubactionPaths[hand], ms_xrGripSpaces[hand]))
			{
				destroyControllerActions();
				return false;
			}
		}

		XrSessionActionSetsAttachInfoLocal attachInfo;
		ZeroMemory(&attachInfo, sizeof(attachInfo));
		attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO_LOCAL;
		attachInfo.countActionSets = 1;
		attachInfo.actionSets = &ms_xrControllerActionSet;
		result = ms_xrAttachSessionActionSets(ms_xrSession, &attachInfo);
		if (result != XR_SUCCESS_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrControllerActions\",\"ready\":false,\"failure\":\"attachActionSets\",\"result\":%d}", result);
			destroyControllerActions();
			return false;
		}

		ms_xrControllerActionsReady = true;
		ms_xrControllerSnapshotCount = 0;
		bridgeLog("{\"event\":\"openxrControllerActions\",\"ready\":true,\"profile\":\"multi\",\"profiles\":%u,\"bindings\":%u}", suggestedProfileCount, totalBindingCount);
		return true;
	}

	XrActionStateFloatLocal getFloatActionState(XrAction action, XrPath subactionPath)
	{
		XrActionStateFloatLocal state;
		ZeroMemory(&state, sizeof(state));
		state.type = XR_TYPE_ACTION_STATE_FLOAT_LOCAL;
		if (!ms_xrGetActionStateFloat || !action)
			return state;

		XrActionStateGetInfoLocal getInfo;
		ZeroMemory(&getInfo, sizeof(getInfo));
		getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO_LOCAL;
		getInfo.action = action;
		getInfo.subactionPath = subactionPath;
		ms_xrGetActionStateFloat(ms_xrSession, &getInfo, &state);
		return state;
	}

	XrActionStateBooleanLocal getBooleanActionState(XrAction action, XrPath subactionPath)
	{
		XrActionStateBooleanLocal state;
		ZeroMemory(&state, sizeof(state));
		state.type = XR_TYPE_ACTION_STATE_BOOLEAN_LOCAL;
		if (!ms_xrGetActionStateBoolean || !action)
			return state;

		XrActionStateGetInfoLocal getInfo;
		ZeroMemory(&getInfo, sizeof(getInfo));
		getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO_LOCAL;
		getInfo.action = action;
		getInfo.subactionPath = subactionPath;
		ms_xrGetActionStateBoolean(ms_xrSession, &getInfo, &state);
		return state;
	}

	XrActionStateVector2fLocal getVector2ActionState(XrAction action, XrPath subactionPath)
	{
		XrActionStateVector2fLocal state;
		ZeroMemory(&state, sizeof(state));
		state.type = XR_TYPE_ACTION_STATE_VECTOR2F_LOCAL;
		if (!ms_xrGetActionStateVector2f || !action)
			return state;

		XrActionStateGetInfoLocal getInfo;
		ZeroMemory(&getInfo, sizeof(getInfo));
		getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO_LOCAL;
		getInfo.action = action;
		getInfo.subactionPath = subactionPath;
		ms_xrGetActionStateVector2f(ms_xrSession, &getInfo, &state);
		return state;
	}

	XrActionStatePoseLocal getPoseActionState(XrAction action, XrPath subactionPath)
	{
		XrActionStatePoseLocal state;
		ZeroMemory(&state, sizeof(state));
		state.type = XR_TYPE_ACTION_STATE_POSE_LOCAL;
		if (!ms_xrGetActionStatePose || !action)
			return state;

		XrActionStateGetInfoLocal getInfo;
		ZeroMemory(&getInfo, sizeof(getInfo));
		getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO_LOCAL;
		getInfo.action = action;
		getInfo.subactionPath = subactionPath;
		ms_xrGetActionStatePose(ms_xrSession, &getInfo, &state);
		return state;
	}

	XrSpaceLocationLocal locateActionSpace(XrSpace space, XrTime displayTime)
	{
		XrSpaceLocationLocal location;
		ZeroMemory(&location, sizeof(location));
		location.type = XR_TYPE_SPACE_LOCATION_LOCAL;
		location.pose = identityPose();
		if (!ms_xrLocateSpace || !space || !ms_xrLocalSpace)
			return location;

		ms_xrLocateSpace(space, ms_xrLocalSpace, displayTime, &location);
		return location;
	}

	bool yawRadiansFromOrientation(XrQuaternionfLocal const &orientation, float &yawRadians)
	{
		float const quaternionLengthSquared =
			orientation.x * orientation.x +
			orientation.y * orientation.y +
			orientation.z * orientation.z +
			orientation.w * orientation.w;
		if (quaternionLengthSquared < 0.0001f)
			return false;

		XrVector3fLocal const forward = headsetForward(orientation);
		yawRadians = atan2f(-forward.x, -forward.z);
		return true;
	}

	bool getHeadMovementYaw(float &yawRadians)
	{
		if (ms_xrWorldViews[0].type != XR_TYPE_VIEW_LOCAL)
			return false;

		XrPosefLocal const headPose = recenteredWorldPose(ms_xrWorldViews[0].pose);
		return yawRadiansFromOrientation(headPose.orientation, yawRadians);
	}

	bool getHandMovementYaw(int handIndex, XrTime displayTime, float &yawRadians)
	{
		if (handIndex < 0 || handIndex >= 2 || !ms_xrAimSpaces[handIndex])
			return false;

		XrSpaceLocationLocal const aimLocation = locateActionSpace(ms_xrAimSpaces[handIndex], displayTime);
		bool const orientationValid = (aimLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
		if (!orientationValid)
			return false;

		XrPosefLocal const aimPose = recenteredWorldPose(aimLocation.pose);
		return yawRadiansFromOrientation(aimPose.orientation, yawRadians);
	}

	void submitVrControllerInput(float leftX, float leftY, bool leftActive, float rightX, float rightY, bool rightActive, XrTime displayTime, bool turnModeClickPressed)
	{
		float headYawRadians = 0.0f;
		float leftHandYawRadians = 0.0f;
		float rightHandYawRadians = 0.0f;
		bool const headYawValid = getHeadMovementYaw(headYawRadians);
		bool const leftHandYawValid = getHandMovementYaw(0, displayTime, leftHandYawRadians);
		bool const rightHandYawValid = getHandMovementYaw(1, displayTime, rightHandYawRadians);
		static int s_controllerInputLogCount = 0;
		bool const stickMovedForLog =
			(leftActive && (fabsf(leftX) > 0.20f || fabsf(leftY) > 0.20f)) ||
			(rightActive && (fabsf(rightX) > 0.20f || fabsf(rightY) > 0.20f));
		if ((stickMovedForLog || turnModeClickPressed) && (s_controllerInputLogCount < 120 || (ms_frameCount % 120) == 0))
		{
			++s_controllerInputLogCount;
			bridgeLog("{\"event\":\"openxrControllerInput\",\"frame\":%d,\"left\":[%0.3f,%0.3f],\"leftActive\":%s,\"right\":[%0.3f,%0.3f],\"rightActive\":%s,\"turnModeClick\":%s,\"headYawDegrees\":%0.3f,\"headYawValid\":%s,\"leftHandYawDegrees\":%0.3f,\"leftHandYawValid\":%s,\"rightHandYawDegrees\":%0.3f,\"rightHandYawValid\":%s}",
				ms_frameCount,
				leftX,
				leftY,
				leftActive ? "true" : "false",
				rightX,
				rightY,
				rightActive ? "true" : "false",
				turnModeClickPressed ? "true" : "false",
				headYawRadians * 57.2957795f,
				headYawValid ? "true" : "false",
				leftHandYawRadians * 57.2957795f,
				leftHandYawValid ? "true" : "false",
				rightHandYawRadians * 57.2957795f,
				rightHandYawValid ? "true" : "false");
		}

		ms_vrControllerInput(
			leftX,
			leftY,
			leftActive,
			rightX,
			rightY,
			rightActive,
			headYawRadians,
			headYawValid,
			leftHandYawRadians,
			leftHandYawValid,
			rightHandYawRadians,
			rightHandYawValid,
			turnModeClickPressed);
	}

	bool intersectUiQuad(XrVector3fLocal const &origin, XrVector3fLocal const &direction, XrVector3fLocal &hitPosition, float &hitDistance, float &u, float &v, bool &inside)
	{
		inside = false;
		if (!ms_xrUiQuadReady || ms_xrUiQuadWidthMeters <= 0.0f || ms_xrUiQuadHeightMeters <= 0.0f)
			return false;

		XrVector3fLocal const normal = quaternionForward(ms_xrUiQuadPose.orientation);
		float const denominator = dotVector(direction, normal);
		if (fabsf(denominator) < 0.0001f)
			return false;

		XrVector3fLocal const planeDelta = subtractVector(ms_xrUiQuadPose.position, origin);
		hitDistance = dotVector(planeDelta, normal) / denominator;
		if (hitDistance < 0.02f || hitDistance > 8.0f)
			return false;

		hitPosition = addVector(origin, scaleVector(direction, hitDistance));
		XrVector3fLocal const fromCenter = subtractVector(hitPosition, ms_xrUiQuadPose.position);
		XrVector3fLocal const right = rightFromForward(normal);
		XrVector3fLocal up;
		up.x = 0.0f;
		up.y = 1.0f;
		up.z = 0.0f;
		float const localX = dotVector(fromCenter, right);
		float const localY = dotVector(fromCenter, up);
		float const rawU = 0.5f + localX / ms_xrUiQuadWidthMeters;
		float const rawV = 0.5f - localY / ms_xrUiQuadHeightMeters;
		inside = rawU >= 0.0f && rawU <= 1.0f && rawV >= 0.0f && rawV <= 1.0f;
		u = clampFloat(rawU, 0.0f, 1.0f);
		v = clampFloat(rawV, 0.0f, 1.0f);
		hitPosition = addVector(ms_xrUiQuadPose.position, addVector(scaleVector(right, (u - 0.5f) * ms_xrUiQuadWidthMeters), scaleVector(up, (0.5f - v) * ms_xrUiQuadHeightMeters)));
		return true;
	}

	XrPosefLocal wristMenuButtonPoseInGripSpace(int hand)
	{
		XrVector3fLocal localNormal;
		localNormal.x = 0.0f;
		localNormal.y = 1.0f;
		localNormal.z = 0.0f;
		XrVector3fLocal localUp;
		localUp.x = 0.0f;
		localUp.y = 0.0f;
		localUp.z = -1.0f;

		XrPosefLocal wristPose = identityPose();
		wristPose.orientation = wandOrientationFromRay(localNormal, localUp, getEnvironmentFloat("SWG_OG_VR_WRIST_MENU_BUTTON_ROLL_DEGREES", 0.0f) * 0.01745329252f);
		wristPose.position.x = getEnvironmentFloat("SWG_OG_VR_WRIST_MENU_BUTTON_OFFSET_X_METERS", hand == 0 ? 0.105f : -0.105f);
		wristPose.position.y = getEnvironmentFloat("SWG_OG_VR_WRIST_MENU_BUTTON_OFFSET_Y_METERS", 0.065f);
		wristPose.position.z = getEnvironmentFloat("SWG_OG_VR_WRIST_MENU_BUTTON_OFFSET_Z_METERS", -0.045f);
		return wristPose;
	}

	float wristMenuButtonWidthMeters()
	{
		return clampFloat(getEnvironmentFloat("SWG_OG_VR_WRIST_MENU_BUTTON_WIDTH_METERS", 0.420f), 0.050f, 0.700f);
	}

	float wristMenuButtonHeightMeters()
	{
		return clampFloat(getEnvironmentFloat("SWG_OG_VR_WRIST_MENU_BUTTON_HEIGHT_METERS", 0.280f), 0.040f, 0.520f);
	}

	bool wristMenuButtonPoseInLocalSpace(XrTime displayTime, int &hand, XrPosefLocal &pose, float &widthMeters, float &heightMeters)
	{
		if (!isWristMenuButtonEnabled() || !ms_xrControllerActionsReady)
			return false;

		hand = clampInt(static_cast<int>(getEnvironmentFloat("SWG_OG_VR_WRIST_MENU_BUTTON_HAND", 0.0f)), 0, 1);
		if (!ms_xrGripSpaces[hand])
			return false;

		XrSpaceLocationLocal const gripLocation = locateActionSpace(ms_xrGripSpaces[hand], displayTime);
		bool const positionValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL) != 0;
		bool const orientationValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
		if (!positionValid || !orientationValid)
			return false;

		XrPosefLocal const wristPose = wristMenuButtonPoseInGripSpace(hand);
		pose = identityPose();
		pose.orientation = multiplyQuaternion(gripLocation.pose.orientation, wristPose.orientation);
		pose.position = addVector(gripLocation.pose.position, rotateVector(gripLocation.pose.orientation, wristPose.position));
		widthMeters = wristMenuButtonWidthMeters();
		heightMeters = wristMenuButtonHeightMeters();
		return true;
	}

	bool intersectWristMenuButton(XrTime displayTime, XrVector3fLocal const &origin, XrVector3fLocal const &direction, XrVector3fLocal &hitPosition, float &hitDistance, bool &inside)
	{
		inside = false;
		int hand = -1;
		float widthMeters = 0.0f;
		float heightMeters = 0.0f;
		XrPosefLocal pose = identityPose();
		if (!wristMenuButtonPoseInLocalSpace(displayTime, hand, pose, widthMeters, heightMeters))
			return false;

		XrVector3fLocal normal;
		normal.x = 0.0f;
		normal.y = 0.0f;
		normal.z = -1.0f;
		normal = rotateVector(pose.orientation, normal);
		float const denominator = dotVector(direction, normal);
		if (fabsf(denominator) < 0.0001f)
			return false;

		XrVector3fLocal const planeDelta = subtractVector(pose.position, origin);
		hitDistance = dotVector(planeDelta, normal) / denominator;
		if (hitDistance < 0.02f || hitDistance > 2.50f)
			return false;

		XrVector3fLocal const rawHitPosition = addVector(origin, scaleVector(direction, hitDistance));
		XrVector3fLocal const fromCenter = subtractVector(rawHitPosition, pose.position);
		XrVector3fLocal right;
		right.x = 1.0f;
		right.y = 0.0f;
		right.z = 0.0f;
		right = rotateVector(pose.orientation, right);
		XrVector3fLocal up;
		up.x = 0.0f;
		up.y = 1.0f;
		up.z = 0.0f;
		up = rotateVector(pose.orientation, up);

		float const localX = dotVector(fromCenter, right);
		float const localY = dotVector(fromCenter, up);
		float const rawU = 0.5f + localX / widthMeters;
		float const rawV = 0.5f - localY / heightMeters;
		inside = rawU >= 0.0f && rawU <= 1.0f && rawV >= 0.0f && rawV <= 1.0f;
		if (!inside)
			return false;

		hitPosition = addVector(pose.position, addVector(scaleVector(right, (rawU - 0.5f) * widthMeters), scaleVector(up, (0.5f - rawV) * heightMeters)));
		return true;
	}

	bool intersectVrUiSurface(XrVector3fLocal const &origin, XrVector3fLocal const &direction, XrVector3fLocal &hitPosition, float &hitDistance, float &u, float &v, bool &inside)
	{
		if (ms_xrWorldHeadOriginSettled && !getEnvironmentFlagDefault("SWG_OG_VR_STOCK_HUD_QUAD", false))
			return false;

		if (intersectUiQuad(origin, direction, hitPosition, hitDistance, u, v, inside))
		{
			return true;
		}

		return false;
	}

	bool intersectObjectContextSurface(XrVector3fLocal const &origin, XrVector3fLocal const &direction, XrVector3fLocal &hitPosition, float &hitDistance, float &u, float &v, bool &inside)
	{
		inside = false;
		u = 0.0f;
		v = 0.0f;
		if (!hasFreshObjectContext() || !hasFreshObjectContextCapture() || !ms_xrObjectContextSwapchainWidth || !ms_xrObjectContextSwapchainHeight)
			return false;

		float widthMeters = 0.0f;
		float heightMeters = 0.0f;
		objectContextLayerSize(widthMeters, heightMeters);
		XrPosefLocal const pose = objectContextPose("AllTargets", 0.0f, 0.0f);

		XrVector3fLocal normal;
		normal.x = 0.0f;
		normal.y = 0.0f;
		normal.z = -1.0f;
		normal = rotateVector(pose.orientation, normal);
		float const denominator = dotVector(direction, normal);
		if (fabsf(denominator) < 0.0001f)
			return false;

		XrVector3fLocal const planeDelta = subtractVector(pose.position, origin);
		hitDistance = dotVector(planeDelta, normal) / denominator;
		if (hitDistance < 0.02f || hitDistance > 5.00f)
			return false;

		XrVector3fLocal const rawHitPosition = addVector(origin, scaleVector(direction, hitDistance));
		XrVector3fLocal const fromCenter = subtractVector(rawHitPosition, pose.position);
		XrVector3fLocal right;
		right.x = 1.0f;
		right.y = 0.0f;
		right.z = 0.0f;
		right = rotateVector(pose.orientation, right);
		XrVector3fLocal up;
		up.x = 0.0f;
		up.y = 1.0f;
		up.z = 0.0f;
		up = rotateVector(pose.orientation, up);

		float const localX = dotVector(fromCenter, right);
		float const localY = dotVector(fromCenter, up);
		u = 0.5f + localX / widthMeters;
		v = 0.5f - localY / heightMeters;
		inside = u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
		if (!inside)
			return false;

		hitPosition = addVector(pose.position, addVector(scaleVector(right, (u - 0.5f) * widthMeters), scaleVector(up, (0.5f - v) * heightMeters)));
		return true;
	}

	bool findUiOcclusionDistance(XrVector3fLocal const &origin, XrVector3fLocal const &direction, float &distanceMeters)
	{
		if (!getEnvironmentFlagDefault("SWG_OG_VR_UI_OCCLUDES_PHYSICS", true))
			return false;

		XrVector3fLocal hitPosition;
		float hitDistance = 0.0f;
		float u = 0.0f;
		float v = 0.0f;
		bool inside = false;
		bool hitAny = false;
		float bestDistance = 0.0f;

		if (intersectUiQuad(origin, direction, hitPosition, hitDistance, u, v, inside) && inside)
		{
			hitAny = true;
			bestDistance = hitDistance;
		}
		if (!hitAny)
			return false;

		distanceMeters = bestDistance;
		return true;
	}

	void ensureVrWindowFocus(char const *reason)
	{
		if (!ms_window || !IsWindow(ms_window) || GetForegroundWindow() == ms_window)
			return;

		SetForegroundWindow(ms_window);
		SetActiveWindow(ms_window);
		SetFocus(ms_window);

		static int s_focusLogCount = 0;
		if (s_focusLogCount < 16)
		{
			++s_focusLogCount;
			bridgeLog("{\"event\":\"openxrWindowFocus\",\"reason\":\"%s\",\"frame\":%d}", reason ? reason : "", ms_frameCount);
		}
	}

	void clientAreaSize(int &width, int &height)
	{
		width = 0;
		height = 0;
		if (ms_window && IsWindow(ms_window))
		{
			RECT rect;
			ZeroMemory(&rect, sizeof(rect));
			if (GetClientRect(ms_window, &rect))
			{
				width = rect.right - rect.left;
				height = rect.bottom - rect.top;
			}
		}

		if (width <= 0 && ms_xrQuadSwapchainWidth)
			width = static_cast<int>(ms_xrQuadSwapchainWidth);
		if (height <= 0 && ms_xrQuadSwapchainHeight)
			height = static_cast<int>(ms_xrQuadSwapchainHeight);
		if (width <= 0)
			width = 1280;
		if (height <= 0)
			height = 720;
	}

	bool worldCursorFromAim(XrPosefLocal const &pose, int &x, int &y)
	{
		int width = 0;
		int height = 0;
		clientAreaSize(width, height);

		if (!getEnvironmentEquals("SWG_OG_VR_WORLD_WAND_CURSOR_MODE", "projected") && !getEnvironmentFlagDefault("SWG_OG_VR_WORLD_WAND_PROJECTED_CURSOR", false))
		{
			float const offsetX = getEnvironmentFloat("SWG_OG_VR_WORLD_WAND_CENTER_OFFSET_X", 0.0f);
			float const offsetY = getEnvironmentFloat("SWG_OG_VR_WORLD_WAND_CENTER_OFFSET_Y", 0.0f);
			float const u = clampFloat(0.5f + offsetX, 0.0f, 1.0f);
			float const v = clampFloat(0.5f + offsetY, 0.0f, 1.0f);
			x = clampInt(static_cast<int>(u * static_cast<float>(width - 1) + 0.5f), 0, width - 1);
			y = clampInt(static_cast<int>(v * static_cast<float>(height - 1) + 0.5f), 0, height - 1);
			return true;
		}

		XrVector3fLocal const forward = quaternionForward(pose.orientation);
		float const plane = -forward.z;
		if (plane < 0.05f)
			return false;

		float const screenScale = getEnvironmentFloat("SWG_OG_VR_WORLD_WAND_SCREEN_SCALE", 0.75f);
		float u = 0.5f + (forward.x / plane) * screenScale;
		float v = 0.5f - (forward.y / plane) * screenScale;
		if (!getEnvironmentFlagDefault("SWG_OG_VR_WORLD_WAND_CLAMP", true) && (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f))
			return false;

		u = clampFloat(u, 0.0f, 1.0f);
		v = clampFloat(v, 0.0f, 1.0f);
		x = clampInt(static_cast<int>(u * static_cast<float>(width - 1) + 0.5f), 0, width - 1);
		y = clampInt(static_cast<int>(v * static_cast<float>(height - 1) + 0.5f), 0, height - 1);
		return true;
	}

	void releaseLeftMouseState(char const *reason)
	{
		Direct3d11_VrBridge::VrMouseInputFunction const mouseInput = ms_vrMouseInput;
		if (!ms_xrRightTriggerDown || (!mouseInput && !ms_vrUiInput))
			return;

		if (mouseInput)
			mouseInput(ms_xrRightTriggerClientX, ms_xrRightTriggerClientY, true, 0, false, true);
		else
			ms_vrUiInput(ms_xrRightTriggerClientX, ms_xrRightTriggerClientY, true, false, true);
		bridgeLog("{\"event\":\"openxrUiMouse\",\"route\":\"%s\",\"action\":\"up\",\"button\":\"left\",\"hand\":\"%s\",\"x\":%d,\"y\":%d}", reason ? reason : "", ms_xrUiTriggerHandIndex == 0 ? "left" : "right", ms_xrRightTriggerClientX, ms_xrRightTriggerClientY);
		ms_xrRightTriggerDown = false;
		ms_xrUiTriggerHandIndex = -1;
	}

	void submitLeftMouseState(int x, int y, bool inside, float triggerValue, int handIndex, char const *route)
	{
		Direct3d11_VrBridge::VrMouseInputFunction const mouseInput = ms_vrMouseInput;
		if (!mouseInput && !ms_vrUiInput)
			return;

		if (inside)
		{
			if (mouseInput)
				mouseInput(x, y, true, 0, false, false);
			else
				ms_vrUiInput(x, y, true, false, false);
		}

		bool const triggerPressed = triggerValue >= getEnvironmentFloat("SWG_OG_VR_TRIGGER_CLICK_THRESHOLD", 0.55f);
		if (triggerPressed && inside && !ms_xrRightTriggerDown)
		{
			ensureVrWindowFocus(route);
			ms_xrRightTriggerClientX = x;
			ms_xrRightTriggerClientY = y;
			if (mouseInput)
				mouseInput(x, y, true, 0, true, false);
			else
				ms_vrUiInput(x, y, true, true, false);
			ms_xrUiTriggerHandIndex = handIndex;
			bridgeLog("{\"event\":\"openxrUiMouse\",\"route\":\"%s\",\"action\":\"down\",\"button\":\"left\",\"hand\":\"%s\",\"x\":%d,\"y\":%d,\"trigger\":%0.3f}", route ? route : "", handIndex == 0 ? "left" : "right", x, y, triggerValue);
			ms_xrRightTriggerDown = true;
		}
		else if ((!triggerPressed || !inside) && ms_xrRightTriggerDown)
		{
			releaseLeftMouseState(route);
		}
	}

	void releaseGripMouseState(char const *reason);

	void submitRightMouseState(int x, int y, bool inside, float triggerValue, int handIndex, char const *route)
	{
		if (!ms_vrMouseInput)
			return;

		if (inside)
			ms_vrMouseInput(x, y, true, 1, false, false);

		bool const triggerPressed = triggerValue >= getEnvironmentFloat("SWG_OG_VR_TRIGGER_CLICK_THRESHOLD", 0.55f);
		if (triggerPressed && inside && !ms_xrGripMouseDown)
		{
			ensureVrWindowFocus(route);
			ms_xrGripMouseClientX = x;
			ms_xrGripMouseClientY = y;
			ms_xrGripMouseHandIndex = handIndex;
			ms_vrMouseInput(x, y, true, 1, true, false);
			ms_xrGripMouseDown = true;
			bridgeLog("{\"event\":\"openxrUiMouse\",\"route\":\"%s\",\"action\":\"down\",\"button\":\"right\",\"hand\":\"%s\",\"x\":%d,\"y\":%d,\"trigger\":%0.3f}", route ? route : "", handIndex == 0 ? "left" : "right", x, y, triggerValue);
		}
		else if ((!triggerPressed || !inside) && ms_xrGripMouseDown)
		{
			releaseGripMouseState(route);
		}
	}

	void releaseGripMouseState(char const *reason)
	{
		if (!ms_xrGripMouseDown)
			return;

		if (ms_vrMouseInput)
			ms_vrMouseInput(ms_xrGripMouseClientX, ms_xrGripMouseClientY, true, 1, false, true);
		bridgeLog("{\"event\":\"openxrUiMouse\",\"route\":\"world-wand\",\"action\":\"up\",\"button\":\"right\",\"reason\":\"%s\",\"hand\":\"%s\",\"x\":%d,\"y\":%d}", reason ? reason : "", ms_xrGripMouseHandIndex == 0 ? "left" : "right", ms_xrGripMouseClientX, ms_xrGripMouseClientY);
		ms_xrGripMouseDown = false;
		ms_xrGripMouseHandIndex = -1;
	}

	void invalidateVrSpatialAnchors(char const *reason)
	{
		bool const preserveLoadingHeadPose = reason && strcmp(reason, "tv-mode-off") == 0;
		ms_xrQuadPoseCaptured = false;
		ms_xrQuadHeadPoseCaptured = false;
		if (!preserveLoadingHeadPose)
			ms_xrLoadingHeadPoseCaptured = false;
		ms_xrHandHeadPoseCaptured = false;
		ms_xrQuadAnchorFailureLogged = false;
		ms_xrUiQuadReady = false;
		ms_xrUiQuadWidthMeters = 0.0f;
		ms_xrUiQuadHeightMeters = 0.0f;
		resetWorldHeadRecenter();
		ms_xrGripFallbackStartSeconds[0] = 0.0;
		ms_xrGripFallbackStartSeconds[1] = 0.0;
		ms_xrSuppressControllerInputFrames = 8;
		releaseLeftMouseState(reason);
		releaseWristMenuButtonState(reason);
		releaseGripMouseState(reason);
		bridgeLog("{\"event\":\"openxrSpatialAnchorsInvalidated\",\"reason\":\"%s\",\"frame\":%d,\"suppressControllerFrames\":%d,\"loadingHeadPosePreserved\":%s}", reason ? reason : "", ms_frameCount, ms_xrSuppressControllerInputFrames, preserveLoadingHeadPose && ms_xrLoadingHeadPoseCaptured ? "true" : "false");
	}

	void submitUiMouseState(float u, float v, bool inside, float leftTriggerValue, float rightTriggerValue, int pointerHandIndex)
	{
		if ((!ms_vrMouseInput && !ms_vrUiInput) || !ms_xrQuadSwapchainWidth || !ms_xrQuadSwapchainHeight)
		{
			if (!inside)
			{
				releaseLeftMouseState("hud-unavailable");
				releaseGripMouseState("hud-unavailable");
			}
			return;
		}

		int const x = clampInt(static_cast<int>(u * static_cast<float>(ms_xrQuadSwapchainWidth - 1) + 0.5f), 0, static_cast<int>(ms_xrQuadSwapchainWidth - 1));
		int const y = clampInt(static_cast<int>(v * static_cast<float>(ms_xrQuadSwapchainHeight - 1) + 0.5f), 0, static_cast<int>(ms_xrQuadSwapchainHeight - 1));
		UNREF(pointerHandIndex);
		submitLeftMouseState(x, y, inside, leftTriggerValue, 0, "og-iowin");
		submitRightMouseState(x, y, inside, rightTriggerValue, 1, "og-iowin");
	}

	bool mapObjectContextInput(float u, float v, int &clientX, int &clientY)
	{
		if (!ms_xrObjectContextSwapchainWidth || !ms_xrObjectContextSwapchainHeight)
			return false;

		int const textureX = clampInt(static_cast<int>(u * static_cast<float>(ms_xrObjectContextSwapchainWidth - 1) + 0.5f), 0, static_cast<int>(ms_xrObjectContextSwapchainWidth - 1));
		int const textureY = clampInt(static_cast<int>(v * static_cast<float>(ms_xrObjectContextSwapchainHeight - 1) + 0.5f), 0, static_cast<int>(ms_xrObjectContextSwapchainHeight - 1));
		static int s_objectContextMapLogCount = 0;
		for (int i = 0; i != static_cast<int>(sizeof(ms_objectContextInputRegions) / sizeof(ms_objectContextInputRegions[0])); ++i)
		{
			ObjectContextInputRegion const &region = ms_objectContextInputRegions[i];
			if (!region.active)
				continue;
			if (textureX < region.textureLeft || textureX >= region.textureRight || textureY < region.textureTop || textureY >= region.textureBottom)
				continue;

			int const textureWidth = region.textureRight - region.textureLeft;
			int const textureHeight = region.textureBottom - region.textureTop;
			int const clientWidth = region.clientRight - region.clientLeft;
			int const clientHeight = region.clientBottom - region.clientTop;
			if (textureWidth <= 0 || textureHeight <= 0 || clientWidth <= 0 || clientHeight <= 0)
				return false;

			float const localU = static_cast<float>(textureX - region.textureLeft) / static_cast<float>(textureWidth);
			float const localV = static_cast<float>(textureY - region.textureTop) / static_cast<float>(textureHeight);
			clientX = clampInt(region.clientLeft + static_cast<int>(localU * static_cast<float>(clientWidth) + 0.5f), region.clientLeft, region.clientRight - 1);
			clientY = clampInt(region.clientTop + static_cast<int>(localV * static_cast<float>(clientHeight) + 0.5f), region.clientTop, region.clientBottom - 1);
			if (s_objectContextMapLogCount < 80 || (ms_frameCount % 120) == 0)
			{
				++s_objectContextMapLogCount;
				bridgeLog("{\"event\":\"openxrObjectContextInputMap\",\"frame\":%d,\"slot\":%d,\"u\":%0.4f,\"v\":%0.4f,\"texture\":[%d,%d],\"textureRect\":[%d,%d,%d,%d],\"client\":[%d,%d],\"clientRect\":[%d,%d,%d,%d]}",
					ms_frameCount,
					i,
					u,
					v,
					textureX,
					textureY,
					region.textureLeft,
					region.textureTop,
					region.textureRight,
					region.textureBottom,
					clientX,
					clientY,
					region.clientLeft,
					region.clientTop,
					region.clientRight,
					region.clientBottom);
			}
			return true;
		}

		if (s_objectContextMapLogCount < 80 || (ms_frameCount % 120) == 0)
		{
			++s_objectContextMapLogCount;
			bridgeLog("{\"event\":\"openxrObjectContextInputMap\",\"frame\":%d,\"hit\":false,\"u\":%0.4f,\"v\":%0.4f,\"texture\":[%d,%d]}",
				ms_frameCount,
				u,
				v,
				textureX,
				textureY);
		}
		return false;
	}

	bool objectContextInputRegionContains(float u, float v)
	{
		if (!ms_xrObjectContextSwapchainWidth || !ms_xrObjectContextSwapchainHeight)
			return false;

		int const textureX = clampInt(static_cast<int>(u * static_cast<float>(ms_xrObjectContextSwapchainWidth - 1) + 0.5f), 0, static_cast<int>(ms_xrObjectContextSwapchainWidth - 1));
		int const textureY = clampInt(static_cast<int>(v * static_cast<float>(ms_xrObjectContextSwapchainHeight - 1) + 0.5f), 0, static_cast<int>(ms_xrObjectContextSwapchainHeight - 1));
		for (int i = 0; i != static_cast<int>(sizeof(ms_objectContextInputRegions) / sizeof(ms_objectContextInputRegions[0])); ++i)
		{
			ObjectContextInputRegion const &region = ms_objectContextInputRegions[i];
			if (!region.active)
				continue;
			if (textureX >= region.textureLeft && textureX < region.textureRight && textureY >= region.textureTop && textureY < region.textureBottom)
				return true;
		}
		return false;
	}

	void submitObjectContextMouseState(float u, float v, bool inside, float leftTriggerValue, float rightTriggerValue, int pointerHandIndex)
	{
		float const triggerThreshold = getEnvironmentFloat("SWG_OG_VR_TRIGGER_CLICK_THRESHOLD", 0.55f);
		bool const leftTriggerPressed = leftTriggerValue >= triggerThreshold;
		bool const rightTriggerPressed = rightTriggerValue >= triggerThreshold;
		if (!leftTriggerPressed)
			ms_xrObjectContextLeftClickConsumed = false;
		if (!rightTriggerPressed)
			ms_xrObjectContextRightClickConsumed = false;

		if (!inside)
		{
			releaseLeftMouseState("object-context-outside");
			releaseGripMouseState("object-context-outside");
			return;
		}

		int clientX = 0;
		int clientY = 0;
		if (!mapObjectContextInput(u, v, clientX, clientY))
		{
			releaseLeftMouseState("object-context-empty");
			releaseGripMouseState("object-context-empty");
			return;
		}

		UNREF(pointerHandIndex);
		Direct3d11_VrBridge::VrMouseInputFunction const mouseInput = ms_vrMouseInput;
		if (mouseInput)
			mouseInput(clientX, clientY, true, 0, false, false);
		else if (ms_vrUiInput)
			ms_vrUiInput(clientX, clientY, true, false, false);

		if (leftTriggerPressed && !ms_xrObjectContextLeftClickConsumed)
		{
			ensureVrWindowFocus("object-context");
			releaseLeftMouseState("object-context-pulse");
			if (mouseInput)
			{
				mouseInput(clientX, clientY, true, 0, true, false);
				mouseInput(clientX, clientY, true, 0, false, true);
			}
			else if (ms_vrUiInput)
			{
				ms_vrUiInput(clientX, clientY, true, true, false);
				ms_vrUiInput(clientX, clientY, true, false, true);
			}
			ms_xrObjectContextLeftClickConsumed = true;
			bridgeLog("{\"event\":\"openxrUiMouse\",\"route\":\"object-context\",\"action\":\"pulse\",\"button\":\"left\",\"hand\":\"%s\",\"x\":%d,\"y\":%d,\"trigger\":%0.3f}", pointerHandIndex == 0 ? "left" : "right", clientX, clientY, leftTriggerValue);
		}

		if (rightTriggerPressed && !ms_xrObjectContextRightClickConsumed && mouseInput)
		{
			ensureVrWindowFocus("object-context");
			releaseGripMouseState("object-context-pulse");
			mouseInput(clientX, clientY, true, 1, true, false);
			mouseInput(clientX, clientY, true, 1, false, true);
			ms_xrObjectContextRightClickConsumed = true;
			bridgeLog("{\"event\":\"openxrUiMouse\",\"route\":\"object-context\",\"action\":\"pulse\",\"button\":\"right\",\"hand\":\"%s\",\"x\":%d,\"y\":%d,\"trigger\":%0.3f}", pointerHandIndex == 0 ? "left" : "right", clientX, clientY, rightTriggerValue);
		}
	}

	void submitWorldMouseState(int x, int y, bool inside, float leftTriggerValue, float rightTriggerValue, float squeezeValue, int handIndex, double sampleTimeSeconds)
	{
		static int s_worldRouteLogCount = 0;
		float const triggerThreshold = getEnvironmentFloat("SWG_OG_VR_TRIGGER_CLICK_THRESHOLD", 0.55f);
		float const gripThreshold = getEnvironmentFloat("SWG_OG_VR_PHYSICS_GRIP_THRESHOLD", 0.60f);
		bool const leftTriggerPressed = leftTriggerValue >= triggerThreshold;
		bool const rightTriggerPressed = rightTriggerValue >= triggerThreshold;
		bool const gripHeld = squeezeValue >= gripThreshold;
		if ((leftTriggerPressed || rightTriggerPressed || gripHeld) && (s_worldRouteLogCount < 80 || (ms_frameCount % 120) == 0))
		{
			++s_worldRouteLogCount;
			char const * const cursorMode = getEnvironmentEquals("SWG_OG_VR_WORLD_WAND_CURSOR_MODE", "projected") || getEnvironmentFlagDefault("SWG_OG_VR_WORLD_WAND_PROJECTED_CURSOR", false) ? "projected" : "center";
			bridgeLog("{\"event\":\"openxrWorldWandRoute\",\"frame\":%d,\"pointerHand\":\"%s\",\"inside\":%s,\"cursorMode\":\"%s\",\"x\":%d,\"y\":%d,\"leftTrigger\":%0.3f,\"rightTrigger\":%0.3f,\"squeeze\":%0.3f}",
				ms_frameCount,
				handIndex == 0 ? "left" : "right",
				inside ? "true" : "false",
				cursorMode,
				x,
				y,
				leftTriggerValue,
				rightTriggerValue,
				squeezeValue);
		}

		UNREF(sampleTimeSeconds);
		if (handIndex >= 0 && handIndex < 2)
			ms_xrGripFallbackStartSeconds[handIndex] = 0.0;
		submitLeftMouseState(x, y, inside, leftTriggerValue, 0, "world-wand");
		submitRightMouseState(x, y, inside, rightTriggerValue, 1, "world-wand");
	}

	void submitControllerInput(XrTime displayTime)
	{
		if (!ms_vrControllerInput || !ms_xrControllerActionsReady)
			return;

		if (ms_xrSuppressControllerInputFrames > 0)
		{
			--ms_xrSuppressControllerInputFrames;
			submitVrControllerInput(0.0f, 0.0f, false, 0.0f, 0.0f, false, displayTime, false);
			return;
		}

		XrActionStateVector2fLocal const leftStick = getVector2ActionState(ms_xrThumbstickAction, ms_xrHandSubactionPaths[0]);
		XrActionStateVector2fLocal const rightStick = getVector2ActionState(ms_xrThumbstickAction, ms_xrHandSubactionPaths[1]);
		XrActionStateBooleanLocal const turnModeClick = getBooleanActionState(ms_xrTurnModeClickAction, ms_xrHandSubactionPaths[1]);
		bool const turnModeClickPressed = turnModeClick.isActive && turnModeClick.currentState;
		static int s_leftStickLogCount = 0;
		bool const leftStickMovedForLog = leftStick.isActive && (fabsf(leftStick.currentState.x) > 0.20f || fabsf(leftStick.currentState.y) > 0.20f);
		if (leftStickMovedForLog && (s_leftStickLogCount < 80 || (ms_frameCount % 120) == 0))
		{
			++s_leftStickLogCount;
			bridgeLog("{\"event\":\"openxrLeftStick\",\"frame\":%d,\"raw\":[%0.3f,%0.3f],\"active\":%s}", ms_frameCount, leftStick.currentState.x, leftStick.currentState.y, openXrBoolString(leftStick.isActive) ? "true" : "false");
		}

		if ((leftStick.isActive && (fabsf(leftStick.currentState.x) > 0.20f || fabsf(leftStick.currentState.y) > 0.20f)) ||
			(rightStick.isActive && (fabsf(rightStick.currentState.x) > 0.20f || fabsf(rightStick.currentState.y) > 0.20f)))
			ensureVrWindowFocus("controller-stick");
		submitVrControllerInput(
			leftStick.currentState.x,
			leftStick.currentState.y,
			openXrBoolString(leftStick.isActive),
			rightStick.currentState.x,
			rightStick.currentState.y,
			openXrBoolString(rightStick.isActive),
			displayTime,
			turnModeClickPressed);
	}

	void sendKeyboardTap(WORD virtualKey, bool shift)
	{
		INPUT inputs[4];
		ZeroMemory(inputs, sizeof(inputs));
		unsigned int count = 0;
		if (shift)
		{
			inputs[count].type = INPUT_KEYBOARD;
			inputs[count].ki.wVk = VK_SHIFT;
			++count;
		}
		inputs[count].type = INPUT_KEYBOARD;
		inputs[count].ki.wVk = virtualKey;
		++count;
		inputs[count].type = INPUT_KEYBOARD;
		inputs[count].ki.wVk = virtualKey;
		inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
		++count;
		if (shift)
		{
			inputs[count].type = INPUT_KEYBOARD;
			inputs[count].ki.wVk = VK_SHIFT;
			inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
			++count;
		}

		SendInput(count, inputs, sizeof(INPUT));
	}

	void submitTargetCycleInput()
	{
		if (!ms_xrControllerActionsReady || !getEnvironmentFlagDefault("SWG_OG_VR_TARGET_CYCLE_BUTTONS", true))
			return;

		bool nextPressed = false;
		bool previousPressed = false;
		for (int hand = 0; hand < 2; ++hand)
		{
			XrActionStateBooleanLocal const nextState = getBooleanActionState(ms_xrTargetNextClickAction, ms_xrHandSubactionPaths[hand]);
			XrActionStateBooleanLocal const previousState = getBooleanActionState(ms_xrTargetPreviousClickAction, ms_xrHandSubactionPaths[hand]);
			nextPressed = nextPressed || (nextState.isActive && nextState.currentState);
			previousPressed = previousPressed || (previousState.isActive && previousState.currentState);
		}

		if (nextPressed != ms_xrTargetNextButtonDown)
		{
			ms_xrTargetNextButtonDown = nextPressed;
			if (nextPressed)
			{
				ensureVrWindowFocus("target-cycle-next");
				sendKeyboardTap(VK_TAB, false);
				bridgeLog("{\"event\":\"openxrTargetCycle\",\"direction\":\"next\",\"frame\":%d}", ms_frameCount);
			}
		}

		if (previousPressed != ms_xrTargetPreviousButtonDown)
		{
			ms_xrTargetPreviousButtonDown = previousPressed;
			if (previousPressed)
			{
				ensureVrWindowFocus("target-cycle-previous");
				sendKeyboardTap(VK_TAB, true);
				bridgeLog("{\"event\":\"openxrTargetCycle\",\"direction\":\"previous\",\"frame\":%d}", ms_frameCount);
			}
		}
	}

	void submitMenuInput()
	{
		if (!ms_xrControllerActionsReady || !ms_xrMenuClickAction || !getEnvironmentFlagDefault("SWG_OG_VR_MENU_BUTTON", true))
			return;

		bool pressed = false;
		for (int hand = 0; hand < 2; ++hand)
		{
			XrActionStateBooleanLocal const state = getBooleanActionState(ms_xrMenuClickAction, ms_xrHandSubactionPaths[hand]);
			pressed = pressed || (state.isActive && state.currentState);
		}

		if (pressed == ms_xrMenuButtonDown)
			return;

		ms_xrMenuButtonDown = pressed;
		if (pressed)
			ensureVrWindowFocus("menu-button");

		if (getEnvironmentFlagDefault("SWG_OG_VR_WEAPON_INVENTORY_A_BUTTON", false))
		{
			if (pressed)
				SWGVRPhysics_PublishWeaponInventoryToggle();

			static int s_weaponInventoryButtonLogCount = 0;
			if (s_weaponInventoryButtonLogCount < 16)
			{
				++s_weaponInventoryButtonLogCount;
				bridgeLog("{\"event\":\"openxrWeaponInventoryButton\",\"pressed\":%s,\"frame\":%d,\"queued\":%s}",
					pressed ? "true" : "false",
					ms_frameCount,
					pressed ? "true" : "false");
			}
			return;
		}

		if (ms_vrMenuInput)
			ms_vrMenuInput(pressed);

		static int s_menuLogCount = 0;
		if (s_menuLogCount < 16)
		{
			++s_menuLogCount;
			bridgeLog("{\"event\":\"openxrMenuButton\",\"pressed\":%s,\"frame\":%d,\"callback\":%s}", pressed ? "true" : "false", ms_frameCount, ms_vrMenuInput ? "true" : "false");
		}
	}

	void submitWristMenuButtonInput(bool inside, float triggerValue, int handIndex)
	{
		bool const pressed = inside && triggerValue >= getEnvironmentFloat("SWG_OG_VR_TRIGGER_CLICK_THRESHOLD", 0.55f);
		if (pressed == ms_xrWristMenuButtonDown)
			return;

		ms_xrWristMenuButtonDown = pressed;
		if (pressed)
			ensureVrWindowFocus("wrist-menu-button");

		if (ms_vrMenuInput)
			ms_vrMenuInput(pressed);

		bridgeLog("{\"event\":\"openxrWristMenuButtonInput\",\"pressed\":%s,\"frame\":%d,\"hand\":\"%s\",\"inside\":%s,\"trigger\":%0.3f,\"callback\":%s}",
			pressed ? "true" : "false",
			ms_frameCount,
			handIndex == 0 ? "left" : "right",
			inside ? "true" : "false",
			triggerValue,
			ms_vrMenuInput ? "true" : "false");
	}

	void logControllerHandSnapshot(char const *handName, int handIndex, XrTime displayTime)
	{
		XrPath const subactionPath = ms_xrHandSubactionPaths[handIndex];
		XrActionStateFloatLocal const trigger = getFloatActionState(ms_xrTriggerValueAction, subactionPath);
		XrActionStateFloatLocal const squeeze = getFloatActionState(ms_xrSqueezeValueAction, subactionPath);
		XrActionStateVector2fLocal const thumbstick = getVector2ActionState(ms_xrThumbstickAction, subactionPath);
		XrActionStatePoseLocal const aimPose = getPoseActionState(ms_xrAimPoseAction, subactionPath);
		XrActionStatePoseLocal const gripPose = getPoseActionState(ms_xrGripPoseAction, subactionPath);
		XrSpaceLocationLocal const aimLocation = locateActionSpace(ms_xrAimSpaces[handIndex], displayTime);
		XrSpaceLocationLocal const gripLocation = locateActionSpace(ms_xrGripSpaces[handIndex], displayTime);
		bool const aimPositionValid = (aimLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL) != 0;
		bool const aimOrientationValid = (aimLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
		bool const aimPositionTracked = (aimLocation.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT_LOCAL) != 0;
		bool const aimOrientationTracked = (aimLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT_LOCAL) != 0;
		bool const gripPositionValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL) != 0;
		bool const gripOrientationValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
		bool const gripPositionTracked = (gripLocation.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT_LOCAL) != 0;
		bool const gripOrientationTracked = (gripLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT_LOCAL) != 0;

		bridgeLog("{\"event\":\"openxrControllerSnapshot\",\"frame\":%d,\"sample\":%d,\"hand\":\"%s\",\"trigger\":%0.3f,\"triggerActive\":%s,\"squeeze\":%0.3f,\"squeezeActive\":%s,\"stick\":[%0.3f,%0.3f],\"stickActive\":%s,\"aimActive\":%s,\"aimPositionValid\":%s,\"aimOrientationValid\":%s,\"aimPositionTracked\":%s,\"aimOrientationTracked\":%s,\"aimPosition\":[%0.3f,%0.3f,%0.3f],\"aimOrientation\":[%0.3f,%0.3f,%0.3f,%0.3f],\"gripActive\":%s,\"gripPositionValid\":%s,\"gripOrientationValid\":%s,\"gripPositionTracked\":%s,\"gripOrientationTracked\":%s,\"gripPosition\":[%0.3f,%0.3f,%0.3f]}",
			ms_frameCount,
			ms_xrControllerSnapshotCount,
			handName,
			trigger.currentState,
			openXrBoolString(trigger.isActive) ? "true" : "false",
			squeeze.currentState,
			openXrBoolString(squeeze.isActive) ? "true" : "false",
			thumbstick.currentState.x,
			thumbstick.currentState.y,
			openXrBoolString(thumbstick.isActive) ? "true" : "false",
			openXrBoolString(aimPose.isActive) ? "true" : "false",
			aimPositionValid ? "true" : "false",
			aimOrientationValid ? "true" : "false",
			aimPositionTracked ? "true" : "false",
			aimOrientationTracked ? "true" : "false",
			aimLocation.pose.position.x,
			aimLocation.pose.position.y,
			aimLocation.pose.position.z,
			aimLocation.pose.orientation.x,
			aimLocation.pose.orientation.y,
			aimLocation.pose.orientation.z,
			aimLocation.pose.orientation.w,
			openXrBoolString(gripPose.isActive) ? "true" : "false",
			gripPositionValid ? "true" : "false",
			gripOrientationValid ? "true" : "false",
			gripPositionTracked ? "true" : "false",
			gripOrientationTracked ? "true" : "false",
			gripLocation.pose.position.x,
			gripLocation.pose.position.y,
			gripLocation.pose.position.z);
	}

	char const *openXrPathToString(XrPath path, char *buffer, size_t bufferSize)
	{
		if (!buffer || bufferSize == 0)
			return "";

		buffer[0] = '\0';
		if (path == XR_NULL_PATH_LOCAL)
		{
			strncpy(buffer, "<none>", bufferSize - 1);
			buffer[bufferSize - 1] = '\0';
			return buffer;
		}

		if (ms_xrPathToString && ms_xrInstance)
		{
			unsigned int required = 0;
			XrResult const result = ms_xrPathToString(ms_xrInstance, path, static_cast<unsigned int>(bufferSize), &required, buffer);
			if (result == XR_SUCCESS_LOCAL && buffer[0])
			{
				buffer[bufferSize - 1] = '\0';
				return buffer;
			}

			_snprintf(buffer, bufferSize - 1, "<path:%llu result:%d required:%u>", path, result, required);
			buffer[bufferSize - 1] = '\0';
			return buffer;
		}

		_snprintf(buffer, bufferSize - 1, "<path:%llu>", path);
		buffer[bufferSize - 1] = '\0';
		return buffer;
	}

	void logCurrentInteractionProfiles(bool force)
	{
		if (!ms_xrGetCurrentInteractionProfile || !ms_xrSession)
		{
			if (!ms_xrInteractionProfileQueryMissingLogged)
			{
				bridgeLog("{\"event\":\"openxrInteractionProfile\",\"ready\":false,\"failure\":\"function-missing\"}");
				ms_xrInteractionProfileQueryMissingLogged = true;
			}
			return;
		}

		DWORD const now = GetTickCount();
		bool const timed = ms_xrLastInteractionProfileLogMilliseconds == 0 || now - ms_xrLastInteractionProfileLogMilliseconds >= 5000;
		if (!force && !timed)
			return;

		bool logged = false;
		for (int hand = 0; hand != 2; ++hand)
		{
			XrInteractionProfileStateLocal state;
			ZeroMemory(&state, sizeof(state));
			state.type = XR_TYPE_INTERACTION_PROFILE_STATE_LOCAL;
			XrResult const result = ms_xrGetCurrentInteractionProfile(ms_xrSession, ms_xrHandSubactionPaths[hand], &state);
			bool const changed = state.interactionProfile != ms_xrLastInteractionProfiles[hand];
			if (!force && !timed && !changed)
				continue;

			char profile[256];
			openXrPathToString(state.interactionProfile, profile, sizeof(profile));
			bridgeLog("{\"event\":\"openxrInteractionProfile\",\"ready\":%s,\"hand\":\"%s\",\"result\":%d,\"profile\":\"%s\",\"profilePath\":%llu,\"changed\":%s}",
				(result == XR_SUCCESS_LOCAL) ? "true" : "false",
				hand == 0 ? "left" : "right",
				result,
				profile,
				state.interactionProfile,
				changed ? "true" : "false");
			ms_xrLastInteractionProfiles[hand] = state.interactionProfile;
			logged = true;
		}

		if (logged)
			ms_xrLastInteractionProfileLogMilliseconds = now;
	}

	bool isHandJointPoseValid(XrHandJointLocationEXTLocal const &joint)
	{
		return (joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL) != 0 &&
			(joint.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
	}

	bool isHandJointPoseTracked(XrHandJointLocationEXTLocal const &joint)
	{
		return (joint.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT_LOCAL) != 0 &&
			(joint.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT_LOCAL) != 0;
	}

	float vectorDistance(XrVector3fLocal const &lhs, XrVector3fLocal const &rhs)
	{
		XrVector3fLocal const delta = subtractVector(lhs, rhs);
		return sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
	}

	XrQuaternionfLocal handTrackingOrientationFromJoints(
		XrHandJointLocationEXTLocal const &palm,
		XrHandJointLocationEXTLocal const &indexProximal,
		XrHandJointLocationEXTLocal const &indexTip)
	{
		XrVector3fLocal forward = subtractVector(indexTip.pose.position, indexProximal.pose.position);
		if (!normalizeVector(forward))
			forward = subtractVector(indexTip.pose.position, palm.pose.position);
		if (!normalizeVector(forward))
			forward = quaternionForward(palm.pose.orientation);
		if (!normalizeVector(forward))
			forward = XrVector3fLocal{0.0f, 0.0f, -1.0f};

		XrVector3fLocal zAxis = scaleVector(forward, -1.0f);
		XrVector3fLocal palmUp{0.0f, 1.0f, 0.0f};
		palmUp = rotateVector(palm.pose.orientation, palmUp);
		XrVector3fLocal yAxis = subtractVector(palmUp, scaleVector(zAxis, dotVector(palmUp, zAxis)));
		if (!normalizeVector(yAxis))
		{
			yAxis = XrVector3fLocal{0.0f, 1.0f, 0.0f};
			yAxis = subtractVector(yAxis, scaleVector(zAxis, dotVector(yAxis, zAxis)));
			normalizeVector(yAxis);
		}

		XrVector3fLocal xAxis = crossVector(yAxis, zAxis);
		if (!normalizeVector(xAxis))
		{
			xAxis = rightFromForward(forward);
			normalizeVector(xAxis);
		}
		yAxis = crossVector(zAxis, xAxis);
		normalizeVector(yAxis);
		return normalizedQuaternion(quaternionFromBasis(xAxis, yAxis, zAxis));
	}

	bool publishHandTrackingHandState(int handIndex, XrTime displayTime)
	{
		if (handIndex < 0 || handIndex >= 2 || !ms_xrHandTrackingReady || !ms_xrLocateHandJointsEXT || !ms_xrHandTrackers[handIndex] || !ms_xrLocalSpace)
			return false;

		XrHandJointLocationEXTLocal joints[XR_HAND_JOINT_COUNT_EXT_LOCAL];
		ZeroMemory(joints, sizeof(joints));
		XrHandJointLocationsEXTLocal locations;
		ZeroMemory(&locations, sizeof(locations));
		locations.type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT_LOCAL;
		locations.jointCount = XR_HAND_JOINT_COUNT_EXT_LOCAL;
		locations.jointLocations = joints;

		XrHandJointsLocateInfoEXTLocal locateInfo;
		ZeroMemory(&locateInfo, sizeof(locateInfo));
		locateInfo.type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT_LOCAL;
		locateInfo.baseSpace = ms_xrLocalSpace;
		locateInfo.time = displayTime;

		XrResult const locateResult = ms_xrLocateHandJointsEXT(ms_xrHandTrackers[handIndex], &locateInfo, &locations);
		++ms_xrHandTrackingSnapshotCount;
		if (locateResult != XR_SUCCESS_LOCAL)
		{
			if (ms_xrHandTrackingSnapshotCount <= 12 || (ms_frameCount % 120) == 0)
				bridgeLog("{\"event\":\"openxrHandTrackingSnapshot\",\"ready\":false,\"hand\":\"%s\",\"failure\":\"locate\",\"result\":%d}", handIndex == 0 ? "left" : "right", locateResult);
			return false;
		}
		if (!locations.isActive)
		{
			if (ms_xrHandTrackingSnapshotCount <= 12 || (ms_frameCount % 120) == 0)
				bridgeLog("{\"event\":\"openxrHandTrackingSnapshot\",\"ready\":false,\"hand\":\"%s\",\"failure\":\"inactive\"}", handIndex == 0 ? "left" : "right");
			return false;
		}

		XrHandJointLocationEXTLocal const &palm = joints[XR_HAND_JOINT_PALM_EXT_LOCAL];
		XrHandJointLocationEXTLocal const &wrist = joints[XR_HAND_JOINT_WRIST_EXT_LOCAL];
		XrHandJointLocationEXTLocal const &indexProximal = joints[XR_HAND_JOINT_INDEX_PROXIMAL_EXT_LOCAL];
		XrHandJointLocationEXTLocal const &indexTip = joints[XR_HAND_JOINT_INDEX_TIP_EXT_LOCAL];
		if (!isHandJointPoseValid(palm) || !isHandJointPoseValid(wrist) || !isHandJointPoseValid(indexProximal) || !isHandJointPoseValid(indexTip))
		{
			if (ms_xrHandTrackingSnapshotCount <= 12 || (ms_frameCount % 120) == 0)
				bridgeLog("{\"event\":\"openxrHandTrackingSnapshot\",\"ready\":false,\"hand\":\"%s\",\"failure\":\"missing-required-joints\",\"palmFlags\":%llu,\"wristFlags\":%llu,\"indexProximalFlags\":%llu,\"indexTipFlags\":%llu}",
					handIndex == 0 ? "left" : "right",
					palm.locationFlags,
					wrist.locationFlags,
					indexProximal.locationFlags,
					indexTip.locationFlags);
			return false;
		}

		XrQuaternionfLocal const handOrientation = handTrackingOrientationFromJoints(palm, indexProximal, indexTip);
		XrPosefLocal aimPose = identityPose();
		aimPose.position = palm.pose.position;
		aimPose.orientation = handOrientation;
		XrPosefLocal gripPose = aimPose;

		bool const handsHeadRelative = getEnvironmentFlagDefault("SWG_OG_VR_HANDS_HEAD_RELATIVE", false);
		bool const worldSpaceReady = ms_xrWorldHeadOriginSettled && ms_xrWorldHeadOriginCaptured && ms_xrWorldYawCaptured;
		bool const menuSpaceReady = ms_vrTvModeEnabled && !worldSpaceReady && menuHandSpaceReady();
		if (!handsHeadRelative && !worldSpaceReady && !menuSpaceReady)
			return false;
		if (handsHeadRelative && !ms_xrHandHeadPoseCaptured)
			return false;

		float uiOcclusionDistance = 0.0f;
		if (findUiOcclusionDistance(aimPose.position, quaternionForward(aimPose.orientation), uiOcclusionDistance))
		{
			(void)uiOcclusionDistance;
		}

		XrPosefLocal const menuHeadPose = menuSpaceReady ? menuHandHeadPose() : identityPose();
		XrPosefLocal const aimPoseLocal = worldSpaceReady ? recenteredWorldPose(aimPose) : (menuSpaceReady ? poseRelativeToHead(aimPose, menuHeadPose) : recenteredWorldPose(aimPose));
		XrPosefLocal const gripPoseLocal = worldSpaceReady ? recenteredWorldPose(gripPose) : (menuSpaceReady ? poseRelativeToHead(gripPose, menuHeadPose) : recenteredWorldPose(gripPose));
		bool const publishHeadRelative = handsHeadRelative && ms_xrHandHeadPoseCaptured;
		XrPosefLocal const aimPoseForClient = publishHeadRelative ? poseRelativeToHead(aimPoseLocal, ms_xrHandHeadPose) : aimPoseLocal;
		XrPosefLocal const gripPoseForClient = handIkPose(publishHeadRelative ? poseRelativeToHead(gripPoseLocal, ms_xrHandHeadPose) : gripPoseLocal, handIndex);

		double const sampleTimeSeconds = static_cast<double>(displayTime) * 0.000000001;
		float const indexPalmDistance = vectorDistance(indexTip.pose.position, palm.pose.position);
		float const gripValue = clampFloat((0.115f - indexPalmDistance) / 0.055f, 0.0f, 1.0f);
		SwgVrPhysics::HandState state{};
		state.hand = static_cast<uint32_t>(handIndex);
		state.flags = SwgVrPhysics::MatrixFlagValid | SwgVrPhysics::MatrixFlagAimValid | SwgVrPhysics::MatrixFlagGripValid;
		if (isHandJointPoseTracked(palm) && isHandJointPoseTracked(indexTip))
			state.flags |= SwgVrPhysics::MatrixFlagTracked | SwgVrPhysics::MatrixFlagAimTracked | SwgVrPhysics::MatrixFlagGripTracked;
		if (gripValue >= getEnvironmentFloat("SWG_OG_VR_PHYSICS_GRIP_THRESHOLD", 0.60f))
			state.flags |= SwgVrPhysics::MatrixFlagGripHeld;
		if (ms_xrMenuButtonDown)
			state.flags |= SwgVrPhysics::MatrixFlagMenuHeld;
		if (uiOcclusionDistance > 0.0f)
		{
			state.flags |= SwgVrPhysics::MatrixFlagUiOccluded;
			state.uiOcclusionDistanceMeters = uiOcclusionDistance;
		}
		state.sampleTimeSeconds = sampleTimeSeconds;
		state.aimFromWorld = matrixFromPose(aimPoseForClient);
		state.gripFromWorld = matrixFromPose(gripPoseForClient);
		state.angularVelocityRadiansPerSecond = angularVelocityFromPose(handIndex, gripPoseLocal, sampleTimeSeconds);
		state.linearVelocityMetersPerSecond = velocityFromPose(handIndex, gripPoseLocal, sampleTimeSeconds);
		state.gripValue = gripValue;
		state.triggerValue = 0.0f;

		static int s_handTrackingPoseSpaceLogCount[2] = {0, 0};
		if (s_handTrackingPoseSpaceLogCount[handIndex] < 12 || (ms_frameCount % 120) == 0)
		{
			++s_handTrackingPoseSpaceLogCount[handIndex];
			bridgeLog("{\"event\":\"openxrHandPoseSpace\",\"source\":\"hand_tracking\",\"frame\":%d,\"hand\":\"%s\",\"space\":\"%s\",\"headRelative\":%s,\"worldSpaceReady\":%s,\"menuSpaceReady\":%s,\"rawPalm\":[%0.3f,%0.3f,%0.3f],\"rawIndexTip\":[%0.3f,%0.3f,%0.3f],\"clientGrip\":[%0.3f,%0.3f,%0.3f],\"grip\":%0.3f}",
				ms_frameCount,
				handIndex == 0 ? "left" : "right",
				referenceSpaceName(ms_xrReferenceSpaceType),
				publishHeadRelative ? "true" : "false",
				worldSpaceReady ? "true" : "false",
				menuSpaceReady ? "true" : "false",
				palm.pose.position.x,
				palm.pose.position.y,
				palm.pose.position.z,
				indexTip.pose.position.x,
				indexTip.pose.position.y,
				indexTip.pose.position.z,
				gripPoseForClient.position.x,
				gripPoseForClient.position.y,
				gripPoseForClient.position.z,
				gripValue);
		}

		if (ms_vrPhysicsManager)
			ms_vrPhysicsManager->recordControllerSample(state.hand, state.gripFromWorld, sampleTimeSeconds, (state.flags & SwgVrPhysics::MatrixFlagGripHeld) != 0);
		SWGVRPhysics_PublishHandState(&state);
		return true;
	}

	void publishVrPhysicsHandState(int handIndex, XrTime displayTime)
	{
		if (handIndex < 0 || handIndex >= 2 || !ms_xrAimSpaces[handIndex] || !ms_xrGripSpaces[handIndex])
			return;

		XrPath const subactionPath = ms_xrHandSubactionPaths[handIndex];
		XrActionStateFloatLocal const trigger = getFloatActionState(ms_xrTriggerValueAction, subactionPath);
		XrActionStateFloatLocal const squeeze = getFloatActionState(ms_xrSqueezeValueAction, subactionPath);
		XrActionStatePoseLocal const aimPose = getPoseActionState(ms_xrAimPoseAction, subactionPath);
		XrActionStatePoseLocal const gripPose = getPoseActionState(ms_xrGripPoseAction, subactionPath);
		XrSpaceLocationLocal const aimLocation = locateActionSpace(ms_xrAimSpaces[handIndex], displayTime);
		XrSpaceLocationLocal const gripLocation = locateActionSpace(ms_xrGripSpaces[handIndex], displayTime);
		bool const aimPositionValid = (aimLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL) != 0;
		bool const aimOrientationValid = (aimLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
		bool const aimPositionTracked = (aimLocation.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT_LOCAL) != 0;
		bool const aimOrientationTracked = (aimLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT_LOCAL) != 0;
		bool const gripPositionValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL) != 0;
		bool const gripOrientationValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
		bool const gripPositionTracked = (gripLocation.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT_LOCAL) != 0;
		bool const gripOrientationTracked = (gripLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT_LOCAL) != 0;
		bool const aimValid = aimPose.isActive && aimPositionValid && aimOrientationValid;
		bool const gripValid = gripPose.isActive && gripPositionValid && gripOrientationValid;
		if (!aimValid && !gripValid)
		{
			if (publishHandTrackingHandState(handIndex, displayTime))
				return;
			return;
		}

		double const sampleTimeSeconds = static_cast<double>(displayTime) * 0.000000001;
		SwgVrPhysics::HandState state{};
		state.hand = static_cast<uint32_t>(handIndex);
		state.flags = SwgVrPhysics::MatrixFlagValid;
		if (aimValid)
			state.flags |= SwgVrPhysics::MatrixFlagAimValid;
		if (gripValid)
			state.flags |= SwgVrPhysics::MatrixFlagGripValid;
		bool const aimTracked = aimValid && aimPositionTracked && aimOrientationTracked;
		bool const gripTracked = gripValid && gripPositionTracked && gripOrientationTracked;
		if (aimTracked || gripTracked)
			state.flags |= SwgVrPhysics::MatrixFlagTracked;
		if (aimTracked)
			state.flags |= SwgVrPhysics::MatrixFlagAimTracked;
		if (gripTracked)
			state.flags |= SwgVrPhysics::MatrixFlagGripTracked;
		if (squeeze.isActive && squeeze.currentState >= getEnvironmentFloat("SWG_OG_VR_PHYSICS_GRIP_THRESHOLD", 0.60f))
			state.flags |= SwgVrPhysics::MatrixFlagGripHeld;
		if (ms_xrMenuButtonDown)
			state.flags |= SwgVrPhysics::MatrixFlagMenuHeld;
		bool const handsHeadRelative = getEnvironmentFlagDefault("SWG_OG_VR_HANDS_HEAD_RELATIVE", false);
		bool const worldSpaceReady = ms_xrWorldHeadOriginSettled && ms_xrWorldHeadOriginCaptured && ms_xrWorldYawCaptured;
		bool const menuSpaceReady = ms_vrTvModeEnabled && !worldSpaceReady && menuHandSpaceReady();
		if (!handsHeadRelative && !worldSpaceReady && !menuSpaceReady)
		{
			static int s_missingHandWorldSpaceLogCount = 0;
			if (s_missingHandWorldSpaceLogCount < 8 || (ms_frameCount % 120) == 0)
			{
				++s_missingHandWorldSpaceLogCount;
				bridgeLog("{\"event\":\"openxrHandPoseSpace\",\"frame\":%d,\"hand\":\"%s\",\"publish\":false,\"reason\":\"missing-world-or-menu-recenter\"}",
					ms_frameCount,
					handIndex == 0 ? "left" : "right");
			}
			return;
		}
		if (handsHeadRelative && !ms_xrHandHeadPoseCaptured)
		{
			static int s_missingHandHeadPoseLogCount = 0;
			if (s_missingHandHeadPoseLogCount < 8 || (ms_frameCount % 120) == 0)
			{
				++s_missingHandHeadPoseLogCount;
				bridgeLog("{\"event\":\"openxrHandPoseSpace\",\"frame\":%d,\"hand\":\"%s\",\"publish\":false,\"reason\":\"missing-head-pose\"}",
					ms_frameCount,
					handIndex == 0 ? "left" : "right");
			}
			return;
		}
		float uiOcclusionDistance = 0.0f;
		bool const recentObjectContextPointer =
			handIndex >= 0 &&
			handIndex < 2 &&
			ms_xrObjectContextPointerInside[handIndex] &&
			(ms_frameCount - ms_xrObjectContextPointerFrame[handIndex]) <= 2;
		bool const uiOccluded =
			recentObjectContextPointer ||
			(aimValid && findUiOcclusionDistance(aimLocation.pose.position, quaternionForward(aimLocation.pose.orientation), uiOcclusionDistance));
		if (uiOccluded)
		{
			state.flags |= SwgVrPhysics::MatrixFlagUiOccluded;
			state.uiOcclusionDistanceMeters = recentObjectContextPointer && uiOcclusionDistance <= 0.0f ? 0.05f : uiOcclusionDistance;
		}
		if (!uiOccluded && trigger.isActive && trigger.currentState >= getEnvironmentFloat("SWG_OG_VR_TRIGGER_CLICK_THRESHOLD", 0.55f))
			state.flags |= SwgVrPhysics::MatrixFlagTriggerHeld;

		XrPosefLocal const menuHeadPose = menuSpaceReady ? menuHandHeadPose() : identityPose();
		XrPosefLocal const aimPoseLocal = worldSpaceReady ? recenteredWorldPose(aimLocation.pose) : (menuSpaceReady ? poseRelativeToHead(aimLocation.pose, menuHeadPose) : recenteredWorldPose(aimLocation.pose));
		XrPosefLocal const gripPoseLocal = worldSpaceReady ? recenteredWorldPose(gripLocation.pose) : (menuSpaceReady ? poseRelativeToHead(gripLocation.pose, menuHeadPose) : recenteredWorldPose(gripLocation.pose));
		bool const publishHeadRelative = handsHeadRelative && ms_xrHandHeadPoseCaptured;
		XrPosefLocal const aimPoseForClient = aimValid && publishHeadRelative ? poseRelativeToHead(aimPoseLocal, ms_xrHandHeadPose) : aimPoseLocal;
		XrPosefLocal const gripPoseForClient = handIkPose(gripValid && publishHeadRelative ? poseRelativeToHead(gripPoseLocal, ms_xrHandHeadPose) : gripPoseLocal, handIndex);
		state.sampleTimeSeconds = sampleTimeSeconds;
		state.aimFromWorld = aimValid ? matrixFromPose(aimPoseForClient) : SwgVrPhysics::identityMatrix();
		state.gripFromWorld = gripValid ? matrixFromPose(gripPoseForClient) : SwgVrPhysics::identityMatrix();
		state.angularVelocityRadiansPerSecond = gripValid ? angularVelocityFromPose(handIndex, gripPoseLocal, sampleTimeSeconds) : SwgVrPhysics::Vector3{0.0f, 0.0f, 0.0f};
		state.linearVelocityMetersPerSecond = velocityFromPose(handIndex, gripValid ? gripPoseLocal : aimPoseLocal, sampleTimeSeconds);
		state.gripValue = squeeze.currentState;
		state.triggerValue = !uiOccluded && trigger.isActive ? trigger.currentState : 0.0f;
		static int s_handPoseSpaceLogCount[2] = {0, 0};
		if (gripValid && (s_handPoseSpaceLogCount[handIndex] < 12 || (ms_frameCount % 120) == 0))
		{
			++s_handPoseSpaceLogCount[handIndex];
			bridgeLog("{\"event\":\"openxrHandPoseSpace\",\"frame\":%d,\"hand\":\"%s\",\"space\":\"%s\",\"headRelative\":%s,\"worldSpaceReady\":%s,\"menuSpaceReady\":%s,\"rawGrip\":[%0.3f,%0.3f,%0.3f],\"recenteredGrip\":[%0.3f,%0.3f,%0.3f],\"head\":[%0.3f,%0.3f,%0.3f],\"clientGrip\":[%0.3f,%0.3f,%0.3f]}",
				ms_frameCount,
				handIndex == 0 ? "left" : "right",
				referenceSpaceName(ms_xrReferenceSpaceType),
				publishHeadRelative ? "true" : "false",
				worldSpaceReady ? "true" : "false",
				menuSpaceReady ? "true" : "false",
				gripLocation.pose.position.x,
				gripLocation.pose.position.y,
				gripLocation.pose.position.z,
				gripPoseLocal.position.x,
				gripPoseLocal.position.y,
				gripPoseLocal.position.z,
				ms_xrHandHeadPoseCaptured ? ms_xrHandHeadPose.position.x : 0.0f,
				ms_xrHandHeadPoseCaptured ? ms_xrHandHeadPose.position.y : 0.0f,
				ms_xrHandHeadPoseCaptured ? ms_xrHandHeadPose.position.z : 0.0f,
				gripPoseForClient.position.x,
				gripPoseForClient.position.y,
				gripPoseForClient.position.z);
		}
		if (ms_vrPhysicsManager && gripValid)
			ms_vrPhysicsManager->recordControllerSample(state.hand, state.gripFromWorld, sampleTimeSeconds, (state.flags & SwgVrPhysics::MatrixFlagGripHeld) != 0);
		SWGVRPhysics_PublishHandState(&state);
	}

	void sampleControllerSnapshot(XrTime displayTime)
	{
		if (!ms_xrControllerActionsReady || !ms_xrSyncActions || !ms_xrSession)
			return;

		XrActiveActionSetLocal activeActionSet;
		ZeroMemory(&activeActionSet, sizeof(activeActionSet));
		activeActionSet.actionSet = ms_xrControllerActionSet;
		activeActionSet.subactionPath = XR_NULL_PATH_LOCAL;

		XrActionsSyncInfoLocal syncInfo;
		ZeroMemory(&syncInfo, sizeof(syncInfo));
		syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO_LOCAL;
		syncInfo.countActiveActionSets = 1;
		syncInfo.activeActionSets = &activeActionSet;
		XrResult const syncResult = ms_xrSyncActions(ms_xrSession, &syncInfo);
		if (syncResult != XR_SUCCESS_LOCAL)
		{
			releaseMenuButtonState("sync-failed");
			releaseLeftMouseState("sync-failed");
			releaseGripMouseState("sync-failed");
			if (ms_xrControllerSnapshotCount == 0)
				bridgeLog("{\"event\":\"openxrControllerSnapshot\",\"ready\":false,\"failure\":\"syncActions\",\"result\":%d}", syncResult);
			return;
		}

		++ms_xrControllerSnapshotCount;
		logCurrentInteractionProfiles(ms_xrControllerSnapshotCount == 1);
		submitMenuInput();
		submitTargetCycleInput();
		submitControllerInput(displayTime);
		publishVrPhysicsHandState(0, displayTime);
		publishVrPhysicsHandState(1, displayTime);
		pumpVrPhysicsReleases();

		int const interval = controllerSnapshotLogInterval();
		if (ms_xrControllerSnapshotCount != 1 && (interval == 0 || (ms_xrControllerSnapshotCount % interval) != 0))
			return;

		logControllerHandSnapshot("left", 0, displayTime);
		logControllerHandSnapshot("right", 1, displayTime);
	}

	bool choosePointerHand(XrTime displayTime, int &handIndex, XrSpaceLocationLocal &aimLocation, XrActionStateFloatLocal &squeeze, XrActionStateFloatLocal &trigger)
	{
		handIndex = -1;
		float const squeezeThreshold = getEnvironmentFloat("SWG_OG_VR_GRIP_POINTER_THRESHOLD", 0.35f);
		int const targetHand = getEnvironmentEquals("SWG_OG_VR_WORLD_WAND_TARGET_HAND", "left") ? 0 : (getEnvironmentEquals("SWG_OG_VR_WORLD_WAND_TARGET_HAND", "right") ? 1 : -1);
		float bestScore = -1.0f;
		for (int hand = 0; hand < 2; ++hand)
		{
			if (targetHand >= 0 && hand != targetHand)
				continue;

			if (!ms_xrAimSpaces[hand])
				continue;

			XrSpaceLocationLocal const candidateAimLocation = locateActionSpace(ms_xrAimSpaces[hand], displayTime);
			bool const positionValid = (candidateAimLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL) != 0;
			bool const orientationValid = (candidateAimLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
			if (!positionValid || !orientationValid)
				continue;

			XrActionStateFloatLocal const candidateSqueeze = getFloatActionState(ms_xrSqueezeValueAction, ms_xrHandSubactionPaths[hand]);
			XrActionStateFloatLocal const candidateTrigger = getFloatActionState(ms_xrTriggerValueAction, ms_xrHandSubactionPaths[hand]);
			float const squeezeValue = candidateSqueeze.isActive ? candidateSqueeze.currentState : 0.0f;
			bool const gripPointer = squeezeValue >= squeezeThreshold;
			float const score = gripPointer ? squeezeValue : 0.0f;
			if (gripPointer && score > bestScore)
			{
				bestScore = score;
				handIndex = hand;
				aimLocation = candidateAimLocation;
				squeeze = candidateSqueeze;
				trigger = candidateTrigger;
			}
		}

		return handIndex >= 0;
	}

	bool preparePointerLayers(XrTime displayTime, XrCompositionLayerQuadLocal *wandLayers, unsigned int maxWandLayers, unsigned int &wandLayerCount)
	{
		wandLayerCount = 0;
		if (!ms_xrControllerActionsReady || !wandLayers || maxWandLayers == 0)
			return false;

		int handIndex = -1;
		XrSpaceLocationLocal aimLocation;
		ZeroMemory(&aimLocation, sizeof(aimLocation));
		XrActionStateFloatLocal squeeze;
		ZeroMemory(&squeeze, sizeof(squeeze));
		XrActionStateFloatLocal trigger;
		ZeroMemory(&trigger, sizeof(trigger));
		if (!choosePointerHand(displayTime, handIndex, aimLocation, squeeze, trigger))
		{
			submitUiMouseState(0.0f, 0.0f, false, 0.0f, 0.0f, handIndex);
			releaseWristMenuButtonState("no-pointer-hand");
			releaseGripMouseState("no-pointer-hand");
			return false;
		}

		XrActionStateFloatLocal const leftTrigger = getFloatActionState(ms_xrTriggerValueAction, ms_xrHandSubactionPaths[0]);
		XrActionStateFloatLocal const rightTrigger = getFloatActionState(ms_xrTriggerValueAction, ms_xrHandSubactionPaths[1]);
		float const physicalLeftTriggerValue = leftTrigger.isActive ? leftTrigger.currentState : 0.0f;
		float const physicalRightTriggerValue = rightTrigger.isActive ? rightTrigger.currentState : 0.0f;
		bool const swapMouseTriggers = getEnvironmentFlagDefault("SWG_OG_VR_SWAP_MOUSE_TRIGGERS", true);
		float const leftTriggerValue = swapMouseTriggers ? physicalRightTriggerValue : physicalLeftTriggerValue;
		float const rightTriggerValue = swapMouseTriggers ? physicalLeftTriggerValue : physicalRightTriggerValue;
		float const objectContextTriggerThreshold = getEnvironmentFloat("SWG_OG_VR_TRIGGER_CLICK_THRESHOLD", 0.55f);
		if (leftTriggerValue < objectContextTriggerThreshold)
			ms_xrObjectContextLeftClickConsumed = false;
		if (rightTriggerValue < objectContextTriggerThreshold)
			ms_xrObjectContextRightClickConsumed = false;
		float const gripPointerThreshold = getEnvironmentFloat("SWG_OG_VR_GRIP_POINTER_THRESHOLD", 0.35f);
		bool const objectContextBlocksWorld = getEnvironmentFlagDefault("SWG_OG_VR_OBJECT_CONTEXT_POINTER_BLOCKS_WORLD", false);
		bool const objectContextWantsInput = objectContextBlocksWorld || ((squeeze.isActive && squeeze.currentState >= gripPointerThreshold) && hasActiveObjectContextInputRegions());

		XrPosefLocal const pointerPose = aimRayOriginPose(aimLocation.pose);
		XrVector3fLocal const forward = quaternionForward(pointerPose.orientation);
		XrVector3fLocal hitPosition;
		float hitDistance = 0.0f;
		float hitU = 0.0f;
		float hitV = 0.0f;
		bool hitInside = false;
		bool hitWristMenuButton = false;
		bool wristInside = false;
		bool hitObjectContextPanel = false;
		bool objectContextInside = false;
		float objectContextU = 0.0f;
		float objectContextV = 0.0f;
		XrVector3fLocal wristHitPosition;
		float wristHitDistance = 0.0f;
		hitWristMenuButton = intersectWristMenuButton(displayTime, pointerPose.position, forward, wristHitPosition, wristHitDistance, wristInside);
		bool hitQuad = false;
		if (hitWristMenuButton && wristInside)
		{
			hitQuad = true;
			hitInside = true;
			hitPosition = wristHitPosition;
			hitDistance = wristHitDistance;
			hitU = 0.5f;
			hitV = 0.5f;
			submitWristMenuButtonInput(true, trigger.currentState, handIndex);
			submitUiMouseState(0.0f, 0.0f, false, 0.0f, 0.0f, handIndex);
			releaseGripMouseState("wrist-menu-button");
		}
		else
		{
			submitWristMenuButtonInput(false, trigger.currentState, handIndex);
			hitObjectContextPanel = objectContextWantsInput && intersectObjectContextSurface(pointerPose.position, forward, hitPosition, hitDistance, objectContextU, objectContextV, objectContextInside);
			if (hitObjectContextPanel && objectContextInside && !objectContextBlocksWorld && !objectContextInputRegionContains(objectContextU, objectContextV))
			{
				hitObjectContextPanel = false;
				objectContextInside = false;
			}
			if (!hitObjectContextPanel || !objectContextInside)
				hitQuad = intersectVrUiSurface(pointerPose.position, forward, hitPosition, hitDistance, hitU, hitV, hitInside);
		}
		bool const hitMainUiQuad = hitQuad && hitInside;
		bool const objectContextUiActive = !hitWristMenuButton && hitObjectContextPanel && objectContextInside;
		if (handIndex >= 0 && handIndex < 2)
		{
			ms_xrObjectContextPointerInside[handIndex] = objectContextUiActive;
			ms_xrObjectContextPointerFrame[handIndex] = ms_frameCount;
		}
		if (objectContextUiActive)
		{
			submitObjectContextMouseState(objectContextU, objectContextV, true, leftTriggerValue, rightTriggerValue, handIndex);
			submitUiMouseState(0.0f, 0.0f, false, 0.0f, 0.0f, handIndex);
		}
		else if (!hitWristMenuButton && hitMainUiQuad)
		{
			submitUiMouseState(hitU, hitV, true, leftTriggerValue, rightTriggerValue, handIndex);
		}
		else if (!hitWristMenuButton)
		{
			int worldX = 0;
			int worldY = 0;
			bool const worldInside = worldCursorFromAim(pointerPose, worldX, worldY);
			submitWorldMouseState(worldX, worldY, worldInside, leftTriggerValue, rightTriggerValue, squeeze.currentState, handIndex, static_cast<double>(displayTime) * 0.000000001);
		}

		if (!arePointerVisualLayersEnabled())
		{
			++ms_xrPointerSubmitCount;
			if (ms_xrPointerSubmitCount == 1 || ms_xrPointerSubmitCount == 30 || ms_xrPointerSubmitCount == 300)
				bridgeLog("{\"event\":\"openxrPointerLayer\",\"ready\":true,\"visible\":false,\"frame\":%d,\"submit\":%d,\"hand\":\"%s\",\"squeeze\":%0.3f,\"trigger\":%0.3f,\"hitQuad\":%s,\"inside\":%s,\"u\":%0.3f,\"v\":%0.3f}",
					ms_frameCount,
					ms_xrPointerSubmitCount,
					handIndex == 0 ? "left" : "right",
					squeeze.currentState,
					handIndex == 0 ? leftTriggerValue : rightTriggerValue,
					hitQuad ? "true" : "false",
					hitInside ? "true" : "false",
					hitU,
					hitV);
			return false;
		}

		if (!ensureWandSwapchain())
			return false;

		unsigned int wandImageIndex = 0;
		XrSwapchainImageAcquireInfoLocal wandAcquireInfo;
		ZeroMemory(&wandAcquireInfo, sizeof(wandAcquireInfo));
		wandAcquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL;
		XrResult const wandAcquireResult = ms_xrAcquireSwapchainImage(ms_xrWandSwapchain, &wandAcquireInfo, &wandImageIndex);
		bool const wandImageValid = wandAcquireResult == XR_SUCCESS_LOCAL && wandImageIndex < ms_xrWandImages.size();
		if (wandImageValid)
		{
			XrSwapchainImageWaitInfoLocal wandWaitInfo;
			ZeroMemory(&wandWaitInfo, sizeof(wandWaitInfo));
			wandWaitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL;
			wandWaitInfo.timeout = XR_INFINITE_DURATION_LOCAL;
			if (ms_xrWaitSwapchainImage(ms_xrWandSwapchain, &wandWaitInfo) == XR_SUCCESS_LOCAL && ms_xrWandImages[wandImageIndex].texture)
				fillPointerTexture(ms_xrWandImages[wandImageIndex].texture, ms_xrWandSwapchainWidth, ms_xrWandSwapchainHeight);
			XrSwapchainImageReleaseInfoLocal wandReleaseInfo;
			ZeroMemory(&wandReleaseInfo, sizeof(wandReleaseInfo));
			wandReleaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
			ms_xrReleaseSwapchainImage(ms_xrWandSwapchain, &wandReleaseInfo);
		}

		bool reticleReady = false;
		unsigned int reticleImageIndex = 0;
		if (hitQuad && hitInside && ensureReticleSwapchain())
		{
			XrSwapchainImageAcquireInfoLocal reticleAcquireInfo;
			ZeroMemory(&reticleAcquireInfo, sizeof(reticleAcquireInfo));
			reticleAcquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL;
			XrResult const reticleAcquireResult = ms_xrAcquireSwapchainImage(ms_xrReticleSwapchain, &reticleAcquireInfo, &reticleImageIndex);
			bool const reticleImageValid = reticleAcquireResult == XR_SUCCESS_LOCAL && reticleImageIndex < ms_xrReticleImages.size();
			if (reticleImageValid)
			{
				XrSwapchainImageWaitInfoLocal reticleWaitInfo;
				ZeroMemory(&reticleWaitInfo, sizeof(reticleWaitInfo));
				reticleWaitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL;
				reticleWaitInfo.timeout = XR_INFINITE_DURATION_LOCAL;
				if (ms_xrWaitSwapchainImage(ms_xrReticleSwapchain, &reticleWaitInfo) == XR_SUCCESS_LOCAL && ms_xrReticleImages[reticleImageIndex].texture)
				{
					fillReticleTexture(ms_xrReticleImages[reticleImageIndex].texture, ms_xrReticleSwapchainWidth, ms_xrReticleSwapchainHeight);
					reticleReady = true;
				}
				XrSwapchainImageReleaseInfoLocal reticleReleaseInfo;
				ZeroMemory(&reticleReleaseInfo, sizeof(reticleReleaseInfo));
				reticleReleaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
				ms_xrReleaseSwapchainImage(ms_xrReticleSwapchain, &reticleReleaseInfo);
			}
		}

		float const maxBeamLengthMeters = getEnvironmentFloat("SWG_OG_VR_POINTER_LAYER_LENGTH_METERS", 2.25f);
		float const beamLengthMeters = hitQuad ? clampFloat(hitDistance, 0.25f, maxBeamLengthMeters) : maxBeamLengthMeters;
		float const beamWidthMeters = clampFloat(getEnvironmentFloat("SWG_OG_VR_POINTER_LAYER_WIDTH_METERS", 0.038f), 0.01f, 0.16f);
		XrVector3fLocal localForward;
		localForward.x = 0.0f;
		localForward.y = 0.0f;
		localForward.z = -1.0f;
		XrVector3fLocal localUp;
		localUp.x = 0.0f;
		localUp.y = 1.0f;
		localUp.z = 0.0f;
		float const rollAngles[1] = { 0.0f };
		unsigned int const desiredWandLayers = maxWandLayers < 1 ? maxWandLayers : 1;
		for (unsigned int i = 0; i < desiredWandLayers; ++i)
		{
			XrPosefLocal wandPose = identityPose();
			wandPose.orientation = wandOrientationFromRay(localForward, localUp, rollAngles[i]);
			wandPose.position.x = 0.0f;
			wandPose.position.y = getEnvironmentFloat("SWG_OG_VR_AIM_RAY_OFFSET_Y_METERS", -0.10f);
			wandPose.position.z = -beamLengthMeters * 0.5f;

			ZeroMemory(&wandLayers[i], sizeof(wandLayers[i]));
			wandLayers[i].type = XR_TYPE_COMPOSITION_LAYER_QUAD_LOCAL;
			wandLayers[i].layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT_LOCAL | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT_LOCAL;
			wandLayers[i].space = worldCompositionLayerSpace();
			wandLayers[i].eyeVisibility = XR_EYE_VISIBILITY_BOTH_LOCAL;
			wandLayers[i].subImage.swapchain = ms_xrWandSwapchain;
			wandLayers[i].subImage.imageRect.offset.x = 0;
			wandLayers[i].subImage.imageRect.offset.y = 0;
			wandLayers[i].subImage.imageRect.extent.width = static_cast<int>(ms_xrWandSwapchainWidth);
			wandLayers[i].subImage.imageRect.extent.height = static_cast<int>(ms_xrWandSwapchainHeight);
			wandLayers[i].pose = worldCompositionLayerPose(composePose(aimLocation.pose, wandPose));
			wandLayers[i].size.width = beamWidthMeters;
			wandLayers[i].size.height = beamLengthMeters;
			++wandLayerCount;
		}

		if (reticleReady && wandLayerCount < maxWandLayers)
		{
			XrPosefLocal reticlePose = ms_xrUiQuadPose;
			if (hitWristMenuButton)
			{
				int wristHand = -1;
				float wristWidth = 0.0f;
				float wristHeight = 0.0f;
				(void)wristMenuButtonPoseInLocalSpace(displayTime, wristHand, reticlePose, wristWidth, wristHeight);
			}
			reticlePose.position = addVector(hitPosition, scaleVector(forward, -0.012f));

			XrCompositionLayerQuadLocal &reticleLayer = wandLayers[wandLayerCount];
			ZeroMemory(&reticleLayer, sizeof(reticleLayer));
			reticleLayer.type = XR_TYPE_COMPOSITION_LAYER_QUAD_LOCAL;
			reticleLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT_LOCAL | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT_LOCAL;
			reticleLayer.space = worldCompositionLayerSpace();
			reticleLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH_LOCAL;
			reticleLayer.subImage.swapchain = ms_xrReticleSwapchain;
			reticleLayer.subImage.imageRect.offset.x = 0;
			reticleLayer.subImage.imageRect.offset.y = 0;
			reticleLayer.subImage.imageRect.extent.width = static_cast<int>(ms_xrReticleSwapchainWidth);
			reticleLayer.subImage.imageRect.extent.height = static_cast<int>(ms_xrReticleSwapchainHeight);
			reticleLayer.pose = worldCompositionLayerPose(reticlePose);
			float const reticleMeters = clampFloat(getEnvironmentFloat("SWG_OG_VR_POINTER_RETICLE_METERS", 0.045f), 0.018f, 0.12f);
			reticleLayer.size.width = reticleMeters;
			reticleLayer.size.height = reticleMeters;
			++wandLayerCount;
		}

		++ms_xrPointerSubmitCount;
		if (ms_xrPointerSubmitCount == 1 || ms_xrPointerSubmitCount == 30 || ms_xrPointerSubmitCount == 300)
			bridgeLog("{\"event\":\"openxrPointerLayer\",\"ready\":true,\"frame\":%d,\"submit\":%d,\"mode\":\"openmw-controller-space-plus-hit-dot\",\"space\":\"aim\",\"hand\":\"%s\",\"pointerHeld\":true,\"squeeze\":%0.3f,\"trigger\":%0.3f,\"hitQuad\":%s,\"hitDistanceMeters\":%0.3f,\"inside\":%s,\"u\":%0.3f,\"v\":%0.3f,\"lengthMeters\":%0.3f,\"widthMeters\":%0.3f,\"reticle\":%s,\"layers\":%u,\"forward\":[%0.3f,%0.3f,%0.3f]}",
				ms_frameCount,
				ms_xrPointerSubmitCount,
				handIndex == 0 ? "left" : "right",
				squeeze.currentState,
				trigger.currentState,
				hitQuad ? "true" : "false",
				hitDistance,
				hitInside ? "true" : "false",
				hitU,
				hitV,
				beamLengthMeters,
				beamWidthMeters,
				reticleReady ? "true" : "false",
				wandLayerCount,
				forward.x,
				forward.y,
				forward.z);
		if (wandLayerCount > 0)
			++ms_xrWandSubmitCount;
		return true;
	}

	bool prepareControllerHandLayers(XrTime displayTime, XrCompositionLayerQuadLocal *handLayers, unsigned int maxHandLayers, unsigned int &handLayerCount)
	{
		handLayerCount = 0;
		if (!areHandVisualLayersEnabled())
			return false;
		if (!ms_xrControllerActionsReady || !ensurePointerSwapchain() || !handLayers || maxHandLayers == 0)
			return false;

		float maxSqueezeValue = 0.0f;
		for (int hand = 0; hand < 2; ++hand)
		{
			XrActionStateFloatLocal const squeeze = getFloatActionState(ms_xrSqueezeValueAction, ms_xrHandSubactionPaths[hand]);
			if (squeeze.isActive && squeeze.currentState > maxSqueezeValue)
				maxSqueezeValue = squeeze.currentState;
		}
		bool const handClosed = maxSqueezeValue >= getEnvironmentFloat("SWG_OG_VR_PHYSICS_GRIP_THRESHOLD", 0.60f);

		unsigned int handImageIndex = 0;
		XrSwapchainImageAcquireInfoLocal acquireInfo;
		ZeroMemory(&acquireInfo, sizeof(acquireInfo));
		acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL;
		XrResult const acquireResult = ms_xrAcquireSwapchainImage(ms_xrPointerSwapchain, &acquireInfo, &handImageIndex);
		bool const handImageValid = acquireResult == XR_SUCCESS_LOCAL && handImageIndex < ms_xrPointerImages.size();
		if (handImageValid)
		{
			XrSwapchainImageWaitInfoLocal waitInfo;
			ZeroMemory(&waitInfo, sizeof(waitInfo));
			waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL;
			waitInfo.timeout = XR_INFINITE_DURATION_LOCAL;
			if (ms_xrWaitSwapchainImage(ms_xrPointerSwapchain, &waitInfo) == XR_SUCCESS_LOCAL && ms_xrPointerImages[handImageIndex].texture)
				fillHandTexture(ms_xrPointerImages[handImageIndex].texture, ms_xrPointerSwapchainWidth, ms_xrPointerSwapchainHeight, handClosed);
			XrSwapchainImageReleaseInfoLocal releaseInfo;
			ZeroMemory(&releaseInfo, sizeof(releaseInfo));
			releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
			ms_xrReleaseSwapchainImage(ms_xrPointerSwapchain, &releaseInfo);
		}

		float const handHeightMeters = clampFloat(getEnvironmentFloat("SWG_OG_VR_HAND_LAYER_HEIGHT_METERS", 0.205f), 0.08f, 0.32f);
		float const handWidthMeters = clampFloat(getEnvironmentFloat("SWG_OG_VR_HAND_LAYER_WIDTH_METERS", 0.125f), 0.05f, 0.22f);
		float const forwardOffsetMeters = getEnvironmentFloat("SWG_OG_VR_HAND_LAYER_FORWARD_OFFSET_METERS", -0.030f);
		float const verticalOffsetMeters = getEnvironmentFloat("SWG_OG_VR_HAND_LAYER_VERTICAL_OFFSET_METERS", -0.035f);
		unsigned int trackedHands = 0;

		for (int hand = 0; hand < 2; ++hand)
		{
			if (!ms_xrGripSpaces[hand])
				continue;

			XrSpaceLocationLocal const gripLocation = locateActionSpace(ms_xrGripSpaces[hand], displayTime);
			bool const positionValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL) != 0;
			bool const orientationValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
			if (!positionValid || !orientationValid)
				continue;

			++trackedHands;
			if (handLayerCount >= maxHandLayers)
				continue;

			XrPosefLocal handPose = identityPose();
			handPose.position.x = 0.0f;
			handPose.position.y = verticalOffsetMeters;
			handPose.position.z = forwardOffsetMeters;

			XrCompositionLayerQuadLocal &layer = handLayers[handLayerCount];
			ZeroMemory(&layer, sizeof(layer));
			layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD_LOCAL;
			layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT_LOCAL | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT_LOCAL;
			layer.space = worldCompositionLayerSpace();
			layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH_LOCAL;
			layer.subImage.swapchain = ms_xrPointerSwapchain;
			layer.subImage.imageRect.offset.x = hand == 0 ? 0 : static_cast<int>(ms_xrPointerSwapchainWidth / 2);
			layer.subImage.imageRect.offset.y = 0;
			layer.subImage.imageRect.extent.width = static_cast<int>(ms_xrPointerSwapchainWidth / 2);
			layer.subImage.imageRect.extent.height = static_cast<int>(ms_xrPointerSwapchainHeight);
			layer.pose = worldCompositionLayerPose(composePose(gripLocation.pose, handPose));
			layer.size.width = handWidthMeters;
			layer.size.height = handHeightMeters;
			++handLayerCount;
		}

		if (handLayerCount > 0)
		{
			++ms_xrHandSubmitCount;
			if (ms_xrHandSubmitCount == 1 || ms_xrHandSubmitCount == 30 || ms_xrHandSubmitCount == 300)
				bridgeLog("{\"event\":\"openxrHandLayers\",\"ready\":true,\"frame\":%d,\"submit\":%d,\"trackedHands\":%u,\"layers\":%u,\"widthMeters\":%0.3f,\"heightMeters\":%0.3f,\"forwardOffsetMeters\":%0.3f,\"verticalOffsetMeters\":%0.3f,\"style\":\"mirrored-hand-atlas\"}",
					ms_frameCount,
					ms_xrHandSubmitCount,
					trackedHands,
					handLayerCount,
					handWidthMeters,
					handHeightMeters,
					forwardOffsetMeters,
					verticalOffsetMeters);
		}

		return handLayerCount > 0;
	}

	bool prepareWristMenuButtonLayer(XrTime displayTime, XrCompositionLayerQuadLocal *wristLayers, unsigned int maxWristLayers, unsigned int &wristLayerCount)
	{
		UNREF(displayTime);
		UNREF(wristLayers);
		UNREF(maxWristLayers);
		wristLayerCount = 0;
		return false;
	}

	bool prepareWristDashboardLayer(XrTime displayTime, XrCompositionLayerQuadLocal *wristLayers, unsigned int maxWristLayers, unsigned int &wristLayerCount)
	{
		wristLayerCount = 0;
		if (!hasFreshWristDashboard() || !ms_xrControllerActionsReady || !ensureWristDashboardSwapchain() || !wristLayers || maxWristLayers < 1)
			return false;

		unsigned int imageIndex = 0;
		XrSwapchainImageAcquireInfoLocal acquireInfo;
		ZeroMemory(&acquireInfo, sizeof(acquireInfo));
		acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL;
		XrResult const acquireResult = ms_xrAcquireSwapchainImage(ms_xrWristDashboardSwapchain, &acquireInfo, &imageIndex);
		bool const imageValid = acquireResult == XR_SUCCESS_LOCAL && imageIndex < ms_xrWristDashboardImages.size();
		if (imageValid)
		{
			XrSwapchainImageWaitInfoLocal waitInfo;
			ZeroMemory(&waitInfo, sizeof(waitInfo));
			waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL;
			waitInfo.timeout = XR_INFINITE_DURATION_LOCAL;
			if (ms_xrWaitSwapchainImage(ms_xrWristDashboardSwapchain, &waitInfo) == XR_SUCCESS_LOCAL && ms_xrWristDashboardImages[imageIndex].texture && ms_xrWristDashboardCaptureTexture)
				ms_context->CopyResource(ms_xrWristDashboardImages[imageIndex].texture, ms_xrWristDashboardCaptureTexture);

			XrSwapchainImageReleaseInfoLocal releaseInfo;
			ZeroMemory(&releaseInfo, sizeof(releaseInfo));
			releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
			ms_xrReleaseSwapchainImage(ms_xrWristDashboardSwapchain, &releaseInfo);
		}

		float const innerOffsetMeters = getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_INNER_X_METERS", 0.055f);
		float const verticalOffsetMeters = getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_OFFSET_Y_METERS", 0.160f);
		float const forearmOffsetMeters = getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_OFFSET_Z_METERS", 0.070f);
		float const rollRadians = getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_ROLL_DEGREES", 0.0f) * 0.01745329252f;
		float const widthMeters = clampFloat(getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_WIDTH_METERS", 0.380f), 0.060f, 0.800f);
		float const heightMeters = clampFloat(getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_HEIGHT_METERS", 0.380f), 0.060f, 0.800f);
		unsigned int const panelTextureWidth = ms_xrWristDashboardSwapchainWidth / 2;
		unsigned int const panelTextureHeight = ms_xrWristDashboardSwapchainHeight;

		struct WristPanel
		{
			int hand;
			char const *name;
			unsigned int textureOffsetX;
		};
		WristPanel const panels[2] =
		{
			{ 0, "stats", 0 },
			{ 1, "radar", panelTextureWidth }
		};

		for (int i = 0; i != 2; ++i)
		{
			int const hand = panels[i].hand;
			if (!ms_xrGripSpaces[hand])
				continue;

			XrSpaceLocationLocal const gripLocation = locateActionSpace(ms_xrGripSpaces[hand], displayTime);
			bool const positionValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT_LOCAL) != 0;
			bool const orientationValid = (gripLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT_LOCAL) != 0;
			if (!positionValid || !orientationValid)
				continue;

			XrPosefLocal pose = identityPose();
			pose.position.x = hand == 0 ? innerOffsetMeters : -innerOffsetMeters;
			pose.position.y = verticalOffsetMeters;
			pose.position.z = forearmOffsetMeters;
			XrPosefLocal worldPose = worldCompositionLayerPose(composePose(gripLocation.pose, pose));
			if (getEnvironmentFlagDefault("SWG_OG_VR_WRIST_DASHBOARD_FACE_HEAD", true) && ms_xrHandHeadPoseCaptured)
			{
				XrVector3fLocal forward = subtractVector(worldPose.position, ms_xrHandHeadPose.position);
				forward.y = 0.0f;
				if (!normalizeVector(forward))
				{
					forward.x = 0.0f;
					forward.y = 0.0f;
					forward.z = -1.0f;
				}
				XrQuaternionfLocal facingHead = quadOrientationFacingHead(forward);
				if (fabsf(rollRadians) > 0.0001f)
				{
					XrQuaternionfLocal roll = identityPose().orientation;
					roll.z = sinf(rollRadians * 0.5f);
					roll.w = cosf(rollRadians * 0.5f);
					facingHead = normalizedQuaternion(multiplyQuaternion(facingHead, roll));
				}
				worldPose.orientation = facingHead;
			}
			else
			{
				XrVector3fLocal localUp;
				localUp.x = 0.0f;
				localUp.y = 0.0f;
				localUp.z = -1.0f;
				XrVector3fLocal localNormal;
				localNormal.x = 0.0f;
				localNormal.y = 1.0f;
				localNormal.z = 0.0f;
				pose.orientation = wandOrientationFromRay(localNormal, localUp, rollRadians);
				worldPose = worldCompositionLayerPose(composePose(gripLocation.pose, pose));
			}

			worldPose.orientation = normalizedQuaternion(worldPose.orientation);
			if (!validPose(worldPose))
			{
				bridgeLog("{\"event\":\"openxrWristDashboard\",\"ready\":false,\"reason\":\"invalid-pose\",\"frame\":%d,\"hand\":%d,\"position\":[%0.3f,%0.3f,%0.3f],\"orientation\":[%0.3f,%0.3f,%0.3f,%0.3f]}",
					ms_frameCount,
					hand,
					worldPose.position.x,
					worldPose.position.y,
					worldPose.position.z,
					worldPose.orientation.x,
					worldPose.orientation.y,
					worldPose.orientation.z,
					worldPose.orientation.w);
				continue;
			}

			XrCompositionLayerQuadLocal &layer = wristLayers[wristLayerCount];
			ZeroMemory(&layer, sizeof(layer));
			layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD_LOCAL;
			layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT_LOCAL | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT_LOCAL;
			layer.space = worldCompositionLayerSpace();
			layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH_LOCAL;
			layer.subImage.swapchain = ms_xrWristDashboardSwapchain;
			layer.subImage.imageRect.offset.x = static_cast<int>(panels[i].textureOffsetX);
			layer.subImage.imageRect.offset.y = 0;
			layer.subImage.imageRect.extent.width = static_cast<int>(panelTextureWidth);
			layer.subImage.imageRect.extent.height = static_cast<int>(panelTextureHeight);
			layer.pose = worldPose;
			layer.size.width = widthMeters;
			layer.size.height = heightMeters;
			++wristLayerCount;
			if (wristLayerCount >= maxWristLayers)
				break;
		}

		++ms_xrWristDashboardSubmitCount;
		if (ms_xrWristDashboardSubmitCount == 1 || ms_xrWristDashboardSubmitCount == 30 || ms_xrWristDashboardSubmitCount == 300)
			bridgeLog("{\"event\":\"openxrWristDashboard\",\"ready\":true,\"frame\":%d,\"submit\":%d,\"layers\":%u,\"left\":\"stats\",\"right\":\"radar\",\"innerOffset\":%0.3f,\"forearmOffset\":%0.3f,\"size\":[%0.3f,%0.3f],\"texture\":[%u,%u],\"source\":\"direct-swapchain-no-hud-crop\"}",
				ms_frameCount,
				ms_xrWristDashboardSubmitCount,
				wristLayerCount,
				innerOffsetMeters,
				forearmOffsetMeters,
				widthMeters,
				heightMeters,
				ms_xrWristDashboardSwapchainWidth,
				ms_xrWristDashboardSwapchainHeight);
		return wristLayerCount > 0;
	}

	bool prepareObjectContextLayer(XrCompositionLayerQuadLocal *objectLayers, unsigned int maxObjectLayers, unsigned int &objectLayerCount)
	{
		objectLayerCount = 0;
		if (!hasFreshObjectContext() || !ensureObjectContextSwapchain() || !objectLayers || maxObjectLayers == 0)
			return false;

		if (!hasFreshObjectContextCapture())
		{
			if (!getEnvironmentFlagDefault("SWG_OG_VR_OBJECT_CONTEXT_DIRECT_FALLBACK", false))
				return false;

			unsigned int imageIndex = 0;
			XrSwapchainImageAcquireInfoLocal acquireInfo;
			ZeroMemory(&acquireInfo, sizeof(acquireInfo));
			acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL;
			XrResult const acquireResult = ms_xrAcquireSwapchainImage(ms_xrObjectContextSwapchain, &acquireInfo, &imageIndex);
			bool const imageValid = acquireResult == XR_SUCCESS_LOCAL && imageIndex < ms_xrObjectContextImages.size();
			if (imageValid)
			{
				XrSwapchainImageWaitInfoLocal waitInfo;
				ZeroMemory(&waitInfo, sizeof(waitInfo));
				waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL;
				waitInfo.timeout = XR_INFINITE_DURATION_LOCAL;
				if (ms_xrWaitSwapchainImage(ms_xrObjectContextSwapchain, &waitInfo) == XR_SUCCESS_LOCAL && ms_xrObjectContextImages[imageIndex].texture)
					fillObjectContextTexture(ms_xrObjectContextImages[imageIndex].texture, ms_xrObjectContextSwapchainWidth, ms_xrObjectContextSwapchainHeight);

				XrSwapchainImageReleaseInfoLocal releaseInfo;
				ZeroMemory(&releaseInfo, sizeof(releaseInfo));
				releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
				ms_xrReleaseSwapchainImage(ms_xrObjectContextSwapchain, &releaseInfo);
			}
		}

		XrCompositionLayerQuadLocal &layer = objectLayers[0];
		ZeroMemory(&layer, sizeof(layer));
		layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD_LOCAL;
		layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT_LOCAL | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT_LOCAL;
		layer.space = worldCompositionLayerSpace();
		layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH_LOCAL;
		layer.subImage.swapchain = ms_xrObjectContextSwapchain;
		layer.subImage.imageRect.offset.x = 0;
		layer.subImage.imageRect.offset.y = 0;
		layer.subImage.imageRect.extent.width = static_cast<int>(ms_xrObjectContextSwapchainWidth);
		layer.subImage.imageRect.extent.height = static_cast<int>(ms_xrObjectContextSwapchainHeight);
		layer.pose = objectContextPose("AllTargets", 0.0f, 0.0f);
		objectContextLayerSize(layer.size.width, layer.size.height);
		objectLayerCount = 1;

		++ms_xrObjectContextSubmitCount;
		if (ms_xrObjectContextSubmitCount == 1 || ms_xrObjectContextSubmitCount == 30 || ms_xrObjectContextSubmitCount == 300)
			bridgeLog("{\"event\":\"openxrObjectContextLayer\",\"ready\":true,\"frame\":%d,\"submit\":%d,\"target\":\"%s\",\"space\":\"%s\",\"worldRenderSpace\":%s,\"position\":[%0.3f,%0.3f,%0.3f],\"size\":[%0.3f,%0.3f],\"texture\":[%u,%u],\"source\":\"direct-swapchain-no-hud-crop\"}",
				ms_frameCount,
				ms_xrObjectContextSubmitCount,
				ms_objectContextTargetName,
				referenceSpaceName(ms_xrReferenceSpaceType),
				ms_xrWorldRenderSpace ? "true" : "false",
				layer.pose.position.x,
				layer.pose.position.y,
				layer.pose.position.z,
				layer.size.width,
				layer.size.height,
				ms_xrObjectContextSwapchainWidth,
				ms_xrObjectContextSwapchainHeight);

		return true;
	}

	bool prepareHoverTargetLayer(XrCompositionLayerQuadLocal *hoverLayers, unsigned int maxHoverLayers, unsigned int &hoverLayerCount)
	{
		hoverLayerCount = 0;
		if (!hasFreshHoverTargetAnchor() || !ms_hoverTargetValid || (ms_frameCount - ms_hoverTargetFrame) > 30 || !ensureHoverTargetSwapchain() || !hoverLayers || maxHoverLayers == 0)
			return false;

		unsigned int imageIndex = 0;
		XrSwapchainImageAcquireInfoLocal acquireInfo;
		ZeroMemory(&acquireInfo, sizeof(acquireInfo));
		acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL;
		XrResult const acquireResult = ms_xrAcquireSwapchainImage(ms_xrHoverTargetSwapchain, &acquireInfo, &imageIndex);
		bool const imageValid = acquireResult == XR_SUCCESS_LOCAL && imageIndex < ms_xrHoverTargetImages.size();
		if (imageValid)
		{
			XrSwapchainImageWaitInfoLocal waitInfo;
			ZeroMemory(&waitInfo, sizeof(waitInfo));
			waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL;
			waitInfo.timeout = XR_INFINITE_DURATION_LOCAL;
			if (ms_xrWaitSwapchainImage(ms_xrHoverTargetSwapchain, &waitInfo) == XR_SUCCESS_LOCAL && ms_xrHoverTargetImages[imageIndex].texture)
				fillHoverTargetTexture(ms_xrHoverTargetImages[imageIndex].texture, ms_xrHoverTargetSwapchainWidth, ms_xrHoverTargetSwapchainHeight);

			XrSwapchainImageReleaseInfoLocal releaseInfo;
			ZeroMemory(&releaseInfo, sizeof(releaseInfo));
			releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
			ms_xrReleaseSwapchainImage(ms_xrHoverTargetSwapchain, &releaseInfo);
		}

		XrCompositionLayerQuadLocal &layer = hoverLayers[0];
		ZeroMemory(&layer, sizeof(layer));
		layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD_LOCAL;
		layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT_LOCAL | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT_LOCAL;
		layer.space = worldCompositionLayerSpace();
		layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH_LOCAL;
		layer.subImage.swapchain = ms_xrHoverTargetSwapchain;
		layer.subImage.imageRect.offset.x = 0;
		layer.subImage.imageRect.offset.y = 0;
		layer.subImage.imageRect.extent.width = static_cast<int>(ms_xrHoverTargetSwapchainWidth);
		layer.subImage.imageRect.extent.height = static_cast<int>(ms_xrHoverTargetSwapchainHeight);
		layer.pose = hoverTargetPose();
		float const scale = clampFloat(getEnvironmentFloat("SWG_OG_VR_HOVER_TARGET_FRAME_SCALE", getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_SCALE", 1.20f)), 0.50f, 3.00f);
		layer.size.width = clampFloat(getEnvironmentFloat("SWG_OG_VR_HOVER_TARGET_FRAME_WIDTH_METERS", getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_WIDTH_METERS", 1.25f)) * scale, 0.65f, 8.00f);
		layer.size.height = clampFloat(getEnvironmentFloat("SWG_OG_VR_HOVER_TARGET_FRAME_HEIGHT_METERS", getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_HEIGHT_METERS", 0.70f)) * scale, 0.35f, 4.00f);
		hoverLayerCount = 1;

		static int s_hoverLayerLogCount = 0;
		if (s_hoverLayerLogCount < 40 || (ms_frameCount % 120) == 0)
		{
			++s_hoverLayerLogCount;
			bridgeLog("{\"event\":\"openxrHoverTargetLayer\",\"ready\":true,\"frame\":%d,\"target\":\"%s\",\"space\":\"%s\",\"worldRenderSpace\":%s,\"position\":[%0.3f,%0.3f,%0.3f],\"size\":[%0.3f,%0.3f],\"source\":\"direct-swapchain-no-hud-crop\"}",
				ms_frameCount,
				ms_hoverTargetName,
				referenceSpaceName(ms_xrReferenceSpaceType),
				ms_xrWorldRenderSpace ? "true" : "false",
				layer.pose.position.x,
				layer.pose.position.y,
				layer.pose.position.z,
				layer.size.width,
				layer.size.height);
		}

		return true;
	}

	bool createReferenceSpace(XrReferenceSpaceType referenceSpaceType, XrSpace &space, XrPosefLocal const *poseInReferenceSpaceOverride = 0)
	{
		if (!ms_xrCreateReferenceSpace || !ms_xrSession)
			return false;

		XrReferenceSpaceCreateInfoLocal spaceInfo;
		ZeroMemory(&spaceInfo, sizeof(spaceInfo));
		spaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO_LOCAL;
		spaceInfo.referenceSpaceType = referenceSpaceType;
		spaceInfo.poseInReferenceSpace = poseInReferenceSpaceOverride ? *poseInReferenceSpaceOverride : identityPose();
		XrResult const result = ms_xrCreateReferenceSpace(ms_xrSession, &spaceInfo, &space);
		if (result != XR_SUCCESS_LOCAL || !space)
		{
			bridgeLog("{\"event\":\"openxrReferenceSpace\",\"ready\":false,\"space\":\"%s\",\"result\":%d}", referenceSpaceName(referenceSpaceType), result);
			return false;
		}

		bridgeLog("{\"event\":\"openxrReferenceSpace\",\"ready\":true,\"space\":\"%s\"}", referenceSpaceName(referenceSpaceType));
		return true;
	}

	bool createWorldRenderSpace(float capturedYawDegrees, char const *phase)
	{
		if (ms_xrWorldRenderSpace && ms_xrDestroySpace)
			ms_xrDestroySpace(ms_xrWorldRenderSpace);
		ms_xrWorldRenderSpace = 0;

		XrVector3fLocal const capturedOrigin = ms_xrWorldHeadOrigin;
		XrPosefLocal worldOriginInCurrentBase = identityPose();
		worldOriginInCurrentBase.position = capturedOrigin;
		worldOriginInCurrentBase.orientation = yawOrientation(capturedYawDegrees / 57.2957795f);
		XrPosefLocal const worldOriginInReferenceSpace = composePose(ms_xrBasePoseInReferenceSpace, worldOriginInCurrentBase);

		XrSpace space = 0;
		if (!createReferenceSpace(ms_xrReferenceSpaceType, space, &worldOriginInReferenceSpace))
		{
			bridgeLog("{\"event\":\"openxrWorldBaseSpace\",\"ready\":false,\"phase\":\"%s\",\"space\":\"%s\",\"origin\":[%0.3f,%0.3f,%0.3f],\"capturedYawDegrees\":%0.3f}",
				phase ? phase : "",
				referenceSpaceName(ms_xrReferenceSpaceType),
				capturedOrigin.x,
				capturedOrigin.y,
				capturedOrigin.z,
				capturedYawDegrees);
			return false;
		}

		XrSpace const oldBaseSpace = ms_xrLocalSpace;
		ms_xrLocalSpace = space;
		if (oldBaseSpace && oldBaseSpace != ms_xrLocalSpace && ms_xrDestroySpace)
			ms_xrDestroySpace(oldBaseSpace);
		ms_xrBasePoseInReferenceSpace = worldOriginInReferenceSpace;

		ms_xrWorldBaseSpaceJustRecentered = true;
		ms_xrWorldHeadOrigin.x = 0.0f;
		ms_xrWorldHeadOrigin.y = 0.0f;
		ms_xrWorldHeadOrigin.z = 0.0f;
		ms_xrWorldYawCos = 1.0f;
		ms_xrWorldYawSin = 0.0f;
		ms_xrWorldYawHalfCos = 1.0f;
		ms_xrWorldYawHalfSin = 0.0f;
		ms_xrWorldYawCaptured = true;

		bridgeLog("{\"event\":\"openxrWorldBaseSpace\",\"ready\":true,\"phase\":\"%s\",\"space\":\"%s\",\"origin\":[%0.3f,%0.3f,%0.3f],\"capturedYawDegrees\":%0.3f,\"replaceBase\":true}",
			phase ? phase : "",
			referenceSpaceName(ms_xrReferenceSpaceType),
			capturedOrigin.x,
			capturedOrigin.y,
			capturedOrigin.z,
			capturedYawDegrees);
		return true;
	}

	bool createPresentationSpace()
	{
		XrSpace space = 0;
		if (!getEnvironmentEquals("SWG_OG_VR_QUAD_SPACE", "local") && createReferenceSpace(XR_REFERENCE_SPACE_TYPE_STAGE_LOCAL, space))
		{
			ms_xrLocalSpace = space;
			ms_xrReferenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE_LOCAL;
			ms_xrBasePoseInReferenceSpace = identityPose();
			return true;
		}

		space = 0;
		if (!createReferenceSpace(XR_REFERENCE_SPACE_TYPE_LOCAL_LOCAL, space))
			return false;

		ms_xrLocalSpace = space;
		ms_xrReferenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL_LOCAL;
		ms_xrBasePoseInReferenceSpace = identityPose();
		return true;
	}

	bool chooseR8G8B8A8SwapchainFormat(char const *eventName, long long &format, bool &srgb)
	{
		format = 0;
		srgb = false;
		if (!ms_xrSession || !ms_xrEnumerateSwapchainFormats)
			return false;

		unsigned int formatCount = 0;
		XrResult formatResult = ms_xrEnumerateSwapchainFormats(ms_xrSession, 0, &formatCount, 0);
		if (formatResult != XR_SUCCESS_LOCAL || formatCount == 0)
		{
			bridgeLog("{\"event\":\"%s\",\"ready\":false,\"failure\":\"formats\",\"result\":%d}", eventName ? eventName : "openxrSwapchain", formatResult);
			return false;
		}

		std::vector<long long> formats(formatCount);
		formatResult = ms_xrEnumerateSwapchainFormats(ms_xrSession, formatCount, &formatCount, &formats[0]);
		if (formatResult != XR_SUCCESS_LOCAL || formatCount == 0)
		{
			bridgeLog("{\"event\":\"%s\",\"ready\":false,\"failure\":\"format-list\",\"result\":%d}", eventName ? eventName : "openxrSwapchain", formatResult);
			return false;
		}

		long long const srgbFormat = static_cast<long long>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
		long long const unormFormat = static_cast<long long>(DXGI_FORMAT_R8G8B8A8_UNORM);
		bool srgbFound = false;
		bool unormFound = false;
		for (unsigned int i = 0; i < formatCount; ++i)
		{
			if (formats[i] == srgbFormat)
				srgbFound = true;
			if (formats[i] == unormFormat)
				unormFound = true;
		}

		if (srgbFound)
		{
			format = srgbFormat;
			srgb = true;
			return true;
		}

		if (unormFound)
		{
			format = unormFormat;
			bridgeLog("{\"event\":\"%s\",\"ready\":true,\"fallback\":\"unorm\",\"preferredFormat\":%I64d,\"format\":%I64d,\"formatCount\":%u}", eventName ? eventName : "openxrSwapchain", srgbFormat, format, formatCount);
			return true;
		}

		bridgeLog("{\"event\":\"%s\",\"ready\":false,\"failure\":\"preferred-format-missing\",\"preferredFormat\":%I64d,\"fallbackFormat\":%I64d,\"formatCount\":%u}", eventName ? eventName : "openxrSwapchain", srgbFormat, unormFormat, formatCount);
		return false;
	}

	bool ensureQuadSwapchain(ID3D11Texture2D *backBuffer)
	{
		if (ms_xrQuadSwapchain)
			return true;
		if (!backBuffer || !ms_xrCreateSwapchain || !ms_xrEnumerateSwapchainImages || !ms_xrEnumerateSwapchainFormats)
			return false;

		D3D11_TEXTURE2D_DESC backBufferDesc;
		backBuffer->GetDesc(&backBufferDesc);

		long long format = 0;
		bool srgb = false;
		if (!chooseR8G8B8A8SwapchainFormat("openxrQuadSwapchain", format, srgb))
			return false;

		XrSwapchainCreateInfoLocal swapchainInfo;
		ZeroMemory(&swapchainInfo, sizeof(swapchainInfo));
		swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO_LOCAL;
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT_LOCAL | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT_LOCAL | XR_SWAPCHAIN_USAGE_SAMPLED_BIT_LOCAL;
		swapchainInfo.format = format;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = backBufferDesc.Width;
		swapchainInfo.height = backBufferDesc.Height;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;

		XrResult const createResult = ms_xrCreateSwapchain(ms_xrSession, &swapchainInfo, &ms_xrQuadSwapchain);
		if (createResult != XR_SUCCESS_LOCAL || !ms_xrQuadSwapchain)
		{
			bridgeLog("{\"event\":\"openxrQuadSwapchain\",\"ready\":false,\"failure\":\"create\",\"result\":%d,\"width\":%u,\"height\":%u,\"format\":%I64d,\"srgb\":%s}", createResult, swapchainInfo.width, swapchainInfo.height, format, srgb ? "true" : "false");
			return false;
		}

		unsigned int imageCount = 0;
		XrResult imageResult = ms_xrEnumerateSwapchainImages(ms_xrQuadSwapchain, 0, &imageCount, 0);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrQuadSwapchain\",\"ready\":false,\"failure\":\"image-count\",\"result\":%d}", imageResult);
			return false;
		}

		ms_xrQuadImages.resize(imageCount);
		ms_xrQuadRenderTargetViews.resize(imageCount, 0);
		for (unsigned int i = 0; i < imageCount; ++i)
		{
			ZeroMemory(&ms_xrQuadImages[i], sizeof(ms_xrQuadImages[i]));
			ms_xrQuadImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR_LOCAL;
		}
		imageResult = ms_xrEnumerateSwapchainImages(ms_xrQuadSwapchain, imageCount, &imageCount, &ms_xrQuadImages[0]);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrQuadSwapchain\",\"ready\":false,\"failure\":\"images\",\"result\":%d}", imageResult);
			return false;
		}

		ms_xrQuadSwapchainWidth = backBufferDesc.Width;
		ms_xrQuadSwapchainHeight = backBufferDesc.Height;
		ms_xrQuadSwapchainFormat = format;
		bridgeLog("{\"event\":\"openxrQuadSwapchain\",\"ready\":true,\"width\":%u,\"height\":%u,\"format\":%I64d,\"srgb\":%s,\"images\":%u}", ms_xrQuadSwapchainWidth, ms_xrQuadSwapchainHeight, format, srgb ? "true" : "false", imageCount);
		return true;
	}

	bool ensureProjectionSwapchain()
	{
		if (ms_xrProjectionSwapchain)
			return true;
		if (!ms_xrCreateSwapchain || !ms_xrEnumerateSwapchainImages || !ms_xrSession)
			return false;

		long long format = 0;
		bool srgb = false;
		if (!chooseR8G8B8A8SwapchainFormat("openxrProjectionSwapchain", format, srgb))
			return false;

		float const eyeScale = clampFloat(getEnvironmentFloat("SWG_OG_VR_EYE_SCALE", 1.0f), 0.50f, 2.00f);
		unsigned int width = ms_xrRecommendedEyeWidth ? static_cast<unsigned int>(static_cast<float>(ms_xrRecommendedEyeWidth) * eyeScale + 0.5f) : 1280;
		unsigned int height = ms_xrRecommendedEyeHeight ? static_cast<unsigned int>(static_cast<float>(ms_xrRecommendedEyeHeight) * eyeScale + 0.5f) : 1280;
		if (ms_xrMaxSwapchainImageWidth && width > ms_xrMaxSwapchainImageWidth)
			width = ms_xrMaxSwapchainImageWidth;
		if (ms_xrMaxSwapchainImageHeight && height > ms_xrMaxSwapchainImageHeight)
			height = ms_xrMaxSwapchainImageHeight;

		XrSwapchainCreateInfoLocal swapchainInfo;
		ZeroMemory(&swapchainInfo, sizeof(swapchainInfo));
		swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO_LOCAL;
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT_LOCAL | XR_SWAPCHAIN_USAGE_SAMPLED_BIT_LOCAL;
		swapchainInfo.format = format;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = width;
		swapchainInfo.height = height;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 2;
		swapchainInfo.mipCount = 1;

		XrResult const createResult = ms_xrCreateSwapchain(ms_xrSession, &swapchainInfo, &ms_xrProjectionSwapchain);
		if (createResult != XR_SUCCESS_LOCAL || !ms_xrProjectionSwapchain)
		{
			bridgeLog("{\"event\":\"openxrProjectionSwapchain\",\"ready\":false,\"failure\":\"create\",\"result\":%d,\"width\":%u,\"height\":%u,\"eyeScale\":%0.3f,\"format\":%I64d,\"srgb\":%s}", createResult, width, height, eyeScale, format, srgb ? "true" : "false");
			return false;
		}

		unsigned int imageCount = 0;
		XrResult imageResult = ms_xrEnumerateSwapchainImages(ms_xrProjectionSwapchain, 0, &imageCount, 0);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
			return false;

		ms_xrProjectionImages.resize(imageCount);
		for (unsigned int i = 0; i < imageCount; ++i)
		{
			ZeroMemory(&ms_xrProjectionImages[i], sizeof(ms_xrProjectionImages[i]));
			ms_xrProjectionImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR_LOCAL;
		}
		imageResult = ms_xrEnumerateSwapchainImages(ms_xrProjectionSwapchain, imageCount, &imageCount, &ms_xrProjectionImages[0]);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
			return false;

		ms_xrProjectionSwapchainWidth = width;
		ms_xrProjectionSwapchainHeight = height;
		bridgeLog("{\"event\":\"openxrProjectionSwapchain\",\"ready\":true,\"width\":%u,\"height\":%u,\"eyeScale\":%0.3f,\"arraySize\":2,\"format\":%I64d,\"srgb\":%s,\"images\":%u}", width, height, eyeScale, format, srgb ? "true" : "false", imageCount);
		return true;
	}

	bool ensurePointerSwapchain()
	{
		if (ms_xrPointerSwapchain)
			return true;
		if (!ms_xrCreateSwapchain || !ms_xrEnumerateSwapchainImages || !ms_xrSession)
			return false;

		unsigned int const width = 64;
		unsigned int const height = 128;
		long long format = 0;
		bool srgb = false;
		if (!chooseR8G8B8A8SwapchainFormat("openxrPointerSwapchain", format, srgb))
			return false;

		XrSwapchainCreateInfoLocal swapchainInfo;
		ZeroMemory(&swapchainInfo, sizeof(swapchainInfo));
		swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO_LOCAL;
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT_LOCAL | XR_SWAPCHAIN_USAGE_SAMPLED_BIT_LOCAL;
		swapchainInfo.format = format;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = width;
		swapchainInfo.height = height;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;

		XrResult const createResult = ms_xrCreateSwapchain(ms_xrSession, &swapchainInfo, &ms_xrPointerSwapchain);
		if (createResult != XR_SUCCESS_LOCAL || !ms_xrPointerSwapchain)
		{
			bridgeLog("{\"event\":\"openxrPointerSwapchain\",\"ready\":false,\"failure\":\"create\",\"result\":%d,\"format\":%I64d,\"srgb\":%s}", createResult, format, srgb ? "true" : "false");
			return false;
		}

		unsigned int imageCount = 0;
		XrResult imageResult = ms_xrEnumerateSwapchainImages(ms_xrPointerSwapchain, 0, &imageCount, 0);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrPointerSwapchain\",\"ready\":false,\"failure\":\"image-count\",\"result\":%d}", imageResult);
			return false;
		}

		ms_xrPointerImages.resize(imageCount);
		for (unsigned int i = 0; i < imageCount; ++i)
		{
			ZeroMemory(&ms_xrPointerImages[i], sizeof(ms_xrPointerImages[i]));
			ms_xrPointerImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR_LOCAL;
		}

		imageResult = ms_xrEnumerateSwapchainImages(ms_xrPointerSwapchain, imageCount, &imageCount, &ms_xrPointerImages[0]);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrPointerSwapchain\",\"ready\":false,\"failure\":\"images\",\"result\":%d}", imageResult);
			return false;
		}

		ms_xrPointerSwapchainWidth = width;
		ms_xrPointerSwapchainHeight = height;
		bridgeLog("{\"event\":\"openxrPointerSwapchain\",\"ready\":true,\"width\":%u,\"height\":%u,\"format\":%I64d,\"srgb\":%s,\"images\":%u}", width, height, format, srgb ? "true" : "false", imageCount);
		return true;
	}

	bool ensureObjectContextSwapchain()
	{
		if (ms_xrObjectContextSwapchain)
			return true;
		if (!ms_xrCreateSwapchain || !ms_xrEnumerateSwapchainImages || !ms_xrSession)
			return false;

		unsigned int const width = static_cast<unsigned int>(clampFloat(getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_TEXTURE_WIDTH", 1536.0f), 512.0f, 2048.0f));
		unsigned int const height = static_cast<unsigned int>(clampFloat(getEnvironmentFloat("SWG_OG_VR_OBJECT_CONTEXT_TEXTURE_HEIGHT", 768.0f), 256.0f, 2048.0f));
		long long format = 0;
		bool srgb = false;
		if (!chooseR8G8B8A8SwapchainFormat("openxrObjectContextSwapchain", format, srgb))
			return false;

		XrSwapchainCreateInfoLocal swapchainInfo;
		ZeroMemory(&swapchainInfo, sizeof(swapchainInfo));
		swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO_LOCAL;
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT_LOCAL | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT_LOCAL | XR_SWAPCHAIN_USAGE_SAMPLED_BIT_LOCAL;
		swapchainInfo.format = format;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = width;
		swapchainInfo.height = height;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;

		XrResult const createResult = ms_xrCreateSwapchain(ms_xrSession, &swapchainInfo, &ms_xrObjectContextSwapchain);
		if (createResult != XR_SUCCESS_LOCAL || !ms_xrObjectContextSwapchain)
		{
			bridgeLog("{\"event\":\"openxrObjectContextSwapchain\",\"ready\":false,\"failure\":\"create\",\"result\":%d,\"format\":%I64d,\"srgb\":%s}", createResult, format, srgb ? "true" : "false");
			return false;
		}

		unsigned int imageCount = 0;
		XrResult imageResult = ms_xrEnumerateSwapchainImages(ms_xrObjectContextSwapchain, 0, &imageCount, 0);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrObjectContextSwapchain\",\"ready\":false,\"failure\":\"image-count\",\"result\":%d}", imageResult);
			return false;
		}

		ms_xrObjectContextImages.resize(imageCount);
		ms_xrObjectContextRenderTargetViews.resize(imageCount, 0);
		for (unsigned int i = 0; i < imageCount; ++i)
		{
			ZeroMemory(&ms_xrObjectContextImages[i], sizeof(ms_xrObjectContextImages[i]));
			ms_xrObjectContextImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR_LOCAL;
		}

		imageResult = ms_xrEnumerateSwapchainImages(ms_xrObjectContextSwapchain, imageCount, &imageCount, &ms_xrObjectContextImages[0]);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrObjectContextSwapchain\",\"ready\":false,\"failure\":\"images\",\"result\":%d}", imageResult);
			return false;
		}

		for (unsigned int i = 0; i < imageCount; ++i)
		{
			ID3D11Texture2D * const texture = ms_xrObjectContextImages[i].texture;
			if (!texture)
				return false;

			D3D11_TEXTURE2D_DESC textureDesc;
			texture->GetDesc(&textureDesc);

			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
			ZeroMemory(&rtvDesc, sizeof(rtvDesc));
			rtvDesc.Format = textureDesc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM : textureDesc.Format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Texture2D.MipSlice = 0;

			HRESULT const hr = ms_device->CreateRenderTargetView(texture, &rtvDesc, &ms_xrObjectContextRenderTargetViews[i]);
			if (FAILED(hr))
			{
				bridgeLog("{\"event\":\"openxrObjectContextSwapchain\",\"ready\":false,\"failure\":\"create-rtv\",\"result\":\"0x%08x\",\"image\":%u}", static_cast<unsigned int>(hr), i);
				return false;
			}
		}

		ms_xrObjectContextSwapchainWidth = width;
		ms_xrObjectContextSwapchainHeight = height;
		bridgeLog("{\"event\":\"openxrObjectContextSwapchain\",\"ready\":true,\"width\":%u,\"height\":%u,\"format\":%I64d,\"srgb\":%s,\"images\":%u}", width, height, format, srgb ? "true" : "false", imageCount);
		return true;
	}

	bool ensureHoverTargetSwapchain()
	{
		if (ms_xrHoverTargetSwapchain)
			return true;
		if (!ms_xrCreateSwapchain || !ms_xrEnumerateSwapchainImages || !ms_xrSession)
			return false;

		unsigned int const width = static_cast<unsigned int>(clampFloat(getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_TEXTURE_WIDTH", 1536.0f), 512.0f, 2048.0f));
		unsigned int const height = static_cast<unsigned int>(clampFloat(getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_TEXTURE_HEIGHT", 768.0f), 256.0f, 2048.0f));
		long long format = 0;
		bool srgb = false;
		if (!chooseR8G8B8A8SwapchainFormat("openxrHoverTargetSwapchain", format, srgb))
			return false;

		XrSwapchainCreateInfoLocal swapchainInfo;
		ZeroMemory(&swapchainInfo, sizeof(swapchainInfo));
		swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO_LOCAL;
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT_LOCAL | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT_LOCAL | XR_SWAPCHAIN_USAGE_SAMPLED_BIT_LOCAL;
		swapchainInfo.format = format;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = width;
		swapchainInfo.height = height;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;

		XrResult const createResult = ms_xrCreateSwapchain(ms_xrSession, &swapchainInfo, &ms_xrHoverTargetSwapchain);
		if (createResult != XR_SUCCESS_LOCAL || !ms_xrHoverTargetSwapchain)
		{
			bridgeLog("{\"event\":\"openxrHoverTargetSwapchain\",\"ready\":false,\"failure\":\"create\",\"result\":%d,\"format\":%I64d,\"srgb\":%s}", createResult, format, srgb ? "true" : "false");
			return false;
		}

		unsigned int imageCount = 0;
		XrResult imageResult = ms_xrEnumerateSwapchainImages(ms_xrHoverTargetSwapchain, 0, &imageCount, 0);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrHoverTargetSwapchain\",\"ready\":false,\"failure\":\"image-count\",\"result\":%d}", imageResult);
			return false;
		}

		ms_xrHoverTargetImages.resize(imageCount);
		for (unsigned int i = 0; i < imageCount; ++i)
		{
			ZeroMemory(&ms_xrHoverTargetImages[i], sizeof(ms_xrHoverTargetImages[i]));
			ms_xrHoverTargetImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR_LOCAL;
		}

		imageResult = ms_xrEnumerateSwapchainImages(ms_xrHoverTargetSwapchain, imageCount, &imageCount, &ms_xrHoverTargetImages[0]);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrHoverTargetSwapchain\",\"ready\":false,\"failure\":\"images\",\"result\":%d}", imageResult);
			return false;
		}

		ms_xrHoverTargetSwapchainWidth = width;
		ms_xrHoverTargetSwapchainHeight = height;
		bridgeLog("{\"event\":\"openxrHoverTargetSwapchain\",\"ready\":true,\"width\":%u,\"height\":%u,\"format\":%I64d,\"srgb\":%s,\"images\":%u}", width, height, format, srgb ? "true" : "false", imageCount);
		return true;
	}

	bool ensureWristDashboardSwapchain()
	{
		if (ms_xrWristDashboardSwapchain)
			return true;
		if (!ms_xrCreateSwapchain || !ms_xrEnumerateSwapchainImages || !ms_xrSession)
			return false;

		unsigned int const width = static_cast<unsigned int>(clampFloat(getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_TEXTURE_WIDTH", 1536.0f), 512.0f, 2048.0f));
		unsigned int const height = static_cast<unsigned int>(clampFloat(getEnvironmentFloat("SWG_OG_VR_WRIST_DASHBOARD_TEXTURE_HEIGHT", 768.0f), 256.0f, 2048.0f));
		long long format = 0;
		bool srgb = false;
		if (!chooseR8G8B8A8SwapchainFormat("openxrWristDashboardSwapchain", format, srgb))
			return false;

		XrSwapchainCreateInfoLocal swapchainInfo;
		ZeroMemory(&swapchainInfo, sizeof(swapchainInfo));
		swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO_LOCAL;
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT_LOCAL | XR_SWAPCHAIN_USAGE_SAMPLED_BIT_LOCAL;
		swapchainInfo.format = format;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = width;
		swapchainInfo.height = height;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;

		XrResult const createResult = ms_xrCreateSwapchain(ms_xrSession, &swapchainInfo, &ms_xrWristDashboardSwapchain);
		if (createResult != XR_SUCCESS_LOCAL || !ms_xrWristDashboardSwapchain)
		{
			bridgeLog("{\"event\":\"openxrWristDashboardSwapchain\",\"ready\":false,\"failure\":\"create\",\"result\":%d,\"format\":%I64d,\"srgb\":%s}", createResult, format, srgb ? "true" : "false");
			return false;
		}

		unsigned int imageCount = 0;
		XrResult imageResult = ms_xrEnumerateSwapchainImages(ms_xrWristDashboardSwapchain, 0, &imageCount, 0);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrWristDashboardSwapchain\",\"ready\":false,\"failure\":\"image-count\",\"result\":%d}", imageResult);
			return false;
		}

		ms_xrWristDashboardImages.resize(imageCount);
		for (unsigned int i = 0; i < imageCount; ++i)
		{
			ZeroMemory(&ms_xrWristDashboardImages[i], sizeof(ms_xrWristDashboardImages[i]));
			ms_xrWristDashboardImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR_LOCAL;
		}

		imageResult = ms_xrEnumerateSwapchainImages(ms_xrWristDashboardSwapchain, imageCount, &imageCount, &ms_xrWristDashboardImages[0]);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
		{
			bridgeLog("{\"event\":\"openxrWristDashboardSwapchain\",\"ready\":false,\"failure\":\"images\",\"result\":%d}", imageResult);
			return false;
		}

		D3D11_TEXTURE2D_DESC captureDesc;
		ZeroMemory(&captureDesc, sizeof(captureDesc));
		captureDesc.Width = width;
		captureDesc.Height = height;
		captureDesc.MipLevels = 1;
		captureDesc.ArraySize = 1;
		captureDesc.Format = static_cast<DXGI_FORMAT>(format);
		captureDesc.SampleDesc.Count = 1;
		captureDesc.SampleDesc.Quality = 0;
		captureDesc.Usage = D3D11_USAGE_DEFAULT;
		captureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		HRESULT captureHr = ms_device->CreateTexture2D(&captureDesc, 0, &ms_xrWristDashboardCaptureTexture);
		if (FAILED(captureHr) && captureDesc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
		{
			captureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			captureHr = ms_device->CreateTexture2D(&captureDesc, 0, &ms_xrWristDashboardCaptureTexture);
		}
		if (FAILED(captureHr) || !ms_xrWristDashboardCaptureTexture)
		{
			bridgeLog("{\"event\":\"openxrWristDashboardSwapchain\",\"ready\":false,\"failure\":\"create-capture-texture\",\"result\":\"0x%08x\",\"format\":%I64d}", static_cast<unsigned int>(captureHr), format);
			return false;
		}
		HRESULT const captureRtvHr = ms_device->CreateRenderTargetView(ms_xrWristDashboardCaptureTexture, 0, &ms_xrWristDashboardCaptureRenderTargetView);
		if (FAILED(captureRtvHr) || !ms_xrWristDashboardCaptureRenderTargetView)
		{
			bridgeLog("{\"event\":\"openxrWristDashboardSwapchain\",\"ready\":false,\"failure\":\"create-capture-rtv\",\"result\":\"0x%08x\",\"format\":%u}", static_cast<unsigned int>(captureRtvHr), static_cast<unsigned int>(captureDesc.Format));
			return false;
		}

		ms_xrWristDashboardSwapchainWidth = width;
		ms_xrWristDashboardSwapchainHeight = height;
		bridgeLog("{\"event\":\"openxrWristDashboardSwapchain\",\"ready\":true,\"width\":%u,\"height\":%u,\"format\":%I64d,\"srgb\":%s,\"images\":%u}", width, height, format, srgb ? "true" : "false", imageCount);
		return true;
	}

	bool ensureWandSwapchain()
	{
		if (ms_xrWandSwapchain)
			return true;
		if (!ms_xrCreateSwapchain || !ms_xrEnumerateSwapchainImages || !ms_xrSession)
			return false;

		unsigned int const width = 32;
		unsigned int const height = 128;
		long long format = 0;
		bool srgb = false;
		if (!chooseR8G8B8A8SwapchainFormat("openxrWandSwapchain", format, srgb))
			return false;
		XrSwapchainCreateInfoLocal swapchainInfo;
		ZeroMemory(&swapchainInfo, sizeof(swapchainInfo));
		swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO_LOCAL;
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT_LOCAL | XR_SWAPCHAIN_USAGE_SAMPLED_BIT_LOCAL;
		swapchainInfo.format = format;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = width;
		swapchainInfo.height = height;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;

		XrResult const createResult = ms_xrCreateSwapchain(ms_xrSession, &swapchainInfo, &ms_xrWandSwapchain);
		if (createResult != XR_SUCCESS_LOCAL || !ms_xrWandSwapchain)
		{
			bridgeLog("{\"event\":\"openxrWandSwapchain\",\"ready\":false,\"failure\":\"create\",\"result\":%d,\"format\":%I64d,\"srgb\":%s}", createResult, format, srgb ? "true" : "false");
			return false;
		}

		unsigned int imageCount = 0;
		XrResult imageResult = ms_xrEnumerateSwapchainImages(ms_xrWandSwapchain, 0, &imageCount, 0);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
			return false;

		ms_xrWandImages.resize(imageCount);
		for (unsigned int i = 0; i < imageCount; ++i)
		{
			ZeroMemory(&ms_xrWandImages[i], sizeof(ms_xrWandImages[i]));
			ms_xrWandImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR_LOCAL;
		}
		imageResult = ms_xrEnumerateSwapchainImages(ms_xrWandSwapchain, imageCount, &imageCount, &ms_xrWandImages[0]);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
			return false;

		ms_xrWandSwapchainWidth = width;
		ms_xrWandSwapchainHeight = height;
		bridgeLog("{\"event\":\"openxrWandSwapchain\",\"ready\":true,\"width\":%u,\"height\":%u,\"format\":%I64d,\"srgb\":%s,\"images\":%u}", width, height, format, srgb ? "true" : "false", imageCount);
		return true;
	}

	bool ensureReticleSwapchain()
	{
		if (ms_xrReticleSwapchain)
			return true;
		if (!ms_xrCreateSwapchain || !ms_xrEnumerateSwapchainImages || !ms_xrSession)
			return false;

		unsigned int const width = 32;
		unsigned int const height = 32;
		long long format = 0;
		bool srgb = false;
		if (!chooseR8G8B8A8SwapchainFormat("openxrReticleSwapchain", format, srgb))
			return false;
		XrSwapchainCreateInfoLocal swapchainInfo;
		ZeroMemory(&swapchainInfo, sizeof(swapchainInfo));
		swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO_LOCAL;
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT_LOCAL | XR_SWAPCHAIN_USAGE_SAMPLED_BIT_LOCAL;
		swapchainInfo.format = format;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = width;
		swapchainInfo.height = height;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;

		XrResult const createResult = ms_xrCreateSwapchain(ms_xrSession, &swapchainInfo, &ms_xrReticleSwapchain);
		if (createResult != XR_SUCCESS_LOCAL || !ms_xrReticleSwapchain)
		{
			bridgeLog("{\"event\":\"openxrReticleSwapchain\",\"ready\":false,\"failure\":\"create\",\"result\":%d,\"format\":%I64d,\"srgb\":%s}", createResult, format, srgb ? "true" : "false");
			return false;
		}

		unsigned int imageCount = 0;
		XrResult imageResult = ms_xrEnumerateSwapchainImages(ms_xrReticleSwapchain, 0, &imageCount, 0);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
			return false;

		ms_xrReticleImages.resize(imageCount);
		for (unsigned int i = 0; i < imageCount; ++i)
		{
			ZeroMemory(&ms_xrReticleImages[i], sizeof(ms_xrReticleImages[i]));
			ms_xrReticleImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR_LOCAL;
		}
		imageResult = ms_xrEnumerateSwapchainImages(ms_xrReticleSwapchain, imageCount, &imageCount, &ms_xrReticleImages[0]);
		if (imageResult != XR_SUCCESS_LOCAL || imageCount == 0)
			return false;

		ms_xrReticleSwapchainWidth = width;
		ms_xrReticleSwapchainHeight = height;
		bridgeLog("{\"event\":\"openxrReticleSwapchain\",\"ready\":true,\"width\":%u,\"height\":%u,\"format\":%I64d,\"srgb\":%s,\"images\":%u}", width, height, format, srgb ? "true" : "false", imageCount);
		return true;
	}

	void pollOpenXrEvents()
	{
		if (!ms_xrPollEvent || !ms_xrInstance)
			return;

		for (;;)
		{
			XrEventDataBufferLocal eventData;
			ZeroMemory(&eventData, sizeof(eventData));
			eventData.type = XR_TYPE_EVENT_DATA_BUFFER_LOCAL;
			XrResult const result = ms_xrPollEvent(ms_xrInstance, &eventData);
			if (result == XR_EVENT_UNAVAILABLE_LOCAL)
				break;
			if (result != XR_SUCCESS_LOCAL)
			{
				bridgeLog("{\"event\":\"openxrPollEvent\",\"result\":%d}", result);
				break;
			}

			if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED_LOCAL)
			{
				XrEventDataSessionStateChangedLocal const *const stateEvent = reinterpret_cast<XrEventDataSessionStateChangedLocal const *>(&eventData);
				ms_xrSessionState = stateEvent->state;
				ms_xrSessionRunning = ms_xrSessionState != XR_SESSION_STATE_STOPPING_LOCAL && ms_xrSessionState != XR_SESSION_STATE_EXITING_LOCAL;
				bridgeLog("{\"event\":\"openxrSessionState\",\"state\":%d}", ms_xrSessionState);
			}
			else if (eventData.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING_LOCAL)
			{
				XrEventDataReferenceSpaceChangePendingLocal const *const spaceEvent = reinterpret_cast<XrEventDataReferenceSpaceChangePendingLocal const *>(&eventData);
				bridgeLog("{\"event\":\"openxrReferenceSpaceChangePending\",\"space\":\"%s\",\"poseValid\":%s,\"changeTime\":%I64d}", referenceSpaceName(spaceEvent->referenceSpaceType), openXrBoolString(spaceEvent->poseValid) ? "true" : "false", static_cast<long long>(spaceEvent->changeTime));
				if (!ms_xrSession || spaceEvent->session == ms_xrSession)
					invalidateVrSpatialAnchors("reference-space-change");
			}
		}
	}

	bool ensureSessionBegun()
	{
		if (ms_xrSessionBegun)
			return true;
		if (!ms_xrBeginSession || !ms_xrSession)
			return false;

		pollOpenXrEvents();
		if (ms_xrSessionState != XR_SESSION_STATE_READY_LOCAL)
			return false;

		XrSessionBeginInfoLocal beginInfo;
		ZeroMemory(&beginInfo, sizeof(beginInfo));
		beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO_LOCAL;
		beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_LOCAL;
		XrResult const result = ms_xrBeginSession(ms_xrSession, &beginInfo);
		if (result != XR_SUCCESS_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrBeginSession\",\"ready\":false,\"result\":%d}", result);
			return false;
		}

		ms_xrSessionBegun = true;
		ms_xrSessionRunning = true;
		bridgeLog("{\"event\":\"openxrBeginSession\",\"ready\":true}");
		return true;
	}

	bool captureAnchoredQuadPose(XrTime displayTime)
	{
		if (ms_xrQuadPoseCaptured)
			return true;
		if (!ms_xrLocateViews || !ms_xrSession || !ms_xrLocalSpace)
			return false;

		XrViewLocateInfoLocal locateInfo;
		ZeroMemory(&locateInfo, sizeof(locateInfo));
		locateInfo.type = XR_TYPE_VIEW_LOCATE_INFO_LOCAL;
		locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_LOCAL;
		locateInfo.displayTime = displayTime;
		locateInfo.space = ms_xrLocalSpace;

		XrViewStateLocal viewState;
		ZeroMemory(&viewState, sizeof(viewState));
		viewState.type = XR_TYPE_VIEW_STATE_LOCAL;

		XrViewLocal views[2];
		ZeroMemory(views, sizeof(views));
		views[0].type = XR_TYPE_VIEW_LOCAL;
		views[1].type = XR_TYPE_VIEW_LOCAL;
		unsigned int viewCount = 0;
		XrResult const locateResult = ms_xrLocateViews(ms_xrSession, &locateInfo, &viewState, 2, &viewCount, views);
		if (locateResult != XR_SUCCESS_LOCAL || viewCount == 0)
		{
			if (!ms_xrQuadAnchorFailureLogged)
			{
				bridgeLog("{\"event\":\"openxrQuadAnchor\",\"ready\":false,\"failure\":\"locateViews\",\"result\":%d,\"views\":%u}", locateResult, viewCount);
				ms_xrQuadAnchorFailureLogged = true;
			}
			return false;
		}
		if ((viewState.viewStateFlags & (XR_VIEW_STATE_ORIENTATION_VALID_BIT_LOCAL | XR_VIEW_STATE_POSITION_VALID_BIT_LOCAL)) != (XR_VIEW_STATE_ORIENTATION_VALID_BIT_LOCAL | XR_VIEW_STATE_POSITION_VALID_BIT_LOCAL))
		{
			if (!ms_xrQuadAnchorFailureLogged)
			{
				bridgeLog("{\"event\":\"openxrQuadAnchor\",\"ready\":false,\"failure\":\"invalid-view-state\",\"flags\":\"0x%I64x\"}", static_cast<unsigned __int64>(viewState.viewStateFlags));
				ms_xrQuadAnchorFailureLogged = true;
			}
			return false;
		}

		XrPosefLocal headPose = views[0].pose;
		if (viewCount > 1)
		{
			headPose.position.x = (views[0].pose.position.x + views[1].pose.position.x) * 0.5f;
			headPose.position.y = (views[0].pose.position.y + views[1].pose.position.y) * 0.5f;
			headPose.position.z = (views[0].pose.position.z + views[1].pose.position.z) * 0.5f;
		}
		setHandHeadPose(headPose);

		float const distanceMeters = getEnvironmentFloat("SWG_OG_VR_QUAD_DISTANCE_METERS", 1.75f);
		float const yawDegrees = getEnvironmentFloat("SWG_OG_VR_QUAD_YAW_DEGREES", 0.0f);
		float const yawRadians = yawDegrees * 0.01745329252f;
		float const lateralOffsetMeters = getEnvironmentFloat("SWG_OG_VR_QUAD_LATERAL_OFFSET_METERS", 0.0f);
		float const verticalOffsetMeters = getEnvironmentFloat("SWG_OG_VR_QUAD_VERTICAL_OFFSET_METERS", 0.0f);
		XrVector3fLocal const forward = headsetForward(headPose.orientation);
		XrVector3fLocal const quadForward = rotateForwardByYaw(forward, yawRadians);
		XrVector3fLocal const quadRight = rightFromForward(quadForward);
		ms_xrQuadPose = identityPose();
		ms_xrQuadPose.position.x = headPose.position.x + quadForward.x * distanceMeters + quadRight.x * lateralOffsetMeters;
		ms_xrQuadPose.position.y = headPose.position.y + verticalOffsetMeters;
		ms_xrQuadPose.position.z = headPose.position.z + quadForward.z * distanceMeters + quadRight.z * lateralOffsetMeters;
		ms_xrQuadPose.orientation = quadOrientationFacingHead(quadForward);
		ms_xrQuadHeadPose = headPose;
		ms_xrLoadingHeadPose = headPose;
		ms_xrQuadHeadPoseCaptured = true;
		ms_xrLoadingHeadPoseCaptured = true;
		ms_xrQuadPoseCaptured = true;
		bridgeLog("{\"event\":\"openxrQuadAnchor\",\"ready\":true,\"anchored\":true,\"space\":\"%s\",\"views\":%u,\"distanceMeters\":%0.3f,\"yawDegrees\":%0.3f,\"lateralOffsetMeters\":%0.3f,\"verticalOffsetMeters\":%0.3f,\"head\":[%0.3f,%0.3f,%0.3f],\"forward\":[%0.3f,%0.3f,%0.3f],\"quadForward\":[%0.3f,%0.3f,%0.3f],\"quad\":[%0.3f,%0.3f,%0.3f],\"orientation\":[%0.3f,%0.3f,%0.3f,%0.3f]}",
		referenceSpaceName(ms_xrReferenceSpaceType),
		viewCount,
		distanceMeters,
		yawDegrees,
		lateralOffsetMeters,
		verticalOffsetMeters,
		headPose.position.x,
		headPose.position.y,
		headPose.position.z,
		forward.x,
		forward.y,
		forward.z,
		quadForward.x,
		quadForward.y,
		quadForward.z,
		ms_xrQuadPose.position.x,
		ms_xrQuadPose.position.y,
		ms_xrQuadPose.position.z,
		ms_xrQuadPose.orientation.x,
		ms_xrQuadPose.orientation.y,
		ms_xrQuadPose.orientation.z,
		ms_xrQuadPose.orientation.w);
		return true;
	}

	bool copyBackBufferToQuadSwapchain(ID3D11Texture2D *backBuffer)
	{
		if (!ms_openXrFrameSubmitReady || !backBuffer || !ensureQuadSwapchain(backBuffer))
			return false;

		unsigned int imageIndex = 0;
		XrSwapchainImageAcquireInfoLocal acquireInfo;
		ZeroMemory(&acquireInfo, sizeof(acquireInfo));
		acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL;
		XrResult const acquireResult = ms_xrAcquireSwapchainImage(ms_xrQuadSwapchain, &acquireInfo, &imageIndex);
		bool imageReady = acquireResult == XR_SUCCESS_LOCAL && imageIndex < ms_xrQuadImages.size();
		if (imageReady)
		{
			XrSwapchainImageWaitInfoLocal imageWaitInfo;
			ZeroMemory(&imageWaitInfo, sizeof(imageWaitInfo));
			imageWaitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL;
			imageWaitInfo.timeout = XR_INFINITE_DURATION_LOCAL;
			XrResult const imageWaitResult = ms_xrWaitSwapchainImage(ms_xrQuadSwapchain, &imageWaitInfo);
			imageReady = imageWaitResult == XR_SUCCESS_LOCAL;
			if (imageReady && ms_xrQuadImages[imageIndex].texture)
			{
				ms_context->CopyResource(ms_xrQuadImages[imageIndex].texture, backBuffer);
			}

			XrSwapchainImageReleaseInfoLocal releaseInfo;
			ZeroMemory(&releaseInfo, sizeof(releaseInfo));
			releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
			ms_xrReleaseSwapchainImage(ms_xrQuadSwapchain, &releaseInfo);
		}

		return imageReady;
	}

	bool prepareBackBufferQuadLayer(ID3D11Texture2D *backBuffer, XrCompositionLayerQuadLocal &quadLayer)
	{
		if (!backBuffer || !ensureQuadSwapchain(backBuffer))
			return false;

		bool const imageReady = copyBackBufferToQuadSwapchain(backBuffer);
		if (!imageReady)
			return false;

		float const widthMeters = getEnvironmentFloat("SWG_OG_VR_QUAD_WIDTH_METERS", 1.65f);
		float const stageHeightMeters = getEnvironmentFloat("SWG_OG_VR_QUAD_HEIGHT_METERS", 1.45f);
		float const aspect = ms_xrQuadSwapchainWidth > 0 ? static_cast<float>(ms_xrQuadSwapchainHeight) / static_cast<float>(ms_xrQuadSwapchainWidth) : 0.75f;
		ZeroMemory(&quadLayer, sizeof(quadLayer));
		quadLayer.type = XR_TYPE_COMPOSITION_LAYER_QUAD_LOCAL;
		quadLayer.space = ms_xrLocalSpace;
		quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH_LOCAL;
		quadLayer.subImage.swapchain = ms_xrQuadSwapchain;
		quadLayer.subImage.imageRect.offset.x = 0;
		quadLayer.subImage.imageRect.offset.y = 0;
		quadLayer.subImage.imageRect.extent.width = static_cast<int>(ms_xrQuadSwapchainWidth);
		quadLayer.subImage.imageRect.extent.height = static_cast<int>(ms_xrQuadSwapchainHeight);
		quadLayer.pose = ms_xrQuadPoseCaptured ? ms_xrQuadPose : identityPose();
		if (!ms_xrQuadPoseCaptured && ms_xrReferenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE_LOCAL)
			quadLayer.pose.position.y = stageHeightMeters;
		if (!ms_xrQuadPoseCaptured)
		{
			float const distanceMeters = getEnvironmentFloat("SWG_OG_VR_QUAD_DISTANCE_METERS", 1.75f);
			float const yawRadians = getEnvironmentFloat("SWG_OG_VR_QUAD_YAW_DEGREES", 0.0f) * 0.01745329252f;
			float const lateralOffsetMeters = getEnvironmentFloat("SWG_OG_VR_QUAD_LATERAL_OFFSET_METERS", 0.0f);
			float const verticalOffsetMeters = getEnvironmentFloat("SWG_OG_VR_QUAD_VERTICAL_OFFSET_METERS", 0.0f);
			XrVector3fLocal fallbackForward;
			fallbackForward.x = 0.0f;
			fallbackForward.y = 0.0f;
			fallbackForward.z = -1.0f;
			XrVector3fLocal const quadForward = rotateForwardByYaw(fallbackForward, yawRadians);
			XrVector3fLocal const quadRight = rightFromForward(quadForward);
			quadLayer.pose.orientation = quadOrientationFacingHead(quadForward);
			quadLayer.pose.position.x = quadForward.x * distanceMeters + quadRight.x * lateralOffsetMeters;
			quadLayer.pose.position.y += verticalOffsetMeters;
			quadLayer.pose.position.z = quadForward.z * distanceMeters + quadRight.z * lateralOffsetMeters;
		}
		quadLayer.size.width = widthMeters;
		quadLayer.size.height = widthMeters * aspect;
		ms_xrUiQuadPose = quadLayer.pose;
		ms_xrUiQuadWidthMeters = quadLayer.size.width;
		ms_xrUiQuadHeightMeters = quadLayer.size.height;
		ms_xrUiQuadReady = true;
		return true;
	}

	bool submitFrontQuad(ID3D11Texture2D *backBuffer)
	{
		if (!ms_openXrFrameSubmitReady || !backBuffer)
			return false;
		if (!ensureSessionBegun())
			return false;
		pollOpenXrEvents();
		if (!ensureQuadSwapchain(backBuffer))
			return false;

		XrFrameWaitInfoLocal waitInfo;
		ZeroMemory(&waitInfo, sizeof(waitInfo));
		waitInfo.type = XR_TYPE_FRAME_WAIT_INFO_LOCAL;
		XrFrameStateLocal frameState;
		ZeroMemory(&frameState, sizeof(frameState));
		frameState.type = XR_TYPE_FRAME_STATE_LOCAL;
		XrResult const waitResult = ms_xrWaitFrame(ms_xrSession, &waitInfo, &frameState);
		if (waitResult != XR_SUCCESS_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrWaitFrame\",\"result\":%d}", waitResult);
			return false;
		}

		XrFrameBeginInfoLocal beginInfo;
		ZeroMemory(&beginInfo, sizeof(beginInfo));
		beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO_LOCAL;
		XrResult const beginResult = ms_xrBeginFrame(ms_xrSession, &beginInfo);
		if (beginResult != XR_SUCCESS_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrBeginFrame\",\"result\":%d}", beginResult);
			return false;
		}
		(void)captureAnchoredQuadPose(frameState.predictedDisplayTime);
		sampleControllerSnapshot(frameState.predictedDisplayTime);

		bool const tvModeEnabled = isTvModeEnabled();
		if (tvModeEnabled != ms_lastSubmittedTvModeEnabled)
		{
			bridgeLog("{\"event\":\"openxrTvMode\",\"enabled\":%s,\"frame\":%d,\"submit\":%d}", tvModeEnabled ? "true" : "false", ms_frameCount, ms_xrQuadSubmitCount);
			ms_lastSubmittedTvModeEnabled = tvModeEnabled;
		}
		XrCompositionLayerBaseHeaderLocal const *layers[8];
		unsigned int layerCount = 0;
		XrCompositionLayerQuadLocal quadLayer;
		ZeroMemory(&quadLayer, sizeof(quadLayer));
		bool const imageReady = tvModeEnabled ? prepareBackBufferQuadLayer(backBuffer, quadLayer) : false;
		if (imageReady && tvModeEnabled)
		{
			layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&quadLayer);
			++layerCount;
		}
		else if (!tvModeEnabled)
		{
			ms_xrUiQuadReady = false;
			ms_xrUiQuadWidthMeters = 0.0f;
			ms_xrUiQuadHeightMeters = 0.0f;
		}

		XrCompositionLayerQuadLocal handLayers[4];
		unsigned int handLayerCount = 0;
		if (prepareControllerHandLayers(frameState.predictedDisplayTime, handLayers, 4, handLayerCount))
		{
			for (unsigned int i = 0; i < handLayerCount && layerCount < 8; ++i)
			{
				layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&handLayers[i]);
				++layerCount;
			}
		}

		XrCompositionLayerQuadLocal wandLayers[3];
		unsigned int wandLayerCount = 0;
		if (preparePointerLayers(frameState.predictedDisplayTime, wandLayers, 3, wandLayerCount))
		{
			for (unsigned int i = 0; i < wandLayerCount && layerCount < 8; ++i)
			{
				layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&wandLayers[i]);
				++layerCount;
			}
		}

		XrFrameEndInfoLocal endInfo;
		ZeroMemory(&endInfo, sizeof(endInfo));
		endInfo.type = XR_TYPE_FRAME_END_INFO_LOCAL;
		endInfo.displayTime = frameState.predictedDisplayTime;
		endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE_LOCAL;
		endInfo.layerCount = layerCount;
		endInfo.layers = layerCount ? layers : 0;
		XrResult const endResult = ms_xrEndFrame(ms_xrSession, &endInfo);
		if (endResult != XR_SUCCESS_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrEndFrame\",\"result\":%d,\"layers\":%u}", endResult, layerCount);
			return false;
		}

		++ms_xrQuadSubmitCount;
		if (ms_xrQuadSubmitCount == 1 || ms_xrQuadSubmitCount == 30 || ms_xrQuadSubmitCount == 300)
			bridgeLog("{\"event\":\"openxrQuadSubmit\",\"frame\":%d,\"submit\":%d,\"mode\":\"%s\",\"layers\":%u,\"handLayers\":%u,\"wandLayers\":%u,\"imageReady\":%s,\"shouldRender\":%s,\"sessionState\":%d,\"space\":\"%s\"}", ms_frameCount, ms_xrQuadSubmitCount, tvModeEnabled ? "tv" : "world", layerCount, handLayerCount, wandLayerCount, imageReady ? "true" : "false", frameState.shouldRender ? "true" : "false", ms_xrSessionState, referenceSpaceName(ms_xrReferenceSpaceType));

		return layerCount > 0;
	}

	void probeOpenXrD3D11(ID3D11Device *device)
	{
		char loaderPath[MAX_PATH];
		ms_openXrLoader = loadOpenXrLoader(loaderPath, sizeof(loaderPath));
		if (!ms_openXrLoader)
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"failure\":\"loader-not-found\"}");
			return;
		}

		PFN_xrGetInstanceProcAddrLocal getProc = reinterpret_cast<PFN_xrGetInstanceProcAddrLocal>(GetProcAddress(ms_openXrLoader, "xrGetInstanceProcAddr"));
		if (!getProc)
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"failure\":\"xrGetInstanceProcAddr-missing\"}", loaderPath);
			return;
		}

		PFN_xrEnumerateInstanceExtensionPropertiesLocal enumerateExtensions = 0;
		if (!getXrProc(getProc, XR_NULL_HANDLE_LOCAL, "xrEnumerateInstanceExtensionProperties", enumerateExtensions))
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"failure\":\"xrEnumerateInstanceExtensionProperties-missing\"}", loaderPath);
			return;
		}

		unsigned int extensionCount = 0;
		bool d3d11ExtensionSupported = false;
		bool handTrackingExtensionSupported = false;
		if (enumerateExtensions(0, 0, &extensionCount, 0) == XR_SUCCESS_LOCAL && extensionCount > 0)
		{
			std::vector<XrExtensionPropertiesLocal> extensions(extensionCount);
			for (unsigned int i = 0; i < extensionCount; ++i)
			{
				ZeroMemory(&extensions[i], sizeof(extensions[i]));
				extensions[i].type = XR_TYPE_EXTENSION_PROPERTIES_LOCAL;
			}
			if (enumerateExtensions(0, extensionCount, &extensionCount, &extensions[0]) == XR_SUCCESS_LOCAL)
			{
				for (unsigned int i = 0; i < extensionCount; ++i)
				{
					if (strcmp(extensions[i].extensionName, "XR_KHR_D3D11_enable") == 0)
						d3d11ExtensionSupported = true;
					if (strcmp(extensions[i].extensionName, "XR_EXT_hand_tracking") == 0)
						handTrackingExtensionSupported = true;
				}
			}
		}

		if (!d3d11ExtensionSupported)
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"extensions\":%u,\"failure\":\"XR_KHR_D3D11_enable-missing\"}", loaderPath, extensionCount);
			return;
		}

		PFN_xrCreateInstanceLocal createInstance = 0;
		if (!getXrProc(getProc, XR_NULL_HANDLE_LOCAL, "xrCreateInstance", createInstance))
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"failure\":\"xrCreateInstance-missing\"}", loaderPath);
			return;
		}

		char const *enabledExtensions[2];
		unsigned int enabledExtensionCount = 0;
		enabledExtensions[enabledExtensionCount++] = "XR_KHR_D3D11_enable";
		if (handTrackingExtensionSupported)
			enabledExtensions[enabledExtensionCount++] = "XR_EXT_hand_tracking";
		ms_xrHandTrackingExtensionSupported = handTrackingExtensionSupported;
		XrInstanceCreateInfoLocal createInfo;
		ZeroMemory(&createInfo, sizeof(createInfo));
		createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO_LOCAL;
		_snprintf(createInfo.applicationInfo.applicationName, sizeof(createInfo.applicationInfo.applicationName) - 1, "swg_og_client");
		_snprintf(createInfo.applicationInfo.engineName, sizeof(createInfo.applicationInfo.engineName) - 1, "swg-og-d3d11");
		createInfo.applicationInfo.apiVersion = xrMakeVersion(1, 0, 0);
		createInfo.enabledExtensionCount = enabledExtensionCount;
		createInfo.enabledExtensionNames = enabledExtensions;

		XrInstance instance = XR_NULL_HANDLE_LOCAL;
		XrResult const createResult = createInstance(&createInfo, &instance);
		if (createResult != XR_SUCCESS_LOCAL || instance == XR_NULL_HANDLE_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"failure\":\"xrCreateInstance\",\"result\":%d}", loaderPath, createResult);
			return;
		}

		PFN_xrDestroyInstanceLocal destroyInstance = 0;
		PFN_xrGetInstancePropertiesLocal getInstanceProperties = 0;
		PFN_xrGetSystemLocal getSystem = 0;
		PFN_xrGetSystemPropertiesLocal getSystemProperties = 0;
		PFN_xrEnumerateViewConfigurationsLocal enumerateViewConfigurations = 0;
		PFN_xrEnumerateViewConfigurationViewsLocal enumerateViewConfigurationViews = 0;
		PFN_xrGetD3D11GraphicsRequirementsKHRLocal getD3D11Requirements = 0;
		PFN_xrCreateSessionLocal createSession = 0;
		PFN_xrDestroySessionLocal destroySession = 0;
		PFN_xrEnumerateSwapchainFormatsLocal enumerateSwapchainFormats = 0;

		getXrProc(getProc, instance, "xrDestroyInstance", destroyInstance);
		getXrProc(getProc, instance, "xrGetInstanceProperties", getInstanceProperties);
		getXrProc(getProc, instance, "xrGetSystem", getSystem);
		getXrProc(getProc, instance, "xrGetSystemProperties", getSystemProperties);
		getXrProc(getProc, instance, "xrEnumerateViewConfigurations", enumerateViewConfigurations);
		getXrProc(getProc, instance, "xrEnumerateViewConfigurationViews", enumerateViewConfigurationViews);
		getXrProc(getProc, instance, "xrGetD3D11GraphicsRequirementsKHR", getD3D11Requirements);
		getXrProc(getProc, instance, "xrCreateSession", createSession);
		getXrProc(getProc, instance, "xrDestroySession", destroySession);
		getXrProc(getProc, instance, "xrEnumerateSwapchainFormats", enumerateSwapchainFormats);

		char runtimeName[128] = "";
		if (getInstanceProperties)
		{
			XrInstancePropertiesLocal properties;
			ZeroMemory(&properties, sizeof(properties));
			properties.type = XR_TYPE_INSTANCE_PROPERTIES_LOCAL;
			if (getInstanceProperties(instance, &properties) == XR_SUCCESS_LOCAL)
				strncpy(runtimeName, properties.runtimeName, sizeof(runtimeName) - 1);
		}

		XrSystemId systemId = XR_NULL_SYSTEM_ID_LOCAL;
		XrResult systemResult = -1;
		if (getSystem)
		{
			XrSystemGetInfoLocal systemInfo;
			ZeroMemory(&systemInfo, sizeof(systemInfo));
			systemInfo.type = XR_TYPE_SYSTEM_GET_INFO_LOCAL;
			systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY_LOCAL;
			systemResult = getSystem(instance, &systemInfo, &systemId);
		}
		if ((systemResult != XR_SUCCESS_LOCAL || systemId == XR_NULL_SYSTEM_ID_LOCAL) && handTrackingExtensionSupported)
		{
			bridgeLog("{\"event\":\"openxrHandTrackingExtensionRetry\",\"enabled\":false,\"reason\":\"xrGetSystem-HMD\",\"result\":%d}", systemResult);
			if (destroyInstance)
				destroyInstance(instance);

			handTrackingExtensionSupported = false;
			ms_xrHandTrackingExtensionSupported = false;
			ms_xrHandTrackingSystemSupported = false;
			char const *fallbackExtensions[] = { "XR_KHR_D3D11_enable" };
			XrInstanceCreateInfoLocal fallbackCreateInfo;
			ZeroMemory(&fallbackCreateInfo, sizeof(fallbackCreateInfo));
			fallbackCreateInfo.type = XR_TYPE_INSTANCE_CREATE_INFO_LOCAL;
			_snprintf(fallbackCreateInfo.applicationInfo.applicationName, sizeof(fallbackCreateInfo.applicationInfo.applicationName) - 1, "swg_og_client");
			_snprintf(fallbackCreateInfo.applicationInfo.engineName, sizeof(fallbackCreateInfo.applicationInfo.engineName) - 1, "swg-og-d3d11");
			fallbackCreateInfo.applicationInfo.apiVersion = xrMakeVersion(1, 0, 0);
			fallbackCreateInfo.enabledExtensionCount = 1;
			fallbackCreateInfo.enabledExtensionNames = fallbackExtensions;

			instance = XR_NULL_HANDLE_LOCAL;
			XrResult const fallbackCreateResult = createInstance(&fallbackCreateInfo, &instance);
			if (fallbackCreateResult != XR_SUCCESS_LOCAL || instance == XR_NULL_HANDLE_LOCAL)
			{
				bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"failure\":\"xrCreateInstance-without-hand-tracking\",\"result\":%d}", loaderPath, fallbackCreateResult);
				return;
			}

			destroyInstance = 0;
			getInstanceProperties = 0;
			getSystem = 0;
			getSystemProperties = 0;
			enumerateViewConfigurations = 0;
			enumerateViewConfigurationViews = 0;
			getD3D11Requirements = 0;
			createSession = 0;
			destroySession = 0;
			enumerateSwapchainFormats = 0;
			getXrProc(getProc, instance, "xrDestroyInstance", destroyInstance);
			getXrProc(getProc, instance, "xrGetInstanceProperties", getInstanceProperties);
			getXrProc(getProc, instance, "xrGetSystem", getSystem);
			getXrProc(getProc, instance, "xrGetSystemProperties", getSystemProperties);
			getXrProc(getProc, instance, "xrEnumerateViewConfigurations", enumerateViewConfigurations);
			getXrProc(getProc, instance, "xrEnumerateViewConfigurationViews", enumerateViewConfigurationViews);
			getXrProc(getProc, instance, "xrGetD3D11GraphicsRequirementsKHR", getD3D11Requirements);
			getXrProc(getProc, instance, "xrCreateSession", createSession);
			getXrProc(getProc, instance, "xrDestroySession", destroySession);
			getXrProc(getProc, instance, "xrEnumerateSwapchainFormats", enumerateSwapchainFormats);

			runtimeName[0] = '\0';
			if (getInstanceProperties)
			{
				XrInstancePropertiesLocal properties;
				ZeroMemory(&properties, sizeof(properties));
				properties.type = XR_TYPE_INSTANCE_PROPERTIES_LOCAL;
				if (getInstanceProperties(instance, &properties) == XR_SUCCESS_LOCAL)
					strncpy(runtimeName, properties.runtimeName, sizeof(runtimeName) - 1);
			}

			systemId = XR_NULL_SYSTEM_ID_LOCAL;
			systemResult = -1;
			if (getSystem)
			{
				XrSystemGetInfoLocal systemInfo;
				ZeroMemory(&systemInfo, sizeof(systemInfo));
				systemInfo.type = XR_TYPE_SYSTEM_GET_INFO_LOCAL;
				systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY_LOCAL;
				systemResult = getSystem(instance, &systemInfo, &systemId);
			}
		}
		if (systemResult != XR_SUCCESS_LOCAL || systemId == XR_NULL_SYSTEM_ID_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"runtime\":\"%s\",\"failure\":\"xrGetSystem-HMD\",\"result\":%d}", loaderPath, runtimeName, systemResult);
			if (destroyInstance)
				destroyInstance(instance);
			return;
		}

		char systemName[256] = "";
		unsigned int maxLayerCount = 0;
		ms_xrHandTrackingSystemSupported = false;
		if (getSystemProperties)
		{
			XrSystemHandTrackingPropertiesEXTLocal handTrackingProperties;
			ZeroMemory(&handTrackingProperties, sizeof(handTrackingProperties));
			handTrackingProperties.type = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT_LOCAL;
			XrSystemPropertiesLocal properties;
			ZeroMemory(&properties, sizeof(properties));
			properties.type = XR_TYPE_SYSTEM_PROPERTIES_LOCAL;
			if (handTrackingExtensionSupported)
				properties.next = &handTrackingProperties;
			if (getSystemProperties(instance, systemId, &properties) == XR_SUCCESS_LOCAL)
			{
				strncpy(systemName, properties.systemName, sizeof(systemName) - 1);
				maxLayerCount = properties.graphicsProperties.maxLayerCount;
				ms_xrMaxSwapchainImageWidth = properties.graphicsProperties.maxSwapchainImageWidth;
				ms_xrMaxSwapchainImageHeight = properties.graphicsProperties.maxSwapchainImageHeight;
				ms_xrHandTrackingSystemSupported = handTrackingExtensionSupported && handTrackingProperties.supportsHandTracking != 0;
			}
		}

		bool primaryStereo = false;
		unsigned int recommendedWidth = 0;
		unsigned int recommendedHeight = 0;
		if (enumerateViewConfigurations)
		{
			unsigned int viewConfigCount = 0;
			if (enumerateViewConfigurations(instance, systemId, 0, &viewConfigCount, 0) == XR_SUCCESS_LOCAL && viewConfigCount > 0)
			{
				std::vector<XrViewConfigurationType> configs(viewConfigCount);
				if (enumerateViewConfigurations(instance, systemId, viewConfigCount, &viewConfigCount, &configs[0]) == XR_SUCCESS_LOCAL)
				{
					for (unsigned int i = 0; i < viewConfigCount; ++i)
						primaryStereo = primaryStereo || configs[i] == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_LOCAL;
				}
			}
		}

		if (primaryStereo && enumerateViewConfigurationViews)
		{
			XrViewConfigurationViewLocal views[2];
			ZeroMemory(views, sizeof(views));
			views[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW_LOCAL;
			views[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW_LOCAL;
			unsigned int viewCount = 0;
			if (enumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_LOCAL, 2, &viewCount, views) == XR_SUCCESS_LOCAL && viewCount > 0)
			{
				recommendedWidth = views[0].recommendedImageRectWidth;
				recommendedHeight = views[0].recommendedImageRectHeight;
				ms_xrRecommendedEyeWidth = recommendedWidth;
				ms_xrRecommendedEyeHeight = recommendedHeight;
				ms_xrMaxLayerCount = maxLayerCount;
			}
		}

		if (!primaryStereo)
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"runtime\":\"%s\",\"system\":\"%s\",\"failure\":\"primary-stereo-missing\"}", loaderPath, runtimeName, systemName);
			if (destroyInstance)
				destroyInstance(instance);
			return;
		}

		if (!getD3D11Requirements || !createSession || !destroySession || !enumerateSwapchainFormats)
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"runtime\":\"%s\",\"system\":\"%s\",\"failure\":\"session-functions-missing\"}", loaderPath, runtimeName, systemName);
			if (destroyInstance)
				destroyInstance(instance);
			return;
		}

		XrGraphicsRequirementsD3D11KHRLocal requirements;
		ZeroMemory(&requirements, sizeof(requirements));
		requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR_LOCAL;
		XrResult const requirementsResult = getD3D11Requirements(instance, systemId, &requirements);
		if (requirementsResult != XR_SUCCESS_LOCAL)
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"runtime\":\"%s\",\"system\":\"%s\",\"failure\":\"xrGetD3D11GraphicsRequirementsKHR\",\"result\":%d}", loaderPath, runtimeName, systemName, requirementsResult);
			if (destroyInstance)
				destroyInstance(instance);
			return;
		}

		LUID deviceLuid;
		ZeroMemory(&deviceLuid, sizeof(deviceLuid));
		bool const hasDeviceLuid = d3dDeviceAdapterLuid(device, deviceLuid);
		bool const adapterMatches = hasDeviceLuid && sameLuid(deviceLuid, requirements.adapterLuid);

		XrGraphicsBindingD3D11KHRLocal graphicsBinding;
		ZeroMemory(&graphicsBinding, sizeof(graphicsBinding));
		graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_D3D11_KHR_LOCAL;
		graphicsBinding.device = device;

		XrSessionCreateInfoLocal sessionInfo;
		ZeroMemory(&sessionInfo, sizeof(sessionInfo));
		sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO_LOCAL;
		sessionInfo.next = &graphicsBinding;
		sessionInfo.systemId = systemId;

		XrSession session = 0;
		XrResult const sessionResult = createSession(instance, &sessionInfo, &session);
		if (sessionResult != XR_SUCCESS_LOCAL || !session)
		{
			bridgeLog("{\"event\":\"openxrProbe\",\"ready\":false,\"loader\":\"%s\",\"runtime\":\"%s\",\"system\":\"%s\",\"adapterMatches\":%s,\"failure\":\"xrCreateSession-D3D11\",\"result\":%d}", loaderPath, runtimeName, systemName, adapterMatches ? "true" : "false", sessionResult);
			if (destroyInstance)
				destroyInstance(instance);
			return;
		}

		unsigned int swapchainFormatCount = 0;
		XrResult const formatsResult = enumerateSwapchainFormats(session, 0, &swapchainFormatCount, 0);
		ms_openXrProbeReady = formatsResult == XR_SUCCESS_LOCAL && swapchainFormatCount > 0;

		bridgeLog("{\"event\":\"openxrProbe\",\"ready\":%s,\"loader\":\"%s\",\"runtime\":\"%s\",\"system\":\"%s\",\"systemId\":%I64u,\"primaryStereo\":true,\"recommendedWidth\":%u,\"recommendedHeight\":%u,\"maxLayerCount\":%u,\"adapterMatches\":%s,\"swapchainFormatCount\":%u,\"formatsResult\":%d,\"handTrackingExtension\":%s,\"handTrackingSystem\":%s}",
			ms_openXrProbeReady ? "true" : "false",
			loaderPath,
			runtimeName,
			systemName,
			systemId,
			recommendedWidth,
			recommendedHeight,
			maxLayerCount,
			adapterMatches ? "true" : "false",
			swapchainFormatCount,
			formatsResult,
			ms_xrHandTrackingExtensionSupported ? "true" : "false",
			ms_xrHandTrackingSystemSupported ? "true" : "false");
		if (!ms_openXrProbeReady)
		{
			destroySession(session);
			if (destroyInstance)
				destroyInstance(instance);
			return;
		}

		ms_xrDestroyInstance = destroyInstance;
		ms_xrDestroySession = destroySession;
		ms_xrEnumerateSwapchainFormats = enumerateSwapchainFormats;
		if (!loadOpenXrFrameFunctions(getProc, instance))
		{
			bridgeLog("{\"event\":\"openxrFrameFunctions\",\"ready\":false}");
			destroyOpenXrObjects();
			return;
		}

		ms_xrInstance = instance;
		ms_xrSystemId = systemId;
		ms_xrSession = session;
		ms_xrSessionState = 0;
		ms_xrQuadSubmitCount = 0;
		(void)createControllerActions();
		(void)createHandTrackers();
		ms_openXrFrameSubmitReady = createPresentationSpace();
		bridgeLog("{\"event\":\"openxrFrameBridge\",\"ready\":%s,\"quadMode\":\"front\"}", ms_openXrFrameSubmitReady ? "true" : "false");
	}

	template <typename T>
	void releaseCom(T *&object)
	{
		if (object)
		{
			object->Release();
			object = 0;
		}
	}
}

// ======================================================================

bool Direct3d11_VrBridge::isEnabled()
{
	static bool initialized = false;
	static bool enabled = false;
	if (!initialized)
	{
		enabled = getEnvironmentFlag("SWG_OG_VR") || getEnvironmentFlag("SWG_D3D11_VR");
		initialized = true;
	}

	return enabled;
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::install(ID3D11Device *device, ID3D11DeviceContext *context, HWND window, VrUiInputFunction vrUiInput, VrMouseInputFunction vrMouseInput, VrControllerInputFunction vrControllerInput, VrMenuInputFunction vrMenuInput, bool tvModeEnabled)
{
	if (!isEnabled() || ms_installed)
		return;

	ms_device = device;
	ms_context = context;
	ms_window = window;
	ms_vrUiInput = vrUiInput;
	ms_vrMouseInput = vrMouseInput;
	ms_vrControllerInput = vrControllerInput;
	ms_vrMenuInput = vrMenuInput;
	ms_vrTvModeEnabled = tvModeEnabled;
	ms_lastSubmittedTvModeEnabled = tvModeEnabled;
	if (ms_device)
		ms_device->AddRef();
	if (ms_context)
		ms_context->AddRef();

		ms_frameCount = 0;
		ms_backBufferSubmitCount = 0;
		ms_pointerRaySubmitCount = 0;
	ms_contextPanelAnchorName[0] = '\0';
	ms_contextPanelAnchorValid = false;
	ms_contextPanelAnchorFrame = 0;
	ms_contextPanelAnchorPosition.x = 0.0f;
	ms_contextPanelAnchorPosition.y = 0.0f;
	ms_contextPanelAnchorPosition.z = 0.0f;
	ms_contextPanelAnchorRadius = 0.5f;
	ms_xrObjectContextCaptureImageIndex = 0;
	ms_xrObjectContextCaptureImageAcquired = false;
	ms_xrObjectContextCaptureFrame = 0;
	ms_xrWristDashboardCaptureImageIndex = 0;
	ms_xrWristDashboardCaptureImageAcquired = false;
	ms_xrWristDashboardCaptureFrame = 0;
	ms_hoverTargetAnchorValid = false;
	ms_hoverTargetAnchorFrame = 0;
	ms_hoverTargetAnchorPosition.x = 0.0f;
	ms_hoverTargetAnchorPosition.y = 0.0f;
	ms_hoverTargetAnchorPosition.z = 0.0f;
	ms_hoverTargetAnchorRadius = 0.5f;
	ms_hoverTargetName[0] = '\0';
	ms_hoverTargetHealth = 0;
	ms_hoverTargetHealthMax = 0;
	ms_hoverTargetAction = 0;
	ms_hoverTargetActionMax = 0;
	ms_hoverTargetMind = 0;
	ms_hoverTargetMindMax = 0;
	ms_hoverTargetAttackable = false;
	ms_hoverTargetValid = false;
	ms_hoverTargetFrame = 0;
	ms_xrHandTrackingExtensionSupported = false;
	ms_xrHandTrackingSystemSupported = false;
	ms_xrHandTrackingFunctionsReady = false;
	ms_xrHandTrackingReady = false;
	ms_xrHandTrackingSnapshotCount = 0;
	ms_xrHandTrackers[0] = 0;
	ms_xrHandTrackers[1] = 0;
	ms_xrCreateHandTrackerEXT = 0;
	ms_xrDestroyHandTrackerEXT = 0;
	ms_xrLocateHandJointsEXT = 0;
	ms_xrQuadPoseCaptured = false;
	ms_xrQuadHeadPoseCaptured = false;
	ms_xrLoadingHeadPoseCaptured = false;
	ms_xrHandHeadPose = identityPose();
	ms_xrHandHeadPoseCaptured = false;
	ms_xrQuadAnchorFailureLogged = false;
	ms_xrUiQuadReady = false;
	ms_xrUiQuadPose = identityPose();
	ms_xrUiQuadWidthMeters = 0.0f;
	ms_xrUiQuadHeightMeters = 0.0f;
	ms_xrPointerSubmitCount = 0;
	ms_xrWandSubmitCount = 0;
	ms_xrHandSubmitCount = 0;
	ms_xrWristMenuButtonSubmitCount = 0;
	ms_xrProjectionLayerLogged = false;
	ms_xrWorldFrameBegun = false;
	ms_xrWorldFrameSubmitted = false;
	ms_xrProjectionImageReady = false;
	ms_xrProjectionImageIndex = 0;
	ms_xrIdleRecycleAttempted = false;
	resetWorldHeadRecenter();
	ms_xrMenuButtonDown = false;
	ms_xrWristMenuButtonDown = false;
	ms_xrTargetNextButtonDown = false;
	ms_xrTargetPreviousButtonDown = false;
	ms_xrRightTriggerDown = false;
	ms_xrUiTriggerHandIndex = -1;
	ms_xrGripMouseDown = false;
	ms_xrGripMouseClientX = 0;
	ms_xrGripMouseClientY = 0;
	ms_xrGripMouseHandIndex = -1;
	ms_xrGripFallbackStartSeconds[0] = 0.0;
	ms_xrGripFallbackStartSeconds[1] = 0.0;
	resetVrPhysicsTracking();
	ms_vrPhysicsManager = new VRPhysicsManager();
	ms_vrPhysicsManager->start();
	ms_xrQuadPose = identityPose();
	ms_xrQuadHeadPose = identityPose();
	ms_xrLoadingHeadPose = identityPose();
	ms_installed = true;

	bridgeLog("{\"event\":\"install\",\"device\":\"0x%p\",\"context\":\"0x%p\",\"window\":\"0x%p\",\"vrUiInput\":%s,\"vrMouseInput\":%s,\"vrControllerInput\":%s,\"vrMenuInput\":%s,\"tvMode\":%s,\"requestedTvMode\":%s,\"pointerRayLengthMeters\":2.25,\"pointerRayWidthMeters\":0.045,\"pointerRayWidthMinMeters\":0.01,\"pointerRayWidthMaxMeters\":0.16,\"openMwPointerWidthMaxMeters\":0.32,\"handLayerMarker\":\"%s\"}", ms_device, ms_context, ms_window, ms_vrUiInput ? "true" : "false", ms_vrMouseInput ? "true" : "false", ms_vrControllerInput ? "true" : "false", ms_vrMenuInput ? "true" : "false", ms_vrTvModeEnabled ? "true" : "false", tvModeEnabled ? "true" : "false", ms_vrRendererHandLayerBuildMarker);
	probeOpenXrD3D11(ms_device);
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::setTvModeEnabled(bool enabled)
{
	if (ms_vrTvModeEnabled == enabled)
		return;

	ms_vrTvModeEnabled = enabled;
	if (!enabled)
	{
		invalidateVrSpatialAnchors("tv-mode-off");
		resetWorldHeadRecenterLogged("world-mode-enter");
	}
	bridgeLog("{\"event\":\"setTvMode\",\"enabled\":%s,\"installed\":%s,\"frame\":%d}", enabled ? "true" : "false", ms_installed ? "true" : "false", ms_frameCount);
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::remove()
{
	if (!ms_installed)
		return;

	bridgeLog("{\"event\":\"remove\",\"frames\":%d,\"backBufferSubmits\":%d,\"pointerRaySubmits\":%d}", ms_frameCount, ms_backBufferSubmitCount, ms_pointerRaySubmitCount);

	ms_installed = false;
	ms_openXrProbeReady = false;
	if (ms_vrPhysicsManager)
	{
		ms_vrPhysicsManager->stop();
		delete ms_vrPhysicsManager;
		ms_vrPhysicsManager = 0;
	}
	resetVrPhysicsTracking();
	destroyOpenXrObjects();
	releaseGripMouseState("remove");
	if (ms_openXrLoader)
	{
		bridgeLog("{\"event\":\"openxrLoaderResident\",\"reason\":\"remove\",\"handle\":\"0x%p\"}", ms_openXrLoader);
		ms_openXrLoader = 0;
	}
	releaseLeftMouseState("remove");
	releaseCom(ms_context);
	releaseCom(ms_device);
	ms_vrUiInput = 0;
	ms_vrMouseInput = 0;
	ms_vrControllerInput = 0;
	ms_vrMenuInput = 0;
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::beginFrame()
{
	if (!isEnabled() || !ms_installed)
		return;

	++ms_frameCount;
	if (ms_xrWorldFrameSubmitted)
		return;

	ms_xrWorldFrameSubmitted = false;
}

// ----------------------------------------------------------------------

bool Direct3d11_VrBridge::isWorldRenderingEnabled()
{
	return isEnabled() && ms_installed && !isTvModeEnabled();
}

// ----------------------------------------------------------------------

bool Direct3d11_VrBridge::beginWorldFrame()
{
	if (!isWorldRenderingEnabled())
		return false;
	ms_xrWorldFrameSubmitted = false;
	if (ms_xrWorldFrameBegun)
		return true;
	if (!ms_openXrFrameSubmitReady || !ensureSessionBegun())
		return false;

	pollOpenXrEvents();
	if (!ensureProjectionSwapchain())
		return false;

	XrFrameWaitInfoLocal waitInfo;
	ZeroMemory(&waitInfo, sizeof(waitInfo));
	waitInfo.type = XR_TYPE_FRAME_WAIT_INFO_LOCAL;
	ZeroMemory(&ms_xrWorldFrameState, sizeof(ms_xrWorldFrameState));
	ms_xrWorldFrameState.type = XR_TYPE_FRAME_STATE_LOCAL;
	XrResult const waitResult = ms_xrWaitFrame(ms_xrSession, &waitInfo, &ms_xrWorldFrameState);
	if (waitResult != XR_SUCCESS_LOCAL)
		return false;

	XrFrameBeginInfoLocal beginInfo;
	ZeroMemory(&beginInfo, sizeof(beginInfo));
	beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO_LOCAL;
	XrResult const beginResult = ms_xrBeginFrame(ms_xrSession, &beginInfo);
	if (beginResult != XR_SUCCESS_LOCAL)
		return false;

	(void)captureAnchoredQuadPose(ms_xrWorldFrameState.predictedDisplayTime);

	XrViewLocateInfoLocal locateInfo;
	ZeroMemory(&locateInfo, sizeof(locateInfo));
	locateInfo.type = XR_TYPE_VIEW_LOCATE_INFO_LOCAL;
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_LOCAL;
	locateInfo.displayTime = ms_xrWorldFrameState.predictedDisplayTime;
	locateInfo.space = ms_xrLocalSpace;

	XrViewStateLocal viewState;
	ZeroMemory(&viewState, sizeof(viewState));
	viewState.type = XR_TYPE_VIEW_STATE_LOCAL;

	ZeroMemory(ms_xrWorldViews, sizeof(ms_xrWorldViews));
	ms_xrWorldViews[0].type = XR_TYPE_VIEW_LOCAL;
	ms_xrWorldViews[1].type = XR_TYPE_VIEW_LOCAL;
	unsigned int viewCount = 0;
	XrResult const locateResult = ms_xrLocateViews(ms_xrSession, &locateInfo, &viewState, 2, &viewCount, ms_xrWorldViews);
	if (locateResult != XR_SUCCESS_LOCAL || viewCount < 2)
	{
		XrFrameEndInfoLocal endInfo;
		ZeroMemory(&endInfo, sizeof(endInfo));
		endInfo.type = XR_TYPE_FRAME_END_INFO_LOCAL;
		endInfo.displayTime = ms_xrWorldFrameState.predictedDisplayTime;
		endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE_LOCAL;
		ms_xrEndFrame(ms_xrSession, &endInfo);
		return false;
	}

	if ((viewState.viewStateFlags & (XR_VIEW_STATE_ORIENTATION_VALID_BIT_LOCAL | XR_VIEW_STATE_POSITION_VALID_BIT_LOCAL)) != (XR_VIEW_STATE_ORIENTATION_VALID_BIT_LOCAL | XR_VIEW_STATE_POSITION_VALID_BIT_LOCAL))
	{
		XrFrameEndInfoLocal endInfo;
		ZeroMemory(&endInfo, sizeof(endInfo));
		endInfo.type = XR_TYPE_FRAME_END_INFO_LOCAL;
		endInfo.displayTime = ms_xrWorldFrameState.predictedDisplayTime;
		endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE_LOCAL;
		ms_xrEndFrame(ms_xrSession, &endInfo);
		bridgeLog("{\"event\":\"openxrLocateViews\",\"ready\":false,\"reason\":\"invalid-view-state\",\"flags\":\"0x%I64x\"}", static_cast<unsigned __int64>(viewState.viewStateFlags));
		return false;
	}

	float const centerX = (ms_xrWorldViews[0].pose.position.x + ms_xrWorldViews[1].pose.position.x) * 0.5f;
	float const centerY = (ms_xrWorldViews[0].pose.position.y + ms_xrWorldViews[1].pose.position.y) * 0.5f;
	float const centerZ = (ms_xrWorldViews[0].pose.position.z + ms_xrWorldViews[1].pose.position.z) * 0.5f;
	++ms_xrWorldHeadOriginFrameCount;
	int const recenterDelayFrames = clampInt(static_cast<int>(getEnvironmentFloat("SWG_OG_VR_WORLD_RECENTER_DELAY_FRAMES", 90.0f) + 0.5f), 0, 600);
	if (!ms_xrWorldHeadOriginCaptured && getEnvironmentFlagDefault("SWG_OG_VR_WORLD_CARRY_QUAD_RECENTER", false))
	{
		if (ms_xrQuadHeadPoseCaptured)
			captureWorldHeadOriginFromPose(ms_xrQuadHeadPose, "quad-carry", recenterDelayFrames);
		else if (ms_xrLoadingHeadPoseCaptured)
			captureWorldHeadOriginFromPose(ms_xrLoadingHeadPose, "loading-carry", recenterDelayFrames);
	}
	if (!ms_xrWorldHeadOriginCaptured)
	{
		int const captureFrame = recenterDelayFrames <= 0 ? 1 : recenterDelayFrames;
		if (ms_xrWorldHeadOriginFrameCount < captureFrame)
		{
			if (ms_xrWorldHeadOriginFrameCount == 1 || (ms_xrWorldHeadOriginFrameCount % 30) == 0)
			{
				bridgeLog("{\"event\":\"openxrWorldHeadOrigin\",\"captured\":false,\"phase\":\"waiting-for-settle\",\"frameCount\":%d,\"captureFrame\":%d,\"recenterDelayFrames\":%d,\"space\":\"%s\",\"leftEye\":[%0.3f,%0.3f,%0.3f],\"rightEye\":[%0.3f,%0.3f,%0.3f]}",
					ms_xrWorldHeadOriginFrameCount,
					captureFrame,
					recenterDelayFrames,
					referenceSpaceName(ms_xrReferenceSpaceType),
					ms_xrWorldViews[0].pose.position.x,
					ms_xrWorldViews[0].pose.position.y,
					ms_xrWorldViews[0].pose.position.z,
					ms_xrWorldViews[1].pose.position.x,
					ms_xrWorldViews[1].pose.position.y,
					ms_xrWorldViews[1].pose.position.z);
			}

			XrFrameEndInfoLocal endInfo;
			ZeroMemory(&endInfo, sizeof(endInfo));
			endInfo.type = XR_TYPE_FRAME_END_INFO_LOCAL;
			endInfo.displayTime = ms_xrWorldFrameState.predictedDisplayTime;
			endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE_LOCAL;
			ms_xrEndFrame(ms_xrSession, &endInfo);
			return false;
		}

		ms_xrWorldHeadOrigin.x = centerX;
		ms_xrWorldHeadOrigin.y = centerY;
		ms_xrWorldHeadOrigin.z = centerZ;
		float capturedYawDegrees = 0.0f;
		XrVector3fLocal const forward = captureWorldYawCorrection(ms_xrWorldViews[0].pose.orientation, capturedYawDegrees);
		ms_xrWorldHeadOriginCaptured = true;
		ms_xrWorldHeadOriginSettled = createWorldRenderSpace(capturedYawDegrees, "settled");
		bridgeLog("{\"event\":\"openxrWorldHeadOrigin\",\"captured\":true,\"phase\":\"settled\",\"settled\":%s,\"frameCount\":%d,\"captureFrame\":%d,\"recenterDelayFrames\":%d,\"space\":\"%s\",\"origin\":[%0.3f,%0.3f,%0.3f],\"forward\":[%0.3f,%0.3f,%0.3f],\"capturedYawDegrees\":%0.3f,\"leftEye\":[%0.3f,%0.3f,%0.3f],\"rightEye\":[%0.3f,%0.3f,%0.3f]}",
			ms_xrWorldHeadOriginSettled ? "true" : "false",
			ms_xrWorldHeadOriginFrameCount,
			captureFrame,
			recenterDelayFrames,
			referenceSpaceName(ms_xrReferenceSpaceType),
			ms_xrWorldHeadOrigin.x,
			ms_xrWorldHeadOrigin.y,
			ms_xrWorldHeadOrigin.z,
			forward.x,
			forward.y,
			forward.z,
			capturedYawDegrees,
			ms_xrWorldViews[0].pose.position.x,
			ms_xrWorldViews[0].pose.position.y,
			ms_xrWorldViews[0].pose.position.z,
			ms_xrWorldViews[1].pose.position.x,
			ms_xrWorldViews[1].pose.position.y,
			ms_xrWorldViews[1].pose.position.z);
	}

	if (ms_xrWorldBaseSpaceJustRecentered)
	{
		ms_xrWorldBaseSpaceJustRecentered = false;
		XrFrameEndInfoLocal endInfo;
		ZeroMemory(&endInfo, sizeof(endInfo));
		endInfo.type = XR_TYPE_FRAME_END_INFO_LOCAL;
		endInfo.displayTime = ms_xrWorldFrameState.predictedDisplayTime;
		endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE_LOCAL;
		ms_xrEndFrame(ms_xrSession, &endInfo);
		bridgeLog("{\"event\":\"openxrWorldBaseSpace\",\"ready\":true,\"phase\":\"transition-skip\",\"frame\":%d}", ms_frameCount);
		return false;
	}

	if (!ms_xrWorldHeadOriginSettled)
	{
		XrFrameEndInfoLocal endInfo;
		ZeroMemory(&endInfo, sizeof(endInfo));
		endInfo.type = XR_TYPE_FRAME_END_INFO_LOCAL;
		endInfo.displayTime = ms_xrWorldFrameState.predictedDisplayTime;
		endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE_LOCAL;
		ms_xrEndFrame(ms_xrSession, &endInfo);
		return false;
	}

	XrPosefLocal handHeadPose = recenteredWorldPose(ms_xrWorldViews[0].pose);
	if (ms_xrWorldViews[1].type == XR_TYPE_VIEW_LOCAL)
	{
		XrPosefLocal const rightEyePose = recenteredWorldPose(ms_xrWorldViews[1].pose);
		handHeadPose.position.x = (handHeadPose.position.x + rightEyePose.position.x) * 0.5f;
		handHeadPose.position.y = (handHeadPose.position.y + rightEyePose.position.y) * 0.5f;
		handHeadPose.position.z = (handHeadPose.position.z + rightEyePose.position.z) * 0.5f;
	}
	setHandHeadPose(handHeadPose);

	{
		// Publish HMD head pose to the body-IK bridge channel each world frame.
		// Calibration values are read from environment variables when present so
		// developers can tune proportions without recompiling.
		double const sampleTime = static_cast<double>(ms_xrWorldFrameState.predictedDisplayTime) * 0.000000001;
		SwgVrPhysics::BodyState bodyState{};
		bodyState.flags = SwgVrPhysics::BodyFlagHeadValid;
		bodyState.sampleTimeSeconds = sampleTime;
		bodyState.headFromWorld = matrixFromPose(handHeadPose);
		bodyState.calibration = SwgVrPhysics::defaultCalibration();
		// Per-user calibration overrides from env (values are ignored if unset).
		bodyState.calibration.shoulderWidthMeters  = getEnvironmentFloat("SWG_OG_VR_FB_SHOULDER_WIDTH", bodyState.calibration.shoulderWidthMeters);
		bodyState.calibration.upperArmLengthMeters = getEnvironmentFloat("SWG_OG_VR_FB_UPPER_ARM_LEN",  bodyState.calibration.upperArmLengthMeters);
		bodyState.calibration.foreArmLengthMeters  = getEnvironmentFloat("SWG_OG_VR_FB_FORE_ARM_LEN",   bodyState.calibration.foreArmLengthMeters);
		bodyState.calibration.shoulderDropMeters   = getEnvironmentFloat("SWG_OG_VR_FB_SHOULDER_DROP",  bodyState.calibration.shoulderDropMeters);
		bodyState.calibration.wristOffsetMeters    = getEnvironmentFloat("SWG_OG_VR_FB_WRIST_OFFSET",   bodyState.calibration.wristOffsetMeters);
		bodyState.calibration.bodyHeightMeters     = getEnvironmentFloat("SWG_OG_VR_FB_BODY_HEIGHT",    bodyState.calibration.bodyHeightMeters);
		bodyState.calibration.armLengthScaleLeft   = getEnvironmentFloat("SWG_OG_VR_FB_ARM_SCALE_L",    bodyState.calibration.armLengthScaleLeft);
		bodyState.calibration.armLengthScaleRight  = getEnvironmentFloat("SWG_OG_VR_FB_ARM_SCALE_R",    bodyState.calibration.armLengthScaleRight);
		SWGVRPhysics_PublishBodyState(&bodyState);
	}

	sampleControllerSnapshot(ms_xrWorldFrameState.predictedDisplayTime);

	XrSwapchainImageAcquireInfoLocal acquireInfo;
	ZeroMemory(&acquireInfo, sizeof(acquireInfo));
	acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL;
	XrResult const acquireResult = ms_xrAcquireSwapchainImage(ms_xrProjectionSwapchain, &acquireInfo, &ms_xrProjectionImageIndex);
	ms_xrProjectionImageReady = acquireResult == XR_SUCCESS_LOCAL && ms_xrProjectionImageIndex < ms_xrProjectionImages.size();
	if (ms_xrProjectionImageReady)
	{
		XrSwapchainImageWaitInfoLocal waitImageInfo;
		ZeroMemory(&waitImageInfo, sizeof(waitImageInfo));
		waitImageInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL;
		waitImageInfo.timeout = XR_INFINITE_DURATION_LOCAL;
		ms_xrProjectionImageReady = ms_xrWaitSwapchainImage(ms_xrProjectionSwapchain, &waitImageInfo) == XR_SUCCESS_LOCAL;
	}
	if (!ms_xrProjectionImageReady)
	{
		XrFrameEndInfoLocal endInfo;
		ZeroMemory(&endInfo, sizeof(endInfo));
		endInfo.type = XR_TYPE_FRAME_END_INFO_LOCAL;
		endInfo.displayTime = ms_xrWorldFrameState.predictedDisplayTime;
		endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE_LOCAL;
		ms_xrEndFrame(ms_xrSession, &endInfo);
		return false;
	}

	ms_xrWorldFrameBegun = true;
	ms_xrWorldEyeRendered[0] = false;
	ms_xrWorldEyeRendered[1] = false;
	return true;
}

// ----------------------------------------------------------------------

bool Direct3d11_VrBridge::getEyeInfo(int eye, EyeInfo &eyeInfo)
{
	if (eye < 0 || eye >= 2 || !ms_xrWorldFrameBegun || !ms_xrWorldHeadOriginSettled || !ms_xrWorldHeadOriginCaptured || !ms_xrWorldYawCaptured)
		return false;

	eyeInfo.width = static_cast<int>(ms_xrProjectionSwapchainWidth);
	eyeInfo.height = static_cast<int>(ms_xrProjectionSwapchainHeight);

	XrPosefLocal renderEyePose;
	if (!buildWorldRenderEyePose(eye, renderEyePose))
		return false;

	XrVector3fLocal const rawCenter = {
		(ms_xrWorldViews[0].pose.position.x + ms_xrWorldViews[1].pose.position.x) * 0.5f,
		(ms_xrWorldViews[0].pose.position.y + ms_xrWorldViews[1].pose.position.y) * 0.5f,
		(ms_xrWorldViews[0].pose.position.z + ms_xrWorldViews[1].pose.position.z) * 0.5f
	};
	float const centerLocalX = rawCenter.x - ms_xrWorldHeadOrigin.x;
	float const centerLocalY = rawCenter.y - ms_xrWorldHeadOrigin.y;
	float const centerLocalZ = rawCenter.z - ms_xrWorldHeadOrigin.z;
	float const headPositionScale = worldHeadPositionScale();
	float const stereoScale = worldStereoScale();
	float const centerX = ms_xrWorldYawCos * centerLocalX - ms_xrWorldYawSin * centerLocalZ;
	float const centerY = centerLocalY;
	float const centerZ = ms_xrWorldYawSin * centerLocalX + ms_xrWorldYawCos * centerLocalZ;
	eyeInfo.position[0] = renderEyePose.position.x;
	eyeInfo.position[1] = renderEyePose.position.y;
	eyeInfo.position[2] = renderEyePose.position.z;

	static int s_eyePoseScaleLogCount = 0;
	if (eye == 0 && (s_eyePoseScaleLogCount < 20 || (ms_frameCount % 300) == 0))
	{
		++s_eyePoseScaleLogCount;
		float const rawEyeDeltaX = ms_xrWorldViews[1].pose.position.x - ms_xrWorldViews[0].pose.position.x;
		float const rawEyeDeltaY = ms_xrWorldViews[1].pose.position.y - ms_xrWorldViews[0].pose.position.y;
		float const rawEyeDeltaZ = ms_xrWorldViews[1].pose.position.z - ms_xrWorldViews[0].pose.position.z;
		float const rawIpdMeters = sqrtf(rawEyeDeltaX * rawEyeDeltaX + rawEyeDeltaY * rawEyeDeltaY + rawEyeDeltaZ * rawEyeDeltaZ);
		bridgeLog("{\"event\":\"openxrEyePoseScale\",\"frame\":%d,\"headPositionScale\":%0.3f,\"stereoScale\":%0.3f,\"rawIpdMeters\":%0.4f,\"effectiveIpdUnits\":%0.4f,\"center\":[%0.4f,%0.4f,%0.4f],\"leftOffset\":[%0.4f,%0.4f,%0.4f],\"rightOffset\":[%0.4f,%0.4f,%0.4f]}",
			ms_frameCount,
			headPositionScale,
			stereoScale,
			rawIpdMeters,
			rawIpdMeters * stereoScale,
			centerX,
			centerY,
			centerZ,
			ms_xrWorldYawCos * (ms_xrWorldViews[0].pose.position.x - rawCenter.x) - ms_xrWorldYawSin * (ms_xrWorldViews[0].pose.position.z - rawCenter.z),
			ms_xrWorldViews[0].pose.position.y - rawCenter.y,
			ms_xrWorldYawSin * (ms_xrWorldViews[0].pose.position.x - rawCenter.x) + ms_xrWorldYawCos * (ms_xrWorldViews[0].pose.position.z - rawCenter.z),
			ms_xrWorldYawCos * (ms_xrWorldViews[1].pose.position.x - rawCenter.x) - ms_xrWorldYawSin * (ms_xrWorldViews[1].pose.position.z - rawCenter.z),
			ms_xrWorldViews[1].pose.position.y - rawCenter.y,
			ms_xrWorldYawSin * (ms_xrWorldViews[1].pose.position.x - rawCenter.x) + ms_xrWorldYawCos * (ms_xrWorldViews[1].pose.position.z - rawCenter.z));
	}

	eyeInfo.orientation[0] = renderEyePose.orientation.x;
	eyeInfo.orientation[1] = renderEyePose.orientation.y;
	eyeInfo.orientation[2] = renderEyePose.orientation.z;
	eyeInfo.orientation[3] = renderEyePose.orientation.w;
	eyeInfo.fov[0] = ms_xrWorldViews[eye].fov.angleLeft;
	eyeInfo.fov[1] = ms_xrWorldViews[eye].fov.angleRight;
	eyeInfo.fov[2] = ms_xrWorldViews[eye].fov.angleUp;
	eyeInfo.fov[3] = ms_xrWorldViews[eye].fov.angleDown;
	return true;
}

// ----------------------------------------------------------------------

ID3D11Texture2D *Direct3d11_VrBridge::getEyeTexture(int)
{
	if (!ms_xrWorldFrameBegun || !ms_xrProjectionImageReady || ms_xrProjectionImageIndex >= ms_xrProjectionImages.size())
		return 0;
	return ms_xrProjectionImages[ms_xrProjectionImageIndex].texture;
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::markEyeRendered(int eye)
{
	if (eye >= 0 && eye < 2 && ms_xrWorldFrameBegun)
		ms_xrWorldEyeRendered[eye] = true;
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::logD3dEyeTarget(char const *stage, int eye, bool ready, unsigned int hr, ID3D11Texture2D const *texture, int width, int height, unsigned int format, unsigned int arraySize, unsigned int bindFlags)
{
	static int s_logCount = 0;
	if (s_logCount++ >= 120)
		return;
	bridgeLog("{\"event\":\"d3d11VrEyeTarget\",\"stage\":\"%s\",\"eye\":%d,\"ready\":%s,\"hr\":\"0x%08x\",\"texture\":\"0x%p\",\"width\":%d,\"height\":%d,\"format\":%u,\"arraySize\":%u,\"bindFlags\":\"0x%08x\"}", stage ? stage : "", eye, ready ? "true" : "false", hr, texture, width, height, format, arraySize, bindFlags);
}

// ----------------------------------------------------------------------

bool Direct3d11_VrBridge::beginHudCapture(ID3D11RenderTargetView **renderTargetView, int &width, int &height)
{
	if (renderTargetView)
		*renderTargetView = 0;
	width = 0;
	height = 0;

	if (!isEnabled() || !ms_installed || !ms_context || ms_xrObjectContextCaptureImageAcquired || !ensureObjectContextSwapchain())
		return false;

	unsigned int imageIndex = 0;
	XrSwapchainImageAcquireInfoLocal acquireInfo;
	ZeroMemory(&acquireInfo, sizeof(acquireInfo));
	acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO_LOCAL;
	XrResult const acquireResult = ms_xrAcquireSwapchainImage(ms_xrObjectContextSwapchain, &acquireInfo, &imageIndex);
	if (acquireResult != XR_SUCCESS_LOCAL || imageIndex >= ms_xrObjectContextRenderTargetViews.size() || !ms_xrObjectContextRenderTargetViews[imageIndex])
		return false;

	XrSwapchainImageWaitInfoLocal waitInfo;
	ZeroMemory(&waitInfo, sizeof(waitInfo));
	waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO_LOCAL;
	waitInfo.timeout = XR_INFINITE_DURATION_LOCAL;
	if (ms_xrWaitSwapchainImage(ms_xrObjectContextSwapchain, &waitInfo) != XR_SUCCESS_LOCAL)
	{
		XrSwapchainImageReleaseInfoLocal releaseInfo;
		ZeroMemory(&releaseInfo, sizeof(releaseInfo));
		releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
		ms_xrReleaseSwapchainImage(ms_xrObjectContextSwapchain, &releaseInfo);
		return false;
	}

	ms_xrObjectContextCaptureImageIndex = imageIndex;
	ms_xrObjectContextCaptureImageAcquired = true;
	if (renderTargetView)
		*renderTargetView = ms_xrObjectContextRenderTargetViews[imageIndex];
	width = static_cast<int>(ms_xrObjectContextSwapchainWidth);
	height = static_cast<int>(ms_xrObjectContextSwapchainHeight);
	return true;
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::endHudCapture()
{
	if (!ms_xrObjectContextCaptureImageAcquired)
		return;

	XrSwapchainImageReleaseInfoLocal releaseInfo;
	ZeroMemory(&releaseInfo, sizeof(releaseInfo));
	releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
	ms_xrReleaseSwapchainImage(ms_xrObjectContextSwapchain, &releaseInfo);
	ms_xrObjectContextCaptureImageAcquired = false;
	ms_xrObjectContextCaptureFrame = ms_frameCount;
}

// ----------------------------------------------------------------------

bool Direct3d11_VrBridge::beginWristDashboardCapture(ID3D11RenderTargetView **renderTargetView, int &width, int &height)
{
	if (renderTargetView)
		*renderTargetView = 0;
	width = 0;
	height = 0;

	if (!isEnabled() || !ms_installed || !ms_context || ms_xrWristDashboardCaptureImageAcquired || !ensureWristDashboardSwapchain())
		return false;

	if (!ms_xrWristDashboardCaptureRenderTargetView)
		return false;

	ms_xrWristDashboardCaptureImageIndex = 0;
	ms_xrWristDashboardCaptureImageAcquired = true;
	if (renderTargetView)
		*renderTargetView = ms_xrWristDashboardCaptureRenderTargetView;
	width = static_cast<int>(ms_xrWristDashboardSwapchainWidth);
	height = static_cast<int>(ms_xrWristDashboardSwapchainHeight);
	return true;
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::endWristDashboardCapture()
{
	if (!ms_xrWristDashboardCaptureImageAcquired)
		return;

	ms_xrWristDashboardCaptureImageAcquired = false;
	ms_xrWristDashboardCaptureFrame = ms_frameCount;
}

// ----------------------------------------------------------------------

bool endWorldFrameInternal(ID3D11Texture2D *menuBackBuffer, bool alphaProjection)
{
	if (!ms_xrWorldFrameBegun)
		return false;

	XrSwapchainImageReleaseInfoLocal releaseInfo;
	ZeroMemory(&releaseInfo, sizeof(releaseInfo));
	releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO_LOCAL;
	ms_xrReleaseSwapchainImage(ms_xrProjectionSwapchain, &releaseInfo);
	ms_xrProjectionImageReady = false;

	XrCompositionLayerProjectionViewLocal projectionViews[2];
	ZeroMemory(projectionViews, sizeof(projectionViews));
	for (int eye = 0; eye < 2; ++eye)
	{
		XrPosefLocal renderEyePose;
		if (!buildWorldRenderEyePose(eye, renderEyePose))
			renderEyePose = ms_xrWorldViews[eye].pose;
		projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW_LOCAL;
		projectionViews[eye].pose = renderEyePose;
		projectionViews[eye].fov = ms_xrWorldViews[eye].fov;
		projectionViews[eye].subImage.swapchain = ms_xrProjectionSwapchain;
		projectionViews[eye].subImage.imageRect.offset.x = 0;
		projectionViews[eye].subImage.imageRect.offset.y = 0;
		projectionViews[eye].subImage.imageRect.extent.width = static_cast<int>(ms_xrProjectionSwapchainWidth);
		projectionViews[eye].subImage.imageRect.extent.height = static_cast<int>(ms_xrProjectionSwapchainHeight);
		projectionViews[eye].subImage.imageArrayIndex = static_cast<unsigned int>(eye);
	}

	XrCompositionLayerProjectionLocal projectionLayer;
	ZeroMemory(&projectionLayer, sizeof(projectionLayer));
	projectionLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_LOCAL;
	if (alphaProjection)
		projectionLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT_LOCAL | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT_LOCAL;
	projectionLayer.space = ms_xrLocalSpace;
	projectionLayer.viewCount = 2;
	projectionLayer.views = projectionViews;

	XrCompositionLayerBaseHeaderLocal const *layers[64];
	unsigned int layerCount = 0;
	unsigned int maxSubmittedLayers = ms_xrMaxLayerCount ? ms_xrMaxLayerCount : 16;
	if (maxSubmittedLayers < 1)
		maxSubmittedLayers = 1;
	if (maxSubmittedLayers > 64)
		maxSubmittedLayers = 64;

	XrCompositionLayerQuadLocal menuQuadLayer;
	unsigned int menuQuadLayerCount = 0;
	if (menuBackBuffer && prepareBackBufferQuadLayer(menuBackBuffer, menuQuadLayer))
	{
		layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&menuQuadLayer);
		++layerCount;
		menuQuadLayerCount = 1;
	}

	bool const projectionReady = ms_xrWorldEyeRendered[0] && ms_xrWorldEyeRendered[1];
	if (projectionReady)
	{
		layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&projectionLayer);
		++layerCount;
	}
	else
	{
		bridgeLog("{\"event\":\"openxrProjectionSubmit\",\"ready\":false,\"reason\":\"incomplete-eyes\",\"leftRendered\":%s,\"rightRendered\":%s}", ms_xrWorldEyeRendered[0] ? "true" : "false", ms_xrWorldEyeRendered[1] ? "true" : "false");
	}

	XrCompositionLayerQuadLocal objectContextLayers[1];
	unsigned int objectContextLayerCount = 0;
	if (prepareObjectContextLayer(objectContextLayers, 1, objectContextLayerCount))
	{
		for (unsigned int i = 0; i < objectContextLayerCount && layerCount < maxSubmittedLayers; ++i)
		{
			layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&objectContextLayers[i]);
			++layerCount;
		}
	}

	XrCompositionLayerQuadLocal hoverTargetLayers[1];
	unsigned int hoverTargetLayerCount = 0;
	if (prepareHoverTargetLayer(hoverTargetLayers, 1, hoverTargetLayerCount))
	{
		for (unsigned int i = 0; i < hoverTargetLayerCount && layerCount < maxSubmittedLayers; ++i)
		{
			layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&hoverTargetLayers[i]);
			++layerCount;
		}
	}

	XrCompositionLayerQuadLocal wristLayers[2];
	unsigned int wristLayerCount = 0;
	if (prepareWristDashboardLayer(ms_xrWorldFrameState.predictedDisplayTime, wristLayers, 2, wristLayerCount))
	{
		for (unsigned int i = 0; i < wristLayerCount && layerCount < maxSubmittedLayers; ++i)
		{
			layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&wristLayers[i]);
			++layerCount;
		}
	}

	XrCompositionLayerQuadLocal handLayers[4];
	unsigned int handLayerCount = 0;
	if (prepareControllerHandLayers(ms_xrWorldFrameState.predictedDisplayTime, handLayers, 4, handLayerCount))
	{
		for (unsigned int i = 0; i < handLayerCount && layerCount < maxSubmittedLayers; ++i)
		{
			layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&handLayers[i]);
			++layerCount;
		}
	}

	XrCompositionLayerQuadLocal wandLayers[3];
	unsigned int wandLayerCount = 0;
	if (preparePointerLayers(ms_xrWorldFrameState.predictedDisplayTime, wandLayers, 3, wandLayerCount))
	{
		for (unsigned int i = 0; i < wandLayerCount && layerCount < maxSubmittedLayers; ++i)
		{
			layers[layerCount] = reinterpret_cast<XrCompositionLayerBaseHeaderLocal const *>(&wandLayers[i]);
			++layerCount;
		}
	}

	XrFrameEndInfoLocal endInfo;
	ZeroMemory(&endInfo, sizeof(endInfo));
	endInfo.type = XR_TYPE_FRAME_END_INFO_LOCAL;
	endInfo.displayTime = ms_xrWorldFrameState.predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE_LOCAL;
	endInfo.layerCount = layerCount;
	endInfo.layers = layers;
	XrResult const endResult = ms_xrEndFrame(ms_xrSession, &endInfo);
	ms_xrWorldFrameBegun = false;
	ms_xrWorldFrameSubmitted = endResult == XR_SUCCESS_LOCAL;
	if (!ms_xrProjectionLayerLogged)
	{
		ms_xrProjectionLayerLogged = true;
		bridgeLog("{\"event\":\"openxrProjectionSubmit\",\"ready\":%s,\"result\":%d,\"width\":%u,\"height\":%u,\"leftSlice\":0,\"rightSlice\":1,\"poseContract\":\"camera-render-eye-pose\",\"layers\":%u,\"menuQuadLayers\":%u,\"alphaProjection\":%s,\"objectContextLayers\":%u,\"hoverTargetLayers\":%u,\"wristLayers\":%u,\"handLayers\":%u,\"wandLayers\":%u}", ms_xrWorldFrameSubmitted ? "true" : "false", endResult, ms_xrProjectionSwapchainWidth, ms_xrProjectionSwapchainHeight, layerCount, menuQuadLayerCount, alphaProjection ? "true" : "false", objectContextLayerCount, hoverTargetLayerCount, wristLayerCount, handLayerCount, wandLayerCount);
	}
	return ms_xrWorldFrameSubmitted;
}

// ----------------------------------------------------------------------

bool Direct3d11_VrBridge::endWorldFrame()
{
	if (getEnvironmentFlagDefault("SWG_OG_VR_STOCK_HUD_QUAD", false))
		return ms_xrWorldFrameBegun;

	return endWorldFrameInternal(0, false);
}

// ----------------------------------------------------------------------

bool Direct3d11_VrBridge::endWorldFrameWithMenuQuad(ID3D11Texture2D *backBuffer)
{
	return endWorldFrameInternal(backBuffer, true);
}

// ----------------------------------------------------------------------

bool Direct3d11_VrBridge::submitBackBuffer(ID3D11Texture2D *backBuffer)
{
	if (!isEnabled() || !ms_installed || !backBuffer)
		return false;
	if (ms_xrWorldFrameBegun && getEnvironmentFlagDefault("SWG_OG_VR_STOCK_HUD_QUAD", false))
	{
		++ms_backBufferSubmitCount;
		bool const submitted = endWorldFrameInternal(backBuffer, true);
		if (ms_backBufferSubmitCount == 1 || ms_backBufferSubmitCount == 30 || ms_backBufferSubmitCount == 300)
			bridgeLog("{\"event\":\"submitBackBufferStockHudQuad\",\"frame\":%d,\"submit\":%d,\"submitted\":%s}", ms_frameCount, ms_backBufferSubmitCount, submitted ? "true" : "false");
		return submitted;
	}

	if (ms_xrWorldFrameSubmitted)
	{
		ms_xrWorldFrameSubmitted = false;
		return true;
	}

	++ms_backBufferSubmitCount;
	if (!ms_openXrFrameSubmitReady && ms_device && ms_backBufferSubmitCount % 300 == 0)
	{
		bridgeLog("{\"event\":\"openxrProbeRetry\",\"frame\":%d,\"submit\":%d}", ms_frameCount, ms_backBufferSubmitCount);
		destroyOpenXrObjects();
		probeOpenXrD3D11(ms_device);
	}
	if (!ms_xrSessionBegun && ms_xrSessionState == 1 && ms_backBufferSubmitCount > 360 && !ms_xrIdleRecycleAttempted)
	{
		ms_xrIdleRecycleAttempted = true;
		bridgeLog("{\"event\":\"openxrIdleWait\",\"frame\":%d,\"submit\":%d}", ms_frameCount, ms_backBufferSubmitCount);
	}
	bool const quadSubmitted = submitFrontQuad(backBuffer);
	if (ms_backBufferSubmitCount == 1 || ms_backBufferSubmitCount == 30 || ms_backBufferSubmitCount == 300)
	{
		D3D11_TEXTURE2D_DESC desc;
		backBuffer->GetDesc(&desc);
		bridgeLog("{\"event\":\"submitBackBuffer\",\"frame\":%d,\"submit\":%d,\"width\":%u,\"height\":%u,\"format\":%u,\"openXrProbeReady\":%s,\"openXrQuadSubmitted\":%s}", ms_frameCount, ms_backBufferSubmitCount, desc.Width, desc.Height, static_cast<unsigned>(desc.Format), ms_openXrProbeReady ? "true" : "false", quadSubmitted ? "true" : "false");
	}

	return quadSubmitted;
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::submitHudPanelRect(char const *panelName, int left, int top, int right, int bottom, int sourceWidth, int sourceHeight)
{
	UNREF(panelName);
	UNREF(left);
	UNREF(top);
	UNREF(right);
	UNREF(bottom);
	UNREF(sourceWidth);
	UNREF(sourceHeight);
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::submitHudPanelAnchor(char const *panelName, float x, float y, float z, float radius, bool valid)
{
	if (!isEnabled() || !ms_installed)
		return;

	if (panelName && _stricmp(panelName, "VrHoverTarget") == 0)
	{
		if (!valid)
		{
			ms_hoverTargetAnchorValid = false;
			return;
		}

		ms_hoverTargetAnchorPosition.x = x;
		ms_hoverTargetAnchorPosition.y = y;
		ms_hoverTargetAnchorPosition.z = z;
		ms_hoverTargetAnchorRadius = clampFloat(radius, 0.15f, 4.0f);
		ms_hoverTargetAnchorFrame = ms_frameCount;
		ms_hoverTargetAnchorValid = true;

		static int s_hoverAnchorLogCount = 0;
		if (s_hoverAnchorLogCount < 40 || (ms_frameCount % 120) == 0)
		{
			++s_hoverAnchorLogCount;
			bridgeLog("{\"event\":\"openxrHoverTargetAnchor\",\"ready\":true,\"frame\":%d,\"position\":[%0.3f,%0.3f,%0.3f],\"radius\":%0.3f}",
				ms_frameCount,
				ms_hoverTargetAnchorPosition.x,
				ms_hoverTargetAnchorPosition.y,
				ms_hoverTargetAnchorPosition.z,
				ms_hoverTargetAnchorRadius);
		}
		return;
	}

	if (!valid || !isObjectContextPanelName(panelName))
	{
		if (ms_contextPanelAnchorValid)
			bridgeLog("{\"event\":\"openxrObjectContextAnchor\",\"ready\":false,\"panel\":\"%s\",\"frame\":%d}", panelName ? panelName : "", ms_frameCount);
		ms_contextPanelAnchorValid = false;
		ms_contextPanelAnchorName[0] = '\0';
		return;
	}

	strncpy(ms_contextPanelAnchorName, panelName, sizeof(ms_contextPanelAnchorName) - 1);
	ms_contextPanelAnchorName[sizeof(ms_contextPanelAnchorName) - 1] = '\0';
	ms_contextPanelAnchorPosition.x = x;
	ms_contextPanelAnchorPosition.y = y;
	ms_contextPanelAnchorPosition.z = z;
	ms_contextPanelAnchorRadius = clampFloat(radius, 0.15f, 4.0f);
	ms_contextPanelAnchorFrame = ms_frameCount;
	ms_contextPanelAnchorValid = true;

	static int s_anchorLogCount = 0;
	if (s_anchorLogCount < 40 || (ms_frameCount % 120) == 0)
	{
		++s_anchorLogCount;
		bridgeLog("{\"event\":\"openxrObjectContextAnchor\",\"ready\":true,\"panel\":\"%s\",\"frame\":%d,\"position\":[%0.3f,%0.3f,%0.3f],\"radius\":%0.3f}",
			ms_contextPanelAnchorName,
			ms_frameCount,
			ms_contextPanelAnchorPosition.x,
			ms_contextPanelAnchorPosition.y,
			ms_contextPanelAnchorPosition.z,
			ms_contextPanelAnchorRadius);
	}
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::submitObjectContextInputRegion(int slot, int textureLeft, int textureTop, int textureRight, int textureBottom, int clientLeft, int clientTop, int clientRight, int clientBottom, bool active)
{
	if (slot < 0 || slot >= static_cast<int>(sizeof(ms_objectContextInputRegions) / sizeof(ms_objectContextInputRegions[0])))
		return;

	ObjectContextInputRegion &region = ms_objectContextInputRegions[slot];
	region.active = active && textureRight > textureLeft && textureBottom > textureTop && clientRight > clientLeft && clientBottom > clientTop;
	region.textureLeft = textureLeft;
	region.textureTop = textureTop;
	region.textureRight = textureRight;
	region.textureBottom = textureBottom;
	region.clientLeft = clientLeft;
	region.clientTop = clientTop;
	region.clientRight = clientRight;
	region.clientBottom = clientBottom;

	static int s_logCount = 0;
	if (region.active && (s_logCount < 8 || (ms_frameCount % 300) == 0))
	{
		++s_logCount;
		bridgeLog("{\"event\":\"openxrObjectContextInputRegion\",\"slot\":%d,\"texture\":[%d,%d,%d,%d],\"client\":[%d,%d,%d,%d],\"frame\":%d}",
			slot,
			textureLeft,
			textureTop,
			textureRight,
			textureBottom,
			clientLeft,
			clientTop,
			clientRight,
			clientBottom,
			ms_frameCount);
	}
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::submitObjectContext(char const *targetName, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool attackable)
{
	if (!isEnabled() || !ms_installed)
		return;

	if (!targetName || !targetName[0])
	{
		ms_objectContextValid = false;
		return;
	}

	strncpy(ms_objectContextTargetName, targetName, sizeof(ms_objectContextTargetName) - 1);
	ms_objectContextTargetName[sizeof(ms_objectContextTargetName) - 1] = '\0';
	ms_objectContextHealth = clampInt(health, 0, healthMax > 0 ? healthMax : health);
	ms_objectContextHealthMax = healthMax > 0 ? healthMax : 0;
	ms_objectContextAction = clampInt(action, 0, actionMax > 0 ? actionMax : action);
	ms_objectContextActionMax = actionMax > 0 ? actionMax : 0;
	ms_objectContextMind = clampInt(mind, 0, mindMax > 0 ? mindMax : mind);
	ms_objectContextMindMax = mindMax > 0 ? mindMax : 0;
	ms_objectContextAttackable = attackable;
	ms_objectContextValid = true;
	ms_objectContextFrame = ms_frameCount;

	static int s_objectContextSubmitLogCount = 0;
	if (s_objectContextSubmitLogCount < 12 || (ms_frameCount % 120) == 0)
	{
		++s_objectContextSubmitLogCount;
		bridgeLog("{\"event\":\"openxrObjectContextData\",\"frame\":%d,\"target\":\"%s\",\"health\":[%d,%d],\"action\":[%d,%d],\"mind\":[%d,%d],\"attackable\":%s}",
			ms_frameCount,
			ms_objectContextTargetName,
			ms_objectContextHealth,
			ms_objectContextHealthMax,
			ms_objectContextAction,
			ms_objectContextActionMax,
			ms_objectContextMind,
			ms_objectContextMindMax,
			ms_objectContextAttackable ? "true" : "false");
	}
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::submitHoverTargetContext(char const *targetName, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool attackable)
{
	if (!isEnabled() || !ms_installed)
		return;

	if (!targetName || !*targetName)
	{
		ms_hoverTargetValid = false;
		return;
	}

	strncpy(ms_hoverTargetName, targetName, sizeof(ms_hoverTargetName) - 1);
	ms_hoverTargetName[sizeof(ms_hoverTargetName) - 1] = '\0';
	ms_hoverTargetHealth = clampInt(health, 0, healthMax > 0 ? healthMax : health);
	ms_hoverTargetHealthMax = healthMax > 0 ? healthMax : 0;
	ms_hoverTargetAction = clampInt(action, 0, actionMax > 0 ? actionMax : action);
	ms_hoverTargetActionMax = actionMax > 0 ? actionMax : 0;
	ms_hoverTargetMind = clampInt(mind, 0, mindMax > 0 ? mindMax : mind);
	ms_hoverTargetMindMax = mindMax > 0 ? mindMax : 0;
	ms_hoverTargetAttackable = attackable;
	ms_hoverTargetValid = true;
	ms_hoverTargetFrame = ms_frameCount;

	static int s_hoverTargetSubmitLogCount = 0;
	if (s_hoverTargetSubmitLogCount < 20 || (ms_frameCount % 120) == 0)
	{
		++s_hoverTargetSubmitLogCount;
		bridgeLog("{\"event\":\"openxrHoverTargetData\",\"frame\":%d,\"target\":\"%s\",\"health\":[%d,%d],\"action\":[%d,%d],\"mind\":[%d,%d],\"attackable\":%s}",
			ms_frameCount,
			ms_hoverTargetName,
			ms_hoverTargetHealth,
			ms_hoverTargetHealthMax,
			ms_hoverTargetAction,
			ms_hoverTargetActionMax,
			ms_hoverTargetMind,
			ms_hoverTargetMindMax,
			ms_hoverTargetAttackable ? "true" : "false");
	}
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::submitWristDashboard(float playerX, float playerZ, float headingRadians, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool valid)
{
	if (!isEnabled() || !ms_installed)
		return;

	if (!valid)
	{
		ms_wristDashboardValid = false;
		return;
	}

	ms_wristDashboardPlayerX = playerX;
	ms_wristDashboardPlayerZ = playerZ;
	ms_wristDashboardHeadingRadians = headingRadians;
	ms_wristDashboardHealth = clampInt(health, 0, healthMax > 0 ? healthMax : health);
	ms_wristDashboardHealthMax = healthMax > 0 ? healthMax : 0;
	ms_wristDashboardAction = clampInt(action, 0, actionMax > 0 ? actionMax : action);
	ms_wristDashboardActionMax = actionMax > 0 ? actionMax : 0;
	ms_wristDashboardMind = clampInt(mind, 0, mindMax > 0 ? mindMax : mind);
	ms_wristDashboardMindMax = mindMax > 0 ? mindMax : 0;
	ms_wristDashboardValid = true;
	ms_wristDashboardFrame = ms_frameCount;

	static int s_wristDashboardSubmitLogCount = 0;
	if (s_wristDashboardSubmitLogCount < 12 || (ms_frameCount % 120) == 0)
	{
		++s_wristDashboardSubmitLogCount;
		bridgeLog("{\"event\":\"openxrWristDashboardData\",\"frame\":%d,\"position\":[%0.1f,%0.1f],\"heading\":%0.3f,\"health\":[%d,%d],\"action\":[%d,%d],\"mind\":[%d,%d]}",
			ms_frameCount,
			ms_wristDashboardPlayerX,
			ms_wristDashboardPlayerZ,
			ms_wristDashboardHeadingRadians,
			ms_wristDashboardHealth,
			ms_wristDashboardHealthMax,
			ms_wristDashboardAction,
			ms_wristDashboardActionMax,
			ms_wristDashboardMind,
			ms_wristDashboardMindMax);
	}
}

// ----------------------------------------------------------------------

void Direct3d11_VrBridge::submitPointerRay(bool leftHand)
{
	if (!isEnabled() || !ms_installed)
		return;

	++ms_pointerRaySubmitCount;
	bridgeLog("{\"event\":\"submitPointerRay\",\"frame\":%d,\"hand\":\"%s\",\"textureWidth\":8,\"textureHeight\":8,\"lengthMeters\":2.25,\"widthMeters\":0.045}", ms_frameCount, leftHand ? "left" : "right");
}

// ======================================================================
