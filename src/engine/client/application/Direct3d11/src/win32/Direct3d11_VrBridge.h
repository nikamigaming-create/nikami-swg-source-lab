// ======================================================================
//
// Direct3d11_VrBridge.h
//
// ======================================================================

#ifndef INCLUDED_Direct3d11_VrBridge_H
#define INCLUDED_Direct3d11_VrBridge_H

// ======================================================================

#include "FirstDirect3d11.h"

#include <d3d11.h>

// ======================================================================

class Direct3d11_VrBridge
{
public:
	typedef void (*VrUiInputFunction)(int x, int y, bool inside, bool leftButtonDown, bool leftButtonUp);
	typedef void (*VrMouseInputFunction)(int x, int y, bool inside, int button, bool buttonDown, bool buttonUp);
	typedef void (*VrControllerInputFunction)(float leftX, float leftY, bool leftActive, float rightX, float rightY, bool rightActive, float headYawRadians, bool headYawValid, float leftHandYawRadians, bool leftHandYawValid, float rightHandYawRadians, bool rightHandYawValid, bool turnModeClickPressed);
	typedef void (*VrMenuInputFunction)(bool pressed);

	struct EyeInfo
	{
		int width;
		int height;
		float position[3];
		float orientation[4];
		float fov[4];
	};

	static void install(ID3D11Device *device, ID3D11DeviceContext *context, HWND window, VrUiInputFunction vrUiInput, VrMouseInputFunction vrMouseInput, VrControllerInputFunction vrControllerInput, VrMenuInputFunction vrMenuInput, bool tvModeEnabled);
	static void remove();
	static bool isEnabled();
	static void setTvModeEnabled(bool enabled);
	static void beginFrame();
	static bool isWorldRenderingEnabled();
	static bool beginWorldFrame();
	static bool getEyeInfo(int eye, EyeInfo &eyeInfo);
	static ID3D11Texture2D *getEyeTexture(int eye);
	static void markEyeRendered(int eye);
	static bool endWorldFrame();
	static bool endWorldFrameWithMenuQuad(ID3D11Texture2D *backBuffer);
	static void logD3dEyeTarget(char const *stage, int eye, bool ready, unsigned int hr, ID3D11Texture2D const *texture, int width, int height, unsigned int format, unsigned int arraySize, unsigned int bindFlags);
	static bool beginHudCapture(ID3D11RenderTargetView **renderTargetView, int &width, int &height);
	static void endHudCapture();
	static bool beginWristDashboardCapture(ID3D11RenderTargetView **renderTargetView, int &width, int &height);
	static void endWristDashboardCapture();
	static bool submitBackBuffer(ID3D11Texture2D *backBuffer);
	static void submitHudPanelRect(char const *panelName, int left, int top, int right, int bottom, int sourceWidth, int sourceHeight);
	static void submitHudPanelAnchor(char const *panelName, float x, float y, float z, float radius, bool valid);
	static void submitObjectContextInputRegion(int slot, int textureLeft, int textureTop, int textureRight, int textureBottom, int clientLeft, int clientTop, int clientRight, int clientBottom, bool active);
	static void submitObjectContext(char const *targetName, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool attackable);
	static void submitHoverTargetContext(char const *targetName, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool attackable);
	static void submitWristDashboard(float playerX, float playerZ, float headingRadians, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool valid);
	static void submitPointerRay(bool leftHand);

private:
	Direct3d11_VrBridge();
	Direct3d11_VrBridge(Direct3d11_VrBridge const &);
	Direct3d11_VrBridge &operator =(Direct3d11_VrBridge const &);
};

// ======================================================================

#endif
