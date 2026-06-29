// ======================================================================
//
// ConfigDirect3d11.h
//
// ======================================================================

#ifndef INCLUDED_ConfigDirect3d11_H
#define INCLUDED_ConfigDirect3d11_H

// ======================================================================

class ConfigDirect3d11
{
public:
	static void install();

	static bool getDisableVertexAndPixelShaders();
	static int  getShaderCapabilityOverride();

	static int  getMaxVertexShaderVersion();
	static int  getMaxPixelShaderVersion();

	static bool  getModernShadows();
	static int   getModernShadowQuality();
	static int   getModernShadowCascadeCount();
	static int   getModernShadowMapSize();
	static float getModernShadowDistance();
	static float getModernShadowSplitLambda();
	static bool  getModernShadowStabilize();
	static int   getModernShadowFilter();
	static int   getModernShadowPcfTaps();
	static float getModernShadowFilterRadius();
	static float getModernShadowDepthBias();
	static float getModernShadowSlopeBias();
	static float getModernShadowNormalBias();
	static float getModernShadowFadeStart();
	static bool  getModernShadowContactShadows();
	static float getModernShadowContactDistance();
	static bool  getModernShadowVrSinglePass();
	static int   getModernShadowMaxCasters();
	static bool  getModernShadowTerrain();
	static bool  getModernShadowCharacters();
	static bool  getModernShadowObjects();
	static bool  getLegacyStencilShadowsInVr();
};

// ======================================================================

#endif
