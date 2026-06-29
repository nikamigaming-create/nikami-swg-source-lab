// ======================================================================
//
// ConfigDirect3d11.cpp
//
// ======================================================================

#include "FirstDirect3d11.h"
#include "ConfigDirect3d11.h"

#include "sharedFoundation/ConfigFile.h"

// ======================================================================

namespace ConfigDirect3d11Namespace
{
	bool ms_disableVertexAndPixelShaders;
	int  ms_shaderCapabilityOverride;

	int  ms_maxVertexShaderVersion;
	int  ms_maxPixelShaderVersion;

	bool  ms_modernShadows;
	int   ms_modernShadowQuality;
	int   ms_modernShadowCascadeCount;
	int   ms_modernShadowMapSize;
	float ms_modernShadowDistance;
	float ms_modernShadowSplitLambda;
	bool  ms_modernShadowStabilize;
	int   ms_modernShadowFilter;
	int   ms_modernShadowPcfTaps;
	float ms_modernShadowFilterRadius;
	float ms_modernShadowDepthBias;
	float ms_modernShadowSlopeBias;
	float ms_modernShadowNormalBias;
	float ms_modernShadowFadeStart;
	bool  ms_modernShadowContactShadows;
	float ms_modernShadowContactDistance;
	bool  ms_modernShadowVrSinglePass;
	int   ms_modernShadowMaxCasters;
	bool  ms_modernShadowTerrain;
	bool  ms_modernShadowCharacters;
	bool  ms_modernShadowObjects;
	bool  ms_legacyStencilShadowsInVr;
}
using namespace ConfigDirect3d11Namespace;

// ======================================================================

#define KEY_INT(a,b)  (ms_ ## a = ConfigFile::getKeyInt("Direct3d11", #a, ConfigFile::getKeyInt("Direct3d9", #a, b)))
#define KEY_BOOL(a,b) (ms_ ## a = ConfigFile::getKeyBool("Direct3d11", #a, ConfigFile::getKeyBool("Direct3d9", #a, b)))
#define KEY_FLOAT(a,b) (ms_ ## a = b)

// ======================================================================

void ConfigDirect3d11::install()
{
	KEY_BOOL(disableVertexAndPixelShaders, true);
	KEY_INT (shaderCapabilityOverride, 0);

	KEY_INT (maxVertexShaderVersion, 0xffff);
	KEY_INT (maxPixelShaderVersion, 0xffff);

	KEY_BOOL (modernShadows, true);
	KEY_INT  (modernShadowQuality, 4);
	KEY_INT  (modernShadowCascadeCount, 4);
	KEY_INT  (modernShadowMapSize, 4096);
	KEY_FLOAT(modernShadowDistance, 512.0f);
	KEY_FLOAT(modernShadowSplitLambda, 0.85f);
	KEY_BOOL (modernShadowStabilize, true);
	KEY_INT  (modernShadowFilter, 2);
	KEY_INT  (modernShadowPcfTaps, 32);
	KEY_FLOAT(modernShadowFilterRadius, 2.50f);
	KEY_FLOAT(modernShadowDepthBias, 0.0008f);
	KEY_FLOAT(modernShadowSlopeBias, 2.0f);
	KEY_FLOAT(modernShadowNormalBias, 0.05f);
	KEY_FLOAT(modernShadowFadeStart, 0.85f);
	KEY_BOOL (modernShadowContactShadows, true);
	KEY_FLOAT(modernShadowContactDistance, 12.0f);
	KEY_BOOL (modernShadowVrSinglePass, true);
	KEY_INT  (modernShadowMaxCasters, 4096);
	KEY_BOOL (modernShadowTerrain, true);
	KEY_BOOL (modernShadowCharacters, true);
	KEY_BOOL (modernShadowObjects, true);
	KEY_BOOL (legacyStencilShadowsInVr, false);
}

// ----------------------------------------------------------------------

bool ConfigDirect3d11::getDisableVertexAndPixelShaders()
{
	return ms_disableVertexAndPixelShaders;
}

// ----------------------------------------------------------------------

int ConfigDirect3d11::getShaderCapabilityOverride()
{
	return ms_shaderCapabilityOverride;
}

// ----------------------------------------------------------------------

int ConfigDirect3d11::getMaxVertexShaderVersion()
{
	return ms_maxVertexShaderVersion;
}

// ----------------------------------------------------------------------

int ConfigDirect3d11::getMaxPixelShaderVersion()
{
	return ms_maxPixelShaderVersion;
}

// ----------------------------------------------------------------------

bool ConfigDirect3d11::getModernShadows()
{
	return ms_modernShadows;
}

// ----------------------------------------------------------------------

int ConfigDirect3d11::getModernShadowQuality()
{
	return ms_modernShadowQuality;
}

// ----------------------------------------------------------------------

int ConfigDirect3d11::getModernShadowCascadeCount()
{
	return ms_modernShadowCascadeCount;
}

// ----------------------------------------------------------------------

int ConfigDirect3d11::getModernShadowMapSize()
{
	return ms_modernShadowMapSize;
}

// ----------------------------------------------------------------------

float ConfigDirect3d11::getModernShadowDistance()
{
	return ms_modernShadowDistance;
}

// ----------------------------------------------------------------------

float ConfigDirect3d11::getModernShadowSplitLambda()
{
	return ms_modernShadowSplitLambda;
}

// ----------------------------------------------------------------------

bool ConfigDirect3d11::getModernShadowStabilize()
{
	return ms_modernShadowStabilize;
}

// ----------------------------------------------------------------------

int ConfigDirect3d11::getModernShadowFilter()
{
	return ms_modernShadowFilter;
}

// ----------------------------------------------------------------------

int ConfigDirect3d11::getModernShadowPcfTaps()
{
	return ms_modernShadowPcfTaps;
}

// ----------------------------------------------------------------------

float ConfigDirect3d11::getModernShadowFilterRadius()
{
	return ms_modernShadowFilterRadius;
}

// ----------------------------------------------------------------------

float ConfigDirect3d11::getModernShadowDepthBias()
{
	return ms_modernShadowDepthBias;
}

// ----------------------------------------------------------------------

float ConfigDirect3d11::getModernShadowSlopeBias()
{
	return ms_modernShadowSlopeBias;
}

// ----------------------------------------------------------------------

float ConfigDirect3d11::getModernShadowNormalBias()
{
	return ms_modernShadowNormalBias;
}

// ----------------------------------------------------------------------

float ConfigDirect3d11::getModernShadowFadeStart()
{
	return ms_modernShadowFadeStart;
}

// ----------------------------------------------------------------------

bool ConfigDirect3d11::getModernShadowContactShadows()
{
	return ms_modernShadowContactShadows;
}

// ----------------------------------------------------------------------

float ConfigDirect3d11::getModernShadowContactDistance()
{
	return ms_modernShadowContactDistance;
}

// ----------------------------------------------------------------------

bool ConfigDirect3d11::getModernShadowVrSinglePass()
{
	return ms_modernShadowVrSinglePass;
}

// ----------------------------------------------------------------------

int ConfigDirect3d11::getModernShadowMaxCasters()
{
	return ms_modernShadowMaxCasters;
}

// ----------------------------------------------------------------------

bool ConfigDirect3d11::getModernShadowTerrain()
{
	return ms_modernShadowTerrain;
}

// ----------------------------------------------------------------------

bool ConfigDirect3d11::getModernShadowCharacters()
{
	return ms_modernShadowCharacters;
}

// ----------------------------------------------------------------------

bool ConfigDirect3d11::getModernShadowObjects()
{
	return ms_modernShadowObjects;
}

// ----------------------------------------------------------------------

bool ConfigDirect3d11::getLegacyStencilShadowsInVr()
{
	return ms_legacyStencilShadowsInVr;
}

// ======================================================================
