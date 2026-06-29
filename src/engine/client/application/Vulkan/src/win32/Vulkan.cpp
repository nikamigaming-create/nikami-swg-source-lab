// ======================================================================
//
// Vulkan.cpp
//
// First flat Vulkan backend slice for the original SWG client graphics ABI.
// This is intentionally a conservative scaffold: it exports the same Gl_api
// table as the legacy render backends, dynamically loads vulkan-1.dll, creates a
// Win32 swapchain, and can clear/present. Mesh/material parity comes next.
//
// ======================================================================

#include "FirstVulkan.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "clientGraphics/DynamicIndexBuffer.h"
#include "clientGraphics/DynamicVertexBuffer.h"
#include "clientGraphics/Gl_dll.def"
#include "clientGraphics/HardwareIndexBuffer.h"
#include "clientGraphics/HardwareVertexBuffer.h"
#include "clientGraphics/Light.h"
#include "clientGraphics/Material.h"
#include "clientGraphics/ShaderCapability.h"
#include "clientGraphics/ShaderEffect.h"
#include "clientGraphics/ShaderImplementation.h"
#include "clientGraphics/StaticIndexBuffer.h"
#include "clientGraphics/StaticShader.h"
#include "clientGraphics/StaticVertexBuffer.h"
#include "clientGraphics/Texture.h"
#include "clientGraphics/TextureFormatInfo.h"
#include "clientGraphics/VertexBufferDescriptor.h"
#include "clientGraphics/VertexBufferFormat.h"
#include "clientGraphics/VertexBufferVector.h"
#include "sharedMath/PackedArgb.h"
#include "sharedMath/Rectangle2d.h"
#include "sharedMath/Transform.h"
#include "sharedMath/Vector.h"
#include "sharedMath/VectorRgba.h"

#include <algorithm>
#include <ctype.h>
#include <limits.h>
#include <map>
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <vector>

#include "../../../../../../../../UIHook/external/imgui/backends/imgui_impl_sdlgpu3_shaders.h"
#include "VulkanGpuDrawDepthSpv.inc"
#include "VulkanGpuDrawCubeDepthSpv.inc"
#include "VulkanGpuDrawAlphaFragSpv.inc"
#include "VulkanGpuDrawActorAuxFragSpv.inc"
#include "VulkanGpuDrawCubeAlphaFragSpv.inc"
#include "VulkanGpuDrawFragSpv.inc"

// ======================================================================

class VulkanTextureData;

class VulkanStaticShaderData : public StaticShaderGraphicsData
{
public:
	struct PassTexture
	{
		PassTexture()
		: textureTag(0),
		  textureCoordinateSet(0),
		  textureStage(0),
		  pixelProgramMode(0),
		  pixelProgramName(0),
		  stageTextureCount(0),
		  fogMode(ShaderImplementation::Pass::FM_Normal),
		  addressU(StaticShaderTemplate::TA_wrap),
		  addressV(StaticShaderTemplate::TA_wrap),
		  addressW(StaticShaderTemplate::TA_wrap),
		  lightingEnabled(false),
		  lightingColorVertex(false),
		  lightingSpecularEnabled(false),
		  lightingAmbientColorSource(ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material),
		  lightingDiffuseColorSource(ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material),
		  lightingSpecularColorSource(ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material),
		  lightingEmissiveColorSource(ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material),
		  obeysLightScale(false),
		  fullAmbient(false),
		  materialColorValid(false),
		  textureFactorValid(false)
		{
			materialAmbientColor[0] = 1.0f;
			materialAmbientColor[1] = 1.0f;
			materialAmbientColor[2] = 1.0f;
			materialAmbientColor[3] = 1.0f;
			materialColor[0] = 1.0f;
			materialColor[1] = 1.0f;
			materialColor[2] = 1.0f;
			materialColor[3] = 1.0f;
			materialEmissiveColor[0] = 0.0f;
			materialEmissiveColor[1] = 0.0f;
			materialEmissiveColor[2] = 0.0f;
			materialEmissiveColor[3] = 0.0f;
			materialSpecularColor[0] = 0.0f;
			materialSpecularColor[1] = 0.0f;
			materialSpecularColor[2] = 0.0f;
			materialSpecularColor[3] = 0.0f;
			materialSpecularPower = 0.0f;
			textureFactor[0] = 1.0f;
			textureFactor[1] = 1.0f;
			textureFactor[2] = 1.0f;
			textureFactor[3] = 1.0f;
			textureFactor2[0] = 0.0f;
			textureFactor2[1] = 0.0f;
			textureFactor2[2] = 0.0f;
			textureFactor2[3] = 1.0f;
			for (int i = 0; i < 2; ++i)
			{
				stageTextureTag[i] = 0;
				stageTextureCoordinateSet[i] = 0;
				stageTextureCoordinateGeneration[i] = ShaderImplementation::Pass::Stage::CG_passThru;
				stageColorOperation[i] = -1;
				stageAlphaOperation[i] = -1;
				stageColorArgument0[i] = -1;
				stageColorArgument1[i] = -1;
				stageColorArgument2[i] = -1;
				stageAlphaArgument0[i] = -1;
				stageAlphaArgument1[i] = -1;
				stageAlphaArgument2[i] = -1;
				stageResultArgument[i] = ShaderImplementation::Pass::Stage::TA_current;
				stageColorArgument0Complement[i] = false;
				stageColorArgument1Complement[i] = false;
				stageColorArgument2Complement[i] = false;
				stageAlphaArgument0Complement[i] = false;
				stageAlphaArgument1Complement[i] = false;
				stageAlphaArgument2Complement[i] = false;
				stageColorArgument0AlphaReplicate[i] = false;
				stageColorArgument1AlphaReplicate[i] = false;
				stageColorArgument2AlphaReplicate[i] = false;
			}
		}

		Tag textureTag;
		uint8 textureCoordinateSet;
		int textureStage;
		int pixelProgramMode;
		char const *pixelProgramName;
		int stageTextureCount;
		ShaderImplementation::Pass::FogMode fogMode;
		StaticShaderTemplate::TextureAddress addressU;
		StaticShaderTemplate::TextureAddress addressV;
		StaticShaderTemplate::TextureAddress addressW;
		bool lightingEnabled;
		bool lightingColorVertex;
		bool lightingSpecularEnabled;
		ShaderImplementation::Pass::FixedFunctionPipeline::MaterialSource lightingAmbientColorSource;
		ShaderImplementation::Pass::FixedFunctionPipeline::MaterialSource lightingDiffuseColorSource;
		ShaderImplementation::Pass::FixedFunctionPipeline::MaterialSource lightingSpecularColorSource;
		ShaderImplementation::Pass::FixedFunctionPipeline::MaterialSource lightingEmissiveColorSource;
		bool obeysLightScale;
		bool fullAmbient;
		Tag stageTextureTag[2];
		uint8 stageTextureCoordinateSet[2];
		ShaderImplementation::Pass::Stage::CoordinateGeneration stageTextureCoordinateGeneration[2];
		int stageColorOperation[2];
		int stageAlphaOperation[2];
		int stageColorArgument0[2];
		int stageColorArgument1[2];
		int stageColorArgument2[2];
		int stageAlphaArgument0[2];
		int stageAlphaArgument1[2];
		int stageAlphaArgument2[2];
		int stageResultArgument[2];
		bool stageColorArgument0Complement[2];
		bool stageColorArgument1Complement[2];
		bool stageColorArgument2Complement[2];
		bool stageAlphaArgument0Complement[2];
		bool stageAlphaArgument1Complement[2];
		bool stageAlphaArgument2Complement[2];
		bool stageColorArgument0AlphaReplicate[2];
		bool stageColorArgument1AlphaReplicate[2];
		bool stageColorArgument2AlphaReplicate[2];
		bool materialColorValid;
		float materialAmbientColor[4];
		float materialColor[4];
		float materialEmissiveColor[4];
		float materialSpecularColor[4];
		float materialSpecularPower;
		bool textureFactorValid;
		float textureFactor[4];
		float textureFactor2[4];
	};

	struct PassAlpha
	{
		PassAlpha()
		: alphaBlendEnable(true),
		  alphaBlendOperation(ShaderImplementation::Pass::BO_Add),
		  alphaBlendSource(ShaderImplementation::Pass::B_SourceAlpha),
		  alphaBlendDestination(ShaderImplementation::Pass::B_InverseSourceAlpha),
		  alphaTestEnable(false),
		  alphaTestCompare(ShaderImplementation::Pass::C_Always),
		  alphaTestReference(0),
		  writeEnable(0x0f)
		{
		}

		bool alphaBlendEnable;
		ShaderImplementation::Pass::BlendOperation alphaBlendOperation;
		ShaderImplementation::Pass::Blend alphaBlendSource;
		ShaderImplementation::Pass::Blend alphaBlendDestination;
		bool alphaTestEnable;
		ShaderImplementation::Pass::Compare alphaTestCompare;
		uint8 alphaTestReference;
		uint8 writeEnable;
	};

	explicit VulkanStaticShaderData(StaticShader const &shader)
	: m_textureSortKey(0)
	{
		UNREF(shader);
	}

	virtual void update(StaticShader const &shader) { UNREF(shader); m_textureSortKey = 0; }
	virtual uintptr_t getTextureSortKey() const { return m_textureSortKey; }

	static ShaderImplementation const *getImplementation(StaticShader const &shader)
	{
		StaticShaderTemplate const &shaderTemplate = shader.getStaticShaderTemplate();
		return shaderTemplate.m_effect ? shaderTemplate.m_effect->m_implementation : 0;
	}

	static bool getPassTexture(StaticShader const &shader, int passNumber, PassTexture &passTexture)
	{
		passTexture = PassTexture();
		ShaderImplementation const *implementation = getImplementation(shader);
		if (!implementation || !implementation->m_pass || passNumber < 0 || passNumber >= static_cast<int>(implementation->m_pass->size()))
			return false;

		ShaderImplementation::Pass const &pass = *(*implementation->m_pass)[static_cast<ShaderImplementation::Passes::size_type>(passNumber)];
		passTexture.fogMode = pass.m_fogMode;
		passTexture.fullAmbient = shader.containsPrecalculatedVertexLighting();
		if (pass.m_materialTag)
		{
			Material material;
			if (shader.getMaterial(pass.m_materialTag, material))
			{
				passTexture.materialColorValid = true;
				passTexture.materialAmbientColor[0] = material.getAmbientColor().r;
				passTexture.materialAmbientColor[1] = material.getAmbientColor().g;
				passTexture.materialAmbientColor[2] = material.getAmbientColor().b;
				passTexture.materialAmbientColor[3] = material.getAmbientColor().a;
				passTexture.materialColor[0] = material.getDiffuseColor().r;
				passTexture.materialColor[1] = material.getDiffuseColor().g;
				passTexture.materialColor[2] = material.getDiffuseColor().b;
				passTexture.materialColor[3] = material.getDiffuseColor().a;
				passTexture.materialEmissiveColor[0] = material.getEmissiveColor().r;
				passTexture.materialEmissiveColor[1] = material.getEmissiveColor().g;
				passTexture.materialEmissiveColor[2] = material.getEmissiveColor().b;
				passTexture.materialEmissiveColor[3] = material.getEmissiveColor().a;
				passTexture.materialSpecularColor[0] = material.getSpecularColor().r;
				passTexture.materialSpecularColor[1] = material.getSpecularColor().g;
				passTexture.materialSpecularColor[2] = material.getSpecularColor().b;
				passTexture.materialSpecularColor[3] = material.getSpecularColor().a;
				passTexture.materialSpecularPower = material.getSpecularPower();
			}
		}

		if (pass.m_pixelShader && pass.m_pixelShader->m_program)
		{
			char const *programName = pass.m_pixelShader->m_program->getFileName();
			passTexture.pixelProgramName = programName;
			if (programName)
			{
				if (strstr(programName, "skybox.psh") || strstr(programName, "skybox_6sided.psh"))
					passTexture.pixelProgramMode = 1;
				else if (strstr(programName, "stars.psh"))
					passTexture.pixelProgramMode = 2;
				else if (strstr(programName, "gradient_sky.psh"))
					passTexture.pixelProgramMode = 3;
				else if (strstr(programName, "procedural_clouds.psh"))
					passTexture.pixelProgramMode = 4;
				else if (strstr(programName, "terrain_cloud.psh"))
					passTexture.pixelProgramMode = 5;
				else if (strstr(programName, "cloudlayer.psh"))
					passTexture.pixelProgramMode = 12;
				else if (strstr(programName, "a_detail_bump_ps20.psh"))
					passTexture.pixelProgramMode = 6;
				else if (strstr(programName, "a_simple_bump_ps20.psh"))
					passTexture.pixelProgramMode = 7;
				else if (strstr(programName, "a_2blend_dirt_ps20.psh"))
					passTexture.pixelProgramMode = 8;
				else if (strstr(programName, "h_simple_bump_ps20.psh") || strstr(programName, "h_simple_bump_ps11.psh"))
					passTexture.pixelProgramMode = 22;
				else if (strstr(programName, "h_color2_bump_ps20.psh"))
					passTexture.pixelProgramMode = 23;
				else if (strstr(programName, "h_specmap_bump_ps20.psh") || strstr(programName, "specmap_cbmp_light_pass_ps20.psh"))
					passTexture.pixelProgramMode = 24;
				else if (strstr(programName, "h_color2_specmap_bump") || strstr(programName, "h_color2_specmap_cbmp"))
					passTexture.pixelProgramMode = 25;
				else if (strstr(programName, "h_alpha_color2_cbmp") || strstr(programName, "h_alpha_color2_bump"))
					passTexture.pixelProgramMode = 26;
				else if (strstr(programName, "h_simple_pp_ps20.psh"))
					passTexture.pixelProgramMode = 13;
				else if (strstr(programName, "h_color2_pp_ps20.psh"))
					passTexture.pixelProgramMode = 14;
				else if (strstr(programName, "h_specmap_pp_ps20.psh") || strstr(programName, "h_specmap_aniso_ps20.psh"))
					passTexture.pixelProgramMode = 15;
				else if (strstr(programName, "h_color2_specmap_pp_ps20.psh") || strstr(programName, "h_color2_specmap_ps11.psh"))
					passTexture.pixelProgramMode = 16;
				else if (strstr(programName, "h_alpha_pp_ps20.psh"))
					passTexture.pixelProgramMode = 17;
				else if (strstr(programName, "h_alpha_color2_pp_ps20.psh"))
					passTexture.pixelProgramMode = 18;
				else if (strstr(programName, "h_color2w_pp_ps20.psh"))
					passTexture.pixelProgramMode = 19;
				else if (strstr(programName, "h_spec_pp_ps20.psh"))
					passTexture.pixelProgramMode = 20;
				else if (strstr(programName, "/t.psh") || strstr(programName, "\\t.psh") || strcmp(programName, "t.psh") == 0)
					passTexture.pixelProgramMode = 21;
				else if (strstr(programName, "a_simple_pp_ps20.psh"))
					passTexture.pixelProgramMode = 9;
				else if (strstr(programName, "ui_radar.psh"))
					passTexture.pixelProgramMode = 11;
				else if (strstr(programName, "ui.psh"))
					passTexture.pixelProgramMode = 27;
				else if (strstr(programName, "a_specmap_pp_ps20.psh"))
					passTexture.pixelProgramMode = 10;
			}
		}

		if (pass.m_pixelShader && pass.m_pixelShader->m_textureSamplers)
		{
			int bestTextureIndex = 0x7fffffff;
			ShaderImplementation::Pass::PixelShader::TextureSamplers::const_iterator const end = pass.m_pixelShader->m_textureSamplers->end();
			for (ShaderImplementation::Pass::PixelShader::TextureSamplers::const_iterator iter = pass.m_pixelShader->m_textureSamplers->begin(); iter != end; ++iter)
			{
				ShaderImplementation::Pass::PixelShader::TextureSampler const *sampler = *iter;
				if (sampler && sampler->m_textureTag && sampler->m_textureIndex < bestTextureIndex)
				{
					bestTextureIndex = sampler->m_textureIndex;
					passTexture.textureTag = sampler->m_textureTag;
					passTexture.textureCoordinateSet = 0;
					passTexture.textureStage = sampler->m_textureIndex;
					passTexture.addressU = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressU);
					passTexture.addressV = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressV);
					passTexture.addressW = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressW);
				}
				if (sampler && sampler->m_textureTag == TAG(M,A,I,N))
				{
					passTexture.textureTag = sampler->m_textureTag;
					passTexture.textureCoordinateSet = 0;
					passTexture.textureStage = sampler->m_textureIndex;
					passTexture.addressU = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressU);
					passTexture.addressV = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressV);
					passTexture.addressW = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressW);
					break;
				}
			}
		}

		if (pass.m_stage)
		{
			ShaderImplementation::Pass::Stages::const_iterator const end = pass.m_stage->end();
			int textureStage = 0;
			bool sawStageTexture = false;
			for (ShaderImplementation::Pass::Stages::const_iterator iter = pass.m_stage->begin(); iter != end; ++iter)
			{
				ShaderImplementation::Pass::Stage const *stage = *iter;
				if (stage && stage->m_textureTag)
				{
					uint8 textureCoordinateSet = 0;
					IGNORE_RETURN(shader.getTextureCoordinateSet(stage->m_textureCoordinateSetTag, textureCoordinateSet));
					int const stageIndex = passTexture.stageTextureCount;
					if (stageIndex < 2)
					{
						passTexture.stageTextureTag[stageIndex] = stage->m_textureTag;
						passTexture.stageTextureCoordinateSet[stageIndex] = textureCoordinateSet;
						passTexture.stageTextureCoordinateGeneration[stageIndex] = stage->m_textureCoordinateGeneration;
						passTexture.stageColorOperation[stageIndex] = static_cast<int>(stage->m_colorOperation);
						passTexture.stageAlphaOperation[stageIndex] = static_cast<int>(stage->m_alphaOperation);
						passTexture.stageColorArgument0[stageIndex] = static_cast<int>(stage->m_colorArgument0);
						passTexture.stageColorArgument1[stageIndex] = static_cast<int>(stage->m_colorArgument1);
						passTexture.stageColorArgument2[stageIndex] = static_cast<int>(stage->m_colorArgument2);
						passTexture.stageAlphaArgument0[stageIndex] = static_cast<int>(stage->m_alphaArgument0);
						passTexture.stageAlphaArgument1[stageIndex] = static_cast<int>(stage->m_alphaArgument1);
						passTexture.stageAlphaArgument2[stageIndex] = static_cast<int>(stage->m_alphaArgument2);
						passTexture.stageResultArgument[stageIndex] = static_cast<int>(stage->m_resultArgument);
						passTexture.stageColorArgument0Complement[stageIndex] = stage->m_colorArgument0Complement;
						passTexture.stageColorArgument1Complement[stageIndex] = stage->m_colorArgument1Complement;
						passTexture.stageColorArgument2Complement[stageIndex] = stage->m_colorArgument2Complement;
						passTexture.stageAlphaArgument0Complement[stageIndex] = stage->m_alphaArgument0Complement;
						passTexture.stageAlphaArgument1Complement[stageIndex] = stage->m_alphaArgument1Complement;
						passTexture.stageAlphaArgument2Complement[stageIndex] = stage->m_alphaArgument2Complement;
						passTexture.stageColorArgument0AlphaReplicate[stageIndex] = stage->m_colorArgument0AlphaReplicate;
						passTexture.stageColorArgument1AlphaReplicate[stageIndex] = stage->m_colorArgument1AlphaReplicate;
						passTexture.stageColorArgument2AlphaReplicate[stageIndex] = stage->m_colorArgument2AlphaReplicate;
					}
					++passTexture.stageTextureCount;
					sawStageTexture = true;
					if (!passTexture.textureTag)
					{
						passTexture.textureTag = stage->m_textureTag;
						passTexture.textureStage = textureStage;
						passTexture.textureCoordinateSet = textureCoordinateSet;
						passTexture.addressU = static_cast<StaticShaderTemplate::TextureAddress>(stage->m_textureAddressU);
						passTexture.addressV = static_cast<StaticShaderTemplate::TextureAddress>(stage->m_textureAddressV);
						passTexture.addressW = static_cast<StaticShaderTemplate::TextureAddress>(stage->m_textureAddressW);
					}
				}
				++textureStage;
			}
			char const *shaderName = shader.getStaticShaderTemplate().getName().getString();
			bool const preferStageTexture =
				sawStageTexture &&
				passTexture.stageTextureTag[0] &&
				(passTexture.pixelProgramMode == 0 ||
				(shaderName && strstr(shaderName, "shader/terrain_blend")));
			if (preferStageTexture)
			{
				passTexture.textureTag = passTexture.stageTextureTag[0];
				passTexture.textureCoordinateSet = passTexture.stageTextureCoordinateSet[0];
				passTexture.textureStage = 0;
			}
		}

		if (passTexture.textureTag)
		{
			StaticShaderTemplate::TextureData textureData;
			if (shader.getTextureData(passTexture.textureTag, textureData))
			{
				if (textureData.addressU != StaticShaderTemplate::TA_invalid)
					passTexture.addressU = textureData.addressU;
				if (textureData.addressV != StaticShaderTemplate::TA_invalid)
					passTexture.addressV = textureData.addressV;
				if (textureData.addressW != StaticShaderTemplate::TA_invalid)
					passTexture.addressW = textureData.addressW;
			}
		}

		if (pass.m_textureFactorTag)
		{
			uint32 textureFactor = 0;
			if (shader.getTextureFactor(pass.m_textureFactorTag, textureFactor))
			{
				passTexture.textureFactorValid = true;
				passTexture.textureFactor[0] = static_cast<float>((textureFactor >> 16) & 0xff) / 255.0f;
				passTexture.textureFactor[1] = static_cast<float>((textureFactor >>  8) & 0xff) / 255.0f;
				passTexture.textureFactor[2] = static_cast<float>((textureFactor >>  0) & 0xff) / 255.0f;
				passTexture.textureFactor[3] = static_cast<float>((textureFactor >> 24) & 0xff) / 255.0f;
			}
		}
		if (pass.m_textureFactorTag2)
		{
			uint32 textureFactor2 = 0;
			if (shader.getTextureFactor(pass.m_textureFactorTag2, textureFactor2))
			{
				passTexture.textureFactorValid = true;
				passTexture.textureFactor2[0] = static_cast<float>((textureFactor2 >> 16) & 0xff) / 255.0f;
				passTexture.textureFactor2[1] = static_cast<float>((textureFactor2 >>  8) & 0xff) / 255.0f;
				passTexture.textureFactor2[2] = static_cast<float>((textureFactor2 >>  0) & 0xff) / 255.0f;
				passTexture.textureFactor2[3] = static_cast<float>((textureFactor2 >> 24) & 0xff) / 255.0f;
			}
		}

		passTexture.lightingEnabled = pass.m_fixedFunctionPipeline && pass.m_fixedFunctionPipeline->m_lighting;
		passTexture.lightingColorVertex = pass.m_fixedFunctionPipeline && pass.m_fixedFunctionPipeline->m_lightingColorVertex;
		passTexture.lightingSpecularEnabled = pass.m_fixedFunctionPipeline && pass.m_fixedFunctionPipeline->m_lightingSpecularEnable;
		if (pass.m_fixedFunctionPipeline)
		{
			passTexture.lightingAmbientColorSource = pass.m_fixedFunctionPipeline->m_lightingAmbientColorSource;
			passTexture.lightingDiffuseColorSource = pass.m_fixedFunctionPipeline->m_lightingDiffuseColorSource;
			passTexture.lightingSpecularColorSource = pass.m_fixedFunctionPipeline->m_lightingSpecularColorSource;
			passTexture.lightingEmissiveColorSource = pass.m_fixedFunctionPipeline->m_lightingEmissiveColorSource;
		}
		passTexture.obeysLightScale = shader.obeysLightScale();

		return passTexture.textureTag != 0 || passTexture.materialColorValid || passTexture.textureFactorValid;
	}

	static bool getPassDepth(StaticShader const &shader, int passNumber, bool &depthEnabled, bool &depthWrite)
	{
		ShaderImplementation const *implementation = getImplementation(shader);
		if (!implementation || !implementation->m_pass || passNumber < 0 || passNumber >= static_cast<int>(implementation->m_pass->size()))
			return false;

		ShaderImplementation::Pass const &pass = *(*implementation->m_pass)[static_cast<ShaderImplementation::Passes::size_type>(passNumber)];
		depthEnabled = pass.m_zEnable;
		depthWrite = pass.m_zWrite;
		return true;
	}

	static bool getPassAlpha(StaticShader const &shader, int passNumber, PassAlpha &passAlpha)
	{
		passAlpha = PassAlpha();
		ShaderImplementation const *implementation = getImplementation(shader);
		if (!implementation || !implementation->m_pass || passNumber < 0 || passNumber >= static_cast<int>(implementation->m_pass->size()))
			return false;

		ShaderImplementation::Pass const &pass = *(*implementation->m_pass)[static_cast<ShaderImplementation::Passes::size_type>(passNumber)];
		passAlpha.alphaBlendEnable = pass.m_alphaBlendEnable;
		passAlpha.alphaBlendOperation = pass.m_alphaBlendOperation;
		passAlpha.alphaBlendSource = pass.m_alphaBlendSource;
		passAlpha.alphaBlendDestination = pass.m_alphaBlendDestination;
		passAlpha.alphaTestEnable = pass.m_alphaTestEnable;
		passAlpha.alphaTestCompare = pass.m_alphaTestFunction;
		passAlpha.writeEnable = pass.m_writeEnable;
		if (pass.m_alphaTestEnable)
		{
			Tag const tagA255 = TAG(A,2,5,5);
			Tag const tagA128 = TAG(A,1,2,8);
			Tag const tagA001 = TAG(A,0,0,1);
			Tag const tagA000 = TAG(A,0,0,0);

			if (pass.m_alphaTestReferenceValueTag == tagA255)
				passAlpha.alphaTestReference = 255;
			else if (pass.m_alphaTestReferenceValueTag == tagA128)
				passAlpha.alphaTestReference = 128;
			else if (pass.m_alphaTestReferenceValueTag == tagA001)
				passAlpha.alphaTestReference = 1;
			else if (pass.m_alphaTestReferenceValueTag == tagA000)
				passAlpha.alphaTestReference = 0;
			else if (!shader.getAlphaTestReferenceValue(pass.m_alphaTestReferenceValueTag, passAlpha.alphaTestReference))
				passAlpha.alphaTestReference = 1;
		}
		return true;
	}

private:
	uintptr_t m_textureSortKey;
};

namespace VulkanNamespace
{
	struct VertexBufferDescriptorEntry
	{
		uint32 flags;
		VertexBufferDescriptor descriptor;
	};

	class VulkanStaticVertexBufferData;
	class VulkanDynamicVertexBufferData;
	class VulkanStaticIndexBufferData;
	class VulkanDynamicIndexBufferData;

	typedef std::map<StaticVertexBuffer const *, VulkanStaticVertexBufferData *> StaticVertexBufferDataMap;
	typedef std::map<DynamicVertexBuffer const *, VulkanDynamicVertexBufferData *> DynamicVertexBufferDataMap;
	typedef std::map<StaticIndexBuffer const *, VulkanStaticIndexBufferData *> StaticIndexBufferDataMap;
	typedef std::map<Tag, Texture const *> GlobalTextureMap;

	struct GpuAlphaPipelineKey
	{
		bool depthTest;
		ShaderImplementation::Pass::BlendOperation operation;
		ShaderImplementation::Pass::Blend source;
		ShaderImplementation::Pass::Blend destination;
		uint8 colorWriteMask;

		bool operator <(GpuAlphaPipelineKey const &rhs) const
		{
			if (depthTest != rhs.depthTest)
				return depthTest < rhs.depthTest;
			if (operation != rhs.operation)
				return operation < rhs.operation;
			if (source != rhs.source)
				return source < rhs.source;
			if (destination != rhs.destination)
				return destination < rhs.destination;
			return colorWriteMask < rhs.colorWriteMask;
		}
	};
	typedef std::map<GpuAlphaPipelineKey, VkPipeline> GpuAlphaPipelineMap;

	struct PairedTextureDescriptorKey
	{
		VulkanTextureData const *primary;
		VulkanTextureData const *secondary;
		bool clampSampler;
		bool secondaryPointSampler;

		bool operator <(PairedTextureDescriptorKey const &rhs) const
		{
			if (primary != rhs.primary)
				return primary < rhs.primary;
			if (secondary != rhs.secondary)
				return secondary < rhs.secondary;
			if (clampSampler != rhs.clampSampler)
				return clampSampler < rhs.clampSampler;
			return secondaryPointSampler < rhs.secondaryPointSampler;
		}
	};
	typedef std::map<PairedTextureDescriptorKey, VkDescriptorSet> PairedTextureDescriptorMap;

	struct Matrix4x4
	{
		float m[4][4];
	};

	struct GpuScreenVertex
	{
		float x;
		float y;
		float z;
		float w;
		float u;
		float v;
		float u2;
		float v2;
		float r;
		float g;
		float b;
		float a;
		float cubeDirX;
		float cubeDirY;
		float cubeDirZ;
	};

	struct GpuUiConstants
	{
		float scale[2];
		float translate[2];
	};

	struct GpuDrawPushConstants
	{
		float auxParams[4];
		float batchColor[4];
		float textureFactor[4];
		float textureFactor2[4];
		float actorBaseColor[4];
		float stageOps[4];
		float stageArgs01[4];
		float stageArgs23[4];
	};

	struct PendingTextureStagingRelease
	{
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkDeviceSize bytes;
	};

	struct GpuDrawBatch
	{
		VulkanTextureData const *textureData;
		VulkanTextureData const *secondaryTextureData;
		Tag textureTag;
		Tag secondaryTextureTag;
		char const *shaderName;
		bool alphaBlend;
		ShaderImplementation::Pass::BlendOperation alphaBlendOperation;
		ShaderImplementation::Pass::Blend alphaBlendSource;
		ShaderImplementation::Pass::Blend alphaBlendDestination;
		uint8 colorWriteMask;
		bool alphaTest;
		ShaderImplementation::Pass::Compare alphaTestCompare;
		uint8 alphaTestReference;
		bool depthTest;
		bool depthWrite;
		bool worldGeometry;
		bool actorBody;
		bool actorAuxiliary;
		bool textureFactorValid;
		float textureFactor[4];
		float textureFactor2[4];
		bool lightingEnabled;
		bool lightingSpecularEnabled;
		bool alphaMaskTexture;
		bool terrainAlphaStage;
		bool clampSampler;
		bool textureCube;
		bool scissorEnabled;
		int scissorX;
		int scissorY;
		int scissorWidth;
		int scissorHeight;
		int pixelProgramMode;
		char const *pixelProgramName;
		int textureCoordinateSet;
		int secondaryTextureCoordinateSet;
		int stageTextureCount;
		int stageColorOperation[2];
		int stageAlphaOperation[2];
		int stageColorArgument0[2];
		int stageColorArgument1[2];
		int stageColorArgument2[2];
		int stageAlphaArgument0[2];
		int stageAlphaArgument1[2];
		int stageAlphaArgument2[2];
		int stageResultArgument[2];
		bool stageColorArgument0Complement[2];
		bool stageColorArgument1Complement[2];
		bool stageColorArgument2Complement[2];
		bool stageAlphaArgument0Complement[2];
		bool stageAlphaArgument1Complement[2];
		bool stageAlphaArgument2Complement[2];
		bool stageColorArgument0AlphaReplicate[2];
		bool stageColorArgument1AlphaReplicate[2];
		bool stageColorArgument2AlphaReplicate[2];
		uint32_t firstVertex;
		uint32_t vertexCount;
		float sortDepth;
		float minX;
		float minY;
		float maxX;
		float maxY;
		float minW;
		float maxW;
		float minU;
		float minV;
		float maxU;
		float maxV;
		float maxTriangleSpanX;
		float maxTriangleSpanY;
		uint32_t suspiciousTriangles;
		float colorSum[4];
		float textureSampleSum[4];
		unsigned textureSampleCount;
	};

	Gl_api ms_glApi;
	HMODULE ms_vulkanDll;
	HWND ms_window;
	int ms_width;
	int ms_height;
	bool ms_windowed;
	bool ms_engineOwnsWindow;
	bool ms_borderlessWindow;
	int ms_windowX;
	int ms_windowY;
	bool ms_installed;
	uint32 ms_clearColor;
	int ms_nextSortKey;
	std::vector<VertexBufferDescriptorEntry> *ms_vertexBufferDescriptors;
	StaticVertexBufferDataMap *ms_staticVertexBufferDataMap;
	DynamicVertexBufferDataMap *ms_dynamicVertexBufferDataMap;
	StaticIndexBufferDataMap *ms_staticIndexBufferDataMap;
	VulkanDynamicVertexBufferData *ms_lastDynamicVertexBufferData;
	VulkanDynamicIndexBufferData *ms_lastDynamicIndexBufferData;
	byte const *ms_activeVertexData;
	std::vector<byte> ms_activeDynamicVertexSnapshot;
	std::vector<byte> ms_activeDynamicVertexStreamSnapshot[2];
	VertexBufferDescriptor const *ms_activeVertexDescriptor;
	uint32 ms_activeVertexFormatFlags;
	int ms_activeVertexStride;
	int ms_activeVertexCount;
	bool ms_activeVertexVector;
	int ms_activeVertexStreamCount;
	byte const *ms_activeVertexStreamData[2];
	int ms_activeVertexStreamStrides[2];
	VertexBufferDescriptor const *ms_activeVertexStreamDescriptors[2];
	uint32 ms_activeVertexStreamFormatFlags[2];
	int ms_activeVertexStreamCounts[2];
	Index const *ms_activeIndexData;
	int ms_activeIndexCount;
	GlobalTextureMap *ms_globalTextureMap;
	PairedTextureDescriptorMap *ms_pairedTextureDescriptorMap;
	VulkanTextureData const *ms_activeTextureData;
	VulkanTextureData const *ms_activeSecondaryTextureData;
	Tag ms_activeTextureTag;
	Tag ms_activeSecondaryTextureTag;
	bool ms_activeTerrainAlphaStage;
	bool ms_activeTextureCube;
	char ms_activeStaticShaderName[MAX_PATH];
	char const *ms_activeStaticShaderNamePtr;
	int ms_activeTextureCoordinateSet;
	int ms_activeSecondaryTextureCoordinateSet;
	int ms_activeTextureStage;
	int ms_activePixelProgramMode;
	char const *ms_activePixelProgramName;
	int ms_activeStageTextureCount;
	int ms_activeStageColorOperation[2];
	int ms_activeStageAlphaOperation[2];
	int ms_activeStageColorArgument0[2];
	int ms_activeStageColorArgument1[2];
	int ms_activeStageColorArgument2[2];
	int ms_activeStageAlphaArgument0[2];
	int ms_activeStageAlphaArgument1[2];
	int ms_activeStageAlphaArgument2[2];
	int ms_activeStageResultArgument[2];
	bool ms_activeStageColorArgument0Complement[2];
	bool ms_activeStageColorArgument1Complement[2];
	bool ms_activeStageColorArgument2Complement[2];
	bool ms_activeStageAlphaArgument0Complement[2];
	bool ms_activeStageAlphaArgument1Complement[2];
	bool ms_activeStageAlphaArgument2Complement[2];
	bool ms_activeStageColorArgument0AlphaReplicate[2];
	bool ms_activeStageColorArgument1AlphaReplicate[2];
	bool ms_activeStageColorArgument2AlphaReplicate[2];
	bool ms_activeTextureFactorValid;
	float ms_activeTextureFactor[4];
	float ms_activeTextureFactor2[4];
	bool ms_activeLightingEnabled;
	bool ms_activeLightingColorVertex;
	bool ms_activeLightingSpecularEnabled;
	ShaderImplementation::Pass::FixedFunctionPipeline::MaterialSource ms_activeLightingAmbientColorSource;
	ShaderImplementation::Pass::FixedFunctionPipeline::MaterialSource ms_activeLightingDiffuseColorSource;
	ShaderImplementation::Pass::FixedFunctionPipeline::MaterialSource ms_activeLightingSpecularColorSource;
	ShaderImplementation::Pass::FixedFunctionPipeline::MaterialSource ms_activeLightingEmissiveColorSource;
	bool ms_activeClampSampler;
	bool ms_obeysLightScale;
	bool ms_activeFullAmbient;
	bool ms_activeMaterialColorValid;
	float ms_activeMaterialAmbientColor[4];
	float ms_activeMaterialColor[4];
	float ms_activeMaterialEmissiveColor[4];
	float ms_activeMaterialSpecularColor[4];
	float ms_activeMaterialSpecularPower;
	float ms_lightAmbient[4];
	float ms_lightDiffuse[4];
	float ms_lightDirection[4];
	float ms_parallelLightDiffuse[3][4];
	float ms_parallelLightSpecular[3][4];
	float ms_parallelLightDirection[3][4];
	bool ms_lightDirectionalEnabled;
	unsigned ms_selectedLightMask;
	int ms_lightSetCount;
	int ms_lightLogCount;
	bool ms_fogEnabled;
	float ms_fogDensity;
	float ms_fogColor[4];
	uint32 ms_fogColorPacked;
	float ms_activeFogColor[4];
	uint32 ms_activeFogColorPacked;
	float ms_pointSize;
	float ms_pointSizeMin;
	float ms_pointSizeMax;
	bool ms_pointSpriteEnable;
	Matrix4x4 ms_textureTransform[8];
	bool ms_textureTransformEnabled[8];
	int ms_textureTransformDimension[8];
	bool ms_textureTransformProjected[8];
	int ms_viewportX;
	int ms_viewportY;
	int ms_viewportWidth;
	int ms_viewportHeight;
	real ms_viewportMinZ;
	real ms_viewportMaxZ;
	bool ms_scissorEnabled;
	int ms_scissorX;
	int ms_scissorY;
	int ms_scissorWidth;
	int ms_scissorHeight;
	Matrix4x4 ms_objectToWorldMatrix;
	Matrix4x4 ms_worldToCameraMatrix;
	Matrix4x4 ms_projectionMatrix;
	unsigned ms_worldTrianglesAttempted;
	unsigned ms_worldTrianglesValid;
	unsigned ms_worldTrianglePixels;
	unsigned ms_worldTrianglesOffscreen;
	unsigned ms_terrainCorruptTriangleDrops;
	unsigned ms_terrainCorruptTriangleRepairs;
	unsigned ms_terrainLocalFanCenterRepairs;
	bool ms_depthEnabled;
	bool ms_depthWriteEnabled;
	GlCullMode ms_cullMode;
	bool ms_alphaBlendEnabled;
	bool ms_alphaTestEnabled;
	ShaderImplementation::Pass::Compare ms_alphaTestCompare;
	uint8 ms_alphaTestReference;
	bool ms_shaderAlphaBlendEnabled;
	ShaderImplementation::Pass::BlendOperation ms_shaderAlphaBlendOperation;
	ShaderImplementation::Pass::Blend ms_shaderAlphaBlendSource;
	ShaderImplementation::Pass::Blend ms_shaderAlphaBlendDestination;
	uint8 ms_shaderColorWriteMask;
	bool ms_shaderAlphaTestEnabled;
	ShaderImplementation::Pass::Compare ms_shaderAlphaTestCompare;
	uint8 ms_shaderAlphaTestReference;
	int ms_activeStaticShaderPass;
	float ms_vertexUserConstants[8][4];

	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddrLocal;
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddrLocal;
	PFN_vkCreateInstance vkCreateInstanceLocal;
	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevicesLocal;
	PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeaturesLocal;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDevicePropertiesLocal;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyPropertiesLocal;
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryPropertiesLocal;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHRLocal;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHRLocal;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHRLocal;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHRLocal;
	PFN_vkCreateDevice vkCreateDeviceLocal;
	PFN_vkDestroyInstance vkDestroyInstanceLocal;
	PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHRLocal;
	PFN_vkDestroySurfaceKHR vkDestroySurfaceKHRLocal;

	PFN_vkDestroyDevice vkDestroyDeviceLocal;
	PFN_vkGetDeviceQueue vkGetDeviceQueueLocal;
	PFN_vkCreateSwapchainKHR vkCreateSwapchainKHRLocal;
	PFN_vkDestroySwapchainKHR vkDestroySwapchainKHRLocal;
	PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHRLocal;
	PFN_vkCreateImageView vkCreateImageViewLocal;
	PFN_vkDestroyImageView vkDestroyImageViewLocal;
	PFN_vkCreateCommandPool vkCreateCommandPoolLocal;
	PFN_vkDestroyCommandPool vkDestroyCommandPoolLocal;
	PFN_vkAllocateCommandBuffers vkAllocateCommandBuffersLocal;
	PFN_vkResetCommandBuffer vkResetCommandBufferLocal;
	PFN_vkBeginCommandBuffer vkBeginCommandBufferLocal;
	PFN_vkEndCommandBuffer vkEndCommandBufferLocal;
	PFN_vkCmdPipelineBarrier vkCmdPipelineBarrierLocal;
	PFN_vkCmdClearColorImage vkCmdClearColorImageLocal;
	PFN_vkCmdCopyBuffer vkCmdCopyBufferLocal;
	PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImageLocal;
	PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBufferLocal;
	PFN_vkCreateRenderPass vkCreateRenderPassLocal;
	PFN_vkDestroyRenderPass vkDestroyRenderPassLocal;
	PFN_vkCreateFramebuffer vkCreateFramebufferLocal;
	PFN_vkDestroyFramebuffer vkDestroyFramebufferLocal;
	PFN_vkCmdBeginRenderPass vkCmdBeginRenderPassLocal;
	PFN_vkCmdEndRenderPass vkCmdEndRenderPassLocal;
	PFN_vkCreateShaderModule vkCreateShaderModuleLocal;
	PFN_vkDestroyShaderModule vkDestroyShaderModuleLocal;
	PFN_vkCreatePipelineLayout vkCreatePipelineLayoutLocal;
	PFN_vkDestroyPipelineLayout vkDestroyPipelineLayoutLocal;
	PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelinesLocal;
	PFN_vkDestroyPipeline vkDestroyPipelineLocal;
	PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayoutLocal;
	PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayoutLocal;
	PFN_vkCreateDescriptorPool vkCreateDescriptorPoolLocal;
	PFN_vkDestroyDescriptorPool vkDestroyDescriptorPoolLocal;
	PFN_vkAllocateDescriptorSets vkAllocateDescriptorSetsLocal;
	PFN_vkUpdateDescriptorSets vkUpdateDescriptorSetsLocal;
	PFN_vkCreateImage vkCreateImageLocal;
	PFN_vkDestroyImage vkDestroyImageLocal;
	PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirementsLocal;
	PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayoutLocal;
	PFN_vkBindImageMemory vkBindImageMemoryLocal;
	PFN_vkCreateSampler vkCreateSamplerLocal;
	PFN_vkDestroySampler vkDestroySamplerLocal;
	PFN_vkCmdBindPipeline vkCmdBindPipelineLocal;
	PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffersLocal;
	PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSetsLocal;
	PFN_vkCmdPushConstants vkCmdPushConstantsLocal;
	PFN_vkCmdSetViewport vkCmdSetViewportLocal;
	PFN_vkCmdSetScissor vkCmdSetScissorLocal;
	PFN_vkCmdDraw vkCmdDrawLocal;
	PFN_vkCmdBindIndexBuffer vkCmdBindIndexBufferLocal;
	PFN_vkCmdDrawIndexed vkCmdDrawIndexedLocal;
	PFN_vkCreateBuffer vkCreateBufferLocal;
	PFN_vkDestroyBuffer vkDestroyBufferLocal;
	PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirementsLocal;
	PFN_vkAllocateMemory vkAllocateMemoryLocal;
	PFN_vkFreeMemory vkFreeMemoryLocal;
	PFN_vkBindBufferMemory vkBindBufferMemoryLocal;
	PFN_vkMapMemory vkMapMemoryLocal;
	PFN_vkUnmapMemory vkUnmapMemoryLocal;
	PFN_vkCreateSemaphore vkCreateSemaphoreLocal;
	PFN_vkDestroySemaphore vkDestroySemaphoreLocal;
	PFN_vkCreateFence vkCreateFenceLocal;
	PFN_vkDestroyFence vkDestroyFenceLocal;
	PFN_vkWaitForFences vkWaitForFencesLocal;
	PFN_vkResetFences vkResetFencesLocal;
	PFN_vkQueueSubmit vkQueueSubmitLocal;
	PFN_vkQueueWaitIdle vkQueueWaitIdleLocal;
	PFN_vkQueuePresentKHR vkQueuePresentKHRLocal;
	PFN_vkAcquireNextImageKHR vkAcquireNextImageKHRLocal;
	PFN_vkDeviceWaitIdle vkDeviceWaitIdleLocal;

	VkInstance ms_instance;
	VkSurfaceKHR ms_surface;
	VkPhysicalDevice ms_physicalDevice;
	VkDevice ms_device;
	VkQueue ms_queue;
	uint32_t ms_queueFamilyIndex;
	VkSwapchainKHR ms_swapchain;
	VkFormat ms_swapchainFormat;
	VkExtent2D ms_swapchainExtent;
	std::vector<VkImage> ms_swapchainImages;
	std::vector<VkImageView> ms_swapchainImageViews;
	std::vector<VkFramebuffer> ms_swapchainFramebuffers;
	std::vector<VkCommandBuffer> ms_commandBuffers;
	VkCommandPool ms_commandPool;
	VkRenderPass ms_swapchainRenderPass;
	VkImage ms_depthImage;
	VkDeviceMemory ms_depthMemory;
	VkImageView ms_depthImageView;
	VkFormat ms_depthFormat;
	VkBuffer ms_frameUploadBuffer;
	VkDeviceMemory ms_frameUploadMemory;
	uint32 *ms_frameUploadMapped;
	VkBuffer ms_gpuTriangleVertexBuffer;
	VkDeviceMemory ms_gpuTriangleVertexMemory;
	VkBuffer ms_gpuTriangleVertexStagingBuffer;
	VkDeviceMemory ms_gpuTriangleVertexStagingMemory;
	GpuScreenVertex *ms_gpuTriangleVertexMapped;
	VkBuffer ms_gpuTriangleIndexBuffer;
	VkDeviceMemory ms_gpuTriangleIndexMemory;
	VkBuffer ms_gpuTriangleIndexStagingBuffer;
	VkDeviceMemory ms_gpuTriangleIndexStagingMemory;
	uint32_t *ms_gpuTriangleIndexMapped;
	VkBuffer ms_gpuUiConstantsBuffer;
	VkDeviceMemory ms_gpuUiConstantsMemory;
	GpuUiConstants *ms_gpuUiConstantsMapped;
	VkBuffer ms_gpuReadbackBuffer;
	VkDeviceMemory ms_gpuReadbackMemory;
	byte *ms_gpuReadbackMapped;
	VkDeviceSize ms_gpuReadbackByteSize;
	VkImage ms_whiteTextureImage;
	VkDeviceMemory ms_whiteTextureMemory;
	VkImageView ms_whiteTextureView;
	VkSampler ms_whiteTextureSampler;
	VkSampler ms_clampTextureSampler;
	VkSampler ms_pointTextureSampler;
	VkDescriptorSetLayout ms_emptyDescriptorSetLayout;
	VkDescriptorSetLayout ms_uiConstantsDescriptorSetLayout;
	VkDescriptorSetLayout ms_whiteTextureDescriptorSetLayout;
	VkDescriptorPool ms_gpuDrawDescriptorPool;
	VkDescriptorSet ms_uiConstantsDescriptorSet;
	VkDescriptorSet ms_whiteTextureDescriptorSet;
	bool ms_samplerAnisotropyAvailable;
	float ms_samplerMaxAnisotropy;
	VkPipelineLayout ms_gpuDrawPipelineLayout;
	VkPipeline ms_gpuDrawPipeline;
	VkPipeline ms_gpuDrawPipelineOpaque;
	VkPipeline ms_gpuDrawPipelineOpaqueNoWrite;
	VkPipeline ms_gpuDrawPipelineAlphaNoDepth;
	VkPipeline ms_gpuDrawPipelineAlphaAdd;
	VkPipeline ms_gpuDrawPipelineAlphaAddNoDepth;
	VkPipeline ms_gpuDrawPipelineAlphaOneOne;
	VkPipeline ms_gpuDrawPipelineAlphaOneOneNoDepth;
	VkPipeline ms_gpuDrawPipelineAlphaSrcColorOne;
	VkPipeline ms_gpuDrawPipelineAlphaSrcColorOneNoDepth;
	GpuAlphaPipelineMap ms_gpuAlphaPipelineMap;
	VkPipeline ms_gpuDrawPipelineActorAux;
	VkPipeline ms_gpuDrawPipelineCubeAlphaNoDepth;
	VkPipeline ms_gpuDrawPipelineOpaqueNoDepth;
	uint32_t ms_gpuDescriptorGeneration;
	uint32_t ms_gpuTriangleVertexCapacity;
	uint32_t ms_gpuTriangleVertexCount;
	uint32_t ms_gpuTriangleIndexCount;
	unsigned ms_gpuTriangleDropped;
	unsigned ms_gpuTrianglePathologicalDropped;
	unsigned ms_gpuTriangleFramesWithData;
	unsigned ms_gpuTriangleMaxFrameVertices;
	unsigned ms_perfTextureUploadsThisPresent;
	unsigned ms_perfTextureUploadsTotal;
	unsigned ms_perfTextureUploadBytesThisPresent;
	unsigned ms_perfTextureUploadBytesTotal;
	unsigned ms_lastRealTextureBatches;
	unsigned ms_lastPendingTextureBatches;
	unsigned ms_lastMissingTextureBatches;
	unsigned ms_actorGpuTrianglesSeen;
	unsigned ms_actorGpuTrianglesStaged;
	unsigned ms_actorGpuTrianglesSkippedNrml;
	unsigned ms_actorGpuTrianglesSkippedSpec;
	unsigned ms_actorGpuTrianglesSkippedBlackSpec;
	unsigned ms_actorGpuTrianglesNonActorTextured;
	float ms_actorGpuMinX;
	float ms_actorGpuMinY;
	float ms_actorGpuMaxX;
	float ms_actorGpuMaxY;
	bool ms_whiteTextureNeedsLayoutTransition;
	std::vector<GpuDrawBatch> ms_gpuDrawBatches;
	std::vector<PendingTextureStagingRelease> ms_pendingTextureStagingReleases;
	std::vector<uint32> ms_framePixels;
	std::vector<float> ms_depthPixels;
	VkSemaphore ms_imageAvailableSemaphore;
	VkSemaphore ms_renderFinishedSemaphore;
	VkFence ms_frameFence;
	bool ms_frameFenceSubmitted;
	bool ms_deviceLost;
	bool ms_backBufferLocked;

	void ensureFramePixels();
	bool gpuTextureMipmapsEnabled();
	bool gpuTextureAnisotropyEnabled();
	float gpuTextureAnisotropyLevel();
	bool shaderNameLooksCloudLayer(char const *shaderName);

	void resetActorGpuTelemetry()
	{
		ms_actorGpuTrianglesSeen = 0;
		ms_actorGpuTrianglesStaged = 0;
		ms_actorGpuTrianglesSkippedNrml = 0;
		ms_actorGpuTrianglesSkippedSpec = 0;
		ms_actorGpuTrianglesSkippedBlackSpec = 0;
		ms_actorGpuTrianglesNonActorTextured = 0;
		ms_actorGpuMinX = 1000000000.0f;
		ms_actorGpuMinY = 1000000000.0f;
		ms_actorGpuMaxX = -1000000000.0f;
		ms_actorGpuMaxY = -1000000000.0f;
	}

	void logLine(char const *message)
	{
		OutputDebugStringA("[SWG Vulkan] ");
		OutputDebugStringA(message);
		OutputDebugStringA("\n");

		char logPath[MAX_PATH];
		DWORD const logPathLength = GetEnvironmentVariableA("SWG_LOG_FILE", logPath, sizeof(logPath));
		char const *path = (logPathLength > 0 && logPathLength < sizeof(logPath)) ? logPath : "swg_vulkan_backend.log";
		FILE *file = fopen(path, "a");
		if (file)
		{
			fprintf(file, "%s\n", message);
			fclose(file);
		}
	}

	void queueTextureStagingRelease(VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize bytes)
	{
		if (!buffer && !memory)
			return;

		PendingTextureStagingRelease release;
		release.buffer = buffer;
		release.memory = memory;
		release.bytes = bytes;
		ms_pendingTextureStagingReleases.push_back(release);
	}

	void releasePendingTextureStaging(char const *reason)
	{
		if (!ms_device || ms_pendingTextureStagingReleases.empty())
			return;

		unsigned count = 0;
		VkDeviceSize bytes = 0;
		for (std::vector<PendingTextureStagingRelease>::iterator iter = ms_pendingTextureStagingReleases.begin(); iter != ms_pendingTextureStagingReleases.end(); ++iter)
		{
			if (iter->buffer)
				vkDestroyBufferLocal(ms_device, iter->buffer, 0);
			if (iter->memory)
				vkFreeMemoryLocal(ms_device, iter->memory, 0);
			bytes += iter->bytes;
			++count;
		}
		ms_pendingTextureStagingReleases.clear();

		static unsigned s_logCount = 0;
		if (s_logCount < 80)
		{
			char buffer[192];
			_snprintf(buffer, sizeof(buffer) - 1, "released texture staging reason=%s count=%u bytes=%u", reason ? reason : "unknown", count, static_cast<unsigned>(std::min<VkDeviceSize>(bytes, 0xffffffffu)));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_logCount;
		}
	}

	bool verboseGeometryLoggingEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_VERBOSE_GEOMETRY", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuBatchDiagnosticsEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_BATCH_DIAG", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuPresentEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_GPU_PRESENT", text, sizeof(text));
			// The native Vulkan backend now has a real render-pass path. Default
			// it on so a plain OG launch cannot silently fall back to CPU upload.
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuForceWhiteTexturesEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_FORCE_WHITE_TEXTURES", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuDxt1OpaqueDecodeEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_DXT1_OPAQUE", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuActorDxtAlphaEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_ACTOR_DXT_ALPHA", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuActorAuxColorEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_ACTOR_AUX_COLOR", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuMaterialTintEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_MATERIAL_TINT", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	int gpuShaderCapability()
	{
		static int s_capability = 0;
		if (!s_capability)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_SHADER_CAPABILITY", text, sizeof(text));
			if (length > 0 && length < sizeof(text))
			{
				int const value = atoi(text);
				if (value == 20 || value == 200)
					s_capability = ShaderCapability(2, 0);
				else if (value == 14 || value == 140)
					s_capability = ShaderCapability(1, 4);
				else if (value == 11 || value == 110)
					s_capability = ShaderCapability(1, 1);
				else if (value == 3 || value == 30)
					s_capability = ShaderCapability(0, 3);
			}
			if (!s_capability)
				s_capability = ShaderCapability(1, 1);

			char buffer[96];
			_snprintf(buffer, sizeof(buffer) - 1, "vulkan shader capability=%d.%d", GetShaderCapabilityMajor(s_capability), GetShaderCapabilityMinor(s_capability));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
		}
		return s_capability;
	}

	bool gpuTerrainAlphaStageEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_TERRAIN_ALPHA_STAGE", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuTerrainFrustumClipEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_TERRAIN_FRUSTUM_CLIP", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuAtmosphereFrustumClipEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_ATMOSPHERE_FRUSTUM_CLIP", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuAtmosphereNearRejectEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_ATMOSPHERE_NEAR_REJECT", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	float gpuCloudAlphaScale()
	{
		static float s_scale = -1.0f;
		if (s_scale < 0.0f)
		{
			char text[32];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_CLOUD_ALPHA_SCALE", text, sizeof(text));
			s_scale = (length > 0 && length < sizeof(text)) ? static_cast<float>(atof(text)) : 0.25f;
			s_scale = std::max(0.0f, std::min(1.0f, s_scale));
		}
		return s_scale;
	}

	float gpuActorAuxBlendAlpha()
	{
		static float s_alpha = -1.0f;
		if (s_alpha < 0.0f)
		{
			char text[32];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_ACTOR_AUX_ALPHA", text, sizeof(text));
			s_alpha = (length > 0 && length < sizeof(text)) ? static_cast<float>(atof(text)) : 0.06f;
			s_alpha = std::max(0.0f, std::min(1.0f, s_alpha));
		}
		return s_alpha;
	}

	VkBlendFactor clientBlendToVulkan(ShaderImplementation::Pass::Blend blend)
	{
		switch (blend)
		{
		case ShaderImplementation::Pass::B_Zero:
			return VK_BLEND_FACTOR_ZERO;
		case ShaderImplementation::Pass::B_One:
			return VK_BLEND_FACTOR_ONE;
		case ShaderImplementation::Pass::B_SourceColor:
			return VK_BLEND_FACTOR_SRC_COLOR;
		case ShaderImplementation::Pass::B_InverseSourceColor:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case ShaderImplementation::Pass::B_SourceAlpha:
			return VK_BLEND_FACTOR_SRC_ALPHA;
		case ShaderImplementation::Pass::B_InverseSourceAlpha:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case ShaderImplementation::Pass::B_DestinationAlpha:
			return VK_BLEND_FACTOR_DST_ALPHA;
		case ShaderImplementation::Pass::B_InverseDestinationAlpha:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case ShaderImplementation::Pass::B_DestinationColor:
			return VK_BLEND_FACTOR_DST_COLOR;
		case ShaderImplementation::Pass::B_InverseDestinationColor:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case ShaderImplementation::Pass::B_SourceAlphaSaturate:
			return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		default:
			return VK_BLEND_FACTOR_SRC_ALPHA;
		}
	}

	VkBlendFactor clientAlphaBlendToVulkan(ShaderImplementation::Pass::Blend blend)
	{
		switch (blend)
		{
		case ShaderImplementation::Pass::B_SourceColor:
			return VK_BLEND_FACTOR_SRC_ALPHA;
		case ShaderImplementation::Pass::B_InverseSourceColor:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case ShaderImplementation::Pass::B_DestinationColor:
			return VK_BLEND_FACTOR_DST_ALPHA;
		case ShaderImplementation::Pass::B_InverseDestinationColor:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		default:
			return clientBlendToVulkan(blend);
		}
	}

	VkBlendOp clientBlendOpToVulkan(ShaderImplementation::Pass::BlendOperation operation)
	{
		switch (operation)
		{
		case ShaderImplementation::Pass::BO_Add:
			return VK_BLEND_OP_ADD;
		case ShaderImplementation::Pass::BO_Subtract:
			return VK_BLEND_OP_SUBTRACT;
		case ShaderImplementation::Pass::BO_ReverseSubtract:
			return VK_BLEND_OP_REVERSE_SUBTRACT;
		case ShaderImplementation::Pass::BO_Min:
			return VK_BLEND_OP_MIN;
		case ShaderImplementation::Pass::BO_Max:
			return VK_BLEND_OP_MAX;
		default:
			return VK_BLEND_OP_ADD;
		}
	}

	VkColorComponentFlags clientColorWriteMaskToVulkan(uint8 writeEnable)
	{
		VkColorComponentFlags mask = 0;
		if (writeEnable & 0x4)
			mask |= VK_COLOR_COMPONENT_R_BIT;
		if (writeEnable & 0x2)
			mask |= VK_COLOR_COMPONENT_G_BIT;
		if (writeEnable & 0x1)
			mask |= VK_COLOR_COMPONENT_B_BIT;
		if (writeEnable & 0x8)
			mask |= VK_COLOR_COMPONENT_A_BIT;
		return mask;
	}

	bool isStandardAlphaBlend(ShaderImplementation::Pass::BlendOperation operation, ShaderImplementation::Pass::Blend source, ShaderImplementation::Pass::Blend destination)
	{
		return operation == ShaderImplementation::Pass::BO_Add &&
			source == ShaderImplementation::Pass::B_SourceAlpha &&
			destination == ShaderImplementation::Pass::B_InverseSourceAlpha;
	}

	VkPipeline getGpuAlphaBlendPipeline(GpuDrawBatch const &batch);

	VkPipeline selectGpuAlphaPipeline(GpuDrawBatch const &batch, char const **pipelineName)
	{
		if (pipelineName)
			*pipelineName = batch.depthTest ? "alpha-legacy-depth" : "alpha-legacy-nodepth";
		VkPipeline const legacyPipeline = getGpuAlphaBlendPipeline(batch);
		if (legacyPipeline)
			return legacyPipeline;
		if (pipelineName)
			*pipelineName = batch.depthTest ? "alpha-standard-depth-fallback" : "alpha-standard-nodepth-fallback";
		if (!batch.depthTest)
		{
			if (batch.alphaBlendOperation == ShaderImplementation::Pass::BO_Add &&
				batch.alphaBlendSource == ShaderImplementation::Pass::B_SourceAlpha &&
				batch.alphaBlendDestination == ShaderImplementation::Pass::B_One)
			{
				if (pipelineName)
					*pipelineName = "alpha-srcalpha-one-nodepth";
				return ms_gpuDrawPipelineAlphaAddNoDepth;
			}
		if (batch.alphaBlendOperation == ShaderImplementation::Pass::BO_Add &&
			batch.alphaBlendSource == ShaderImplementation::Pass::B_One &&
			batch.alphaBlendDestination == ShaderImplementation::Pass::B_One)
		{
			if (pipelineName)
				*pipelineName = "alpha-one-one-nodepth";
			return ms_gpuDrawPipelineAlphaOneOneNoDepth;
		}
			if (batch.alphaBlendOperation == ShaderImplementation::Pass::BO_Add &&
				batch.alphaBlendSource == ShaderImplementation::Pass::B_SourceColor &&
				batch.alphaBlendDestination == ShaderImplementation::Pass::B_One)
			{
				if (pipelineName)
					*pipelineName = "alpha-srccolor-one-nodepth";
				return ms_gpuDrawPipelineAlphaSrcColorOneNoDepth;
			}
			return ms_gpuDrawPipelineAlphaNoDepth;
		}
		if (batch.alphaBlendOperation == ShaderImplementation::Pass::BO_Add &&
			batch.alphaBlendSource == ShaderImplementation::Pass::B_SourceAlpha &&
			batch.alphaBlendDestination == ShaderImplementation::Pass::B_One)
		{
			if (pipelineName)
				*pipelineName = "alpha-srcalpha-one-depth";
			return ms_gpuDrawPipelineAlphaAdd;
		}
		if (batch.alphaBlendOperation == ShaderImplementation::Pass::BO_Add &&
			batch.alphaBlendSource == ShaderImplementation::Pass::B_One &&
			batch.alphaBlendDestination == ShaderImplementation::Pass::B_One)
		{
			if (pipelineName)
				*pipelineName = "alpha-one-one-depth";
			return ms_gpuDrawPipelineAlphaOneOne;
		}
		if (batch.alphaBlendOperation == ShaderImplementation::Pass::BO_Add &&
			batch.alphaBlendSource == ShaderImplementation::Pass::B_SourceColor &&
			batch.alphaBlendDestination == ShaderImplementation::Pass::B_One)
		{
			if (pipelineName)
				*pipelineName = "alpha-srccolor-one-depth";
			return ms_gpuDrawPipelineAlphaSrcColorOne;
		}
		return ms_gpuDrawPipeline;
	}

	bool gpuOpaqueTriangleSortEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_OPAQUE_TRIANGLE_SORT", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuOpaqueBatchSortEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_OPAQUE_BATCH_SORT", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	ULONGLONG nowMilliseconds()
	{
		return GetTickCount64();
	}

	bool waitForSubmittedFrame(char const *reason, uint64_t timeoutNanoseconds)
	{
		if (!ms_device || !ms_frameFence || !ms_frameFenceSubmitted)
			return true;

		VkResult const waitResult = vkWaitForFencesLocal(ms_device, 1, &ms_frameFence, VK_TRUE, timeoutNanoseconds);
		if (waitResult == VK_SUCCESS)
		{
			ms_frameFenceSubmitted = false;
			releasePendingTextureStaging(reason);
			return true;
		}

		char text[192];
		_snprintf(text, sizeof(text) - 1, "%s frame fence wait result=%d", reason ? reason : "unknown", static_cast<int>(waitResult));
		text[sizeof(text) - 1] = 0;
		logLine(text);
		if (waitResult == VK_ERROR_DEVICE_LOST)
			ms_deviceLost = true;
		return false;
	}

	bool buildPath(char *target, size_t targetSize, char const *baseName, char const *extension)
	{
		if (!target || !targetSize || !baseName || !extension)
			return false;

		_snprintf(target, targetSize - 1, "%s%s", baseName, extension);
		target[targetSize - 1] = 0;
		return target[0] != 0;
	}

	void writeU16(FILE *file, unsigned value)
	{
		fputc(value & 0xff, file);
		fputc((value >> 8) & 0xff, file);
	}

	void writeU32(FILE *file, unsigned value)
	{
		fputc(value & 0xff, file);
		fputc((value >> 8) & 0xff, file);
		fputc((value >> 16) & 0xff, file);
		fputc((value >> 24) & 0xff, file);
	}

	bool writeFrameBmp(char const *path)
	{
		if (!path || ms_width <= 0 || ms_height <= 0)
			return false;
		ensureFramePixels();
		if (ms_framePixels.empty())
			return false;

		FILE *file = fopen(path, "wb");
		if (!file)
			return false;

		unsigned const rowBytes = static_cast<unsigned>(ms_width) * 4u;
		unsigned const pixelBytes = rowBytes * static_cast<unsigned>(ms_height);
		unsigned const headerBytes = 14u + 40u;
		unsigned const fileBytes = headerBytes + pixelBytes;

		fputc('B', file);
		fputc('M', file);
		writeU32(file, fileBytes);
		writeU16(file, 0);
		writeU16(file, 0);
		writeU32(file, headerBytes);
		writeU32(file, 40);
		writeU32(file, static_cast<unsigned>(ms_width));
		writeU32(file, static_cast<unsigned>(ms_height));
		writeU16(file, 1);
		writeU16(file, 32);
		writeU32(file, 0);
		writeU32(file, pixelBytes);
		writeU32(file, 2835);
		writeU32(file, 2835);
		writeU32(file, 0);
		writeU32(file, 0);

		for (int y = ms_height - 1; y >= 0; --y)
		{
			uint32 const *row = &ms_framePixels[static_cast<size_t>(y) * static_cast<size_t>(ms_width)];
			for (int x = 0; x < ms_width; ++x)
			{
				uint32 const pixel = row[x];
				fputc(pixel & 0xff, file);
				fputc((pixel >> 8) & 0xff, file);
				fputc((pixel >> 16) & 0xff, file);
				fputc((pixel >> 24) & 0xff, file);
			}
		}

		fclose(file);
		return true;
	}

	bool writeFrameTga(char const *path)
	{
		if (!path || ms_width <= 0 || ms_height <= 0)
			return false;
		ensureFramePixels();
		if (ms_framePixels.empty())
			return false;

		FILE *file = fopen(path, "wb");
		if (!file)
			return false;

		unsigned char header[18];
		memset(header, 0, sizeof(header));
		header[2] = 2;
		header[12] = static_cast<unsigned char>(ms_width & 0xff);
		header[13] = static_cast<unsigned char>((ms_width >> 8) & 0xff);
		header[14] = static_cast<unsigned char>(ms_height & 0xff);
		header[15] = static_cast<unsigned char>((ms_height >> 8) & 0xff);
		header[16] = 32;
		header[17] = 0x20 | 8;
		fwrite(header, 1, sizeof(header), file);

		for (int y = 0; y < ms_height; ++y)
		{
			uint32 const *row = &ms_framePixels[static_cast<size_t>(y) * static_cast<size_t>(ms_width)];
			for (int x = 0; x < ms_width; ++x)
			{
				uint32 const pixel = row[x];
				fputc(pixel & 0xff, file);
				fputc((pixel >> 8) & 0xff, file);
				fputc((pixel >> 16) & 0xff, file);
				fputc((pixel >> 24) & 0xff, file);
			}
		}

		fclose(file);
		return true;
	}

	bool writeFrameShot(GlScreenShotFormat format, char const *fileName)
	{
		char path[MAX_PATH];
		char const *extension = format == GSSF_tga ? ".tga" : ".bmp";
		if (!buildPath(path, sizeof(path), fileName, extension))
			return false;

		bool const ok = format == GSSF_tga ? writeFrameTga(path) : writeFrameBmp(path);
		char buffer[MAX_PATH + 64];
		_snprintf(buffer, sizeof(buffer) - 1, "screenshot %s path=%s", ok ? "ok" : "failed", path);
		buffer[sizeof(buffer) - 1] = 0;
		logLine(buffer);
		return ok;
	}

	bool writeGpuReadbackBmp(char const *path)
	{
		if (!path || !ms_gpuReadbackMapped || ms_swapchainExtent.width == 0 || ms_swapchainExtent.height == 0)
			return false;

		int const width = static_cast<int>(ms_swapchainExtent.width);
		int const height = static_cast<int>(ms_swapchainExtent.height);
		FILE *file = fopen(path, "wb");
		if (!file)
			return false;

		unsigned const rowBytes = static_cast<unsigned>(width) * 4u;
		unsigned const pixelBytes = rowBytes * static_cast<unsigned>(height);
		unsigned const headerBytes = 14u + 40u;
		unsigned const fileBytes = headerBytes + pixelBytes;

		fputc('B', file);
		fputc('M', file);
		writeU32(file, fileBytes);
		writeU16(file, 0);
		writeU16(file, 0);
		writeU32(file, headerBytes);
		writeU32(file, 40);
		writeU32(file, static_cast<unsigned>(width));
		writeU32(file, static_cast<unsigned>(height));
		writeU16(file, 1);
		writeU16(file, 32);
		writeU32(file, 0);
		writeU32(file, pixelBytes);
		writeU32(file, 2835);
		writeU32(file, 2835);
		writeU32(file, 0);
		writeU32(file, 0);

		for (int y = height - 1; y >= 0; --y)
		{
			byte const *row = ms_gpuReadbackMapped + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
			fwrite(row, 1, rowBytes, file);
		}

		fclose(file);
		return true;
	}

	int getEnvInt(char const *name, int defaultValue, int minValue);

	bool shouldGpuAutocapture(int presentCount, char *seriesPath, size_t seriesPathSize, char *latestPath, size_t latestPathSize)
	{
		static int s_gpuAutoCaptureCount = 0;
		static int s_gpuAutoCaptureLastPresent = -1;
		static bool s_gpuAutoCaptureDone = false;
		if (s_gpuAutoCaptureDone)
			return false;

		char captureBase[MAX_PATH];
		DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_AUTOCAPTURE", captureBase, sizeof(captureBase));
		if (length == 0 || length >= sizeof(captureBase))
			return false;

		int const capturePresent = getEnvInt("SWG_VULKAN_AUTOCAPTURE_PRESENT", 30, 1);
		int const captureMax = getEnvInt("SWG_RENDERER_AUTOCAPTURE_MAX", 1, 1);
		int const captureInterval = getEnvInt("SWG_RENDERER_AUTOCAPTURE_INTERVAL", 1, 1);
		bool const intervalReady = s_gpuAutoCaptureLastPresent < 0 || (presentCount - s_gpuAutoCaptureLastPresent) >= captureInterval;
		if (presentCount < capturePresent || !intervalReady)
			return false;

		_snprintf(seriesPath, seriesPathSize - 1, "%s-gpu-present%04d.bmp", captureBase, presentCount);
		seriesPath[seriesPathSize - 1] = 0;
		_snprintf(latestPath, latestPathSize - 1, "%s-gpu.bmp", captureBase);
		latestPath[latestPathSize - 1] = 0;

		++s_gpuAutoCaptureCount;
		s_gpuAutoCaptureLastPresent = presentCount;
		if (s_gpuAutoCaptureCount >= captureMax)
			s_gpuAutoCaptureDone = true;
		return true;
	}

	int getEnvInt(char const *name, int defaultValue, int minValue)
	{
		char text[64];
		DWORD const length = GetEnvironmentVariableA(name, text, sizeof(text));
		if (length > 0 && length < sizeof(text))
		{
			int value = atoi(text);
			if (value < minValue)
				value = minValue;
			return value;
		}
		return defaultValue;
	}

	int gpuTextureUploadBudgetPerPresent()
	{
		char text[64];
		DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_TEXTURE_UPLOADS_PER_PRESENT", text, sizeof(text));
		if (length > 0 && length < sizeof(text))
			return getEnvInt("SWG_VULKAN_TEXTURE_UPLOADS_PER_PRESENT", 64, 0);

		if (ms_lastPendingTextureBatches > 2048)
			return 2048;
		if (ms_lastPendingTextureBatches > 1024)
			return 1536;
		if (ms_lastPendingTextureBatches > 512)
			return 1024;
		if (ms_lastPendingTextureBatches > 128)
			return 512;
		if (ms_lastPendingTextureBatches > 32)
			return 256;
		if (ms_lastPendingTextureBatches > 0)
			return 128;
		return 64;
	}

	unsigned gpuTextureUploadByteBudgetPerPresent()
	{
		int const envBudgetMb = getEnvInt("SWG_VULKAN_TEXTURE_UPLOAD_MB_PER_PRESENT", 0, 0);
		if (envBudgetMb > 0)
			return static_cast<unsigned>(std::min<int>(envBudgetMb, 256)) * 1024u * 1024u;

		if (ms_lastPendingTextureBatches > 2048)
			return 128u * 1024u * 1024u;
		if (ms_lastPendingTextureBatches > 1024)
			return 96u * 1024u * 1024u;
		if (ms_lastPendingTextureBatches > 512)
			return 64u * 1024u * 1024u;
		if (ms_lastPendingTextureBatches > 128)
			return 32u * 1024u * 1024u;
		if (ms_lastPendingTextureBatches > 32)
			return 16u * 1024u * 1024u;
		if (ms_lastPendingTextureBatches > 0)
			return 8u * 1024u * 1024u;
		return 6u * 1024u * 1024u;
	}

	void setIdentity(Matrix4x4 &matrix)
	{
		memset(&matrix, 0, sizeof(matrix));
		matrix.m[0][0] = 1.0f;
		matrix.m[1][1] = 1.0f;
		matrix.m[2][2] = 1.0f;
		matrix.m[3][3] = 1.0f;
	}

	void resetTextureTransform(int stage)
	{
		if (stage < 0 || stage >= 8)
			return;

		setIdentity(ms_textureTransform[stage]);
		ms_textureTransformEnabled[stage] = false;
		ms_textureTransformDimension[stage] = 2;
		ms_textureTransformProjected[stage] = false;
	}

	Matrix4x4 multiply(Matrix4x4 const &lhs, Matrix4x4 const &rhs)
	{
		Matrix4x4 result;
		memset(&result, 0, sizeof(result));
		for (int row = 0; row < 4; ++row)
			for (int col = 0; col < 4; ++col)
				for (int k = 0; k < 4; ++k)
					result.m[row][col] += lhs.m[row][k] * rhs.m[k][col];
		return result;
	}

	void setMatrixFromTransform(Matrix4x4 &matrix, Transform const &transform, Vector const *scale)
	{
		Transform::matrix_t const &source = transform.getMatrix();
		matrix.m[0][0] = source[0][0] * (scale ? scale->x : 1.0f);
		matrix.m[0][1] = source[1][0] * (scale ? scale->x : 1.0f);
		matrix.m[0][2] = source[2][0] * (scale ? scale->x : 1.0f);
		matrix.m[0][3] = 0.0f;

		matrix.m[1][0] = source[0][1] * (scale ? scale->y : 1.0f);
		matrix.m[1][1] = source[1][1] * (scale ? scale->y : 1.0f);
		matrix.m[1][2] = source[2][1] * (scale ? scale->y : 1.0f);
		matrix.m[1][3] = 0.0f;

		matrix.m[2][0] = source[0][2] * (scale ? scale->z : 1.0f);
		matrix.m[2][1] = source[1][2] * (scale ? scale->z : 1.0f);
		matrix.m[2][2] = source[2][2] * (scale ? scale->z : 1.0f);
		matrix.m[2][3] = 0.0f;

		matrix.m[3][0] = source[0][3];
		matrix.m[3][1] = source[1][3];
		matrix.m[3][2] = source[2][3];
		matrix.m[3][3] = 1.0f;
	}

	void setMatrixFromGl(Matrix4x4 &matrix, GlMatrix4x4 const &glMatrix)
	{
		for (int row = 0; row < 4; ++row)
			for (int col = 0; col < 4; ++col)
				matrix.m[row][col] = glMatrix.matrix[col][row];
	}

	bool loadVulkanDll()
	{
		if (ms_vulkanDll)
			return true;

		ms_vulkanDll = LoadLibraryA("vulkan-1.dll");
		if (!ms_vulkanDll)
		{
			logLine("vulkan-1.dll not found");
			return false;
		}

		vkGetInstanceProcAddrLocal = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(ms_vulkanDll, "vkGetInstanceProcAddr"));
		if (!vkGetInstanceProcAddrLocal)
		{
			logLine("vkGetInstanceProcAddr not found");
			return false;
		}

		vkCreateInstanceLocal = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddrLocal(0, "vkCreateInstance"));
		if (!vkCreateInstanceLocal)
		{
			logLine("vkCreateInstance not found");
			return false;
		}

		return true;
	}

	template <typename T>
	bool loadInstanceProc(T &target, char const *name)
	{
		target = reinterpret_cast<T>(vkGetInstanceProcAddrLocal(ms_instance, name));
		if (!target)
		{
			char buffer[256];
			_snprintf(buffer, sizeof(buffer) - 1, "missing instance proc %s", name);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			return false;
		}
		return true;
	}

	template <typename T>
	bool loadDeviceProc(T &target, char const *name)
	{
		target = reinterpret_cast<T>(vkGetDeviceProcAddrLocal(ms_device, name));
		if (!target)
		{
			char buffer[256];
			_snprintf(buffer, sizeof(buffer) - 1, "missing device proc %s", name);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			return false;
		}
		return true;
	}

	uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties)
	{
		VkPhysicalDeviceMemoryProperties memoryProperties;
		memset(&memoryProperties, 0, sizeof(memoryProperties));
		vkGetPhysicalDeviceMemoryPropertiesLocal(ms_physicalDevice, &memoryProperties);

		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			if ((typeBits & (1u << i)) && ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties))
				return i;
		}

		return 0xffffffff;
	}

	void destroyFrameUploadResources()
	{
		if (ms_frameUploadMapped)
		{
			vkUnmapMemoryLocal(ms_device, ms_frameUploadMemory);
			ms_frameUploadMapped = 0;
		}
		if (ms_frameUploadBuffer)
		{
			vkDestroyBufferLocal(ms_device, ms_frameUploadBuffer, 0);
			ms_frameUploadBuffer = 0;
		}
		if (ms_frameUploadMemory)
		{
			vkFreeMemoryLocal(ms_device, ms_frameUploadMemory, 0);
			ms_frameUploadMemory = 0;
		}
		ms_framePixels.clear();
	}

	void destroyGpuTriangleResources()
	{
		if (ms_gpuTriangleVertexMapped)
		{
			vkUnmapMemoryLocal(ms_device, ms_gpuTriangleVertexStagingMemory);
			ms_gpuTriangleVertexMapped = 0;
		}
		if (ms_gpuTriangleIndexMapped)
		{
			vkUnmapMemoryLocal(ms_device, ms_gpuTriangleIndexStagingMemory);
			ms_gpuTriangleIndexMapped = 0;
		}
		if (ms_gpuTriangleVertexStagingBuffer)
		{
			vkDestroyBufferLocal(ms_device, ms_gpuTriangleVertexStagingBuffer, 0);
			ms_gpuTriangleVertexStagingBuffer = 0;
		}
		if (ms_gpuTriangleIndexStagingBuffer)
		{
			vkDestroyBufferLocal(ms_device, ms_gpuTriangleIndexStagingBuffer, 0);
			ms_gpuTriangleIndexStagingBuffer = 0;
		}
		if (ms_gpuTriangleVertexBuffer)
		{
			vkDestroyBufferLocal(ms_device, ms_gpuTriangleVertexBuffer, 0);
			ms_gpuTriangleVertexBuffer = 0;
		}
		if (ms_gpuTriangleIndexBuffer)
		{
			vkDestroyBufferLocal(ms_device, ms_gpuTriangleIndexBuffer, 0);
			ms_gpuTriangleIndexBuffer = 0;
		}
		if (ms_gpuTriangleVertexMemory)
		{
			vkFreeMemoryLocal(ms_device, ms_gpuTriangleVertexMemory, 0);
			ms_gpuTriangleVertexMemory = 0;
		}
		if (ms_gpuTriangleIndexMemory)
		{
			vkFreeMemoryLocal(ms_device, ms_gpuTriangleIndexMemory, 0);
			ms_gpuTriangleIndexMemory = 0;
		}
		if (ms_gpuTriangleVertexStagingMemory)
		{
			vkFreeMemoryLocal(ms_device, ms_gpuTriangleVertexStagingMemory, 0);
			ms_gpuTriangleVertexStagingMemory = 0;
		}
		if (ms_gpuTriangleIndexStagingMemory)
		{
			vkFreeMemoryLocal(ms_device, ms_gpuTriangleIndexStagingMemory, 0);
			ms_gpuTriangleIndexStagingMemory = 0;
		}
		ms_gpuTriangleVertexCapacity = 0;
		ms_gpuTriangleVertexCount = 0;
		ms_gpuTriangleIndexCount = 0;
		ms_gpuTriangleDropped = 0;
		ms_gpuTrianglePathologicalDropped = 0;
		ms_gpuTriangleFramesWithData = 0;
		ms_gpuTriangleMaxFrameVertices = 0;
		resetActorGpuTelemetry();
	}

	bool createGpuTriangleResources()
	{
		destroyGpuTriangleResources();

		ms_gpuTriangleVertexCapacity = 262144;
		VkDeviceSize const byteSize = static_cast<VkDeviceSize>(ms_gpuTriangleVertexCapacity) * static_cast<VkDeviceSize>(sizeof(GpuScreenVertex));
		VkDeviceSize const indexByteSize = static_cast<VkDeviceSize>(ms_gpuTriangleVertexCapacity) * static_cast<VkDeviceSize>(sizeof(uint32_t));

		VkBufferCreateInfo bufferInfo;
		memset(&bufferInfo, 0, sizeof(bufferInfo));
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = byteSize;
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBufferLocal(ms_device, &bufferInfo, 0, &ms_gpuTriangleVertexBuffer) != VK_SUCCESS)
		{
			logLine("vkCreateBuffer gpu triangle vertices failed");
			return false;
		}

		VkMemoryRequirements requirements;
		memset(&requirements, 0, sizeof(requirements));
		vkGetBufferMemoryRequirementsLocal(ms_device, ms_gpuTriangleVertexBuffer, &requirements);

		uint32_t const memoryType = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (memoryType == 0xffffffff)
		{
			logLine("gpu triangle device-local vertex memory type not found");
			return false;
		}

		VkMemoryAllocateInfo allocateInfo;
		memset(&allocateInfo, 0, sizeof(allocateInfo));
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = memoryType;
		if (vkAllocateMemoryLocal(ms_device, &allocateInfo, 0, &ms_gpuTriangleVertexMemory) != VK_SUCCESS)
		{
			logLine("vkAllocateMemory gpu triangle vertices failed");
			return false;
		}

		if (vkBindBufferMemoryLocal(ms_device, ms_gpuTriangleVertexBuffer, ms_gpuTriangleVertexMemory, 0) != VK_SUCCESS)
		{
			logLine("vkBindBufferMemory gpu triangle vertices failed");
			return false;
		}

		VkBufferCreateInfo indexBufferInfo = bufferInfo;
		indexBufferInfo.size = indexByteSize;
		indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (vkCreateBufferLocal(ms_device, &indexBufferInfo, 0, &ms_gpuTriangleIndexBuffer) != VK_SUCCESS)
		{
			logLine("vkCreateBuffer gpu triangle indices failed");
			return false;
		}

		VkMemoryRequirements indexRequirements;
		memset(&indexRequirements, 0, sizeof(indexRequirements));
		vkGetBufferMemoryRequirementsLocal(ms_device, ms_gpuTriangleIndexBuffer, &indexRequirements);
		uint32_t const indexMemoryType = findMemoryType(indexRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (indexMemoryType == 0xffffffff)
		{
			logLine("gpu triangle device-local index memory type not found");
			return false;
		}

		VkMemoryAllocateInfo indexAllocateInfo = allocateInfo;
		indexAllocateInfo.allocationSize = indexRequirements.size;
		indexAllocateInfo.memoryTypeIndex = indexMemoryType;
		if (vkAllocateMemoryLocal(ms_device, &indexAllocateInfo, 0, &ms_gpuTriangleIndexMemory) != VK_SUCCESS)
		{
			logLine("vkAllocateMemory gpu triangle indices failed");
			return false;
		}

		if (vkBindBufferMemoryLocal(ms_device, ms_gpuTriangleIndexBuffer, ms_gpuTriangleIndexMemory, 0) != VK_SUCCESS)
		{
			logLine("vkBindBufferMemory gpu triangle indices failed");
			return false;
		}

		VkBufferCreateInfo stagingBufferInfo = bufferInfo;
		stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		if (vkCreateBufferLocal(ms_device, &stagingBufferInfo, 0, &ms_gpuTriangleVertexStagingBuffer) != VK_SUCCESS)
		{
			logLine("vkCreateBuffer gpu triangle vertex staging failed");
			return false;
		}

		VkMemoryRequirements stagingRequirements;
		memset(&stagingRequirements, 0, sizeof(stagingRequirements));
		vkGetBufferMemoryRequirementsLocal(ms_device, ms_gpuTriangleVertexStagingBuffer, &stagingRequirements);
		uint32_t const stagingMemoryType = findMemoryType(stagingRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (stagingMemoryType == 0xffffffff)
		{
			logLine("gpu triangle vertex staging memory type not found");
			return false;
		}

		VkMemoryAllocateInfo stagingAllocateInfo = allocateInfo;
		stagingAllocateInfo.allocationSize = stagingRequirements.size;
		stagingAllocateInfo.memoryTypeIndex = stagingMemoryType;
		if (vkAllocateMemoryLocal(ms_device, &stagingAllocateInfo, 0, &ms_gpuTriangleVertexStagingMemory) != VK_SUCCESS)
		{
			logLine("vkAllocateMemory gpu triangle vertex staging failed");
			return false;
		}

		if (vkBindBufferMemoryLocal(ms_device, ms_gpuTriangleVertexStagingBuffer, ms_gpuTriangleVertexStagingMemory, 0) != VK_SUCCESS)
		{
			logLine("vkBindBufferMemory gpu triangle vertex staging failed");
			return false;
		}

		VkBufferCreateInfo indexStagingBufferInfo = stagingBufferInfo;
		indexStagingBufferInfo.size = indexByteSize;
		if (vkCreateBufferLocal(ms_device, &indexStagingBufferInfo, 0, &ms_gpuTriangleIndexStagingBuffer) != VK_SUCCESS)
		{
			logLine("vkCreateBuffer gpu triangle index staging failed");
			return false;
		}

		VkMemoryRequirements indexStagingRequirements;
		memset(&indexStagingRequirements, 0, sizeof(indexStagingRequirements));
		vkGetBufferMemoryRequirementsLocal(ms_device, ms_gpuTriangleIndexStagingBuffer, &indexStagingRequirements);
		uint32_t const indexStagingMemoryType = findMemoryType(indexStagingRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (indexStagingMemoryType == 0xffffffff)
		{
			logLine("gpu triangle index staging memory type not found");
			return false;
		}

		VkMemoryAllocateInfo indexStagingAllocateInfo = allocateInfo;
		indexStagingAllocateInfo.allocationSize = indexStagingRequirements.size;
		indexStagingAllocateInfo.memoryTypeIndex = indexStagingMemoryType;
		if (vkAllocateMemoryLocal(ms_device, &indexStagingAllocateInfo, 0, &ms_gpuTriangleIndexStagingMemory) != VK_SUCCESS)
		{
			logLine("vkAllocateMemory gpu triangle index staging failed");
			return false;
		}

		if (vkBindBufferMemoryLocal(ms_device, ms_gpuTriangleIndexStagingBuffer, ms_gpuTriangleIndexStagingMemory, 0) != VK_SUCCESS)
		{
			logLine("vkBindBufferMemory gpu triangle index staging failed");
			return false;
		}

		void *mapped = 0;
		if (vkMapMemoryLocal(ms_device, ms_gpuTriangleVertexStagingMemory, 0, byteSize, 0, &mapped) != VK_SUCCESS || !mapped)
		{
			logLine("vkMapMemory gpu triangle vertex staging failed");
			return false;
		}

		ms_gpuTriangleVertexMapped = static_cast<GpuScreenVertex *>(mapped);
		void *indexMapped = 0;
		if (vkMapMemoryLocal(ms_device, ms_gpuTriangleIndexStagingMemory, 0, indexByteSize, 0, &indexMapped) != VK_SUCCESS || !indexMapped)
		{
			logLine("vkMapMemory gpu triangle index staging failed");
			return false;
		}
		ms_gpuTriangleIndexMapped = static_cast<uint32_t *>(indexMapped);
		ms_gpuTriangleVertexCount = 0;
		ms_gpuTriangleIndexCount = 0;

		char buffer[192];
		_snprintf(buffer, sizeof(buffer) - 1, "gpu triangle buffers ok deviceLocal=1 vertices=%u vertexBytes=%u indexBytes=%u", static_cast<unsigned>(ms_gpuTriangleVertexCapacity), static_cast<unsigned>(byteSize), static_cast<unsigned>(indexByteSize));
		buffer[sizeof(buffer) - 1] = 0;
		logLine(buffer);
		return true;
	}

	void destroyGpuReadbackResources()
	{
		if (ms_gpuReadbackMapped)
		{
			vkUnmapMemoryLocal(ms_device, ms_gpuReadbackMemory);
			ms_gpuReadbackMapped = 0;
		}
		if (ms_gpuReadbackBuffer)
		{
			vkDestroyBufferLocal(ms_device, ms_gpuReadbackBuffer, 0);
			ms_gpuReadbackBuffer = 0;
		}
		if (ms_gpuReadbackMemory)
		{
			vkFreeMemoryLocal(ms_device, ms_gpuReadbackMemory, 0);
			ms_gpuReadbackMemory = 0;
		}
		ms_gpuReadbackByteSize = 0;
	}

	bool createHostVisibleBuffer(VkDeviceSize byteSize, VkBufferUsageFlags usage, VkBuffer &buffer, VkDeviceMemory &memory, void **mapped, char const *name)
	{
		buffer = 0;
		memory = 0;
		if (mapped)
			*mapped = 0;

		VkBufferCreateInfo bufferInfo;
		memset(&bufferInfo, 0, sizeof(bufferInfo));
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = byteSize;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBufferLocal(ms_device, &bufferInfo, 0, &buffer) != VK_SUCCESS)
		{
			char text[160];
			_snprintf(text, sizeof(text) - 1, "vkCreateBuffer %s failed", name);
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}

		VkMemoryRequirements requirements;
		memset(&requirements, 0, sizeof(requirements));
		vkGetBufferMemoryRequirementsLocal(ms_device, buffer, &requirements);
		uint32_t const memoryType = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memoryType == 0xffffffff)
		{
			char text[160];
			_snprintf(text, sizeof(text) - 1, "%s memory type not found", name);
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}

		VkMemoryAllocateInfo allocateInfo;
		memset(&allocateInfo, 0, sizeof(allocateInfo));
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = memoryType;
		if (vkAllocateMemoryLocal(ms_device, &allocateInfo, 0, &memory) != VK_SUCCESS)
		{
			char text[160];
			_snprintf(text, sizeof(text) - 1, "vkAllocateMemory %s failed", name);
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}

		if (vkBindBufferMemoryLocal(ms_device, buffer, memory, 0) != VK_SUCCESS)
		{
			char text[160];
			_snprintf(text, sizeof(text) - 1, "vkBindBufferMemory %s failed", name);
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}

		if (mapped)
		{
			void *localMapped = 0;
			if (vkMapMemoryLocal(ms_device, memory, 0, byteSize, 0, &localMapped) != VK_SUCCESS || !localMapped)
			{
				char text[160];
				_snprintf(text, sizeof(text) - 1, "vkMapMemory %s failed", name);
				text[sizeof(text) - 1] = 0;
				logLine(text);
				return false;
			}
			*mapped = localMapped;
		}

		return true;
	}

	bool createGpuReadbackResources()
	{
		destroyGpuReadbackResources();
		ms_gpuReadbackByteSize = static_cast<VkDeviceSize>(ms_swapchainExtent.width) * static_cast<VkDeviceSize>(ms_swapchainExtent.height) * 4u;
		void *mapped = 0;
		if (!createHostVisibleBuffer(ms_gpuReadbackByteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, ms_gpuReadbackBuffer, ms_gpuReadbackMemory, &mapped, "gpu readback"))
			return false;
		ms_gpuReadbackMapped = static_cast<byte *>(mapped);
		char buffer[160];
		_snprintf(buffer, sizeof(buffer) - 1, "gpu readback buffer ok bytes=%u", static_cast<unsigned>(ms_gpuReadbackByteSize));
		buffer[sizeof(buffer) - 1] = 0;
		logLine(buffer);
		return true;
	}

	VkShaderModule createShaderModule(uint8_t const *bytes, size_t byteSize, char const *name)
	{
		if (!bytes || byteSize == 0 || (byteSize & 3) != 0)
			return 0;

		VkShaderModuleCreateInfo moduleInfo;
		memset(&moduleInfo, 0, sizeof(moduleInfo));
		moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleInfo.codeSize = byteSize;
		moduleInfo.pCode = reinterpret_cast<uint32_t const *>(bytes);

		VkShaderModule module = 0;
		if (vkCreateShaderModuleLocal(ms_device, &moduleInfo, 0, &module) != VK_SUCCESS)
		{
			char text[160];
			_snprintf(text, sizeof(text) - 1, "vkCreateShaderModule %s failed", name);
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return 0;
		}
		return module;
	}

	VkPipeline createGpuAlphaBlendPipeline(GpuAlphaPipelineKey const &key)
	{
		VkShaderModule vertexShader = createShaderModule(swg_gpu_draw_depth_vertex_spv, sizeof(swg_gpu_draw_depth_vertex_spv), "swg-gpu-alpha-cache-vertex");
		VkShaderModule alphaFragmentShader = createShaderModule(swg_gpu_draw_alpha_fragment_spv, sizeof(swg_gpu_draw_alpha_fragment_spv), "swg-gpu-alpha-cache-fragment");
		if (!vertexShader || !alphaFragmentShader)
		{
			if (vertexShader)
				vkDestroyShaderModuleLocal(ms_device, vertexShader, 0);
			if (alphaFragmentShader)
				vkDestroyShaderModuleLocal(ms_device, alphaFragmentShader, 0);
			return 0;
		}

		VkPipelineShaderStageCreateInfo stages[2];
		memset(stages, 0, sizeof(stages));
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertexShader;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = alphaFragmentShader;
		stages[1].pName = "main";

		VkVertexInputBindingDescription bindingDescription;
		memset(&bindingDescription, 0, sizeof(bindingDescription));
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(GpuScreenVertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attributes[5];
		memset(attributes, 0, sizeof(attributes));
		attributes[0].location = 0;
		attributes[0].binding = 0;
		attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributes[0].offset = offsetof(GpuScreenVertex, x);
		attributes[1].location = 1;
		attributes[1].binding = 0;
		attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
		attributes[1].offset = offsetof(GpuScreenVertex, u);
		attributes[2].location = 2;
		attributes[2].binding = 0;
		attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributes[2].offset = offsetof(GpuScreenVertex, r);
		attributes[3].location = 3;
		attributes[3].binding = 0;
		attributes[3].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributes[3].offset = offsetof(GpuScreenVertex, cubeDirX);
		attributes[4].location = 4;
		attributes[4].binding = 0;
		attributes[4].format = VK_FORMAT_R32G32_SFLOAT;
		attributes[4].offset = offsetof(GpuScreenVertex, u2);

		VkPipelineVertexInputStateCreateInfo vertexInput;
		memset(&vertexInput, 0, sizeof(vertexInput));
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &bindingDescription;
		vertexInput.vertexAttributeDescriptionCount = 5;
		vertexInput.pVertexAttributeDescriptions = attributes;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly;
		memset(&inputAssembly, 0, sizeof(inputAssembly));
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewportState;
		memset(&viewportState, 0, sizeof(viewportState));
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterization;
		memset(&rasterization, 0, sizeof(rasterization));
		rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization.cullMode = VK_CULL_MODE_NONE;
		rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterization.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisample;
		memset(&multisample, 0, sizeof(multisample));
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorAttachment;
		memset(&colorAttachment, 0, sizeof(colorAttachment));
		colorAttachment.colorWriteMask = clientColorWriteMaskToVulkan(key.colorWriteMask);
		colorAttachment.blendEnable = VK_TRUE;
		colorAttachment.srcColorBlendFactor = clientBlendToVulkan(key.source);
		colorAttachment.dstColorBlendFactor = clientBlendToVulkan(key.destination);
		colorAttachment.colorBlendOp = clientBlendOpToVulkan(key.operation);
		colorAttachment.srcAlphaBlendFactor = clientAlphaBlendToVulkan(key.source);
		colorAttachment.dstAlphaBlendFactor = clientAlphaBlendToVulkan(key.destination);
		colorAttachment.alphaBlendOp = clientBlendOpToVulkan(key.operation);

		VkPipelineColorBlendStateCreateInfo colorBlend;
		memset(&colorBlend, 0, sizeof(colorBlend));
		colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlend.attachmentCount = 1;
		colorBlend.pAttachments = &colorAttachment;

		VkPipelineDepthStencilStateCreateInfo depthStencil;
		memset(&depthStencil, 0, sizeof(depthStencil));
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = key.depthTest ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState;
		memset(&dynamicState, 0, sizeof(dynamicState));
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		VkGraphicsPipelineCreateInfo pipelineInfo;
		memset(&pipelineInfo, 0, sizeof(pipelineInfo));
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = stages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterization;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlend;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = ms_gpuDrawPipelineLayout;
		pipelineInfo.renderPass = ms_swapchainRenderPass;
		pipelineInfo.subpass = 0;

		VkPipeline pipeline = 0;
		VkResult const result = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &pipeline);
		vkDestroyShaderModuleLocal(ms_device, vertexShader, 0);
		vkDestroyShaderModuleLocal(ms_device, alphaFragmentShader, 0);
		if (result != VK_SUCCESS || !pipeline)
		{
			char text[192];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu alpha legacy failed result=%d depth=%d op=%d src=%d dst=%d write=0x%02x",
				static_cast<int>(result),
				key.depthTest ? 1 : 0,
				static_cast<int>(key.operation),
				static_cast<int>(key.source),
				static_cast<int>(key.destination),
				static_cast<unsigned>(key.colorWriteMask));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return 0;
		}
		return pipeline;
	}

	VkPipeline getGpuAlphaBlendPipeline(GpuDrawBatch const &batch)
	{
		if (!ms_device || !ms_gpuDrawPipelineLayout || !ms_swapchainRenderPass)
			return 0;
		GpuAlphaPipelineKey key;
		key.depthTest = batch.depthTest;
		key.operation = batch.alphaBlendOperation;
		key.source = batch.alphaBlendSource;
		key.destination = batch.alphaBlendDestination;
		key.colorWriteMask = batch.colorWriteMask;
		GpuAlphaPipelineMap::const_iterator found = ms_gpuAlphaPipelineMap.find(key);
		if (found != ms_gpuAlphaPipelineMap.end())
			return found->second;
		VkPipeline const pipeline = createGpuAlphaBlendPipeline(key);
		if (pipeline)
		{
			ms_gpuAlphaPipelineMap[key] = pipeline;
			static int s_alphaPipelineCreateLogCount = 0;
			if (s_alphaPipelineCreateLogCount < 64)
			{
				char text[192];
				_snprintf(text, sizeof(text) - 1, "gpu alpha legacy pipeline created count=%d depth=%d op=%d src=%d dst=%d write=0x%02x pipeline=%p",
					s_alphaPipelineCreateLogCount + 1,
					key.depthTest ? 1 : 0,
					static_cast<int>(key.operation),
					static_cast<int>(key.source),
					static_cast<int>(key.destination),
					static_cast<unsigned>(key.colorWriteMask),
					reinterpret_cast<void *>(pipeline));
				text[sizeof(text) - 1] = 0;
				logLine(text);
				++s_alphaPipelineCreateLogCount;
			}
		}
		return pipeline;
	}

	void destroyGpuDrawResources()
	{
		if (ms_device)
		{
			vkDeviceWaitIdleLocal(ms_device);
			releasePendingTextureStaging("destroyGpuDrawResources");
		}

		for (GpuAlphaPipelineMap::iterator i = ms_gpuAlphaPipelineMap.begin(); i != ms_gpuAlphaPipelineMap.end(); ++i)
		{
			if (i->second)
				vkDestroyPipelineLocal(ms_device, i->second, 0);
		}
		ms_gpuAlphaPipelineMap.clear();

		if (ms_gpuDrawPipeline)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipeline, 0);
			ms_gpuDrawPipeline = 0;
		}
		if (ms_gpuDrawPipelineOpaque)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineOpaque, 0);
			ms_gpuDrawPipelineOpaque = 0;
		}
		if (ms_gpuDrawPipelineOpaqueNoWrite)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineOpaqueNoWrite, 0);
			ms_gpuDrawPipelineOpaqueNoWrite = 0;
		}
		if (ms_gpuDrawPipelineAlphaNoDepth)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineAlphaNoDepth, 0);
			ms_gpuDrawPipelineAlphaNoDepth = 0;
		}
		if (ms_gpuDrawPipelineAlphaAdd)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineAlphaAdd, 0);
			ms_gpuDrawPipelineAlphaAdd = 0;
		}
		if (ms_gpuDrawPipelineAlphaAddNoDepth)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineAlphaAddNoDepth, 0);
			ms_gpuDrawPipelineAlphaAddNoDepth = 0;
		}
		if (ms_gpuDrawPipelineAlphaOneOne)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineAlphaOneOne, 0);
			ms_gpuDrawPipelineAlphaOneOne = 0;
		}
		if (ms_gpuDrawPipelineAlphaOneOneNoDepth)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineAlphaOneOneNoDepth, 0);
			ms_gpuDrawPipelineAlphaOneOneNoDepth = 0;
		}
		if (ms_gpuDrawPipelineAlphaSrcColorOne)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineAlphaSrcColorOne, 0);
			ms_gpuDrawPipelineAlphaSrcColorOne = 0;
		}
		if (ms_gpuDrawPipelineAlphaSrcColorOneNoDepth)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineAlphaSrcColorOneNoDepth, 0);
			ms_gpuDrawPipelineAlphaSrcColorOneNoDepth = 0;
		}
		if (ms_gpuDrawPipelineActorAux)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineActorAux, 0);
			ms_gpuDrawPipelineActorAux = 0;
		}
		if (ms_gpuDrawPipelineCubeAlphaNoDepth)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineCubeAlphaNoDepth, 0);
			ms_gpuDrawPipelineCubeAlphaNoDepth = 0;
		}
		if (ms_gpuDrawPipelineOpaqueNoDepth)
		{
			vkDestroyPipelineLocal(ms_device, ms_gpuDrawPipelineOpaqueNoDepth, 0);
			ms_gpuDrawPipelineOpaqueNoDepth = 0;
		}
		if (ms_gpuDrawPipelineLayout)
		{
			vkDestroyPipelineLayoutLocal(ms_device, ms_gpuDrawPipelineLayout, 0);
			ms_gpuDrawPipelineLayout = 0;
		}
		if (ms_gpuDrawDescriptorPool)
		{
			vkDestroyDescriptorPoolLocal(ms_device, ms_gpuDrawDescriptorPool, 0);
			ms_gpuDrawDescriptorPool = 0;
		}
		if (ms_pairedTextureDescriptorMap)
			ms_pairedTextureDescriptorMap->clear();
		++ms_gpuDescriptorGeneration;
		ms_uiConstantsDescriptorSet = 0;
		ms_whiteTextureDescriptorSet = 0;
		if (ms_emptyDescriptorSetLayout)
		{
			vkDestroyDescriptorSetLayoutLocal(ms_device, ms_emptyDescriptorSetLayout, 0);
			ms_emptyDescriptorSetLayout = 0;
		}
		if (ms_uiConstantsDescriptorSetLayout)
		{
			vkDestroyDescriptorSetLayoutLocal(ms_device, ms_uiConstantsDescriptorSetLayout, 0);
			ms_uiConstantsDescriptorSetLayout = 0;
		}
		if (ms_whiteTextureDescriptorSetLayout)
		{
			vkDestroyDescriptorSetLayoutLocal(ms_device, ms_whiteTextureDescriptorSetLayout, 0);
			ms_whiteTextureDescriptorSetLayout = 0;
		}
		if (ms_whiteTextureSampler)
		{
			vkDestroySamplerLocal(ms_device, ms_whiteTextureSampler, 0);
			ms_whiteTextureSampler = 0;
		}
		if (ms_clampTextureSampler)
		{
			vkDestroySamplerLocal(ms_device, ms_clampTextureSampler, 0);
			ms_clampTextureSampler = 0;
		}
		if (ms_pointTextureSampler)
		{
			vkDestroySamplerLocal(ms_device, ms_pointTextureSampler, 0);
			ms_pointTextureSampler = 0;
		}
		if (ms_whiteTextureView)
		{
			vkDestroyImageViewLocal(ms_device, ms_whiteTextureView, 0);
			ms_whiteTextureView = 0;
		}
		if (ms_whiteTextureImage)
		{
			vkDestroyImageLocal(ms_device, ms_whiteTextureImage, 0);
			ms_whiteTextureImage = 0;
		}
		if (ms_whiteTextureMemory)
		{
			vkFreeMemoryLocal(ms_device, ms_whiteTextureMemory, 0);
			ms_whiteTextureMemory = 0;
		}
		if (ms_gpuUiConstantsMapped)
		{
			vkUnmapMemoryLocal(ms_device, ms_gpuUiConstantsMemory);
			ms_gpuUiConstantsMapped = 0;
		}
		if (ms_gpuUiConstantsBuffer)
		{
			vkDestroyBufferLocal(ms_device, ms_gpuUiConstantsBuffer, 0);
			ms_gpuUiConstantsBuffer = 0;
		}
		if (ms_gpuUiConstantsMemory)
		{
			vkFreeMemoryLocal(ms_device, ms_gpuUiConstantsMemory, 0);
			ms_gpuUiConstantsMemory = 0;
		}
		ms_whiteTextureNeedsLayoutTransition = false;
		ms_gpuDrawBatches.clear();
	}

	bool createGpuDrawResources()
	{
		destroyGpuDrawResources();

		void *constantsMapped = 0;
		if (!createHostVisibleBuffer(sizeof(GpuUiConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ms_gpuUiConstantsBuffer, ms_gpuUiConstantsMemory, &constantsMapped, "gpu ui constants"))
			return false;
		ms_gpuUiConstantsMapped = static_cast<GpuUiConstants *>(constantsMapped);
		ms_gpuUiConstantsMapped->scale[0] = 1.0f;
		ms_gpuUiConstantsMapped->scale[1] = 1.0f;
		ms_gpuUiConstantsMapped->translate[0] = 0.0f;
		ms_gpuUiConstantsMapped->translate[1] = 0.0f;

		VkImageCreateInfo imageInfo;
		memset(&imageInfo, 0, sizeof(imageInfo));
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageInfo.extent.width = 1;
		imageInfo.extent.height = 1;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		if (vkCreateImageLocal(ms_device, &imageInfo, 0, &ms_whiteTextureImage) != VK_SUCCESS)
		{
			logLine("vkCreateImage white texture failed");
			return false;
		}

		VkMemoryRequirements imageRequirements;
		memset(&imageRequirements, 0, sizeof(imageRequirements));
		vkGetImageMemoryRequirementsLocal(ms_device, ms_whiteTextureImage, &imageRequirements);
		uint32_t const imageMemoryType = findMemoryType(imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (imageMemoryType == 0xffffffff)
		{
			logLine("white texture memory type not found");
			return false;
		}

		VkMemoryAllocateInfo imageAllocateInfo;
		memset(&imageAllocateInfo, 0, sizeof(imageAllocateInfo));
		imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		imageAllocateInfo.allocationSize = imageRequirements.size;
		imageAllocateInfo.memoryTypeIndex = imageMemoryType;
		if (vkAllocateMemoryLocal(ms_device, &imageAllocateInfo, 0, &ms_whiteTextureMemory) != VK_SUCCESS)
		{
			logLine("vkAllocateMemory white texture failed");
			return false;
		}

		if (vkBindImageMemoryLocal(ms_device, ms_whiteTextureImage, ms_whiteTextureMemory, 0) != VK_SUCCESS)
		{
			logLine("vkBindImageMemory white texture failed");
			return false;
		}

		VkImageSubresource subresource;
		memset(&subresource, 0, sizeof(subresource));
		subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkSubresourceLayout layout;
		memset(&layout, 0, sizeof(layout));
		vkGetImageSubresourceLayoutLocal(ms_device, ms_whiteTextureImage, &subresource, &layout);

		void *whiteMapped = 0;
		if (vkMapMemoryLocal(ms_device, ms_whiteTextureMemory, 0, imageRequirements.size, 0, &whiteMapped) != VK_SUCCESS || !whiteMapped)
		{
			logLine("vkMapMemory white texture failed");
			return false;
		}
		uint32 *whitePixel = reinterpret_cast<uint32 *>(static_cast<byte *>(whiteMapped) + layout.offset);
		*whitePixel = 0xffffffff;
		vkUnmapMemoryLocal(ms_device, ms_whiteTextureMemory);
		ms_whiteTextureNeedsLayoutTransition = true;

		VkImageViewCreateInfo whiteViewInfo;
		memset(&whiteViewInfo, 0, sizeof(whiteViewInfo));
		whiteViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		whiteViewInfo.image = ms_whiteTextureImage;
		whiteViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		whiteViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		whiteViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		whiteViewInfo.subresourceRange.levelCount = 1;
		whiteViewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageViewLocal(ms_device, &whiteViewInfo, 0, &ms_whiteTextureView) != VK_SUCCESS)
		{
			logLine("vkCreateImageView white texture failed");
			return false;
		}

		VkSamplerCreateInfo samplerInfo;
		memset(&samplerInfo, 0, sizeof(samplerInfo));
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.maxLod = 16.0f;
		if (ms_samplerAnisotropyAvailable && gpuTextureAnisotropyEnabled())
		{
			samplerInfo.anisotropyEnable = VK_TRUE;
			samplerInfo.maxAnisotropy = std::max(1.0f, std::min(ms_samplerMaxAnisotropy, gpuTextureAnisotropyLevel()));
		}
		if (vkCreateSamplerLocal(ms_device, &samplerInfo, 0, &ms_whiteTextureSampler) != VK_SUCCESS)
		{
			logLine("vkCreateSampler white texture failed");
			return false;
		}
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		if (vkCreateSamplerLocal(ms_device, &samplerInfo, 0, &ms_clampTextureSampler) != VK_SUCCESS)
		{
			logLine("vkCreateSampler clamp texture failed");
			return false;
		}
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.maxLod = 0.0f;
		if (vkCreateSamplerLocal(ms_device, &samplerInfo, 0, &ms_pointTextureSampler) != VK_SUCCESS)
		{
			logLine("vkCreateSampler point texture failed");
			return false;
		}
		logLine("gpu mask point sampler enabled");

		VkDescriptorSetLayoutCreateInfo emptyLayoutInfo;
		memset(&emptyLayoutInfo, 0, sizeof(emptyLayoutInfo));
		emptyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		if (vkCreateDescriptorSetLayoutLocal(ms_device, &emptyLayoutInfo, 0, &ms_emptyDescriptorSetLayout) != VK_SUCCESS)
		{
			logLine("vkCreateDescriptorSetLayout empty failed");
			return false;
		}

		VkDescriptorSetLayoutBinding constantsBinding;
		memset(&constantsBinding, 0, sizeof(constantsBinding));
		constantsBinding.binding = 0;
		constantsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		constantsBinding.descriptorCount = 1;
		constantsBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		VkDescriptorSetLayoutCreateInfo constantsLayoutInfo;
		memset(&constantsLayoutInfo, 0, sizeof(constantsLayoutInfo));
		constantsLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		constantsLayoutInfo.bindingCount = 1;
		constantsLayoutInfo.pBindings = &constantsBinding;
		if (vkCreateDescriptorSetLayoutLocal(ms_device, &constantsLayoutInfo, 0, &ms_uiConstantsDescriptorSetLayout) != VK_SUCCESS)
		{
			logLine("vkCreateDescriptorSetLayout constants failed");
			return false;
		}

		VkDescriptorSetLayoutBinding textureBindings[2];
		memset(textureBindings, 0, sizeof(textureBindings));
		textureBindings[0].binding = 0;
		textureBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		textureBindings[0].descriptorCount = 1;
		textureBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		textureBindings[1] = textureBindings[0];
		textureBindings[1].binding = 1;
		VkDescriptorSetLayoutCreateInfo textureLayoutInfo;
		memset(&textureLayoutInfo, 0, sizeof(textureLayoutInfo));
		textureLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		textureLayoutInfo.bindingCount = 2;
		textureLayoutInfo.pBindings = textureBindings;
		if (vkCreateDescriptorSetLayoutLocal(ms_device, &textureLayoutInfo, 0, &ms_whiteTextureDescriptorSetLayout) != VK_SUCCESS)
		{
			logLine("vkCreateDescriptorSetLayout texture failed");
			return false;
		}

		VkDescriptorPoolSize poolSizes[2];
		memset(poolSizes, 0, sizeof(poolSizes));
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = 16384;
		VkDescriptorPoolCreateInfo poolInfo;
		memset(&poolInfo, 0, sizeof(poolInfo));
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 8193;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = poolSizes;
		if (vkCreateDescriptorPoolLocal(ms_device, &poolInfo, 0, &ms_gpuDrawDescriptorPool) != VK_SUCCESS)
		{
			logLine("vkCreateDescriptorPool gpu draw failed");
			return false;
		}

		VkDescriptorSetLayout allocateLayouts[2] = { ms_uiConstantsDescriptorSetLayout, ms_whiteTextureDescriptorSetLayout };
		VkDescriptorSet allocatedSets[2] = { 0, 0 };
		VkDescriptorSetAllocateInfo allocateInfo;
		memset(&allocateInfo, 0, sizeof(allocateInfo));
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = ms_gpuDrawDescriptorPool;
		allocateInfo.descriptorSetCount = 2;
		allocateInfo.pSetLayouts = allocateLayouts;
		if (vkAllocateDescriptorSetsLocal(ms_device, &allocateInfo, allocatedSets) != VK_SUCCESS)
		{
			logLine("vkAllocateDescriptorSets gpu draw failed");
			return false;
		}
		ms_uiConstantsDescriptorSet = allocatedSets[0];
		ms_whiteTextureDescriptorSet = allocatedSets[1];

		VkDescriptorBufferInfo constantsDescriptor;
		memset(&constantsDescriptor, 0, sizeof(constantsDescriptor));
		constantsDescriptor.buffer = ms_gpuUiConstantsBuffer;
		constantsDescriptor.range = sizeof(GpuUiConstants);
		VkDescriptorImageInfo textureDescriptor;
		memset(&textureDescriptor, 0, sizeof(textureDescriptor));
		textureDescriptor.sampler = ms_whiteTextureSampler;
		textureDescriptor.imageView = ms_whiteTextureView;
		textureDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		VkWriteDescriptorSet writes[3];
		memset(writes, 0, sizeof(writes));
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = ms_uiConstantsDescriptorSet;
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[0].pBufferInfo = &constantsDescriptor;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = ms_whiteTextureDescriptorSet;
		writes[1].dstBinding = 0;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].pImageInfo = &textureDescriptor;
		writes[2] = writes[1];
		writes[2].dstBinding = 1;
		vkUpdateDescriptorSetsLocal(ms_device, 3, writes, 0, 0);

		VkDescriptorSetLayout pipelineLayouts[3] = { ms_emptyDescriptorSetLayout, ms_uiConstantsDescriptorSetLayout, ms_whiteTextureDescriptorSetLayout };
		VkPushConstantRange pushConstantRange;
		memset(&pushConstantRange, 0, sizeof(pushConstantRange));
		pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(GpuDrawPushConstants);
		VkPipelineLayoutCreateInfo pipelineLayoutInfo;
		memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 3;
		pipelineLayoutInfo.pSetLayouts = pipelineLayouts;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
		if (vkCreatePipelineLayoutLocal(ms_device, &pipelineLayoutInfo, 0, &ms_gpuDrawPipelineLayout) != VK_SUCCESS)
		{
			logLine("vkCreatePipelineLayout gpu draw failed");
			return false;
		}

		VkShaderModule vertexShader = createShaderModule(swg_gpu_draw_depth_vertex_spv, sizeof(swg_gpu_draw_depth_vertex_spv), "swg-gpu-depth-vertex");
		VkShaderModule cubeVertexShader = createShaderModule(swg_gpu_draw_cube_depth_vertex_spv, sizeof(swg_gpu_draw_cube_depth_vertex_spv), "swg-gpu-cube-depth-vertex");
		VkShaderModule alphaFragmentShader = createShaderModule(swg_gpu_draw_alpha_fragment_spv, sizeof(swg_gpu_draw_alpha_fragment_spv), "swg-gpu-alpha-fragment");
		VkShaderModule actorAuxFragmentShader = createShaderModule(swg_gpu_draw_actor_aux_fragment_spv, sizeof(swg_gpu_draw_actor_aux_fragment_spv), "swg-gpu-actor-aux-fragment");
		VkShaderModule cubeAlphaFragmentShader = createShaderModule(swg_gpu_draw_cube_alpha_fragment_spv, sizeof(swg_gpu_draw_cube_alpha_fragment_spv), "swg-gpu-cube-alpha-fragment");
		VkShaderModule opaqueFragmentShader = createShaderModule(swg_gpu_draw_fragment_spv, sizeof(swg_gpu_draw_fragment_spv), "swg-gpu-opaque-fragment");
		if (!vertexShader || !cubeVertexShader || !alphaFragmentShader || !actorAuxFragmentShader || !cubeAlphaFragmentShader || !opaqueFragmentShader)
		{
			if (vertexShader)
				vkDestroyShaderModuleLocal(ms_device, vertexShader, 0);
			if (cubeVertexShader)
				vkDestroyShaderModuleLocal(ms_device, cubeVertexShader, 0);
			if (alphaFragmentShader)
				vkDestroyShaderModuleLocal(ms_device, alphaFragmentShader, 0);
			if (actorAuxFragmentShader)
				vkDestroyShaderModuleLocal(ms_device, actorAuxFragmentShader, 0);
			if (cubeAlphaFragmentShader)
				vkDestroyShaderModuleLocal(ms_device, cubeAlphaFragmentShader, 0);
			if (opaqueFragmentShader)
				vkDestroyShaderModuleLocal(ms_device, opaqueFragmentShader, 0);
			return false;
		}

		VkPipelineShaderStageCreateInfo stages[2];
		memset(stages, 0, sizeof(stages));
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertexShader;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = alphaFragmentShader;
		stages[1].pName = "main";

		VkVertexInputBindingDescription bindingDescription;
		memset(&bindingDescription, 0, sizeof(bindingDescription));
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(GpuScreenVertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		VkVertexInputAttributeDescription attributes[5];
		memset(attributes, 0, sizeof(attributes));
		attributes[0].location = 0;
		attributes[0].binding = 0;
		attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributes[0].offset = offsetof(GpuScreenVertex, x);
		attributes[1].location = 1;
		attributes[1].binding = 0;
		attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
		attributes[1].offset = offsetof(GpuScreenVertex, u);
		attributes[2].location = 2;
		attributes[2].binding = 0;
		attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributes[2].offset = offsetof(GpuScreenVertex, r);
		attributes[3].location = 3;
		attributes[3].binding = 0;
		attributes[3].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributes[3].offset = offsetof(GpuScreenVertex, cubeDirX);
		attributes[4].binding = 0;
		attributes[4].location = 4;
		attributes[4].format = VK_FORMAT_R32G32_SFLOAT;
		attributes[4].offset = offsetof(GpuScreenVertex, u2);

		VkPipelineVertexInputStateCreateInfo vertexInput;
		memset(&vertexInput, 0, sizeof(vertexInput));
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &bindingDescription;
		vertexInput.vertexAttributeDescriptionCount = 5;
		vertexInput.pVertexAttributeDescriptions = attributes;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly;
		memset(&inputAssembly, 0, sizeof(inputAssembly));
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewportState;
		memset(&viewportState, 0, sizeof(viewportState));
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterization;
		memset(&rasterization, 0, sizeof(rasterization));
		rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization.cullMode = VK_CULL_MODE_NONE;
		rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterization.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisample;
		memset(&multisample, 0, sizeof(multisample));
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorAttachment;
		memset(&colorAttachment, 0, sizeof(colorAttachment));
		colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorAttachment.blendEnable = VK_TRUE;
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
		VkPipelineColorBlendStateCreateInfo colorBlend;
		memset(&colorBlend, 0, sizeof(colorBlend));
		colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlend.attachmentCount = 1;
		colorBlend.pAttachments = &colorAttachment;

		VkPipelineDepthStencilStateCreateInfo depthStencil;
		memset(&depthStencil, 0, sizeof(depthStencil));
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState;
		memset(&dynamicState, 0, sizeof(dynamicState));
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		VkGraphicsPipelineCreateInfo pipelineInfo;
		memset(&pipelineInfo, 0, sizeof(pipelineInfo));
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = stages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterization;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlend;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = ms_gpuDrawPipelineLayout;
		pipelineInfo.renderPass = ms_swapchainRenderPass;
		pipelineInfo.subpass = 0;
		VkResult const pipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipeline);
		colorAttachment.blendEnable = VK_FALSE;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		stages[1].module = opaqueFragmentShader;
		VkResult const opaqueNoWritePipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineOpaqueNoWrite);
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		stages[1].module = opaqueFragmentShader;
		VkResult const opaquePipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineOpaque);
		colorAttachment.blendEnable = VK_TRUE;
		depthStencil.depthTestEnable = VK_FALSE;
		depthStencil.depthWriteEnable = VK_FALSE;
		stages[1].module = alphaFragmentShader;
		VkResult const alphaNoDepthPipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineAlphaNoDepth);
		depthStencil.depthTestEnable = VK_TRUE;
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
		VkResult const alphaAddPipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineAlphaAdd);
		depthStencil.depthTestEnable = VK_FALSE;
		VkResult const alphaAddNoDepthPipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineAlphaAddNoDepth);
		depthStencil.depthTestEnable = VK_TRUE;
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		VkResult const alphaOneOnePipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineAlphaOneOne);
		depthStencil.depthTestEnable = VK_FALSE;
		VkResult const alphaOneOneNoDepthPipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineAlphaOneOneNoDepth);
		depthStencil.depthTestEnable = VK_TRUE;
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		VkResult const alphaSrcColorOnePipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineAlphaSrcColorOne);
		depthStencil.depthTestEnable = VK_FALSE;
		VkResult const alphaSrcColorOneNoDepthPipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineAlphaSrcColorOneNoDepth);
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		bool const actorAuxColor = gpuActorAuxColorEnabled();
		colorAttachment.blendEnable = VK_FALSE;
		colorAttachment.colorWriteMask = actorAuxColor ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) : 0;
		stages[1].module = actorAuxFragmentShader;
		VkResult const actorAuxPipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineActorAux);
		colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
		colorBlend.blendConstants[3] = 0.0f;
		colorAttachment.blendEnable = VK_TRUE;
		depthStencil.depthTestEnable = VK_FALSE;
		stages[0].module = cubeVertexShader;
		stages[1].module = cubeAlphaFragmentShader;
		VkResult const cubeAlphaNoDepthPipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineCubeAlphaNoDepth);
		stages[0].module = vertexShader;
		colorAttachment.blendEnable = VK_FALSE;
		stages[1].module = opaqueFragmentShader;
		VkResult const opaqueNoDepthPipelineResult = vkCreateGraphicsPipelinesLocal(ms_device, 0, 1, &pipelineInfo, 0, &ms_gpuDrawPipelineOpaqueNoDepth);
		vkDestroyShaderModuleLocal(ms_device, vertexShader, 0);
		vkDestroyShaderModuleLocal(ms_device, cubeVertexShader, 0);
		vkDestroyShaderModuleLocal(ms_device, alphaFragmentShader, 0);
		vkDestroyShaderModuleLocal(ms_device, actorAuxFragmentShader, 0);
		vkDestroyShaderModuleLocal(ms_device, cubeAlphaFragmentShader, 0);
		vkDestroyShaderModuleLocal(ms_device, opaqueFragmentShader, 0);
		if (pipelineResult != VK_SUCCESS || !ms_gpuDrawPipeline)
		{
			char text[128];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu alpha draw failed result=%d", static_cast<int>(pipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}
		if (opaqueNoWritePipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineOpaqueNoWrite)
		{
			char text[128];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu opaque no-write draw failed result=%d", static_cast<int>(opaqueNoWritePipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}
		if (opaquePipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineOpaque)
		{
			char text[128];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu opaque draw failed result=%d", static_cast<int>(opaquePipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}
		if (alphaNoDepthPipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineAlphaNoDepth)
		{
			char text[128];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu alpha no-depth draw failed result=%d", static_cast<int>(alphaNoDepthPipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}
		if (alphaAddPipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineAlphaAdd || alphaAddNoDepthPipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineAlphaAddNoDepth)
		{
			char text[160];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu alpha add draw failed result=%d/%d", static_cast<int>(alphaAddPipelineResult), static_cast<int>(alphaAddNoDepthPipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}
		if (alphaOneOnePipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineAlphaOneOne || alphaOneOneNoDepthPipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineAlphaOneOneNoDepth)
		{
			char text[160];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu alpha one-one draw failed result=%d/%d", static_cast<int>(alphaOneOnePipelineResult), static_cast<int>(alphaOneOneNoDepthPipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}
		if (alphaSrcColorOnePipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineAlphaSrcColorOne || alphaSrcColorOneNoDepthPipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineAlphaSrcColorOneNoDepth)
		{
			char text[192];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu alpha src-color-one draw failed result=%d/%d", static_cast<int>(alphaSrcColorOnePipelineResult), static_cast<int>(alphaSrcColorOneNoDepthPipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}
		if (actorAuxPipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineActorAux)
		{
			char text[128];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu actor aux draw failed result=%d", static_cast<int>(actorAuxPipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}
		if (cubeAlphaNoDepthPipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineCubeAlphaNoDepth)
		{
			char text[128];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu cube alpha no-depth draw failed result=%d", static_cast<int>(cubeAlphaNoDepthPipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}
		if (opaqueNoDepthPipelineResult != VK_SUCCESS || !ms_gpuDrawPipelineOpaqueNoDepth)
		{
			char text[128];
			_snprintf(text, sizeof(text) - 1, "vkCreateGraphicsPipelines gpu opaque no-depth draw failed result=%d", static_cast<int>(opaqueNoDepthPipelineResult));
			text[sizeof(text) - 1] = 0;
			logLine(text);
			return false;
		}

		logLine("gpu draw resources ok alpha+opaque depth/no-depth pipelines+descriptors+whiteTexture");
		return true;
	}

	bool createFrameUploadResources()
	{
		destroyFrameUploadResources();

		size_t const pixelCount = static_cast<size_t>(ms_swapchainExtent.width) * static_cast<size_t>(ms_swapchainExtent.height);
		if (pixelCount == 0)
			return false;

		VkDeviceSize const byteSize = static_cast<VkDeviceSize>(pixelCount * sizeof(uint32));
		VkBufferCreateInfo bufferInfo;
		memset(&bufferInfo, 0, sizeof(bufferInfo));
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = byteSize;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBufferLocal(ms_device, &bufferInfo, 0, &ms_frameUploadBuffer) != VK_SUCCESS)
		{
			logLine("vkCreateBuffer frame upload failed");
			return false;
		}

		VkMemoryRequirements requirements;
		memset(&requirements, 0, sizeof(requirements));
		vkGetBufferMemoryRequirementsLocal(ms_device, ms_frameUploadBuffer, &requirements);

		uint32_t const memoryType = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memoryType == 0xffffffff)
		{
			logLine("frame upload memory type not found");
			return false;
		}

		VkMemoryAllocateInfo allocateInfo;
		memset(&allocateInfo, 0, sizeof(allocateInfo));
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = memoryType;
		if (vkAllocateMemoryLocal(ms_device, &allocateInfo, 0, &ms_frameUploadMemory) != VK_SUCCESS)
		{
			logLine("vkAllocateMemory frame upload failed");
			return false;
		}

		if (vkBindBufferMemoryLocal(ms_device, ms_frameUploadBuffer, ms_frameUploadMemory, 0) != VK_SUCCESS)
		{
			logLine("vkBindBufferMemory frame upload failed");
			return false;
		}

		void *mapped = 0;
		if (vkMapMemoryLocal(ms_device, ms_frameUploadMemory, 0, byteSize, 0, &mapped) != VK_SUCCESS || !mapped)
		{
			logLine("vkMapMemory frame upload failed");
			return false;
		}

		ms_frameUploadMapped = static_cast<uint32 *>(mapped);
		ms_framePixels.assign(pixelCount, ms_clearColor);

		char buffer[160];
		_snprintf(buffer, sizeof(buffer) - 1, "frame upload ok bytes=%u", static_cast<unsigned>(byteSize));
		buffer[sizeof(buffer) - 1] = 0;
		logLine(buffer);
		return true;
	}

	bool createSwapchainRenderTargets()
	{
		if (!ms_device || ms_swapchainImageViews.empty())
			return false;

		ms_depthFormat = VK_FORMAT_D32_SFLOAT;
		VkImageCreateInfo depthImageInfo;
		memset(&depthImageInfo, 0, sizeof(depthImageInfo));
		depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
		depthImageInfo.format = ms_depthFormat;
		depthImageInfo.extent.width = ms_swapchainExtent.width;
		depthImageInfo.extent.height = ms_swapchainExtent.height;
		depthImageInfo.extent.depth = 1;
		depthImageInfo.mipLevels = 1;
		depthImageInfo.arrayLayers = 1;
		depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (vkCreateImageLocal(ms_device, &depthImageInfo, 0, &ms_depthImage) != VK_SUCCESS)
		{
			logLine("vkCreateImage depth failed");
			return false;
		}

		VkMemoryRequirements depthRequirements;
		memset(&depthRequirements, 0, sizeof(depthRequirements));
		vkGetImageMemoryRequirementsLocal(ms_device, ms_depthImage, &depthRequirements);
		uint32_t const depthMemoryType = findMemoryType(depthRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (depthMemoryType == 0xffffffff)
		{
			logLine("depth memory type not found");
			return false;
		}

		VkMemoryAllocateInfo depthAllocateInfo;
		memset(&depthAllocateInfo, 0, sizeof(depthAllocateInfo));
		depthAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		depthAllocateInfo.allocationSize = depthRequirements.size;
		depthAllocateInfo.memoryTypeIndex = depthMemoryType;
		if (vkAllocateMemoryLocal(ms_device, &depthAllocateInfo, 0, &ms_depthMemory) != VK_SUCCESS)
		{
			logLine("vkAllocateMemory depth failed");
			return false;
		}
		if (vkBindImageMemoryLocal(ms_device, ms_depthImage, ms_depthMemory, 0) != VK_SUCCESS)
		{
			logLine("vkBindImageMemory depth failed");
			return false;
		}

		VkImageViewCreateInfo depthViewInfo;
		memset(&depthViewInfo, 0, sizeof(depthViewInfo));
		depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		depthViewInfo.image = ms_depthImage;
		depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthViewInfo.format = ms_depthFormat;
		depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		depthViewInfo.subresourceRange.levelCount = 1;
		depthViewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageViewLocal(ms_device, &depthViewInfo, 0, &ms_depthImageView) != VK_SUCCESS)
		{
			logLine("vkCreateImageView depth failed");
			return false;
		}

		VkAttachmentDescription colorAttachment;
		memset(&colorAttachment, 0, sizeof(colorAttachment));
		colorAttachment.format = ms_swapchainFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorReference;
		memset(&colorReference, 0, sizeof(colorReference));
		colorReference.attachment = 0;
		colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription depthAttachment;
		memset(&depthAttachment, 0, sizeof(depthAttachment));
		depthAttachment.format = ms_depthFormat;
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthReference;
		memset(&depthReference, 0, sizeof(depthReference));
		depthReference.attachment = 1;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass;
		memset(&subpass, 0, sizeof(subpass));
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		subpass.pDepthStencilAttachment = &depthReference;

		VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };
		VkRenderPassCreateInfo renderPassInfo;
		memset(&renderPassInfo, 0, sizeof(renderPassInfo));
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 2;
		renderPassInfo.pAttachments = attachments;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		if (vkCreateRenderPassLocal(ms_device, &renderPassInfo, 0, &ms_swapchainRenderPass) != VK_SUCCESS)
		{
			logLine("vkCreateRenderPass swapchain failed");
			return false;
		}

		ms_swapchainFramebuffers.resize(ms_swapchainImageViews.size());
		for (size_t i = 0; i < ms_swapchainImageViews.size(); ++i)
		{
			VkImageView framebufferAttachments[] = { ms_swapchainImageViews[i], ms_depthImageView };
			VkFramebufferCreateInfo framebufferInfo;
			memset(&framebufferInfo, 0, sizeof(framebufferInfo));
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = ms_swapchainRenderPass;
			framebufferInfo.attachmentCount = 2;
			framebufferInfo.pAttachments = framebufferAttachments;
			framebufferInfo.width = ms_swapchainExtent.width;
			framebufferInfo.height = ms_swapchainExtent.height;
			framebufferInfo.layers = 1;
			if (vkCreateFramebufferLocal(ms_device, &framebufferInfo, 0, &ms_swapchainFramebuffers[i]) != VK_SUCCESS)
			{
				logLine("vkCreateFramebuffer swapchain failed");
				return false;
			}
		}

		char buffer[160];
		_snprintf(buffer, sizeof(buffer) - 1, "swapchain render targets ok framebuffers=%u depthFormat=%d", static_cast<unsigned>(ms_swapchainFramebuffers.size()), static_cast<int>(ms_depthFormat));
		buffer[sizeof(buffer) - 1] = 0;
		logLine(buffer);
		return true;
	}

	void destroySwapchain()
	{
		if (ms_device)
			vkDeviceWaitIdleLocal(ms_device);

		destroyGpuDrawResources();
		destroyGpuTriangleResources();
		destroyGpuReadbackResources();
		destroyFrameUploadResources();

		for (size_t i = 0; i < ms_swapchainFramebuffers.size(); ++i)
		{
			if (ms_swapchainFramebuffers[i])
				vkDestroyFramebufferLocal(ms_device, ms_swapchainFramebuffers[i], 0);
		}
		ms_swapchainFramebuffers.clear();

		if (ms_depthImageView)
		{
			vkDestroyImageViewLocal(ms_device, ms_depthImageView, 0);
			ms_depthImageView = 0;
		}
		if (ms_depthImage)
		{
			vkDestroyImageLocal(ms_device, ms_depthImage, 0);
			ms_depthImage = 0;
		}
		if (ms_depthMemory)
		{
			vkFreeMemoryLocal(ms_device, ms_depthMemory, 0);
			ms_depthMemory = 0;
		}
		ms_depthFormat = VK_FORMAT_UNDEFINED;

		if (ms_swapchainRenderPass)
		{
			vkDestroyRenderPassLocal(ms_device, ms_swapchainRenderPass, 0);
			ms_swapchainRenderPass = 0;
		}

		for (size_t i = 0; i < ms_swapchainImageViews.size(); ++i)
		{
			if (ms_swapchainImageViews[i])
				vkDestroyImageViewLocal(ms_device, ms_swapchainImageViews[i], 0);
		}
		ms_swapchainImageViews.clear();
		ms_swapchainImages.clear();
		ms_commandBuffers.clear();

		if (ms_commandPool)
		{
			vkDestroyCommandPoolLocal(ms_device, ms_commandPool, 0);
			ms_commandPool = 0;
		}

		if (ms_swapchain)
		{
			vkDestroySwapchainKHRLocal(ms_device, ms_swapchain, 0);
			ms_swapchain = 0;
		}
	}

	VkSurfaceFormatKHR chooseSurfaceFormat(std::vector<VkSurfaceFormatKHR> const &formats)
	{
		for (size_t i = 0; i < formats.size(); ++i)
		{
			if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				return formats[i];
		}
		return formats.empty() ? VkSurfaceFormatKHR() : formats[0];
	}

	VkPresentModeKHR choosePresentMode(std::vector<VkPresentModeKHR> const &presentModes)
	{
		char vsyncText[16];
		DWORD const vsyncLength = GetEnvironmentVariableA("SWG_VULKAN_VSYNC", vsyncText, sizeof(vsyncText));
		if (vsyncLength > 0 && vsyncLength < sizeof(vsyncText) && atoi(vsyncText) != 0)
			return VK_PRESENT_MODE_FIFO_KHR;

		for (size_t i = 0; i < presentModes.size(); ++i)
		{
			if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
				return VK_PRESENT_MODE_MAILBOX_KHR;
		}

		for (size_t i = 0; i < presentModes.size(); ++i)
		{
			if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
				return VK_PRESENT_MODE_IMMEDIATE_KHR;
		}

		return VK_PRESENT_MODE_FIFO_KHR;
	}

	bool createSwapchain()
	{
		logLine("createSwapchain begin");
		if (ms_window && ms_windowed && ms_width > 0 && ms_height > 0)
		{
			RECT rect;
			rect.left = 0;
			rect.top = 0;
			rect.right = ms_width;
			rect.bottom = ms_height;
			DWORD const style = static_cast<DWORD>(GetWindowLong(ms_window, GWL_STYLE));
			DWORD const exStyle = static_cast<DWORD>(GetWindowLong(ms_window, GWL_EXSTYLE));
			AdjustWindowRectEx(&rect, style, FALSE, exStyle);
			int const windowWidth = rect.right - rect.left;
			int const windowHeight = rect.bottom - rect.top;
			SetWindowPos(ms_window, 0, 0, 0, windowWidth, windowHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
		}

		VkSurfaceCapabilitiesKHR caps;
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHRLocal(ms_physicalDevice, ms_surface, &caps) != VK_SUCCESS)
		{
			logLine("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
			return false;
		}

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHRLocal(ms_physicalDevice, ms_surface, &formatCount, 0);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		if (formatCount)
			vkGetPhysicalDeviceSurfaceFormatsKHRLocal(ms_physicalDevice, ms_surface, &formatCount, &formats[0]);

		VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
		ms_swapchainFormat = surfaceFormat.format;

		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHRLocal(ms_physicalDevice, ms_surface, &presentModeCount, 0);
		std::vector<VkPresentModeKHR> presentModes(presentModeCount);
		if (presentModeCount)
			vkGetPhysicalDeviceSurfacePresentModesKHRLocal(ms_physicalDevice, ms_surface, &presentModeCount, &presentModes[0]);
		VkPresentModeKHR const presentMode = choosePresentMode(presentModes);

		ms_swapchainExtent = caps.currentExtent;
		if (ms_swapchainExtent.width == 0xffffffff)
		{
			ms_swapchainExtent.width = static_cast<uint32_t>(std::max(1, ms_width));
			ms_swapchainExtent.height = static_cast<uint32_t>(std::max(1, ms_height));
		}
		else if (ms_windowed && ms_width > 0 && ms_height > 0 && (static_cast<int>(ms_swapchainExtent.width) != ms_width || static_cast<int>(ms_swapchainExtent.height) != ms_height))
		{
			uint32_t const requestedWidth = static_cast<uint32_t>(std::max(1, ms_width));
			uint32_t const requestedHeight = static_cast<uint32_t>(std::max(1, ms_height));
			if (requestedWidth >= caps.minImageExtent.width && requestedWidth <= caps.maxImageExtent.width &&
				requestedHeight >= caps.minImageExtent.height && requestedHeight <= caps.maxImageExtent.height)
			{
				ms_swapchainExtent.width = requestedWidth;
				ms_swapchainExtent.height = requestedHeight;
			}
		}

		uint32_t imageCount = caps.minImageCount + 1;
		if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
			imageCount = caps.maxImageCount;

		VkSwapchainCreateInfoKHR createInfo;
		memset(&createInfo, 0, sizeof(createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = ms_surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = ms_swapchainFormat;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = ms_swapchainExtent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.preTransform = caps.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = 0;

		if (vkCreateSwapchainKHRLocal(ms_device, &createInfo, 0, &ms_swapchain) != VK_SUCCESS)
		{
			logLine("vkCreateSwapchainKHR failed");
			return false;
		}

		uint32_t swapchainImageCount = 0;
		vkGetSwapchainImagesKHRLocal(ms_device, ms_swapchain, &swapchainImageCount, 0);
		ms_swapchainImages.resize(swapchainImageCount);
			vkGetSwapchainImagesKHRLocal(ms_device, ms_swapchain, &swapchainImageCount, &ms_swapchainImages[0]);

		if (!createFrameUploadResources() || !createGpuTriangleResources() || !createGpuReadbackResources())
			return false;

		ms_swapchainImageViews.resize(ms_swapchainImages.size());
		for (size_t i = 0; i < ms_swapchainImages.size(); ++i)
		{
			VkImageViewCreateInfo viewInfo;
			memset(&viewInfo, 0, sizeof(viewInfo));
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = ms_swapchainImages[i];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = ms_swapchainFormat;
			viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;
			if (vkCreateImageViewLocal(ms_device, &viewInfo, 0, &ms_swapchainImageViews[i]) != VK_SUCCESS)
				return false;
		}

		if (!createSwapchainRenderTargets())
			return false;
		if (!createGpuDrawResources())
			return false;

		VkCommandPoolCreateInfo poolInfo;
		memset(&poolInfo, 0, sizeof(poolInfo));
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = ms_queueFamilyIndex;
		if (vkCreateCommandPoolLocal(ms_device, &poolInfo, 0, &ms_commandPool) != VK_SUCCESS)
			return false;

		ms_commandBuffers.resize(ms_swapchainImages.size());
		VkCommandBufferAllocateInfo allocateInfo;
		memset(&allocateInfo, 0, sizeof(allocateInfo));
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = ms_commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = static_cast<uint32_t>(ms_commandBuffers.size());
		if (vkAllocateCommandBuffersLocal(ms_device, &allocateInfo, &ms_commandBuffers[0]) != VK_SUCCESS)
		{
			logLine("vkAllocateCommandBuffers failed");
			return false;
		}

		char buffer[256];
		_snprintf(buffer, sizeof(buffer) - 1, "createSwapchain ok images=%u extent=%ux%u presentMode=%d", static_cast<unsigned>(swapchainImageCount), static_cast<unsigned>(ms_swapchainExtent.width), static_cast<unsigned>(ms_swapchainExtent.height), static_cast<int>(presentMode));
		buffer[sizeof(buffer) - 1] = 0;
		logLine(buffer);
		return true;
	}

	bool createDevice()
	{
		logLine("createDevice begin");
		uint32_t deviceCount = 0;
		if (vkEnumeratePhysicalDevicesLocal(ms_instance, &deviceCount, 0) != VK_SUCCESS || deviceCount == 0)
		{
			logLine("no Vulkan physical devices");
			return false;
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevicesLocal(ms_instance, &deviceCount, &devices[0]);

		for (uint32_t d = 0; d < deviceCount; ++d)
		{
			uint32_t queueCount = 0;
			vkGetPhysicalDeviceQueueFamilyPropertiesLocal(devices[d], &queueCount, 0);
			std::vector<VkQueueFamilyProperties> queues(queueCount);
			vkGetPhysicalDeviceQueueFamilyPropertiesLocal(devices[d], &queueCount, &queues[0]);

			for (uint32_t q = 0; q < queueCount; ++q)
			{
				VkBool32 presentSupported = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHRLocal(devices[d], q, ms_surface, &presentSupported);
				if ((queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupported)
				{
					ms_physicalDevice = devices[d];
					ms_queueFamilyIndex = q;
					break;
				}
			}

			if (ms_physicalDevice)
				break;
		}

		if (!ms_physicalDevice)
		{
			logLine("no graphics+present queue");
			return false;
		}

		VkPhysicalDeviceFeatures supportedFeatures;
		memset(&supportedFeatures, 0, sizeof(supportedFeatures));
		vkGetPhysicalDeviceFeaturesLocal(ms_physicalDevice, &supportedFeatures);
		VkPhysicalDeviceProperties physicalDeviceProperties;
		memset(&physicalDeviceProperties, 0, sizeof(physicalDeviceProperties));
		vkGetPhysicalDevicePropertiesLocal(ms_physicalDevice, &physicalDeviceProperties);
		VkPhysicalDeviceFeatures enabledFeatures;
		memset(&enabledFeatures, 0, sizeof(enabledFeatures));
		ms_samplerAnisotropyAvailable = supportedFeatures.samplerAnisotropy == VK_TRUE;
		ms_samplerMaxAnisotropy = ms_samplerAnisotropyAvailable ? physicalDeviceProperties.limits.maxSamplerAnisotropy : 1.0f;
		if (ms_samplerAnisotropyAvailable)
			enabledFeatures.samplerAnisotropy = VK_TRUE;

		char featureBuffer[512];
		_snprintf(featureBuffer, sizeof(featureBuffer) - 1, "physical device '%s' samplerAnisotropy=%u maxAnisotropy=%.1f", physicalDeviceProperties.deviceName, ms_samplerAnisotropyAvailable ? 1u : 0u, ms_samplerMaxAnisotropy);
		featureBuffer[sizeof(featureBuffer) - 1] = 0;
		logLine(featureBuffer);

		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo;
		memset(&queueInfo, 0, sizeof(queueInfo));
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = ms_queueFamilyIndex;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;

		char const *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		VkDeviceCreateInfo deviceInfo;
		memset(&deviceInfo, 0, sizeof(deviceInfo));
		deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = 1;
		deviceInfo.ppEnabledExtensionNames = extensions;
		deviceInfo.pEnabledFeatures = &enabledFeatures;

		if (vkCreateDeviceLocal(ms_physicalDevice, &deviceInfo, 0, &ms_device) != VK_SUCCESS)
		{
			logLine("vkCreateDevice failed");
			return false;
		}

		vkGetDeviceProcAddrLocal = reinterpret_cast<PFN_vkGetDeviceProcAddr>(vkGetInstanceProcAddrLocal(ms_instance, "vkGetDeviceProcAddr"));
		if (!vkGetDeviceProcAddrLocal)
			return false;

		bool const loaded =
			loadDeviceProc(vkDestroyDeviceLocal, "vkDestroyDevice") &&
			loadDeviceProc(vkGetDeviceQueueLocal, "vkGetDeviceQueue") &&
			loadDeviceProc(vkCreateSwapchainKHRLocal, "vkCreateSwapchainKHR") &&
			loadDeviceProc(vkDestroySwapchainKHRLocal, "vkDestroySwapchainKHR") &&
			loadDeviceProc(vkGetSwapchainImagesKHRLocal, "vkGetSwapchainImagesKHR") &&
			loadDeviceProc(vkCreateImageViewLocal, "vkCreateImageView") &&
			loadDeviceProc(vkDestroyImageViewLocal, "vkDestroyImageView") &&
			loadDeviceProc(vkCreateCommandPoolLocal, "vkCreateCommandPool") &&
			loadDeviceProc(vkDestroyCommandPoolLocal, "vkDestroyCommandPool") &&
			loadDeviceProc(vkAllocateCommandBuffersLocal, "vkAllocateCommandBuffers") &&
			loadDeviceProc(vkResetCommandBufferLocal, "vkResetCommandBuffer") &&
			loadDeviceProc(vkBeginCommandBufferLocal, "vkBeginCommandBuffer") &&
			loadDeviceProc(vkEndCommandBufferLocal, "vkEndCommandBuffer") &&
			loadDeviceProc(vkCmdPipelineBarrierLocal, "vkCmdPipelineBarrier") &&
			loadDeviceProc(vkCmdClearColorImageLocal, "vkCmdClearColorImage") &&
			loadDeviceProc(vkCmdCopyBufferLocal, "vkCmdCopyBuffer") &&
			loadDeviceProc(vkCmdCopyBufferToImageLocal, "vkCmdCopyBufferToImage") &&
			loadDeviceProc(vkCmdCopyImageToBufferLocal, "vkCmdCopyImageToBuffer") &&
			loadDeviceProc(vkCreateRenderPassLocal, "vkCreateRenderPass") &&
			loadDeviceProc(vkDestroyRenderPassLocal, "vkDestroyRenderPass") &&
			loadDeviceProc(vkCreateFramebufferLocal, "vkCreateFramebuffer") &&
			loadDeviceProc(vkDestroyFramebufferLocal, "vkDestroyFramebuffer") &&
			loadDeviceProc(vkCmdBeginRenderPassLocal, "vkCmdBeginRenderPass") &&
			loadDeviceProc(vkCmdEndRenderPassLocal, "vkCmdEndRenderPass") &&
			loadDeviceProc(vkCreateShaderModuleLocal, "vkCreateShaderModule") &&
			loadDeviceProc(vkDestroyShaderModuleLocal, "vkDestroyShaderModule") &&
			loadDeviceProc(vkCreatePipelineLayoutLocal, "vkCreatePipelineLayout") &&
			loadDeviceProc(vkDestroyPipelineLayoutLocal, "vkDestroyPipelineLayout") &&
			loadDeviceProc(vkCreateGraphicsPipelinesLocal, "vkCreateGraphicsPipelines") &&
			loadDeviceProc(vkDestroyPipelineLocal, "vkDestroyPipeline") &&
			loadDeviceProc(vkCreateDescriptorSetLayoutLocal, "vkCreateDescriptorSetLayout") &&
			loadDeviceProc(vkDestroyDescriptorSetLayoutLocal, "vkDestroyDescriptorSetLayout") &&
			loadDeviceProc(vkCreateDescriptorPoolLocal, "vkCreateDescriptorPool") &&
			loadDeviceProc(vkDestroyDescriptorPoolLocal, "vkDestroyDescriptorPool") &&
			loadDeviceProc(vkAllocateDescriptorSetsLocal, "vkAllocateDescriptorSets") &&
			loadDeviceProc(vkUpdateDescriptorSetsLocal, "vkUpdateDescriptorSets") &&
			loadDeviceProc(vkCreateImageLocal, "vkCreateImage") &&
			loadDeviceProc(vkDestroyImageLocal, "vkDestroyImage") &&
			loadDeviceProc(vkGetImageMemoryRequirementsLocal, "vkGetImageMemoryRequirements") &&
			loadDeviceProc(vkGetImageSubresourceLayoutLocal, "vkGetImageSubresourceLayout") &&
			loadDeviceProc(vkBindImageMemoryLocal, "vkBindImageMemory") &&
			loadDeviceProc(vkCreateSamplerLocal, "vkCreateSampler") &&
			loadDeviceProc(vkDestroySamplerLocal, "vkDestroySampler") &&
			loadDeviceProc(vkCmdBindPipelineLocal, "vkCmdBindPipeline") &&
			loadDeviceProc(vkCmdBindVertexBuffersLocal, "vkCmdBindVertexBuffers") &&
			loadDeviceProc(vkCmdBindDescriptorSetsLocal, "vkCmdBindDescriptorSets") &&
			loadDeviceProc(vkCmdPushConstantsLocal, "vkCmdPushConstants") &&
			loadDeviceProc(vkCmdSetViewportLocal, "vkCmdSetViewport") &&
			loadDeviceProc(vkCmdSetScissorLocal, "vkCmdSetScissor") &&
			loadDeviceProc(vkCmdDrawLocal, "vkCmdDraw") &&
			loadDeviceProc(vkCmdBindIndexBufferLocal, "vkCmdBindIndexBuffer") &&
			loadDeviceProc(vkCmdDrawIndexedLocal, "vkCmdDrawIndexed") &&
			loadDeviceProc(vkCreateBufferLocal, "vkCreateBuffer") &&
			loadDeviceProc(vkDestroyBufferLocal, "vkDestroyBuffer") &&
			loadDeviceProc(vkGetBufferMemoryRequirementsLocal, "vkGetBufferMemoryRequirements") &&
			loadDeviceProc(vkAllocateMemoryLocal, "vkAllocateMemory") &&
			loadDeviceProc(vkFreeMemoryLocal, "vkFreeMemory") &&
			loadDeviceProc(vkBindBufferMemoryLocal, "vkBindBufferMemory") &&
			loadDeviceProc(vkMapMemoryLocal, "vkMapMemory") &&
			loadDeviceProc(vkUnmapMemoryLocal, "vkUnmapMemory") &&
			loadDeviceProc(vkCreateSemaphoreLocal, "vkCreateSemaphore") &&
			loadDeviceProc(vkDestroySemaphoreLocal, "vkDestroySemaphore") &&
			loadDeviceProc(vkCreateFenceLocal, "vkCreateFence") &&
			loadDeviceProc(vkDestroyFenceLocal, "vkDestroyFence") &&
			loadDeviceProc(vkWaitForFencesLocal, "vkWaitForFences") &&
			loadDeviceProc(vkResetFencesLocal, "vkResetFences") &&
			loadDeviceProc(vkQueueSubmitLocal, "vkQueueSubmit") &&
			loadDeviceProc(vkQueueWaitIdleLocal, "vkQueueWaitIdle") &&
			loadDeviceProc(vkQueuePresentKHRLocal, "vkQueuePresentKHR") &&
			loadDeviceProc(vkAcquireNextImageKHRLocal, "vkAcquireNextImageKHR") &&
			loadDeviceProc(vkDeviceWaitIdleLocal, "vkDeviceWaitIdle");
		logLine(loaded ? "createDevice ok" : "createDevice proc load failed");
		return loaded;
	}

	bool createSynchronization()
	{
		logLine("createSynchronization begin");
		VkSemaphoreCreateInfo semaphoreInfo;
		memset(&semaphoreInfo, 0, sizeof(semaphoreInfo));
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		if (vkCreateSemaphoreLocal(ms_device, &semaphoreInfo, 0, &ms_imageAvailableSemaphore) != VK_SUCCESS)
			return false;
		if (vkCreateSemaphoreLocal(ms_device, &semaphoreInfo, 0, &ms_renderFinishedSemaphore) != VK_SUCCESS)
			return false;

		VkFenceCreateInfo fenceInfo;
		memset(&fenceInfo, 0, sizeof(fenceInfo));
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		bool const ok = vkCreateFenceLocal(ms_device, &fenceInfo, 0, &ms_frameFence) == VK_SUCCESS;
		ms_frameFenceSubmitted = false;
		ms_deviceLost = false;
		logLine(ok ? "createSynchronization ok" : "createSynchronization failed");
		return ok;
	}

	bool recreateFrameFenceSignaled()
	{
		if (!ms_device)
			return false;
		if (ms_frameFence)
		{
			vkDestroyFenceLocal(ms_device, ms_frameFence, 0);
			ms_frameFence = 0;
		}

		VkFenceCreateInfo fenceInfo;
		memset(&fenceInfo, 0, sizeof(fenceInfo));
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		bool const ok = vkCreateFenceLocal(ms_device, &fenceInfo, 0, &ms_frameFence) == VK_SUCCESS;
		ms_frameFenceSubmitted = false;
		logLine(ok ? "frame fence recreated signaled" : "frame fence recreate failed");
		return ok;
	}

	bool createInstanceAndSurface()
	{
		logLine("createInstanceAndSurface begin");
		char const *extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };

		VkApplicationInfo appInfo;
		memset(&appInfo, 0, sizeof(appInfo));
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "SWG OG Vulkan";
		appInfo.applicationVersion = 1;
		appInfo.pEngineName = "SWG OG";
		appInfo.engineVersion = 1;
		appInfo.apiVersion = VK_API_VERSION_1_0;

		VkInstanceCreateInfo createInfo;
		memset(&createInfo, 0, sizeof(createInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = 2;
		createInfo.ppEnabledExtensionNames = extensions;

		if (vkCreateInstanceLocal(&createInfo, 0, &ms_instance) != VK_SUCCESS)
		{
			logLine("vkCreateInstance failed");
			return false;
		}

		if (!loadInstanceProc(vkDestroyInstanceLocal, "vkDestroyInstance") ||
			!loadInstanceProc(vkEnumeratePhysicalDevicesLocal, "vkEnumeratePhysicalDevices") ||
			!loadInstanceProc(vkGetPhysicalDeviceFeaturesLocal, "vkGetPhysicalDeviceFeatures") ||
			!loadInstanceProc(vkGetPhysicalDevicePropertiesLocal, "vkGetPhysicalDeviceProperties") ||
			!loadInstanceProc(vkGetPhysicalDeviceQueueFamilyPropertiesLocal, "vkGetPhysicalDeviceQueueFamilyProperties") ||
			!loadInstanceProc(vkGetPhysicalDeviceMemoryPropertiesLocal, "vkGetPhysicalDeviceMemoryProperties") ||
			!loadInstanceProc(vkGetPhysicalDeviceSurfaceSupportKHRLocal, "vkGetPhysicalDeviceSurfaceSupportKHR") ||
			!loadInstanceProc(vkGetPhysicalDeviceSurfaceCapabilitiesKHRLocal, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") ||
			!loadInstanceProc(vkGetPhysicalDeviceSurfaceFormatsKHRLocal, "vkGetPhysicalDeviceSurfaceFormatsKHR") ||
			!loadInstanceProc(vkGetPhysicalDeviceSurfacePresentModesKHRLocal, "vkGetPhysicalDeviceSurfacePresentModesKHR") ||
			!loadInstanceProc(vkCreateDeviceLocal, "vkCreateDevice") ||
			!loadInstanceProc(vkCreateWin32SurfaceKHRLocal, "vkCreateWin32SurfaceKHR") ||
			!loadInstanceProc(vkDestroySurfaceKHRLocal, "vkDestroySurfaceKHR"))
		{
			return false;
		}

		VkWin32SurfaceCreateInfoKHR surfaceInfo;
		memset(&surfaceInfo, 0, sizeof(surfaceInfo));
		surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surfaceInfo.hinstance = GetModuleHandle(0);
		surfaceInfo.hwnd = ms_window;
		if (vkCreateWin32SurfaceKHRLocal(ms_instance, &surfaceInfo, 0, &ms_surface) != VK_SUCCESS)
		{
			logLine("vkCreateWin32SurfaceKHR failed");
			return false;
		}

		logLine("createInstanceAndSurface ok");
		return true;
	}

	void destroyVulkan()
	{
		if (ms_device)
		{
			vkDeviceWaitIdleLocal(ms_device);
			releasePendingTextureStaging("destroyVulkan");
		}

		destroySwapchain();

		if (ms_frameFence)
		{
			vkDestroyFenceLocal(ms_device, ms_frameFence, 0);
			ms_frameFence = 0;
		}
		ms_frameFenceSubmitted = false;
		ms_deviceLost = false;
		if (ms_renderFinishedSemaphore)
		{
			vkDestroySemaphoreLocal(ms_device, ms_renderFinishedSemaphore, 0);
			ms_renderFinishedSemaphore = 0;
		}
		if (ms_imageAvailableSemaphore)
		{
			vkDestroySemaphoreLocal(ms_device, ms_imageAvailableSemaphore, 0);
			ms_imageAvailableSemaphore = 0;
		}
		if (ms_device)
		{
			vkDestroyDeviceLocal(ms_device, 0);
			ms_device = 0;
		}
		if (ms_surface)
		{
			vkDestroySurfaceKHRLocal(ms_instance, ms_surface, 0);
			ms_surface = 0;
		}
		if (ms_instance)
		{
			vkDestroyInstanceLocal(ms_instance, 0);
			ms_instance = 0;
		}
		if (ms_vulkanDll)
		{
			FreeLibrary(ms_vulkanDll);
			ms_vulkanDll = 0;
		}
	}

	VertexBufferDescriptor const &getVertexBufferDescriptor(VertexBufferFormat const &vertexFormat)
	{
		if (!ms_vertexBufferDescriptors)
			ms_vertexBufferDescriptors = new std::vector<VertexBufferDescriptorEntry>;

		for (size_t i = 0; i < ms_vertexBufferDescriptors->size(); ++i)
		{
			if ((*ms_vertexBufferDescriptors)[i].flags == vertexFormat.getFlags())
				return (*ms_vertexBufferDescriptors)[i].descriptor;
		}

		VertexBufferDescriptorEntry entry;
		entry.flags = vertexFormat.getFlags();
		{
			if (vertexFormat.hasPosition())
			{
				entry.descriptor.offsetPosition = entry.descriptor.vertexSize;
				entry.descriptor.vertexSize = static_cast<int8>(entry.descriptor.vertexSize + sizeof(float) * 3);
				if (vertexFormat.isTransformed())
				{
					entry.descriptor.offsetOoz = entry.descriptor.vertexSize;
					entry.descriptor.vertexSize = static_cast<int8>(entry.descriptor.vertexSize + sizeof(float));
				}
			}
			if (vertexFormat.hasNormal())
			{
				entry.descriptor.offsetNormal = entry.descriptor.vertexSize;
				entry.descriptor.vertexSize = static_cast<int8>(entry.descriptor.vertexSize + sizeof(float) * 3);
			}
			if (vertexFormat.hasPointSize())
			{
				entry.descriptor.offsetPointSize = entry.descriptor.vertexSize;
				entry.descriptor.vertexSize = static_cast<int8>(entry.descriptor.vertexSize + sizeof(float));
			}
			if (vertexFormat.hasColor0())
			{
				entry.descriptor.offsetColor0 = entry.descriptor.vertexSize;
				entry.descriptor.vertexSize = static_cast<int8>(entry.descriptor.vertexSize + sizeof(uint32));
			}
			if (vertexFormat.hasColor1())
			{
				entry.descriptor.offsetColor1 = entry.descriptor.vertexSize;
				entry.descriptor.vertexSize = static_cast<int8>(entry.descriptor.vertexSize + sizeof(uint32));
			}

			int const numberOfTextureCoordinateSets = vertexFormat.getNumberOfTextureCoordinateSets();
			for (int i = 0; i < numberOfTextureCoordinateSets; ++i)
			{
				entry.descriptor.offsetTextureCoordinateSet[i] = entry.descriptor.vertexSize;
				entry.descriptor.vertexSize = static_cast<int8>(entry.descriptor.vertexSize + sizeof(float) * vertexFormat.getTextureCoordinateSetDimension(i));
			}
			if (entry.descriptor.vertexSize == 0)
				entry.descriptor.vertexSize = 1;
		}

		ms_vertexBufferDescriptors->push_back(entry);
		return ms_vertexBufferDescriptors->back().descriptor;
	}

	class VulkanStaticShaderData : public StaticShaderGraphicsData
	{
	public:
		explicit VulkanStaticShaderData(StaticShader const &shader)
		: m_textureSortKey(0)
		{
			UNREF(shader);
		}
		virtual void update(const StaticShader &shader) { UNREF(shader); m_textureSortKey = 0; }
		virtual uintptr_t getTextureSortKey() const { return m_textureSortKey; }

	private:
		uintptr_t m_textureSortKey;
	};

	class VulkanShaderImplementationData : public ShaderImplementationGraphicsData {};
	class VulkanVertexShaderData : public ShaderImplementationPassVertexShaderGraphicsData {};
	class VulkanPixelShaderData : public ShaderImplementationPassPixelShaderProgramGraphicsData {};
	class VulkanVertexBufferVectorData : public VertexBufferVectorGraphicsData
	{
	public:
		explicit VulkanVertexBufferVectorData(VertexBufferVector const &vertexBufferVector)
		: m_vertexBuffers()
		{
			if (vertexBufferVector.m_vertexBufferList)
			{
				VertexBufferVector::VertexBufferList::const_iterator const end = vertexBufferVector.m_vertexBufferList->end();
				for (VertexBufferVector::VertexBufferList::const_iterator i = vertexBufferVector.m_vertexBufferList->begin(); i != end; ++i)
					m_vertexBuffers.push_back(*i);
			}
		}

		std::vector<HardwareVertexBuffer const *> const &getVertexBuffers() const { return m_vertexBuffers; }

		static VulkanVertexBufferVectorData const *getData(VertexBufferVector const &vertexBufferVector)
		{
			return dynamic_cast<VulkanVertexBufferVectorData const *>(vertexBufferVector.m_graphicsData);
		}

	private:
		std::vector<HardwareVertexBuffer const *> m_vertexBuffers;
	};

	class VulkanStaticVertexBufferData : public StaticVertexBufferGraphicsData
	{
	public:
		explicit VulkanStaticVertexBufferData(StaticVertexBuffer const &vertexBuffer)
		: m_descriptor(getVertexBufferDescriptor(vertexBuffer.getFormat())),
		  m_owner(&vertexBuffer),
		  m_formatFlags(vertexBuffer.getFormat().getFlags()),
		  m_sortKey(++ms_nextSortKey),
		  m_vertexCount(vertexBuffer.getNumberOfVertices()),
		  m_data(static_cast<size_t>(vertexBuffer.getNumberOfVertices()) * static_cast<size_t>(m_descriptor.vertexSize))
		{
		}

		virtual ~VulkanStaticVertexBufferData()
		{
			if (ms_staticVertexBufferDataMap)
				ms_staticVertexBufferDataMap->erase(m_owner);
		}

		virtual VertexBufferDescriptor const &getDescriptor() const { return m_descriptor; }
		virtual uintptr_t getSortKey() { return m_sortKey; }
		virtual void *lock(bool) { return m_data.empty() ? 0 : &m_data[0]; }
		virtual void unlock() {}
		byte const *getData() const { return m_data.empty() ? 0 : &m_data[0]; }
		int getVertexCount() const { return m_vertexCount; }
		uint32 getFormatFlags() const { return m_formatFlags; }

	private:
		VertexBufferDescriptor m_descriptor;
		StaticVertexBuffer const *m_owner;
		uint32 m_formatFlags;
		uintptr_t m_sortKey;
		int m_vertexCount;
		std::vector<byte> m_data;
	};

	class VulkanDynamicVertexBufferData : public DynamicVertexBufferGraphicsData
	{
	public:
		explicit VulkanDynamicVertexBufferData(DynamicVertexBuffer const &vertexBuffer)
		: m_descriptor(getVertexBufferDescriptor(vertexBuffer.getFormat())),
		  m_owner(&vertexBuffer),
		  m_formatFlags(vertexBuffer.getFormat().getFlags()),
		  m_sortKey(++ms_nextSortKey),
		  m_vertexCount(0)
		{
		}

		virtual ~VulkanDynamicVertexBufferData()
		{
			if (ms_dynamicVertexBufferDataMap)
				ms_dynamicVertexBufferDataMap->erase(m_owner);
		}

		virtual void *lock(int numberOfVertices, bool)
		{
			ms_lastDynamicVertexBufferData = this;
			m_vertexCount = numberOfVertices;
			m_data.resize(static_cast<size_t>(numberOfVertices) * static_cast<size_t>(m_descriptor.vertexSize));
			return m_data.empty() ? 0 : &m_data[0];
		}
		virtual void unlock() {}
		virtual void unlock(int numberOfVertices)
		{
			m_vertexCount = numberOfVertices;
			m_data.resize(static_cast<size_t>(numberOfVertices) * static_cast<size_t>(m_descriptor.vertexSize));
		}
		virtual VertexBufferDescriptor const &getDescriptor() const { return m_descriptor; }
		virtual int getNumberOfLockableDynamicVertices(bool) { return m_descriptor.vertexSize > 0 ? (2 * 1024 * 1024) / m_descriptor.vertexSize : 0; }
		virtual uintptr_t getSortKey() { return m_sortKey; }
		byte const *getData() const { return m_data.empty() ? 0 : &m_data[0]; }
		int getVertexCount() const { return m_vertexCount; }
		uint32 getFormatFlags() const { return m_formatFlags; }

	private:
		VertexBufferDescriptor m_descriptor;
		DynamicVertexBuffer const *m_owner;
		uint32 m_formatFlags;
		uintptr_t m_sortKey;
		int m_vertexCount;
		std::vector<byte> m_data;
	};

	class VulkanStaticIndexBufferData : public StaticIndexBufferGraphicsData
	{
	public:
		explicit VulkanStaticIndexBufferData(StaticIndexBuffer const &indexBuffer)
		: m_owner(&indexBuffer),
		  m_indexCount(indexBuffer.getNumberOfIndices()),
		  m_data(indexBuffer.getNumberOfIndices())
		{
		}

		virtual ~VulkanStaticIndexBufferData()
		{
			if (ms_staticIndexBufferDataMap)
				ms_staticIndexBufferDataMap->erase(m_owner);
		}

		virtual Index *lock(bool) { return m_data.empty() ? 0 : &m_data[0]; }
		virtual void unlock() {}
		Index const *getData() const { return m_data.empty() ? 0 : &m_data[0]; }
		int getIndexCount() const { return m_indexCount; }

	private:
		StaticIndexBuffer const *m_owner;
		int m_indexCount;
		std::vector<Index> m_data;
	};

	class VulkanDynamicIndexBufferData : public DynamicIndexBufferGraphicsData
	{
	public:
		VulkanDynamicIndexBufferData()
		: m_indexCount(0)
		{
		}

		virtual Index *lock(int numberOfIndices)
		{
			ms_lastDynamicIndexBufferData = this;
			m_indexCount = numberOfIndices;
			m_data.resize(numberOfIndices);
			return m_data.empty() ? 0 : &m_data[0];
		}
		virtual void unlock() {}
		Index const *getData() const { return m_data.empty() ? 0 : &m_data[0]; }
		int getIndexCount() const { return m_indexCount; }

	private:
		int m_indexCount;
		std::vector<Index> m_data;
	};

}

class VulkanTextureData : public TextureGraphicsData
	{
	public:
		VulkanTextureData(Texture const &texture, TextureFormat const *runtimeFormats, int numberOfRuntimeFormats)
		: m_format(TF_ARGB_8888),
		  m_width(std::max(1, texture.getWidth())),
		  m_height(std::max(1, texture.getHeight())),
		  m_faceCount(texture.isCubeMap() ? 6 : 1),
		  m_pixels(),
		  m_gpuImage(0),
		  m_gpuMemory(0),
		  m_gpuImageView(0),
		  m_gpuStagingBuffer(0),
		  m_gpuStagingMemory(0),
		  m_gpuDescriptorSet(0),
		  m_gpuClampDescriptorSet(0),
		  m_gpuCubeImage(0),
		  m_gpuCubeMemory(0),
		  m_gpuCubeImageView(0),
		  m_gpuCubeStagingBuffer(0),
		  m_gpuCubeStagingMemory(0),
		  m_gpuCubeDescriptorSet(0),
		  m_gpuCubeClampDescriptorSet(0),
		  m_gpuAlphaMaskImage(0),
		  m_gpuAlphaMaskMemory(0),
		  m_gpuAlphaMaskImageView(0),
		  m_gpuAlphaMaskStagingBuffer(0),
		  m_gpuAlphaMaskStagingMemory(0),
		  m_gpuAlphaMaskDescriptorSet(0),
		  m_gpuDescriptorGeneration(0xffffffff),
		  m_gpuCubeDescriptorGeneration(0xffffffff),
		  m_gpuAlphaMaskDescriptorGeneration(0xffffffff),
		  m_gpuUploadRecorded(false),
		  m_gpuCubeUploadRecorded(false),
		  m_gpuAlphaMaskUploadRecorded(false),
		  m_gpuMipLevels(1),
		  m_cubeProxyRowValid(false)
		{
			char const *textureName = texture.getName();
			strncpy(m_name, textureName ? textureName : "<unnamed>", sizeof(m_name) - 1);
			m_name[sizeof(m_name) - 1] = 0;
			if (runtimeFormats && numberOfRuntimeFormats > 0)
				m_format = runtimeFormats[0];
			m_pixels.resize(getFaceStorageSize() * static_cast<size_t>(m_faceCount));
		}

		virtual ~VulkanTextureData()
		{
			destroyGpuResources();
		}

		virtual void copyFrom(int, TextureGraphicsData const &, int, int, int, int, int, int, int, int) {}
		char const *getName() const { return m_name; }
		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }
		TextureFormat getFormat() const { return m_format; }
		bool isCubeMap() const { return m_faceCount > 1; }
		uint32 sample(float u, float v, bool wrap = false) const
		{
			if (m_pixels.empty())
				return 0xffffffff;
			if (m_faceCount > 1)
				return sampleCubeProxy(u, v, wrap);

			if (wrap)
			{
				u = u - floorf(u);
				v = v - floorf(v);
				if (u < 0.0f)
					u += 1.0f;
				if (v < 0.0f)
					v += 1.0f;
			}
			else
			{
				u = std::max(0.0f, std::min(1.0f, u));
				v = std::max(0.0f, std::min(1.0f, v));
			}
			int const x = std::max(0, std::min(m_width - 1, static_cast<int>(u * static_cast<float>(m_width - 1) + 0.5f)));
			int const y = std::max(0, std::min(m_height - 1, static_cast<int>(v * static_cast<float>(m_height - 1) + 0.5f)));
			return samplePixelBestFace(x, y);
		}

		VkDescriptorSet getGpuDescriptorSet(bool clampSampler = false) const
		{
			if (!ensureGpuResources())
				return 0;
			return clampSampler && m_gpuClampDescriptorSet ? m_gpuClampDescriptorSet : m_gpuDescriptorSet;
		}

		VkDescriptorSet getGpuCubeDescriptorSet(bool clampSampler = false) const
		{
			if (!ensureGpuCubeResources())
				return getGpuDescriptorSet(clampSampler);
			return clampSampler && m_gpuCubeClampDescriptorSet ? m_gpuCubeClampDescriptorSet : m_gpuCubeDescriptorSet;
		}

		VkDescriptorSet getGpuAlphaMaskDescriptorSet() const
		{
			if (!ensureGpuAlphaMaskResources())
				return 0;
			return m_gpuAlphaMaskDescriptorSet;
		}
		bool getGpuDescriptorImageInfo(VkDescriptorImageInfo &imageDescriptor, bool clampSampler = false) const
		{
			if (!ensureGpuResources())
				return false;

			memset(&imageDescriptor, 0, sizeof(imageDescriptor));
			imageDescriptor.sampler = clampSampler && VulkanNamespace::ms_clampTextureSampler ? VulkanNamespace::ms_clampTextureSampler : VulkanNamespace::ms_whiteTextureSampler;
			imageDescriptor.imageView = m_gpuImageView;
			imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			return imageDescriptor.imageView != 0 && imageDescriptor.sampler != 0;
		}

		void recordGpuLayoutTransition(VkCommandBuffer commandBuffer) const
		{
			recordGpuImageLayoutTransition(commandBuffer, m_gpuImage, m_gpuStagingBuffer, m_gpuStagingMemory, 1, std::max<uint32_t>(1, m_gpuMipLevels), m_gpuUploadRecorded);
			recordGpuImageLayoutTransition(commandBuffer, m_gpuCubeImage, m_gpuCubeStagingBuffer, m_gpuCubeStagingMemory, static_cast<uint32_t>(std::max(1, m_faceCount)), 1, m_gpuCubeUploadRecorded);
			recordGpuImageLayoutTransition(commandBuffer, m_gpuAlphaMaskImage, m_gpuAlphaMaskStagingBuffer, m_gpuAlphaMaskStagingMemory, 1, 1, m_gpuAlphaMaskUploadRecorded);
		}

	private:
		static uint32_t getMipLevelCount(int width, int height)
		{
			uint32_t levels = 1;
			int mipWidth = std::max(1, width);
			int mipHeight = std::max(1, height);
			while (mipWidth > 1 || mipHeight > 1)
			{
				mipWidth = std::max(1, mipWidth / 2);
				mipHeight = std::max(1, mipHeight / 2);
				++levels;
			}
			return levels;
		}

		static int getMipDimension(int base, uint32_t level)
		{
			int value = std::max(1, base);
			for (uint32_t i = 0; i < level; ++i)
				value = std::max(1, value / 2);
			return value;
		}

		static VkDeviceSize getMipChainByteSize(int width, int height, uint32_t mipLevels, uint32_t layerCount)
		{
			VkDeviceSize bytes = 0;
			for (uint32_t level = 0; level < mipLevels; ++level)
			{
				bytes += static_cast<VkDeviceSize>(getMipDimension(width, level)) *
					static_cast<VkDeviceSize>(getMipDimension(height, level)) * 4u;
			}
			return bytes * static_cast<VkDeviceSize>(std::max<uint32_t>(1, layerCount));
		}

		static VkDeviceSize getMipLevelOffset(int width, int height, uint32_t mipLevels, uint32_t layerCount, uint32_t layer, uint32_t level)
		{
			VkDeviceSize const layerBytes = getMipChainByteSize(width, height, mipLevels, 1);
			VkDeviceSize offset = layerBytes * static_cast<VkDeviceSize>(layer);
			for (uint32_t i = 0; i < level; ++i)
			{
				offset += static_cast<VkDeviceSize>(getMipDimension(width, i)) *
					static_cast<VkDeviceSize>(getMipDimension(height, i)) * 4u;
			}
			UNREF(layerCount);
			return offset;
		}

		static void writeDownsampledMip(byte *base, int width, int height, uint32_t srcLevel, uint32_t dstLevel)
		{
			int const srcWidth = getMipDimension(width, srcLevel);
			int const srcHeight = getMipDimension(height, srcLevel);
			int const dstWidth = getMipDimension(width, dstLevel);
			int const dstHeight = getMipDimension(height, dstLevel);
			byte const *srcBase = base + getMipLevelOffset(width, height, dstLevel + 1, 1, 0, srcLevel);
			byte *dstBase = base + getMipLevelOffset(width, height, dstLevel + 1, 1, 0, dstLevel);
			for (int y = 0; y < dstHeight; ++y)
			{
				for (int x = 0; x < dstWidth; ++x)
				{
					int sum[4] = { 0, 0, 0, 0 };
					int samples = 0;
					for (int oy = 0; oy < 2; ++oy)
					{
						for (int ox = 0; ox < 2; ++ox)
						{
							int const sx = std::min(srcWidth - 1, x * 2 + ox);
							int const sy = std::min(srcHeight - 1, y * 2 + oy);
							byte const *src = srcBase + (static_cast<size_t>(sy) * static_cast<size_t>(srcWidth) + static_cast<size_t>(sx)) * 4u;
							sum[0] += src[0];
							sum[1] += src[1];
							sum[2] += src[2];
							sum[3] += src[3];
							++samples;
						}
					}
					byte *dst = dstBase + (static_cast<size_t>(y) * static_cast<size_t>(dstWidth) + static_cast<size_t>(x)) * 4u;
					dst[0] = static_cast<byte>(sum[0] / samples);
					dst[1] = static_cast<byte>(sum[1] / samples);
					dst[2] = static_cast<byte>(sum[2] / samples);
					dst[3] = static_cast<byte>(sum[3] / samples);
				}
			}
		}

		void recordGpuImageLayoutTransition(VkCommandBuffer commandBuffer, VkImage image, VkBuffer &stagingBuffer, VkDeviceMemory &stagingMemory, uint32_t layerCount, uint32_t mipLevels, bool &uploadRecorded) const
		{
			if (!image || !stagingBuffer || uploadRecorded)
				return;
			layerCount = std::max<uint32_t>(1, layerCount);
			mipLevels = std::max<uint32_t>(1, mipLevels);

			VkImageMemoryBarrier toTransfer;
			memset(&toTransfer, 0, sizeof(toTransfer));
			toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toTransfer.srcAccessMask = 0;
			toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransfer.image = image;
			toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			toTransfer.subresourceRange.levelCount = mipLevels;
			toTransfer.subresourceRange.layerCount = layerCount;
			VulkanNamespace::vkCmdPipelineBarrierLocal(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &toTransfer);

			std::vector<VkBufferImageCopy> copyRegions(layerCount * mipLevels);
			for (uint32_t layer = 0; layer < layerCount; ++layer)
			{
				for (uint32_t level = 0; level < mipLevels; ++level)
				{
					VkBufferImageCopy &copyRegion = copyRegions[static_cast<size_t>(layer) * mipLevels + level];
					memset(&copyRegion, 0, sizeof(copyRegion));
					copyRegion.bufferOffset = getMipLevelOffset(m_width, m_height, mipLevels, layerCount, layer, level);
					copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.imageSubresource.mipLevel = level;
					copyRegion.imageSubresource.baseArrayLayer = layer;
					copyRegion.imageSubresource.layerCount = 1;
					copyRegion.imageExtent.width = static_cast<uint32_t>(getMipDimension(m_width, level));
					copyRegion.imageExtent.height = static_cast<uint32_t>(getMipDimension(m_height, level));
					copyRegion.imageExtent.depth = 1;
				}
			}
			VulkanNamespace::vkCmdCopyBufferToImageLocal(commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copyRegions.size()), &copyRegions[0]);

			VkImageMemoryBarrier toShader = toTransfer;
			toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VulkanNamespace::vkCmdPipelineBarrierLocal(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &toShader);
			uploadRecorded = true;
			VkDeviceSize const stagingByteSize = getMipChainByteSize(m_width, m_height, mipLevels, layerCount);
			VulkanNamespace::queueTextureStagingRelease(stagingBuffer, stagingMemory, stagingByteSize);
			stagingBuffer = 0;
			stagingMemory = 0;
		}

		struct ScratchLock
		{
			std::vector<byte> data;
		};

		void destroyGpuResources() const
		{
			if (!VulkanNamespace::ms_device)
				return;
			VulkanNamespace::waitForSubmittedFrame("textureDestroy", 5000000000ull);
			if (m_gpuImageView)
			{
				VulkanNamespace::vkDestroyImageViewLocal(VulkanNamespace::ms_device, m_gpuImageView, 0);
				m_gpuImageView = 0;
			}
			if (m_gpuImage)
			{
				VulkanNamespace::vkDestroyImageLocal(VulkanNamespace::ms_device, m_gpuImage, 0);
				m_gpuImage = 0;
			}
			if (m_gpuMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuMemory, 0);
				m_gpuMemory = 0;
			}
			if (m_gpuStagingBuffer)
			{
				VulkanNamespace::vkDestroyBufferLocal(VulkanNamespace::ms_device, m_gpuStagingBuffer, 0);
				m_gpuStagingBuffer = 0;
			}
			if (m_gpuStagingMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuStagingMemory, 0);
				m_gpuStagingMemory = 0;
			}
			if (m_gpuCubeImageView)
			{
				VulkanNamespace::vkDestroyImageViewLocal(VulkanNamespace::ms_device, m_gpuCubeImageView, 0);
				m_gpuCubeImageView = 0;
			}
			if (m_gpuCubeImage)
			{
				VulkanNamespace::vkDestroyImageLocal(VulkanNamespace::ms_device, m_gpuCubeImage, 0);
				m_gpuCubeImage = 0;
			}
			if (m_gpuCubeMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuCubeMemory, 0);
				m_gpuCubeMemory = 0;
			}
			if (m_gpuCubeStagingBuffer)
			{
				VulkanNamespace::vkDestroyBufferLocal(VulkanNamespace::ms_device, m_gpuCubeStagingBuffer, 0);
				m_gpuCubeStagingBuffer = 0;
			}
			if (m_gpuCubeStagingMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuCubeStagingMemory, 0);
				m_gpuCubeStagingMemory = 0;
			}
			if (m_gpuAlphaMaskImageView)
			{
				VulkanNamespace::vkDestroyImageViewLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskImageView, 0);
				m_gpuAlphaMaskImageView = 0;
			}
			if (m_gpuAlphaMaskImage)
			{
				VulkanNamespace::vkDestroyImageLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskImage, 0);
				m_gpuAlphaMaskImage = 0;
			}
			if (m_gpuAlphaMaskMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskMemory, 0);
				m_gpuAlphaMaskMemory = 0;
			}
			if (m_gpuAlphaMaskStagingBuffer)
			{
				VulkanNamespace::vkDestroyBufferLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskStagingBuffer, 0);
				m_gpuAlphaMaskStagingBuffer = 0;
			}
			if (m_gpuAlphaMaskStagingMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskStagingMemory, 0);
				m_gpuAlphaMaskStagingMemory = 0;
			}
			m_gpuDescriptorSet = 0;
			m_gpuClampDescriptorSet = 0;
			m_gpuCubeDescriptorSet = 0;
			m_gpuCubeClampDescriptorSet = 0;
			m_gpuAlphaMaskDescriptorSet = 0;
			m_gpuDescriptorGeneration = 0xffffffff;
			m_gpuCubeDescriptorGeneration = 0xffffffff;
			m_gpuAlphaMaskDescriptorGeneration = 0xffffffff;
			m_gpuUploadRecorded = false;
			m_gpuCubeUploadRecorded = false;
			m_gpuAlphaMaskUploadRecorded = false;
			m_gpuMipLevels = 1;
		}

		void destroyGpuAlphaMaskResources() const
		{
			if (!VulkanNamespace::ms_device)
				return;
			if (m_gpuAlphaMaskImageView)
			{
				VulkanNamespace::vkDestroyImageViewLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskImageView, 0);
				m_gpuAlphaMaskImageView = 0;
			}
			if (m_gpuAlphaMaskImage)
			{
				VulkanNamespace::vkDestroyImageLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskImage, 0);
				m_gpuAlphaMaskImage = 0;
			}
			if (m_gpuAlphaMaskMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskMemory, 0);
				m_gpuAlphaMaskMemory = 0;
			}
			if (m_gpuAlphaMaskStagingBuffer)
			{
				VulkanNamespace::vkDestroyBufferLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskStagingBuffer, 0);
				m_gpuAlphaMaskStagingBuffer = 0;
			}
			if (m_gpuAlphaMaskStagingMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskStagingMemory, 0);
				m_gpuAlphaMaskStagingMemory = 0;
			}
			m_gpuAlphaMaskDescriptorSet = 0;
			m_gpuAlphaMaskDescriptorGeneration = 0xffffffff;
			m_gpuAlphaMaskUploadRecorded = false;
		}

		bool ensureGpuResources() const
		{
			if (!VulkanNamespace::ms_device || !VulkanNamespace::ms_gpuDrawDescriptorPool || !VulkanNamespace::ms_whiteTextureDescriptorSetLayout || !VulkanNamespace::ms_whiteTextureSampler)
				return false;
			if (m_gpuDescriptorSet && m_gpuDescriptorGeneration == VulkanNamespace::ms_gpuDescriptorGeneration)
				return true;

			uint32_t const mipLevels = VulkanNamespace::gpuTextureMipmapsEnabled() ? getMipLevelCount(m_width, m_height) : 1;
			VkDeviceSize const stagingByteSize = getMipChainByteSize(m_width, m_height, mipLevels, 1);
			int const uploadBudget = VulkanNamespace::gpuTextureUploadBudgetPerPresent();
			unsigned const uploadByteBudget = VulkanNamespace::gpuTextureUploadByteBudgetPerPresent();
			if ((uploadBudget > 0 && VulkanNamespace::ms_perfTextureUploadsThisPresent >= static_cast<unsigned>(uploadBudget)) ||
				(uploadByteBudget > 0 &&
				VulkanNamespace::ms_perfTextureUploadsThisPresent > 0 &&
				VulkanNamespace::ms_perfTextureUploadBytesThisPresent + static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu)) > uploadByteBudget))
			{
				return false;
			}

			destroyGpuResources();

			VkImageCreateInfo imageInfo;
			memset(&imageInfo, 0, sizeof(imageInfo));
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			imageInfo.extent.width = static_cast<uint32_t>(std::max(1, m_width));
			imageInfo.extent.height = static_cast<uint32_t>(std::max(1, m_height));
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = mipLevels;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (VulkanNamespace::vkCreateImageLocal(VulkanNamespace::ms_device, &imageInfo, 0, &m_gpuImage) != VK_SUCCESS)
			{
				static int s_logCount = 0;
				if (s_logCount++ < 16)
					VulkanNamespace::logLine("vkCreateImage real texture failed");
				return false;
			}

			VkMemoryRequirements requirements;
			memset(&requirements, 0, sizeof(requirements));
			VulkanNamespace::vkGetImageMemoryRequirementsLocal(VulkanNamespace::ms_device, m_gpuImage, &requirements);
			uint32_t const memoryType = VulkanNamespace::findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (memoryType == 0xffffffff)
			{
				static int s_logCount = 0;
				if (s_logCount++ < 16)
					VulkanNamespace::logLine("real texture device-local memory type not found");
				return false;
			}

			VkMemoryAllocateInfo allocateInfo;
			memset(&allocateInfo, 0, sizeof(allocateInfo));
			allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocateInfo.allocationSize = requirements.size;
			allocateInfo.memoryTypeIndex = memoryType;
			if (VulkanNamespace::vkAllocateMemoryLocal(VulkanNamespace::ms_device, &allocateInfo, 0, &m_gpuMemory) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkAllocateMemory real texture failed");
				return false;
			}
			if (VulkanNamespace::vkBindImageMemoryLocal(VulkanNamespace::ms_device, m_gpuImage, m_gpuMemory, 0) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkBindImageMemory real texture failed");
				return false;
			}

			if (!VulkanNamespace::createHostVisibleBuffer(stagingByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_gpuStagingBuffer, m_gpuStagingMemory, 0, "real texture staging"))
				return false;

			void *mapped = 0;
			if (VulkanNamespace::vkMapMemoryLocal(VulkanNamespace::ms_device, m_gpuStagingMemory, 0, stagingByteSize, 0, &mapped) != VK_SUCCESS || !mapped)
			{
				VulkanNamespace::logLine("vkMapMemory real texture staging failed");
				return false;
			}
			byte *base = static_cast<byte *>(mapped);
			int const level0Width = getMipDimension(m_width, 0);
			int const level0Height = getMipDimension(m_height, 0);
			for (int y = 0; y < level0Height; ++y)
			{
				byte *row = base + static_cast<size_t>(y) * static_cast<size_t>(level0Width) * 4u;
				for (int x = 0; x < level0Width; ++x)
				{
					float const u = level0Width > 1 ? static_cast<float>(x) / static_cast<float>(level0Width - 1) : 0.0f;
					float const v = level0Height > 1 ? static_cast<float>(y) / static_cast<float>(level0Height - 1) : 0.0f;
					uint32 const argb = sample(u, v);
					byte *pixel = row + static_cast<size_t>(x) * 4u;
					pixel[0] = static_cast<byte>((argb >> 16) & 0xff);
					pixel[1] = static_cast<byte>((argb >> 8) & 0xff);
					pixel[2] = static_cast<byte>(argb & 0xff);
					pixel[3] = static_cast<byte>((argb >> 24) & 0xff);
				}
			}
			for (uint32_t level = 1; level < mipLevels; ++level)
				writeDownsampledMip(base, m_width, m_height, level - 1, level);
			VulkanNamespace::vkUnmapMemoryLocal(VulkanNamespace::ms_device, m_gpuStagingMemory);

			VkImageViewCreateInfo viewInfo;
			memset(&viewInfo, 0, sizeof(viewInfo));
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_gpuImage;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.levelCount = mipLevels;
			viewInfo.subresourceRange.layerCount = 1;
			if (VulkanNamespace::vkCreateImageViewLocal(VulkanNamespace::ms_device, &viewInfo, 0, &m_gpuImageView) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkCreateImageView real texture failed");
				return false;
			}

			VkDescriptorSetAllocateInfo descriptorAllocate;
			memset(&descriptorAllocate, 0, sizeof(descriptorAllocate));
			descriptorAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorAllocate.descriptorPool = VulkanNamespace::ms_gpuDrawDescriptorPool;
			descriptorAllocate.descriptorSetCount = 1;
			descriptorAllocate.pSetLayouts = &VulkanNamespace::ms_whiteTextureDescriptorSetLayout;
			if (VulkanNamespace::vkAllocateDescriptorSetsLocal(VulkanNamespace::ms_device, &descriptorAllocate, &m_gpuDescriptorSet) != VK_SUCCESS)
			{
				static int s_logCount = 0;
				if (s_logCount++ < 16)
					VulkanNamespace::logLine("vkAllocateDescriptorSets real texture failed");
				return false;
			}

			VkDescriptorImageInfo imageDescriptor;
			memset(&imageDescriptor, 0, sizeof(imageDescriptor));
			imageDescriptor.sampler = VulkanNamespace::ms_whiteTextureSampler;
			imageDescriptor.imageView = m_gpuImageView;
			imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkWriteDescriptorSet writes[2];
			memset(writes, 0, sizeof(writes));
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = m_gpuDescriptorSet;
			writes[0].dstBinding = 0;
			writes[0].descriptorCount = 1;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[0].pImageInfo = &imageDescriptor;
			writes[1] = writes[0];
			writes[1].dstBinding = 1;
			VulkanNamespace::vkUpdateDescriptorSetsLocal(VulkanNamespace::ms_device, 2, writes, 0, 0);

			if (VulkanNamespace::ms_clampTextureSampler)
			{
				VkDescriptorSetAllocateInfo clampDescriptorAllocate = descriptorAllocate;
				if (VulkanNamespace::vkAllocateDescriptorSetsLocal(VulkanNamespace::ms_device, &clampDescriptorAllocate, &m_gpuClampDescriptorSet) == VK_SUCCESS)
				{
					VkDescriptorImageInfo clampImageDescriptor = imageDescriptor;
					clampImageDescriptor.sampler = VulkanNamespace::ms_clampTextureSampler;
					VkWriteDescriptorSet clampWrites[2] = { writes[0], writes[1] };
					clampWrites[0].dstSet = m_gpuClampDescriptorSet;
					clampWrites[0].pImageInfo = &clampImageDescriptor;
					clampWrites[1].dstSet = m_gpuClampDescriptorSet;
					clampWrites[1].pImageInfo = &clampImageDescriptor;
					VulkanNamespace::vkUpdateDescriptorSetsLocal(VulkanNamespace::ms_device, 2, clampWrites, 0, 0);
				}
				else
				{
					m_gpuClampDescriptorSet = 0;
					static int s_logCount = 0;
					if (s_logCount++ < 16)
						VulkanNamespace::logLine("vkAllocateDescriptorSets clamp texture failed");
				}
			}

			m_gpuDescriptorGeneration = VulkanNamespace::ms_gpuDescriptorGeneration;
			m_gpuUploadRecorded = false;
			m_gpuMipLevels = mipLevels;
			++VulkanNamespace::ms_perfTextureUploadsThisPresent;
			++VulkanNamespace::ms_perfTextureUploadsTotal;
			VulkanNamespace::ms_perfTextureUploadBytesThisPresent += static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu));
			VulkanNamespace::ms_perfTextureUploadBytesTotal += static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu));
			static int s_textureUploadLogCount = 0;
			if (s_textureUploadLogCount++ < 80)
			{
				char buffer[224];
				_snprintf(buffer, sizeof(buffer) - 1, "gpu texture upload ok %dx%d mips=%u bytes=%u format=%d descriptor=%p", m_width, m_height, static_cast<unsigned>(mipLevels), static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu)), static_cast<int>(m_format), reinterpret_cast<void *>(m_gpuDescriptorSet));
				buffer[sizeof(buffer) - 1] = 0;
				VulkanNamespace::logLine(buffer);
			}
			return true;
		}

		bool ensureGpuCubeResources() const
		{
			if (m_faceCount <= 1)
				return false;
			if (!VulkanNamespace::ms_device || !VulkanNamespace::ms_gpuDrawDescriptorPool || !VulkanNamespace::ms_whiteTextureDescriptorSetLayout || !VulkanNamespace::ms_whiteTextureSampler)
				return false;
			if (m_gpuCubeDescriptorSet && m_gpuCubeDescriptorGeneration == VulkanNamespace::ms_gpuDescriptorGeneration)
				return true;

			VkDeviceSize const faceByteSize = static_cast<VkDeviceSize>(std::max(1, m_width)) * static_cast<VkDeviceSize>(std::max(1, m_height)) * 4u;
			VkDeviceSize const stagingByteSize = faceByteSize * static_cast<VkDeviceSize>(std::max(1, m_faceCount));
			int const uploadBudget = VulkanNamespace::gpuTextureUploadBudgetPerPresent();
			unsigned const uploadByteBudget = VulkanNamespace::gpuTextureUploadByteBudgetPerPresent();
			if ((uploadBudget > 0 && VulkanNamespace::ms_perfTextureUploadsThisPresent >= static_cast<unsigned>(uploadBudget)) ||
				(uploadByteBudget > 0 &&
				VulkanNamespace::ms_perfTextureUploadsThisPresent > 0 &&
				VulkanNamespace::ms_perfTextureUploadBytesThisPresent + static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu)) > uploadByteBudget))
			{
				return false;
			}

			if (m_gpuCubeImageView)
			{
				VulkanNamespace::vkDestroyImageViewLocal(VulkanNamespace::ms_device, m_gpuCubeImageView, 0);
				m_gpuCubeImageView = 0;
			}
			if (m_gpuCubeImage)
			{
				VulkanNamespace::vkDestroyImageLocal(VulkanNamespace::ms_device, m_gpuCubeImage, 0);
				m_gpuCubeImage = 0;
			}
			if (m_gpuCubeMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuCubeMemory, 0);
				m_gpuCubeMemory = 0;
			}
			if (m_gpuCubeStagingBuffer)
			{
				VulkanNamespace::vkDestroyBufferLocal(VulkanNamespace::ms_device, m_gpuCubeStagingBuffer, 0);
				m_gpuCubeStagingBuffer = 0;
			}
			if (m_gpuCubeStagingMemory)
			{
				VulkanNamespace::vkFreeMemoryLocal(VulkanNamespace::ms_device, m_gpuCubeStagingMemory, 0);
				m_gpuCubeStagingMemory = 0;
			}
			m_gpuCubeDescriptorSet = 0;
			m_gpuCubeClampDescriptorSet = 0;
			m_gpuCubeDescriptorGeneration = 0xffffffff;
			m_gpuCubeUploadRecorded = false;

			VkImageCreateInfo imageInfo;
			memset(&imageInfo, 0, sizeof(imageInfo));
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			imageInfo.extent.width = static_cast<uint32_t>(std::max(1, m_width));
			imageInfo.extent.height = static_cast<uint32_t>(std::max(1, m_height));
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 6;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (VulkanNamespace::vkCreateImageLocal(VulkanNamespace::ms_device, &imageInfo, 0, &m_gpuCubeImage) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkCreateImage cube texture failed");
				return false;
			}

			VkMemoryRequirements requirements;
			memset(&requirements, 0, sizeof(requirements));
			VulkanNamespace::vkGetImageMemoryRequirementsLocal(VulkanNamespace::ms_device, m_gpuCubeImage, &requirements);
			uint32_t const memoryType = VulkanNamespace::findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (memoryType == 0xffffffff)
			{
				VulkanNamespace::logLine("cube texture device-local memory type not found");
				return false;
			}

			VkMemoryAllocateInfo allocateInfo;
			memset(&allocateInfo, 0, sizeof(allocateInfo));
			allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocateInfo.allocationSize = requirements.size;
			allocateInfo.memoryTypeIndex = memoryType;
			if (VulkanNamespace::vkAllocateMemoryLocal(VulkanNamespace::ms_device, &allocateInfo, 0, &m_gpuCubeMemory) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkAllocateMemory cube texture failed");
				return false;
			}
			if (VulkanNamespace::vkBindImageMemoryLocal(VulkanNamespace::ms_device, m_gpuCubeImage, m_gpuCubeMemory, 0) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkBindImageMemory cube texture failed");
				return false;
			}

			if (!VulkanNamespace::createHostVisibleBuffer(stagingByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_gpuCubeStagingBuffer, m_gpuCubeStagingMemory, 0, "cube texture staging"))
				return false;

			void *mapped = 0;
			if (VulkanNamespace::vkMapMemoryLocal(VulkanNamespace::ms_device, m_gpuCubeStagingMemory, 0, stagingByteSize, 0, &mapped) != VK_SUCCESS || !mapped)
			{
				VulkanNamespace::logLine("vkMapMemory cube texture staging failed");
				return false;
			}
			byte *base = static_cast<byte *>(mapped);
			for (int face = 0; face < 6; ++face)
			{
				byte *faceBase = base + faceByteSize * static_cast<VkDeviceSize>(face);
				for (int y = 0; y < m_height; ++y)
				{
					byte *row = faceBase + static_cast<size_t>(y) * static_cast<size_t>(std::max(1, m_width)) * 4u;
					for (int x = 0; x < m_width; ++x)
					{
						uint32 const argb = samplePixel(x, y, face);
						byte *pixel = row + static_cast<size_t>(x) * 4u;
						pixel[0] = static_cast<byte>((argb >> 16) & 0xff);
						pixel[1] = static_cast<byte>((argb >> 8) & 0xff);
						pixel[2] = static_cast<byte>(argb & 0xff);
						pixel[3] = static_cast<byte>((argb >> 24) & 0xff);
					}
				}
			}
			VulkanNamespace::vkUnmapMemoryLocal(VulkanNamespace::ms_device, m_gpuCubeStagingMemory);

			VkImageViewCreateInfo viewInfo;
			memset(&viewInfo, 0, sizeof(viewInfo));
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_gpuCubeImage;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.layerCount = 6;
			if (VulkanNamespace::vkCreateImageViewLocal(VulkanNamespace::ms_device, &viewInfo, 0, &m_gpuCubeImageView) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkCreateImageView cube texture failed");
				return false;
			}

			VkDescriptorSetAllocateInfo descriptorAllocate;
			memset(&descriptorAllocate, 0, sizeof(descriptorAllocate));
			descriptorAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorAllocate.descriptorPool = VulkanNamespace::ms_gpuDrawDescriptorPool;
			descriptorAllocate.descriptorSetCount = 1;
			descriptorAllocate.pSetLayouts = &VulkanNamespace::ms_whiteTextureDescriptorSetLayout;
			if (VulkanNamespace::vkAllocateDescriptorSetsLocal(VulkanNamespace::ms_device, &descriptorAllocate, &m_gpuCubeDescriptorSet) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkAllocateDescriptorSets cube texture failed");
				return false;
			}

			VkDescriptorImageInfo imageDescriptor;
			memset(&imageDescriptor, 0, sizeof(imageDescriptor));
			imageDescriptor.sampler = VulkanNamespace::ms_whiteTextureSampler;
			imageDescriptor.imageView = m_gpuCubeImageView;
			imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkWriteDescriptorSet writes[2];
			memset(writes, 0, sizeof(writes));
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = m_gpuCubeDescriptorSet;
			writes[0].dstBinding = 0;
			writes[0].descriptorCount = 1;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[0].pImageInfo = &imageDescriptor;
			writes[1] = writes[0];
			writes[1].dstBinding = 1;
			VulkanNamespace::vkUpdateDescriptorSetsLocal(VulkanNamespace::ms_device, 2, writes, 0, 0);

			if (VulkanNamespace::ms_clampTextureSampler)
			{
				VkDescriptorSetAllocateInfo clampDescriptorAllocate = descriptorAllocate;
				if (VulkanNamespace::vkAllocateDescriptorSetsLocal(VulkanNamespace::ms_device, &clampDescriptorAllocate, &m_gpuCubeClampDescriptorSet) == VK_SUCCESS)
				{
					VkDescriptorImageInfo clampImageDescriptor = imageDescriptor;
					clampImageDescriptor.sampler = VulkanNamespace::ms_clampTextureSampler;
					VkWriteDescriptorSet clampWrites[2] = { writes[0], writes[1] };
					clampWrites[0].dstSet = m_gpuCubeClampDescriptorSet;
					clampWrites[0].pImageInfo = &clampImageDescriptor;
					clampWrites[1].dstSet = m_gpuCubeClampDescriptorSet;
					clampWrites[1].pImageInfo = &clampImageDescriptor;
					VulkanNamespace::vkUpdateDescriptorSetsLocal(VulkanNamespace::ms_device, 2, clampWrites, 0, 0);
				}
				else
					m_gpuCubeClampDescriptorSet = 0;
			}

			m_gpuCubeDescriptorGeneration = VulkanNamespace::ms_gpuDescriptorGeneration;
			m_gpuCubeUploadRecorded = false;
			++VulkanNamespace::ms_perfTextureUploadsThisPresent;
			++VulkanNamespace::ms_perfTextureUploadsTotal;
			VulkanNamespace::ms_perfTextureUploadBytesThisPresent += static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu));
			VulkanNamespace::ms_perfTextureUploadBytesTotal += static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu));
			static int s_cubeTextureUploadLogCount = 0;
			if (s_cubeTextureUploadLogCount++ < 32)
			{
				uint32 const face0 = samplePixel(m_width / 2, m_height / 2, 0);
				uint32 const face1 = samplePixel(m_width / 2, m_height / 2, 1);
				uint32 const face2 = samplePixel(m_width / 2, m_height / 2, 2);
				uint32 const face3 = samplePixel(m_width / 2, m_height / 2, 3);
				uint32 const face4 = samplePixel(m_width / 2, m_height / 2, 4);
				uint32 const face5 = samplePixel(m_width / 2, m_height / 2, 5);
				uint32 const proxy = sample(0.5f, 0.5f, true);
				char buffer[384];
				_snprintf(buffer, sizeof(buffer) - 1, "gpu cube texture upload ok %dx%d faces=%d format=%d descriptor=%p faceCenter=%08x,%08x,%08x,%08x,%08x,%08x proxy=%08x",
					m_width,
					m_height,
					m_faceCount,
					static_cast<int>(m_format),
					reinterpret_cast<void *>(m_gpuCubeDescriptorSet),
					static_cast<unsigned int>(face0),
					static_cast<unsigned int>(face1),
					static_cast<unsigned int>(face2),
					static_cast<unsigned int>(face3),
					static_cast<unsigned int>(face4),
					static_cast<unsigned int>(face5),
					static_cast<unsigned int>(proxy));
				buffer[sizeof(buffer) - 1] = 0;
				VulkanNamespace::logLine(buffer);
			}
			return true;
		}

		bool ensureGpuAlphaMaskResources() const
		{
			if (!ensureGpuResources())
				return false;
			if (m_gpuAlphaMaskDescriptorSet && m_gpuAlphaMaskDescriptorGeneration == VulkanNamespace::ms_gpuDescriptorGeneration)
				return true;

			VkDeviceSize const stagingByteSize = static_cast<VkDeviceSize>(std::max(1, m_width)) * static_cast<VkDeviceSize>(std::max(1, m_height)) * 4u;
			int const uploadBudget = VulkanNamespace::gpuTextureUploadBudgetPerPresent();
			unsigned const uploadByteBudget = VulkanNamespace::gpuTextureUploadByteBudgetPerPresent();
			if ((uploadBudget > 0 && VulkanNamespace::ms_perfTextureUploadsThisPresent >= static_cast<unsigned>(uploadBudget)) ||
				(uploadByteBudget > 0 &&
				VulkanNamespace::ms_perfTextureUploadsThisPresent > 0 &&
				VulkanNamespace::ms_perfTextureUploadBytesThisPresent + static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu)) > uploadByteBudget))
			{
				return false;
			}

			destroyGpuAlphaMaskResources();

			VkImageCreateInfo imageInfo;
			memset(&imageInfo, 0, sizeof(imageInfo));
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			imageInfo.extent.width = static_cast<uint32_t>(std::max(1, m_width));
			imageInfo.extent.height = static_cast<uint32_t>(std::max(1, m_height));
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (VulkanNamespace::vkCreateImageLocal(VulkanNamespace::ms_device, &imageInfo, 0, &m_gpuAlphaMaskImage) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkCreateImage alpha-mask texture failed");
				return false;
			}

			VkMemoryRequirements requirements;
			memset(&requirements, 0, sizeof(requirements));
			VulkanNamespace::vkGetImageMemoryRequirementsLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskImage, &requirements);
			uint32_t const memoryType = VulkanNamespace::findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (memoryType == 0xffffffff)
			{
				VulkanNamespace::logLine("alpha-mask texture device-local memory type not found");
				return false;
			}

			VkMemoryAllocateInfo allocateInfo;
			memset(&allocateInfo, 0, sizeof(allocateInfo));
			allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocateInfo.allocationSize = requirements.size;
			allocateInfo.memoryTypeIndex = memoryType;
			if (VulkanNamespace::vkAllocateMemoryLocal(VulkanNamespace::ms_device, &allocateInfo, 0, &m_gpuAlphaMaskMemory) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkAllocateMemory alpha-mask texture failed");
				return false;
			}
			if (VulkanNamespace::vkBindImageMemoryLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskImage, m_gpuAlphaMaskMemory, 0) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkBindImageMemory alpha-mask texture failed");
				return false;
			}

			if (!VulkanNamespace::createHostVisibleBuffer(stagingByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_gpuAlphaMaskStagingBuffer, m_gpuAlphaMaskStagingMemory, 0, "alpha-mask texture staging"))
				return false;

			void *mapped = 0;
			if (VulkanNamespace::vkMapMemoryLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskStagingMemory, 0, stagingByteSize, 0, &mapped) != VK_SUCCESS || !mapped)
			{
				VulkanNamespace::logLine("vkMapMemory alpha-mask texture staging failed");
				return false;
			}
			byte *base = static_cast<byte *>(mapped);
			for (int y = 0; y < m_height; ++y)
			{
				byte *row = base + static_cast<size_t>(y) * static_cast<size_t>(std::max(1, m_width)) * 4u;
				for (int x = 0; x < m_width; ++x)
				{
					float const u = m_width > 1 ? static_cast<float>(x) / static_cast<float>(m_width - 1) : 0.0f;
					float const v = m_height > 1 ? static_cast<float>(y) / static_cast<float>(m_height - 1) : 0.0f;
					uint32 const argb = sample(u, v);
					byte *pixel = row + static_cast<size_t>(x) * 4u;
					pixel[0] = 0xff;
					pixel[1] = 0xff;
					pixel[2] = 0xff;
					pixel[3] = static_cast<byte>((argb >> 24) & 0xff);
				}
			}
			VulkanNamespace::vkUnmapMemoryLocal(VulkanNamespace::ms_device, m_gpuAlphaMaskStagingMemory);

			VkImageViewCreateInfo viewInfo;
			memset(&viewInfo, 0, sizeof(viewInfo));
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_gpuAlphaMaskImage;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.layerCount = 1;
			if (VulkanNamespace::vkCreateImageViewLocal(VulkanNamespace::ms_device, &viewInfo, 0, &m_gpuAlphaMaskImageView) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkCreateImageView alpha-mask texture failed");
				return false;
			}

			VkDescriptorSetAllocateInfo descriptorAllocate;
			memset(&descriptorAllocate, 0, sizeof(descriptorAllocate));
			descriptorAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorAllocate.descriptorPool = VulkanNamespace::ms_gpuDrawDescriptorPool;
			descriptorAllocate.descriptorSetCount = 1;
			descriptorAllocate.pSetLayouts = &VulkanNamespace::ms_whiteTextureDescriptorSetLayout;
			if (VulkanNamespace::vkAllocateDescriptorSetsLocal(VulkanNamespace::ms_device, &descriptorAllocate, &m_gpuAlphaMaskDescriptorSet) != VK_SUCCESS)
			{
				VulkanNamespace::logLine("vkAllocateDescriptorSets alpha-mask texture failed");
				return false;
			}

			VkDescriptorImageInfo imageDescriptor;
			memset(&imageDescriptor, 0, sizeof(imageDescriptor));
			imageDescriptor.sampler = VulkanNamespace::ms_pointTextureSampler ? VulkanNamespace::ms_pointTextureSampler : VulkanNamespace::ms_whiteTextureSampler;
			imageDescriptor.imageView = m_gpuAlphaMaskImageView;
			imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkWriteDescriptorSet writes[2];
			memset(writes, 0, sizeof(writes));
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = m_gpuAlphaMaskDescriptorSet;
			writes[0].dstBinding = 0;
			writes[0].descriptorCount = 1;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[0].pImageInfo = &imageDescriptor;
			writes[1] = writes[0];
			writes[1].dstBinding = 1;
			VulkanNamespace::vkUpdateDescriptorSetsLocal(VulkanNamespace::ms_device, 2, writes, 0, 0);

			m_gpuAlphaMaskDescriptorGeneration = VulkanNamespace::ms_gpuDescriptorGeneration;
			m_gpuAlphaMaskUploadRecorded = false;
			++VulkanNamespace::ms_perfTextureUploadsThisPresent;
			++VulkanNamespace::ms_perfTextureUploadsTotal;
			VulkanNamespace::ms_perfTextureUploadBytesThisPresent += static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu));
			VulkanNamespace::ms_perfTextureUploadBytesTotal += static_cast<unsigned>(std::min<VkDeviceSize>(stagingByteSize, 0xffffffffu));
			static int s_alphaMaskTextureUploadLogCount = 0;
			if (s_alphaMaskTextureUploadLogCount++ < 80)
			{
				char buffer[224];
				_snprintf(buffer, sizeof(buffer) - 1, "gpu alpha-mask texture upload ok %dx%d format=%d descriptor=%p", m_width, m_height, static_cast<int>(m_format), reinterpret_cast<void *>(m_gpuAlphaMaskDescriptorSet));
				buffer[sizeof(buffer) - 1] = 0;
				VulkanNamespace::logLine(buffer);
			}
			return true;
		}

		static int getRowPitch(TextureFormat format, int width)
		{
			TextureFormatInfo const &info = TextureFormatInfo::getInfo(format);
			if (info.compressed)
				return ((std::max(1, width) + info.blockWidth - 1) / info.blockWidth) * info.blockSize;
			return std::max(1, width) * info.pixelByteCount;
		}

		static size_t getStorageSize(TextureFormat format, int width, int height)
		{
			TextureFormatInfo const &info = TextureFormatInfo::getInfo(format);
			int const pitch = getRowPitch(format, width);
			int const rows = info.compressed ? ((std::max(1, height) + info.blockHeight - 1) / info.blockHeight) : std::max(1, height);
			return static_cast<size_t>(pitch) * static_cast<size_t>(rows);
		}

		size_t getFaceStorageSize() const
		{
			return getStorageSize(m_format, m_width, m_height);
		}

		size_t getFaceOffset(int face) const
		{
			int const clampedFace = std::max(0, std::min(m_faceCount - 1, face));
			return getFaceStorageSize() * static_cast<size_t>(clampedFace);
		}

		static int getFaceIndex(CubeFace cubeFace)
		{
			int const face = static_cast<int>(cubeFace);
			return face >= 0 && face < 6 ? face : 0;
		}

		static uint32 expand565(uint16 value)
		{
			uint32 const r5 = (value >> 11) & 0x1f;
			uint32 const g6 = (value >> 5) & 0x3f;
			uint32 const b5 = value & 0x1f;
			uint32 const r = (r5 << 3) | (r5 >> 2);
			uint32 const g = (g6 << 2) | (g6 >> 4);
			uint32 const b = (b5 << 3) | (b5 >> 2);
			return 0xff000000 | (r << 16) | (g << 8) | b;
		}

		static uint32 lerpColor(uint32 a, uint32 b, int aw, int bw, int denom)
		{
			uint32 const ar = (a >> 16) & 0xff;
			uint32 const ag = (a >> 8) & 0xff;
			uint32 const ab = a & 0xff;
			uint32 const br = (b >> 16) & 0xff;
			uint32 const bg = (b >> 8) & 0xff;
			uint32 const bb = b & 0xff;
			uint32 const r = (ar * aw + br * bw) / denom;
			uint32 const g = (ag * aw + bg * bw) / denom;
			uint32 const blue = (ab * aw + bb * bw) / denom;
			return 0xff000000 | (r << 16) | (g << 8) | blue;
		}

		uint32 sampleDxtColor(byte const *block, int x, int y, bool dxt1Alpha) const
		{
			uint16 const c0 = static_cast<uint16>(block[0] | (block[1] << 8));
			uint16 const c1 = static_cast<uint16>(block[2] | (block[3] << 8));
			uint32 colors[4];
			colors[0] = expand565(c0);
			colors[1] = expand565(c1);
			if (c0 > c1 || !dxt1Alpha)
			{
				colors[2] = lerpColor(colors[0], colors[1], 2, 1, 3);
				colors[3] = lerpColor(colors[0], colors[1], 1, 2, 3);
			}
			else
			{
				colors[2] = lerpColor(colors[0], colors[1], 1, 1, 2);
				colors[3] = 0x00000000;
			}

			uint32 const indices = static_cast<uint32>(block[4]) | (static_cast<uint32>(block[5]) << 8) | (static_cast<uint32>(block[6]) << 16) | (static_cast<uint32>(block[7]) << 24);
			int const index = (indices >> (2 * (y * 4 + x))) & 0x3;
			return colors[index];
		}

		uint32 sampleDxt(int x, int y, int face) const
		{
			TextureFormatInfo const &info = TextureFormatInfo::getInfo(m_format);
			int const blockX = x / 4;
			int const blockY = y / 4;
			int const localX = x & 3;
			int const localY = y & 3;
			size_t const offset = getFaceOffset(face) + static_cast<size_t>(blockY * getRowPitch(m_format, m_width) + blockX * info.blockSize);
			if (offset + static_cast<size_t>(info.blockSize) > m_pixels.size())
				return 0xffffffff;

			byte const *block = &m_pixels[offset];
			if (m_format == TF_DXT1)
				return sampleDxtColor(block, localX, localY, !VulkanNamespace::gpuDxt1OpaqueDecodeEnabled());

			uint32 alpha = 0xff;
			if (m_format == TF_DXT2 || m_format == TF_DXT3)
			{
				int const alphaByte = (localY * 4 + localX) / 2;
				byte const packed = block[alphaByte];
				alpha = ((localX & 1) ? (packed >> 4) : (packed & 0x0f)) * 17;
				return (sampleDxtColor(block + 8, localX, localY, false) & 0x00ffffff) | (alpha << 24);
			}

			if (m_format == TF_DXT4 || m_format == TF_DXT5)
			{
				byte const a0 = block[0];
				byte const a1 = block[1];
				uint64 bits = 0;
				for (int i = 0; i < 6; ++i)
					bits |= static_cast<uint64>(block[2 + i]) << (8 * i);
				int const alphaIndex = static_cast<int>((bits >> (3 * (localY * 4 + localX))) & 0x7);
				uint32 alphas[8];
				alphas[0] = a0;
				alphas[1] = a1;
				if (a0 > a1)
				{
					alphas[2] = (6 * a0 + 1 * a1) / 7;
					alphas[3] = (5 * a0 + 2 * a1) / 7;
					alphas[4] = (4 * a0 + 3 * a1) / 7;
					alphas[5] = (3 * a0 + 4 * a1) / 7;
					alphas[6] = (2 * a0 + 5 * a1) / 7;
					alphas[7] = (1 * a0 + 6 * a1) / 7;
				}
				else
				{
					alphas[2] = (4 * a0 + 1 * a1) / 5;
					alphas[3] = (3 * a0 + 2 * a1) / 5;
					alphas[4] = (2 * a0 + 3 * a1) / 5;
					alphas[5] = (1 * a0 + 4 * a1) / 5;
					alphas[6] = 0;
					alphas[7] = 255;
				}
				alpha = alphas[alphaIndex];
				return (sampleDxtColor(block + 8, localX, localY, false) & 0x00ffffff) | (alpha << 24);
			}

			return 0xffffffff;
		}

		uint32 samplePixelBestFace(int x, int y) const
		{
			uint32 first = samplePixel(x, y, 0);
			if (m_faceCount <= 1)
				return first;
			if ((first & 0x00ffffff) != 0 || ((first >> 24) & 0xff) != 0)
				return first;
			for (int face = 1; face < m_faceCount; ++face)
			{
				uint32 const candidate = samplePixel(x, y, face);
				if ((candidate & 0x00ffffff) != 0 || ((candidate >> 24) & 0xff) != 0)
					return candidate;
			}
			return first;
		}

		static bool colorHasSignal(uint32 color)
		{
			uint32 const a = (color >> 24) & 0xff;
			uint32 const r = (color >> 16) & 0xff;
			uint32 const g = (color >> 8) & 0xff;
			uint32 const b = color & 0xff;
			return a != 0 && (r + g + b) > 8;
		}

		uint32 averageCubeRow(int y) const
		{
			int const sampleX[3] = { m_width / 4, m_width / 2, (m_width * 3) / 4 };
			int const sampleY[5] = { m_height / 8, m_height / 4, m_height / 2, (m_height * 3) / 4, (m_height * 7) / 8 };
			uint32 r = 0;
			uint32 g = 0;
			uint32 b = 0;
			uint32 count = 0;
			UNREF(y);
			for (int face = 0; face < m_faceCount; ++face)
			{
				if (face == getFaceIndex(CF_positiveY) || face == getFaceIndex(CF_negativeY))
					continue;
				for (int i = 0; i < 3; ++i)
				{
					for (int j = 0; j < 5; ++j)
					{
						uint32 const candidate = samplePixel(sampleX[i], std::max(0, std::min(m_height - 1, sampleY[j])), face);
						if (!colorHasSignal(candidate))
							continue;
						r += (candidate >> 16) & 0xff;
						g += (candidate >> 8) & 0xff;
						b += candidate & 0xff;
						++count;
					}
				}
			}
			if (count == 0)
			{
				uint32 const top = samplePixel(m_width / 2, m_height / 2, getFaceIndex(CF_positiveY));
				if (colorHasSignal(top))
					return top | 0xff000000;
				uint32 const bottom = samplePixel(m_width / 2, m_height / 2, getFaceIndex(CF_negativeY));
				if (colorHasSignal(bottom))
					return bottom | 0xff000000;
				return 0xff000000;
			}
			return 0xff000000 | ((r / count) << 16) | ((g / count) << 8) | (b / count);
		}

		uint32 sampleCubeDirection(float x, float y, float z) const
		{
			float const absX = fabsf(x);
			float const absY = fabsf(y);
			float const absZ = fabsf(z);
			int face = getFaceIndex(CF_positiveZ);
			float s = 0.0f;
			float t = 0.0f;
			if (absX >= absY && absX >= absZ && absX > 0.00001f)
			{
				if (x >= 0.0f)
				{
					face = getFaceIndex(CF_positiveX);
					s = -z / absX;
				}
				else
				{
					face = getFaceIndex(CF_negativeX);
					s = z / absX;
				}
				t = -y / absX;
			}
			else if (absY >= absX && absY >= absZ && absY > 0.00001f)
			{
				if (y >= 0.0f)
				{
					face = getFaceIndex(CF_positiveY);
					t = z / absY;
				}
				else
				{
					face = getFaceIndex(CF_negativeY);
					t = -z / absY;
				}
				s = x / absY;
			}
			else if (absZ > 0.00001f)
			{
				if (z >= 0.0f)
				{
					face = getFaceIndex(CF_positiveZ);
					s = x / absZ;
				}
				else
				{
					face = getFaceIndex(CF_negativeZ);
					s = -x / absZ;
				}
				t = -y / absZ;
			}

			float const faceU = std::max(0.0f, std::min(1.0f, (s + 1.0f) * 0.5f));
			float const faceV = std::max(0.0f, std::min(1.0f, (t + 1.0f) * 0.5f));
			int const px = std::max(0, std::min(m_width - 1, static_cast<int>(faceU * static_cast<float>(m_width - 1) + 0.5f)));
			int const py = std::max(0, std::min(m_height - 1, static_cast<int>(faceV * static_cast<float>(m_height - 1) + 0.5f)));
			uint32 const color = samplePixel(px, py, face);
			if (colorHasSignal(color))
				return color;
			return samplePixelBestFace(px, py);
		}

		uint32 sampleCubeProxy(float u, float v, bool wrap) const
		{
			if (wrap)
			{
				u = u - floorf(u);
				v = v - floorf(v);
				if (u < 0.0f)
					u += 1.0f;
				if (v < 0.0f)
					v += 1.0f;
			}
			else
			{
				u = std::max(0.0f, std::min(1.0f, u));
				v = std::max(0.0f, std::min(1.0f, v));
			}

			size_t const proxyPixelCount = static_cast<size_t>(std::max(1, m_height)) * static_cast<size_t>(std::max(1, m_width));
			if (!m_cubeProxyRowValid || m_cubeProxyRow.size() != proxyPixelCount)
			{
				m_cubeProxyRow.resize(proxyPixelCount);
				for (int y = 0; y < m_height; ++y)
				{
					float const rowV = m_height > 1 ? static_cast<float>(y) / static_cast<float>(m_height - 1) : 0.5f;
					float const pitch = (0.5f - rowV) * 3.1415926535f;
					float const cosPitch = cosf(pitch);
					float const sinPitch = sinf(pitch);
					for (int x = 0; x < m_width; ++x)
					{
						float const rowU = m_width > 1 ? static_cast<float>(x) / static_cast<float>(m_width - 1) : 0.5f;
						float const yaw = (rowU - 0.5f) * 6.283185307f;
						uint32 color = sampleCubeDirection(sinf(yaw) * cosPitch, sinPitch, cosf(yaw) * cosPitch);
						if (!colorHasSignal(color))
							color = averageCubeRow(y);
						m_cubeProxyRow[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)] = color;
					}
				}
				m_cubeProxyRowValid = true;
			}

			int const x = std::max(0, std::min(m_width - 1, static_cast<int>(u * static_cast<float>(m_width - 1) + 0.5f)));
			int const y = std::max(0, std::min(m_height - 1, static_cast<int>(v * static_cast<float>(m_height - 1) + 0.5f)));
			return m_cubeProxyRow[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
		}

		uint32 samplePixel(int x, int y, int face = 0) const
		{
			if (TextureFormatInfo::getInfo(m_format).compressed)
				return sampleDxt(x, y, face);

			size_t const offset = getFaceOffset(face) + static_cast<size_t>(y * getRowPitch(m_format, m_width));
			if (offset >= m_pixels.size())
				return 0xffffffff;
			byte const *pixel = &m_pixels[offset];
			pixel += static_cast<size_t>(x) * TextureFormatInfo::getInfo(m_format).pixelByteCount;
			if (pixel >= &m_pixels[0] + m_pixels.size())
				return 0xffffffff;

			switch (m_format)
			{
			case TF_ARGB_8888:
				{
					uint32 color = 0xffffffff;
					memcpy(&color, pixel, sizeof(color));
					return color;
				}
			case TF_XRGB_8888:
				{
					uint32 color = 0xffffffff;
					memcpy(&color, pixel, sizeof(color));
					return color | 0xff000000;
				}
			case TF_RGB_888:
				return 0xff000000 | (static_cast<uint32>(pixel[0]) << 16) | (static_cast<uint32>(pixel[1]) << 8) | pixel[2];
			case TF_RGB_565:
				return expand565(static_cast<uint16>(pixel[0] | (pixel[1] << 8)));
			case TF_RGB_555:
			case TF_ARGB_1555:
				{
					uint16 const value = static_cast<uint16>(pixel[0] | (pixel[1] << 8));
					uint32 const a = (m_format == TF_ARGB_1555 && !(value & 0x8000)) ? 0x00000000 : 0xff000000;
					uint32 const r = ((value >> 10) & 0x1f) << 3;
					uint32 const g = ((value >> 5) & 0x1f) << 3;
					uint32 const b = (value & 0x1f) << 3;
					return a | (r << 16) | (g << 8) | b;
				}
			case TF_ARGB_4444:
				{
					uint16 const value = static_cast<uint16>(pixel[0] | (pixel[1] << 8));
					uint32 const a = ((value >> 12) & 0x0f) * 17;
					uint32 const r = ((value >> 8) & 0x0f) * 17;
					uint32 const g = ((value >> 4) & 0x0f) * 17;
					uint32 const b = (value & 0x0f) * 17;
					return (a << 24) | (r << 16) | (g << 8) | b;
				}
			case TF_A_8:
				return static_cast<uint32>(*pixel) << 24 | 0x00ffffff;
			case TF_L_8:
			case TF_P_8:
				return 0xff000000 | (static_cast<uint32>(*pixel) << 16) | (static_cast<uint32>(*pixel) << 8) | *pixel;
			default:
				return 0xffffffff;
			}
		}

		virtual TextureFormat getNativeFormat() const { return m_format; }
		virtual void lock(LockData &lockData)
		{
			lockData.m_pitch = getRowPitch(m_format, lockData.getWidth());
			lockData.m_slicePitch = lockData.m_pitch * lockData.getHeight();
			lockData.m_reserved = 0;

			if (lockData.getLevel() != 0)
			{
				ScratchLock *scratch = new ScratchLock;
				scratch->data.resize(static_cast<size_t>(std::max(0, lockData.m_slicePitch)));
				lockData.m_pixelData = scratch->data.empty() ? 0 : &scratch->data[0];
				lockData.m_reserved = scratch;
				return;
			}

			size_t const baseSize = getFaceStorageSize() * static_cast<size_t>(m_faceCount);
			if (m_pixels.size() < baseSize)
				m_pixels.resize(baseSize);
			TextureFormatInfo const &info = TextureFormatInfo::getInfo(m_format);
			size_t offset = getFaceOffset(getFaceIndex(lockData.m_cubeFace));
			if (info.compressed)
			{
				offset += static_cast<size_t>((std::max(0, lockData.getY()) / info.blockHeight) * getRowPitch(m_format, m_width) + (std::max(0, lockData.getX()) / info.blockWidth) * info.blockSize);
			}
			else
			{
				offset += static_cast<size_t>(std::max(0, lockData.getY())) * static_cast<size_t>(getRowPitch(m_format, m_width)) + static_cast<size_t>(std::max(0, lockData.getX())) * static_cast<size_t>(info.pixelByteCount);
			}
			lockData.m_pitch = getRowPitch(m_format, m_width);
			lockData.m_slicePitch = lockData.m_pitch * (info.compressed ? ((m_height + info.blockHeight - 1) / info.blockHeight) : m_height);
			lockData.m_pixelData = !m_pixels.empty() && offset < m_pixels.size() ? &m_pixels[offset] : 0;
		}
		virtual void unlock(LockData &lockData)
		{
			m_cubeProxyRowValid = false;
			if (lockData.m_reserved)
			{
				delete reinterpret_cast<ScratchLock *>(lockData.m_reserved);
				lockData.m_reserved = 0;
			}
			lockData.m_pitch = 0;
			lockData.m_slicePitch = 0;
			lockData.m_pixelData = 0;
		}

	private:
		TextureFormat m_format;
		int m_width;
		int m_height;
		int m_faceCount;
		std::vector<byte> m_pixels;
		mutable VkImage m_gpuImage;
		mutable VkDeviceMemory m_gpuMemory;
		mutable VkImageView m_gpuImageView;
		mutable VkBuffer m_gpuStagingBuffer;
		mutable VkDeviceMemory m_gpuStagingMemory;
		mutable VkDescriptorSet m_gpuDescriptorSet;
		mutable VkDescriptorSet m_gpuClampDescriptorSet;
		mutable VkImage m_gpuCubeImage;
		mutable VkDeviceMemory m_gpuCubeMemory;
		mutable VkImageView m_gpuCubeImageView;
		mutable VkBuffer m_gpuCubeStagingBuffer;
		mutable VkDeviceMemory m_gpuCubeStagingMemory;
		mutable VkDescriptorSet m_gpuCubeDescriptorSet;
		mutable VkDescriptorSet m_gpuCubeClampDescriptorSet;
		mutable VkImage m_gpuAlphaMaskImage;
		mutable VkDeviceMemory m_gpuAlphaMaskMemory;
		mutable VkImageView m_gpuAlphaMaskImageView;
		mutable VkBuffer m_gpuAlphaMaskStagingBuffer;
		mutable VkDeviceMemory m_gpuAlphaMaskStagingMemory;
		mutable VkDescriptorSet m_gpuAlphaMaskDescriptorSet;
		mutable uint32_t m_gpuDescriptorGeneration;
		mutable uint32_t m_gpuCubeDescriptorGeneration;
		mutable uint32_t m_gpuAlphaMaskDescriptorGeneration;
		mutable bool m_gpuUploadRecorded;
		mutable bool m_gpuCubeUploadRecorded;
		mutable bool m_gpuAlphaMaskUploadRecorded;
		mutable uint32_t m_gpuMipLevels;
		mutable bool m_cubeProxyRowValid;
		mutable std::vector<uint32> m_cubeProxyRow;
		char m_name[MAX_PATH];
	};

namespace VulkanNamespace
{
	void updateWindowSettings();

	bool isGlobalTextureTag(Tag tag)
	{
		return tag == TAG(E,N,V,M) || (((tag >> 24) & 0xff) == '_');
	}

	VulkanTextureData const *getTextureData(Texture const *texture)
	{
		if (!texture)
			return 0;

		TextureGraphicsData const *graphicsData = texture->getGraphicsData();
		return dynamic_cast<VulkanTextureData const *>(graphicsData);
	}

	VulkanTextureData const *getGlobalTextureData(Tag tag)
	{
		if (!ms_globalTextureMap)
			return 0;

		GlobalTextureMap::const_iterator iter = ms_globalTextureMap->find(tag);
		return iter != ms_globalTextureMap->end() ? getTextureData(iter->second) : 0;
	}

	void setColor(float *destination, float r, float g, float b, float a)
	{
		destination[0] = r;
		destination[1] = g;
		destination[2] = b;
		destination[3] = a;
	}

	void setColorFromVectorArgb(float *destination, VectorArgb const &source)
	{
		setColor(destination, source.r, source.g, source.b, source.a);
	}

	VectorArgb const &getPossiblyScaledDiffuseColor(Light const &light)
	{
		return ms_obeysLightScale ? light.getScaledDiffuseColor() : light.getDiffuseColor();
	}

	float getPossiblyScaledDiffuseIntensity(Light const &light)
	{
		return ms_obeysLightScale ? light.getScaledDiffuseIntensity() : light.getDiffuseIntensity();
	}

	float getPossiblyScaledSpecularIntensity(Light const &light)
	{
		return ms_obeysLightScale ? light.getScaledSpecularIntensity() : light.getSpecularIntensity();
	}

	VectorArgb const &getPossiblyScaledSpecularColor(Light const &light)
	{
		return ms_obeysLightScale ? light.getScaledSpecularColor() : light.getSpecularColor();
	}

	void resetLighting()
	{
		setColor(ms_lightAmbient, 1.0f, 1.0f, 1.0f, 1.0f);
		setColor(ms_lightDiffuse, 0.0f, 0.0f, 0.0f, 1.0f);
		setColor(ms_lightDirection, 0.0f, 0.0f, -1.0f, 0.0f);
		for (int i = 0; i < 3; ++i)
		{
			setColor(ms_parallelLightDiffuse[i], 0.0f, 0.0f, 0.0f, 1.0f);
			setColor(ms_parallelLightSpecular[i], 0.0f, 0.0f, 0.0f, 1.0f);
			setColor(ms_parallelLightDirection[i], 0.0f, 0.0f, -1.0f, 0.0f);
		}
		ms_lightDirectionalEnabled = false;
		ms_selectedLightMask = 0;
	}

	void updateLightsFromList(const stdvector<const Light*>::fwd &lightList)
	{
		++ms_lightSetCount;
		if (ms_lightLogCount < 24)
		{
			int ambientCount = 0;
			int parallelCount = 0;
			int otherCount = 0;
			for (stdvector<const Light*>::fwd::const_iterator i = lightList.begin(); i != lightList.end(); ++i)
			{
				Light const *light = *i;
				if (!light)
					continue;
				if (light->getType() == Light::T_ambient)
					++ambientCount;
				else if (light->getType() == Light::T_parallel)
					++parallelCount;
				else
					++otherCount;
			}
			char buffer[160];
			_snprintf(buffer, sizeof(buffer) - 1, "lights set total=%u ambient=%d parallel=%d other=%d", static_cast<unsigned>(lightList.size()), ambientCount, parallelCount, otherCount);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++ms_lightLogCount;
		}

		setColor(ms_lightAmbient, 0.0f, 0.0f, 0.0f, 1.0f);
		setColor(ms_lightDiffuse, 0.0f, 0.0f, 0.0f, 1.0f);
		for (int i = 0; i < 3; ++i)
		{
			setColor(ms_parallelLightDiffuse[i], 0.0f, 0.0f, 0.0f, 1.0f);
			setColor(ms_parallelLightSpecular[i], 0.0f, 0.0f, 0.0f, 1.0f);
			setColor(ms_parallelLightDirection[i], 0.0f, 0.0f, -1.0f, 0.0f);
		}
		ms_lightDirectionalEnabled = false;
		ms_selectedLightMask = 0;

		bool sawSupportedLight = false;
		Light const *parallelSpecularLight = 0;
		Light const *parallelLights[2] = { 0, 0 };
		for (stdvector<const Light*>::fwd::const_iterator i = lightList.begin(); i != lightList.end(); ++i)
		{
			Light const *light = *i;
			if (!light)
				continue;

			if (light->getType() == Light::T_ambient)
			{
				sawSupportedLight = true;
				ms_selectedLightMask |= 0x0001;
				VectorArgb const &color = getPossiblyScaledDiffuseColor(*light);
				ms_lightAmbient[0] += color.r;
				ms_lightAmbient[1] += color.g;
				ms_lightAmbient[2] += color.b;
			}
			else if (light->getType() == Light::T_parallel)
			{
				sawSupportedLight = true;
				Light const *candidate = light;
				if (!parallelSpecularLight || getPossiblyScaledSpecularIntensity(*candidate) > getPossiblyScaledSpecularIntensity(*parallelSpecularLight))
				{
					Light const *swapLight = parallelSpecularLight;
					parallelSpecularLight = candidate;
					candidate = swapLight;
				}
				for (int parallelIndex = 0; candidate && parallelIndex < 2; ++parallelIndex)
				{
					if (!parallelLights[parallelIndex] || getPossiblyScaledDiffuseIntensity(*candidate) > getPossiblyScaledDiffuseIntensity(*parallelLights[parallelIndex]))
					{
						Light const *swapLight = parallelLights[parallelIndex];
						parallelLights[parallelIndex] = candidate;
						candidate = swapLight;
					}
				}
			}
		}

		Light const *selectedParallelLights[3] = { parallelSpecularLight, parallelLights[0], parallelLights[1] };
		for (int lightIndex = 0; lightIndex < 3; ++lightIndex)
		{
			Light const *light = selectedParallelLights[lightIndex];
			if (!light)
				continue;

			ms_selectedLightMask |= (1 << (lightIndex + 1));
			setColorFromVectorArgb(ms_parallelLightDiffuse[lightIndex], getPossiblyScaledDiffuseColor(*light));
			setColorFromVectorArgb(ms_parallelLightSpecular[lightIndex], getPossiblyScaledSpecularColor(*light));
			Vector const &direction = light->getObjectFrameK_w();
			float const length = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
			if (length > 0.00001f)
			{
				ms_parallelLightDirection[lightIndex][0] = direction.x / length;
				ms_parallelLightDirection[lightIndex][1] = direction.y / length;
				ms_parallelLightDirection[lightIndex][2] = direction.z / length;
				ms_parallelLightDirection[lightIndex][3] = 1.0f;
				if (!ms_lightDirectionalEnabled)
				{
					setColorFromVectorArgb(ms_lightDiffuse, getPossiblyScaledDiffuseColor(*light));
					ms_lightDirection[0] = ms_parallelLightDirection[lightIndex][0];
					ms_lightDirection[1] = ms_parallelLightDirection[lightIndex][1];
					ms_lightDirection[2] = ms_parallelLightDirection[lightIndex][2];
					ms_lightDirection[3] = 0.0f;
					ms_lightDirectionalEnabled = true;
				}
			}
		}

		if (!sawSupportedLight)
			resetLighting();
		ms_lightAmbient[0] = std::min(ms_lightAmbient[0], 1.0f);
		ms_lightAmbient[1] = std::min(ms_lightAmbient[1], 1.0f);
		ms_lightAmbient[2] = std::min(ms_lightAmbient[2], 1.0f);
	}

	bool verify() { return loadVulkanDll(); }

	bool install(Gl_install *gl_install)
	{
		ms_window = gl_install->window;
		ms_width = gl_install->width;
		ms_height = gl_install->height;
		ms_windowed = gl_install->windowed;
		ms_engineOwnsWindow = gl_install->engineOwnsWindow;
		ms_borderlessWindow = gl_install->borderlessWindow;
		ms_windowX = gl_install->windowX;
		ms_windowY = gl_install->windowY;
		ms_clearColor = 0xff0b1d2a;
		ms_viewportX = 0;
		ms_viewportY = 0;
		ms_viewportWidth = ms_width;
		ms_viewportHeight = ms_height;
		ms_viewportMinZ = CONST_REAL(0);
		ms_viewportMaxZ = CONST_REAL(1);
		ms_scissorEnabled = false;
		ms_scissorX = 0;
		ms_scissorY = 0;
		ms_scissorWidth = ms_width;
		ms_scissorHeight = ms_height;
		setIdentity(ms_objectToWorldMatrix);
		setIdentity(ms_worldToCameraMatrix);
		setIdentity(ms_projectionMatrix);
		ms_worldTrianglesAttempted = 0;
		ms_worldTrianglesValid = 0;
		ms_worldTrianglePixels = 0;
		ms_worldTrianglesOffscreen = 0;
		ms_perfTextureUploadsThisPresent = 0;
		ms_perfTextureUploadsTotal = 0;
		ms_perfTextureUploadBytesThisPresent = 0;
		ms_perfTextureUploadBytesTotal = 0;
		ms_lastRealTextureBatches = 0;
		ms_lastPendingTextureBatches = 0;
		ms_lastMissingTextureBatches = 0;
		ms_depthEnabled = true;
		ms_depthWriteEnabled = true;
		ms_cullMode = GCM_counterClockwise;
		ms_alphaBlendEnabled = true;
		ms_alphaTestEnabled = false;
		ms_alphaTestCompare = ShaderImplementation::Pass::C_Always;
		ms_alphaTestReference = 0;
		ms_shaderAlphaBlendEnabled = true;
		ms_shaderAlphaBlendOperation = ShaderImplementation::Pass::BO_Add;
		ms_shaderAlphaBlendSource = ShaderImplementation::Pass::B_SourceAlpha;
		ms_shaderAlphaBlendDestination = ShaderImplementation::Pass::B_InverseSourceAlpha;
		ms_shaderColorWriteMask = 0x0f;
		ms_shaderAlphaTestEnabled = false;
		ms_shaderAlphaTestCompare = ShaderImplementation::Pass::C_Always;
		ms_shaderAlphaTestReference = 0;
		ms_activeTextureStage = 0;
		ms_activeStaticShaderPass = 0;
		ms_activeTextureFactorValid = false;
		ms_activeTextureFactor[0] = 1.0f;
		ms_activeTextureFactor[1] = 1.0f;
		ms_activeTextureFactor[2] = 1.0f;
		ms_activeTextureFactor[3] = 1.0f;
		ms_activeTextureFactor2[0] = 0.0f;
		ms_activeTextureFactor2[1] = 0.0f;
		ms_activeTextureFactor2[2] = 0.0f;
		ms_activeTextureFactor2[3] = 1.0f;
		ms_activeLightingEnabled = false;
		ms_activeLightingColorVertex = false;
		ms_activeLightingSpecularEnabled = false;
		ms_activeLightingAmbientColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeLightingDiffuseColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeLightingSpecularColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeLightingEmissiveColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeClampSampler = false;
		ms_activeTextureCube = false;
		ms_obeysLightScale = false;
		ms_activeFullAmbient = false;
		resetLighting();
		ms_lightSetCount = 0;
		ms_lightLogCount = 0;
		ms_activeMaterialColorValid = false;
		ms_activeMaterialAmbientColor[0] = 1.0f;
		ms_activeMaterialAmbientColor[1] = 1.0f;
		ms_activeMaterialAmbientColor[2] = 1.0f;
		ms_activeMaterialAmbientColor[3] = 1.0f;
		ms_activeMaterialColor[0] = 1.0f;
		ms_activeMaterialColor[1] = 1.0f;
		ms_activeMaterialColor[2] = 1.0f;
		ms_activeMaterialColor[3] = 1.0f;
		ms_activeMaterialEmissiveColor[0] = 0.0f;
		ms_activeMaterialEmissiveColor[1] = 0.0f;
		ms_activeMaterialEmissiveColor[2] = 0.0f;
		ms_activeMaterialEmissiveColor[3] = 0.0f;
		ms_activeMaterialSpecularColor[0] = 0.0f;
		ms_activeMaterialSpecularColor[1] = 0.0f;
		ms_activeMaterialSpecularColor[2] = 0.0f;
		ms_activeMaterialSpecularColor[3] = 0.0f;
		ms_activeMaterialSpecularPower = 0.0f;
		ms_fogEnabled = false;
		ms_fogDensity = 0.0f;
		ms_fogColor[0] = 0.0f;
		ms_fogColor[1] = 0.0f;
		ms_fogColor[2] = 0.0f;
		ms_fogColor[3] = 1.0f;
		ms_fogColorPacked = 0x00000000;
		ms_activeFogColor[0] = ms_fogColor[0];
		ms_activeFogColor[1] = ms_fogColor[1];
		ms_activeFogColor[2] = ms_fogColor[2];
		ms_activeFogColor[3] = ms_fogColor[3];
		ms_activeFogColorPacked = ms_fogColorPacked;
		ms_pointSize = 1.0f;
		ms_pointSizeMin = 1.0f;
		ms_pointSizeMax = 64.0f;
		ms_pointSpriteEnable = false;
		for (int i = 0; i < 8; ++i)
			resetTextureTransform(i);

		char buffer[256];
		_snprintf(buffer, sizeof(buffer) - 1, "install begin hwnd=0x%p size=%dx%d windowed=%d engineOwns=%d borderless=%d pos=%d,%d", ms_window, ms_width, ms_height, ms_windowed ? 1 : 0, ms_engineOwnsWindow ? 1 : 0, ms_borderlessWindow ? 1 : 0, ms_windowX, ms_windowY);
		buffer[sizeof(buffer) - 1] = 0;
		logLine(buffer);

		if (!loadVulkanDll() || !createInstanceAndSurface() || !createDevice())
		{
			logLine("install failed before swapchain");
			destroyVulkan();
			return false;
		}

		vkGetDeviceQueueLocal(ms_device, ms_queueFamilyIndex, 0, &ms_queue);
		if (!createSwapchain() || !createSynchronization())
		{
			logLine("install failed during swapchain/sync");
			destroyVulkan();
			return false;
		}

		ms_width = static_cast<int>(ms_swapchainExtent.width);
		ms_height = static_cast<int>(ms_swapchainExtent.height);
		updateWindowSettings();
		gl_install->width = ms_width;
		gl_install->height = ms_height;
		gl_install->windowed = true;
		ms_installed = true;
		logLine("installed flat Vulkan clear-present backend");
		return true;
	}

	void remove()
	{
		ms_installed = false;
		destroyVulkan();
		delete ms_vertexBufferDescriptors;
		ms_vertexBufferDescriptors = 0;
		delete ms_staticVertexBufferDataMap;
		ms_staticVertexBufferDataMap = 0;
		delete ms_dynamicVertexBufferDataMap;
		ms_dynamicVertexBufferDataMap = 0;
		delete ms_staticIndexBufferDataMap;
		ms_staticIndexBufferDataMap = 0;
		ms_lastDynamicVertexBufferData = 0;
		ms_lastDynamicIndexBufferData = 0;
		ms_activeVertexData = 0;
		ms_activeVertexDescriptor = 0;
		ms_activeVertexFormatFlags = 0;
		ms_activeVertexStride = 0;
		ms_activeVertexCount = 0;
		ms_activeVertexVector = false;
		ms_activeVertexStreamCount = 0;
		for (int stream = 0; stream < 2; ++stream)
		{
			ms_activeVertexStreamData[stream] = 0;
			ms_activeVertexStreamStrides[stream] = 0;
			ms_activeVertexStreamDescriptors[stream] = 0;
			ms_activeVertexStreamFormatFlags[stream] = 0;
			ms_activeVertexStreamCounts[stream] = 0;
		}
		ms_activeIndexData = 0;
		ms_activeIndexCount = 0;
		if (ms_globalTextureMap)
		{
			for (GlobalTextureMap::iterator iter = ms_globalTextureMap->begin(); iter != ms_globalTextureMap->end(); ++iter)
			{
				if (iter->second)
					iter->second->release();
			}
			delete ms_globalTextureMap;
			ms_globalTextureMap = 0;
		}
		ms_activeTextureData = 0;
		ms_activeSecondaryTextureData = 0;
		ms_activeTextureTag = 0;
		ms_activeSecondaryTextureTag = 0;
		ms_activeStaticShaderName[0] = 0;
		ms_activeStaticShaderNamePtr = 0;
		ms_activeTextureCoordinateSet = 0;
		ms_activeSecondaryTextureCoordinateSet = 0;
		ms_activeTextureStage = 0;
		ms_activeStaticShaderPass = 0;
		ms_activeTextureFactorValid = false;
		ms_activeTextureFactor[0] = 1.0f;
		ms_activeTextureFactor[1] = 1.0f;
		ms_activeTextureFactor[2] = 1.0f;
		ms_activeTextureFactor[3] = 1.0f;
		ms_activeTextureFactor2[0] = 0.0f;
		ms_activeTextureFactor2[1] = 0.0f;
		ms_activeTextureFactor2[2] = 0.0f;
		ms_activeTextureFactor2[3] = 1.0f;
		ms_activeLightingEnabled = false;
		ms_activeLightingColorVertex = false;
		ms_activeLightingSpecularEnabled = false;
		ms_activeLightingAmbientColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeLightingDiffuseColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeLightingSpecularColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeLightingEmissiveColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeClampSampler = false;
		ms_activeTextureCube = false;
		ms_obeysLightScale = false;
		ms_activeFullAmbient = false;
		resetLighting();
		ms_activeMaterialColorValid = false;
		ms_activeMaterialAmbientColor[0] = 1.0f;
		ms_activeMaterialAmbientColor[1] = 1.0f;
		ms_activeMaterialAmbientColor[2] = 1.0f;
		ms_activeMaterialAmbientColor[3] = 1.0f;
		ms_activeMaterialColor[0] = 1.0f;
		ms_activeMaterialColor[1] = 1.0f;
		ms_activeMaterialColor[2] = 1.0f;
		ms_activeMaterialColor[3] = 1.0f;
		ms_activeMaterialEmissiveColor[0] = 0.0f;
		ms_activeMaterialEmissiveColor[1] = 0.0f;
		ms_activeMaterialEmissiveColor[2] = 0.0f;
		ms_activeMaterialEmissiveColor[3] = 0.0f;
		ms_activeMaterialSpecularColor[0] = 0.0f;
		ms_activeMaterialSpecularColor[1] = 0.0f;
		ms_activeMaterialSpecularColor[2] = 0.0f;
		ms_activeMaterialSpecularColor[3] = 0.0f;
		ms_activeMaterialSpecularPower = 0.0f;
		ms_fogEnabled = false;
		ms_fogDensity = 0.0f;
		ms_fogColor[0] = 0.0f;
		ms_fogColor[1] = 0.0f;
		ms_fogColor[2] = 0.0f;
		ms_fogColor[3] = 1.0f;
		ms_fogColorPacked = 0x00000000;
		ms_activeFogColor[0] = ms_fogColor[0];
		ms_activeFogColor[1] = ms_fogColor[1];
		ms_activeFogColor[2] = ms_fogColor[2];
		ms_activeFogColor[3] = ms_fogColor[3];
		ms_activeFogColorPacked = ms_fogColorPacked;
		for (int i = 0; i < 8; ++i)
			resetTextureTransform(i);
	}

	uint32 alphaBlend(uint32 dst, uint32 src)
	{
		uint32 const srcA = (src >> 24) & 0xff;
		if (srcA == 0)
			return dst;
		if (srcA == 0xff)
			return src;

		uint32 const invA = 255 - srcA;
		uint32 const srcR = (src >> 16) & 0xff;
		uint32 const srcG = (src >> 8) & 0xff;
		uint32 const srcB = src & 0xff;
		uint32 const dstR = (dst >> 16) & 0xff;
		uint32 const dstG = (dst >> 8) & 0xff;
		uint32 const dstB = dst & 0xff;
		uint32 const outR = (srcR * srcA + dstR * invA) / 255;
		uint32 const outG = (srcG * srcA + dstG * invA) / 255;
		uint32 const outB = (srcB * srcA + dstB * invA) / 255;
		return 0xff000000 | (outR << 16) | (outG << 8) | outB;
	}

	bool alphaTestPassWith(uint32 color, bool enabled, ShaderImplementation::Pass::Compare compare, uint8 referenceValue)
	{
		if (!enabled)
			return true;

		uint32 const alpha = (color >> 24) & 0xff;
		uint32 const reference = referenceValue;
		switch (compare)
		{
		case ShaderImplementation::Pass::C_Never:
			return false;
		case ShaderImplementation::Pass::C_Less:
			return alpha < reference;
		case ShaderImplementation::Pass::C_Equal:
			return alpha == reference;
		case ShaderImplementation::Pass::C_LessOrEqual:
			return alpha <= reference;
		case ShaderImplementation::Pass::C_Greater:
			return alpha > reference;
		case ShaderImplementation::Pass::C_GreaterOrEqual:
			return alpha != reference;
		case ShaderImplementation::Pass::C_NotEqual:
			return alpha >= reference;
		case ShaderImplementation::Pass::C_Always:
		default:
			return true;
		}
	}

	bool alphaTestPass(uint32 color)
	{
		return alphaTestPassWith(color, ms_alphaTestEnabled, ms_alphaTestCompare, ms_alphaTestReference);
	}

	uint32 blendPixel(uint32 dst, uint32 src)
	{
		if (!alphaTestPass(src))
			return dst;
		if (ms_alphaBlendEnabled)
			return alphaBlend(dst, src);
		return 0xff000000 | (src & 0x00ffffff);
	}

	uint32 modulateColor(uint32 textureColor, uint32 vertexColor)
	{
		uint32 const ta = (textureColor >> 24) & 0xff;
		uint32 const tr = (textureColor >> 16) & 0xff;
		uint32 const tg = (textureColor >> 8) & 0xff;
		uint32 const tb = textureColor & 0xff;
		uint32 const va = (vertexColor >> 24) & 0xff;
		uint32 const vr = (vertexColor >> 16) & 0xff;
		uint32 const vg = (vertexColor >> 8) & 0xff;
		uint32 const vb = vertexColor & 0xff;
		uint32 const a = (ta * va) / 255;
		uint32 const r = (tr * vr) / 255;
		uint32 const g = (tg * vg) / 255;
		uint32 const b = (tb * vb) / 255;
		return (a << 24) | (r << 16) | (g << 8) | b;
	}

	uint32 scaleColorRgb(uint32 color, float scale)
	{
		uint32 const a = (color >> 24) & 0xff;
		uint32 const r = std::max(0, std::min(255, static_cast<int>(static_cast<float>((color >> 16) & 0xff) * scale + 0.5f)));
		uint32 const g = std::max(0, std::min(255, static_cast<int>(static_cast<float>((color >> 8) & 0xff) * scale + 0.5f)));
		uint32 const b = std::max(0, std::min(255, static_cast<int>(static_cast<float>(color & 0xff) * scale + 0.5f)));
		return (a << 24) | (r << 16) | (g << 8) | b;
	}

	uint32 tintColorRgb(uint32 color, float const *rgb)
	{
		uint32 const a = (color >> 24) & 0xff;
		uint32 const r = std::max(0, std::min(255, static_cast<int>(static_cast<float>((color >> 16) & 0xff) * rgb[0] + 0.5f)));
		uint32 const g = std::max(0, std::min(255, static_cast<int>(static_cast<float>((color >> 8) & 0xff) * rgb[1] + 0.5f)));
		uint32 const b = std::max(0, std::min(255, static_cast<int>(static_cast<float>(color & 0xff) * rgb[2] + 0.5f)));
		return (a << 24) | (r << 16) | (g << 8) | b;
	}

	uint32 tintColorRgba(uint32 color, float const *rgba)
	{
		uint32 const a = std::max(0, std::min(255, static_cast<int>(static_cast<float>((color >> 24) & 0xff) * rgba[3] + 0.5f)));
		uint32 const r = std::max(0, std::min(255, static_cast<int>(static_cast<float>((color >> 16) & 0xff) * rgba[0] + 0.5f)));
		uint32 const g = std::max(0, std::min(255, static_cast<int>(static_cast<float>((color >> 8) & 0xff) * rgba[1] + 0.5f)));
		uint32 const b = std::max(0, std::min(255, static_cast<int>(static_cast<float>(color & 0xff) * rgba[2] + 0.5f)));
		return (a << 24) | (r << 16) | (g << 8) | b;
	}

	uint32 applyActiveMaterialColor(uint32 color)
	{
		if (!ms_activeMaterialColorValid)
			return color;
		return tintColorRgba(color, ms_activeMaterialColor);
	}

	void setActiveFogColor(ShaderImplementation::Pass::FogMode fogMode)
	{
		switch (fogMode)
		{
		case ShaderImplementation::Pass::FM_Black:
			ms_activeFogColor[0] = 0.0f;
			ms_activeFogColor[1] = 0.0f;
			ms_activeFogColor[2] = 0.0f;
			ms_activeFogColor[3] = 1.0f;
			ms_activeFogColorPacked = 0x00000000;
			break;

		case ShaderImplementation::Pass::FM_White:
			ms_activeFogColor[0] = 1.0f;
			ms_activeFogColor[1] = 1.0f;
			ms_activeFogColor[2] = 1.0f;
			ms_activeFogColor[3] = 1.0f;
			ms_activeFogColorPacked = 0xffffffff;
			break;

		case ShaderImplementation::Pass::FM_Normal:
		default:
			ms_activeFogColor[0] = ms_fogColor[0];
			ms_activeFogColor[1] = ms_fogColor[1];
			ms_activeFogColor[2] = ms_fogColor[2];
			ms_activeFogColor[3] = ms_fogColor[3];
			ms_activeFogColorPacked = ms_fogColorPacked;
			break;
		}
	}

	uint32 applyFogColor(uint32 color, float distance)
	{
		if (!ms_fogEnabled || ms_fogDensity <= 0.0f || distance <= 0.0f)
			return color;

		float const fogControl = ms_fogDensity * ms_fogDensity * 1.4426950408889634f;
		float const fogAmount = std::max(0.0f, std::min(1.0f, 1.0f - expf(-fogControl * distance * distance * 0.6931471805599453f)));
		if (fogAmount <= 0.0f)
			return color;

		float const keepAmount = 1.0f - fogAmount;
		uint32 const a = (color >> 24) & 0xff;
		uint32 const r = std::max(0, std::min(255, static_cast<int>((static_cast<float>((color >> 16) & 0xff) * keepAmount) + (ms_activeFogColor[0] * 255.0f * fogAmount) + 0.5f)));
		uint32 const g = std::max(0, std::min(255, static_cast<int>((static_cast<float>((color >> 8) & 0xff) * keepAmount) + (ms_activeFogColor[1] * 255.0f * fogAmount) + 0.5f)));
		uint32 const b = std::max(0, std::min(255, static_cast<int>((static_cast<float>(color & 0xff) * keepAmount) + (ms_activeFogColor[2] * 255.0f * fogAmount) + 0.5f)));
		return (a << 24) | (r << 16) | (g << 8) | b;
	}

	void applyActiveTextureTransform(float &u, float &v)
	{
		int const stage = ms_activeTextureStage >= 0 && ms_activeTextureStage < 8 ? ms_activeTextureStage : 0;
		if (!ms_textureTransformEnabled[stage])
			return;

		Matrix4x4 const &matrix = ms_textureTransform[stage];
		float const source[4] = { u, v, 0.0f, 1.0f };
		float transformed[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		for (int col = 0; col < 4; ++col)
			for (int row = 0; row < 4; ++row)
				transformed[col] += source[row] * matrix.m[row][col];

		if (ms_textureTransformProjected[stage] && ms_textureTransformDimension[stage] > 1)
		{
			int const denominatorIndex = ms_textureTransformDimension[stage] < 3 ? 1 : (ms_textureTransformDimension[stage] < 4 ? 2 : 3);
			float const denominator = transformed[denominatorIndex];
			if (fabsf(denominator) > 0.00001f)
			{
				transformed[0] /= denominator;
				transformed[1] /= denominator;
			}
		}

		u = transformed[0];
		v = transformed[1];
	}

	void applyTextureTransformForStage(int stage, float &u, float &v)
	{
		stage = stage >= 0 && stage < 8 ? stage : 0;
		if (!ms_textureTransformEnabled[stage])
			return;

		Matrix4x4 const &matrix = ms_textureTransform[stage];
		float const source[4] = { u, v, 0.0f, 1.0f };
		float transformed[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		for (int col = 0; col < 4; ++col)
			for (int row = 0; row < 4; ++row)
				transformed[col] += source[row] * matrix.m[row][col];

		if (ms_textureTransformProjected[stage] && ms_textureTransformDimension[stage] > 1)
		{
			int const denominatorIndex = ms_textureTransformDimension[stage] < 3 ? 1 : (ms_textureTransformDimension[stage] < 4 ? 2 : 3);
			float const denominator = transformed[denominatorIndex];
			if (fabsf(denominator) > 0.00001f)
			{
				transformed[0] /= denominator;
				transformed[1] /= denominator;
			}
		}

		u = transformed[0];
		v = transformed[1];
	}

	uint32 applyActorTextureFactor(uint32 color, uint32 sampled)
	{
		if (!ms_activeTextureFactorValid)
			return color;

		float const mask = static_cast<float>((sampled >> 24) & 0xff) / 255.0f;
		float rgb[3];
		rgb[0] = ms_activeTextureFactor[0] + ((ms_activeTextureFactor2[0] - ms_activeTextureFactor[0]) * mask);
		rgb[1] = ms_activeTextureFactor[1] + ((ms_activeTextureFactor2[1] - ms_activeTextureFactor[1]) * mask);
		rgb[2] = ms_activeTextureFactor[2] + ((ms_activeTextureFactor2[2] - ms_activeTextureFactor[2]) * mask);
		return tintColorRgb(color, rgb);
	}

	uint32 makeTextureFactorColor()
	{
		uint32 const r = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(ms_activeTextureFactor[0] * 255.0f + 0.5f))));
		uint32 const g = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(ms_activeTextureFactor[1] * 255.0f + 0.5f))));
		uint32 const b = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(ms_activeTextureFactor[2] * 255.0f + 0.5f))));
		return 0xff000000 | (r << 16) | (g << 8) | b;
	}

	bool activeTextureFactorModulateStage()
	{
		if (!ms_activeTextureData || !ms_activeTextureFactorValid || ms_activeStageTextureCount <= 0)
			return false;
		if (ms_activeStageColorOperation[0] != 3)
			return false;
		return (ms_activeStageColorArgument1[0] == 4 && ms_activeStageColorArgument2[0] == 5) ||
			(ms_activeStageColorArgument1[0] == 5 && ms_activeStageColorArgument2[0] == 4);
	}

	bool stringContainsNoCase(char const *text, char const *needle);

	bool activeActorBlackLightingPass()
	{
		return ms_activeLightingSpecularEnabled &&
			ms_activeTextureFactorValid &&
			ms_activeTextureFactor[0] <= 0.001f &&
			ms_activeTextureFactor[1] <= 0.001f &&
			ms_activeTextureFactor[2] <= 0.001f;
	}

	bool activeTextureLooksActorAuxNormal()
	{
		char const *textureName = ms_activeTextureData ? ms_activeTextureData->getName() : 0;
		return ms_activeTextureTag == TAG(N,R,M,L) ||
			stringContainsNoCase(textureName, "nrml") ||
			stringContainsNoCase(textureName, "normal");
	}

	bool activeActorVisibleAuxPass()
	{
		return gpuActorAuxColorEnabled() &&
			activeActorBlackLightingPass() &&
			activeTextureLooksActorAuxNormal();
	}

	int activeActorGpuSkipReason()
	{
		if (ms_activeStaticShaderNamePtr && strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend"))
			return 0;
		if (activeActorBlackLightingPass() && !activeActorVisibleAuxPass())
			return 3;
		return 0;
	}

	bool activeActorDxtAlphaPass()
	{
		return gpuActorDxtAlphaEnabled() &&
			ms_activeTextureData &&
			ms_activeTextureData->getFormat() == TF_DXT5 &&
			!activeActorBlackLightingPass();
	}

	bool isActorShaderName(char const *shaderName)
	{
		return shaderName &&
			(strstr(shaderName, "hum_") ||
			 strstr(shaderName, "human") ||
			 strstr(shaderName, "mgn_") ||
			 strstr(shaderName, "creature") ||
			 strstr(shaderName, "dewback") ||
			 strstr(shaderName, "bantha") ||
			 strstr(shaderName, "mount") ||
			 strstr(shaderName, "player") ||
			 strstr(shaderName, "body") ||
			 strstr(shaderName, "face"));
	}

	bool isActorBodyFixedFunction(VertexBufferFormat const &format)
	{
		bool const actorLikeGeometry = ms_activeVertexVector || isActorShaderName(ms_activeStaticShaderNamePtr);
		return actorLikeGeometry &&
			ms_activeLightingEnabled &&
			ms_activePixelProgramMode == 0 &&
			!format.isTransformed() &&
			format.hasNormal() &&
			!format.hasColor0() &&
			!format.hasColor1();
	}

	void intersectScissor(int &minX, int &minY, int &maxX, int &maxY)
	{
		if (!ms_scissorEnabled)
			return;

		int const width = static_cast<int>(ms_swapchainExtent.width);
		int const height = static_cast<int>(ms_swapchainExtent.height);
		int const scissorMinX = std::max(0, std::min(width, ms_scissorX));
		int const scissorMinY = std::max(0, std::min(height, ms_scissorY));
		int const scissorMaxX = std::max(scissorMinX, std::min(width, ms_scissorX + std::max(0, ms_scissorWidth)));
		int const scissorMaxY = std::max(scissorMinY, std::min(height, ms_scissorY + std::max(0, ms_scissorHeight)));
		minX = std::max(minX, scissorMinX);
		minY = std::max(minY, scissorMinY);
		maxX = std::min(maxX, scissorMaxX);
		maxY = std::min(maxY, scissorMaxY);
	}

	void getCurrentScissorRect(bool &enabled, int &x, int &y, int &width, int &height)
	{
		int const targetWidth = ms_swapchainExtent.width ? static_cast<int>(ms_swapchainExtent.width) : std::max(1, ms_width);
		int const targetHeight = ms_swapchainExtent.height ? static_cast<int>(ms_swapchainExtent.height) : std::max(1, ms_height);
		enabled = ms_scissorEnabled;
		if (!enabled)
		{
			x = 0;
			y = 0;
			width = targetWidth;
			height = targetHeight;
			return;
		}

		int const minX = std::max(0, std::min(targetWidth, ms_scissorX));
		int const minY = std::max(0, std::min(targetHeight, ms_scissorY));
		int const maxX = std::max(minX, std::min(targetWidth, ms_scissorX + std::max(0, ms_scissorWidth)));
		int const maxY = std::max(minY, std::min(targetHeight, ms_scissorY + std::max(0, ms_scissorHeight)));
		x = minX;
		y = minY;
		width = maxX - minX;
		height = maxY - minY;
	}

	void ensureFramePixels()
	{
		size_t const pixelCount = static_cast<size_t>(ms_swapchainExtent.width) * static_cast<size_t>(ms_swapchainExtent.height);
		if (pixelCount && ms_framePixels.size() != pixelCount)
			ms_framePixels.assign(pixelCount, ms_clearColor);
		if (pixelCount && ms_depthPixels.size() != pixelCount)
			ms_depthPixels.assign(pixelCount, 1.0f);
	}

	float readFloat(byte const *vertex, int offset, float fallback)
	{
		if (offset < 0)
			return fallback;
		float value = fallback;
		memcpy(&value, vertex + offset, sizeof(value));
		return value;
	}

	uint32 readColor(byte const *vertex, int offset)
	{
		if (offset < 0)
			return 0xffffffff;
		uint32 value = 0xffffffff;
		memcpy(&value, vertex + offset, sizeof(value));
		return value;
	}

	uint32 normalizeLegacyVertexColor(uint32 color)
	{
		if (((color >> 24) & 0xff) == 0 && (color & 0x00ffffff) != 0)
			return color | 0xff000000;
		return color;
	}

	float readFloat(byte const *vertex, int offset, float fallback);

	float clamp01(float value)
	{
		return std::max(0.0f, std::min(1.0f, value));
	}

	bool textureAddressUsesClamp(StaticShaderTemplate::TextureAddress address)
	{
		return address == StaticShaderTemplate::TA_clamp ||
			address == StaticShaderTemplate::TA_border ||
			address == StaticShaderTemplate::TA_mirrorOnce;
	}

	void normalize3(float &x, float &y, float &z)
	{
		float const length = sqrtf(x * x + y * y + z * z);
		if (length <= 0.00001f)
		{
			x = 0.0f;
			y = 0.0f;
			z = 1.0f;
			return;
		}
		float const invLength = 1.0f / length;
		x *= invLength;
		y *= invLength;
		z *= invLength;
	}

	float computeActorBodyLightScale(byte const *vertex)
	{
		if (!ms_activeLightingEnabled || !ms_activeVertexDescriptor || ms_activeVertexDescriptor->offsetNormal < 0)
			return 1.0f;

		float nx = readFloat(vertex, ms_activeVertexDescriptor->offsetNormal, 0.0f);
		float ny = readFloat(vertex, ms_activeVertexDescriptor->offsetNormal + static_cast<int>(sizeof(float)), 0.0f);
		float nz = readFloat(vertex, ms_activeVertexDescriptor->offsetNormal + static_cast<int>(sizeof(float)) * 2, 1.0f);
		float worldX = nx * ms_objectToWorldMatrix.m[0][0] + ny * ms_objectToWorldMatrix.m[1][0] + nz * ms_objectToWorldMatrix.m[2][0];
		float worldY = nx * ms_objectToWorldMatrix.m[0][1] + ny * ms_objectToWorldMatrix.m[1][1] + nz * ms_objectToWorldMatrix.m[2][1];
		float worldZ = nx * ms_objectToWorldMatrix.m[0][2] + ny * ms_objectToWorldMatrix.m[1][2] + nz * ms_objectToWorldMatrix.m[2][2];
		normalize3(worldX, worldY, worldZ);

		float mobileBodySunShade = 0.0f;
		for (int i = 0; i < 3; ++i)
		{
			if (ms_parallelLightDirection[i][3] <= 0.5f)
				continue;
			float lightX = -ms_parallelLightDirection[i][0];
			float lightY = -ms_parallelLightDirection[i][1];
			float lightZ = -ms_parallelLightDirection[i][2];
			normalize3(lightX, lightY, lightZ);
			mobileBodySunShade = std::max(mobileBodySunShade, clamp01((worldX * lightX) + (worldY * lightY) + (worldZ * lightZ)));
		}
		float const directionalScale = 0.72f * (0.55f + (0.45f * mobileBodySunShade));
		return clamp01(directionalScale);
	}

	void packedColorToFloats(uint32 color, float (&rgba)[4])
	{
		rgba[0] = static_cast<float>((color >> 16) & 0xff) / 255.0f;
		rgba[1] = static_cast<float>((color >> 8) & 0xff) / 255.0f;
		rgba[2] = static_cast<float>(color & 0xff) / 255.0f;
		rgba[3] = static_cast<float>((color >> 24) & 0xff) / 255.0f;
	}

	void copyFloatColor(float (&dst)[4], float const *src)
	{
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		dst[3] = src[3];
	}

	void selectMaterialSourceColor(
		ShaderImplementation::Pass::FixedFunctionPipeline::MaterialSource source,
		float const *material,
		float const (&sourceColor0)[4],
		bool hasColor0,
		float const (&sourceColor1)[4],
		bool hasColor1,
		float (&out)[4])
	{
		if (ms_activeLightingColorVertex && source == ShaderImplementation::Pass::FixedFunctionPipeline::MS_VertexColor0 && hasColor0)
		{
			copyFloatColor(out, sourceColor0);
			return;
		}
		if (ms_activeLightingColorVertex && source == ShaderImplementation::Pass::FixedFunctionPipeline::MS_VertexColor1 && hasColor1)
		{
			copyFloatColor(out, sourceColor1);
			return;
		}
		copyFloatColor(out, material);
	}

	uint32 applyFixedFunctionVertexLighting(byte const *vertex, uint32 color, float *specularOut, bool mobileBodyPass)
	{
		if (specularOut)
		{
			specularOut[0] = 0.0f;
			specularOut[1] = 0.0f;
			specularOut[2] = 0.0f;
		}
		if (!ms_activeLightingEnabled || !vertex || !ms_activeVertexDescriptor || ms_activeVertexDescriptor->offsetNormal < 0)
			return color;

		bool const hasColor0 = ms_activeVertexDescriptor->offsetColor0 >= 0;
		bool const hasColor1 = ms_activeVertexDescriptor->offsetColor1 >= 0;
		float sourceColor0[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		float sourceColor1[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		if (hasColor0)
			packedColorToFloats(normalizeLegacyVertexColor(readColor(vertex, ms_activeVertexDescriptor->offsetColor0)), sourceColor0);
		if (hasColor1)
			packedColorToFloats(normalizeLegacyVertexColor(readColor(vertex, ms_activeVertexDescriptor->offsetColor1)), sourceColor1);

		float diffuseSource[4];
		float ambientSource[4];
		float emissiveSource[4];
		float specularSource[4];
		if (mobileBodyPass)
		{
			diffuseSource[0] = diffuseSource[1] = diffuseSource[2] = diffuseSource[3] = 1.0f;
			ambientSource[0] = ambientSource[1] = ambientSource[2] = ambientSource[3] = 1.0f;
			emissiveSource[0] = emissiveSource[1] = emissiveSource[2] = emissiveSource[3] = 0.0f;
			specularSource[0] = specularSource[1] = specularSource[2] = specularSource[3] = 1.0f;
		}
		else
		{
			selectMaterialSourceColor(ms_activeLightingDiffuseColorSource, ms_activeMaterialColor, sourceColor0, hasColor0, sourceColor1, hasColor1, diffuseSource);
			selectMaterialSourceColor(ms_activeLightingAmbientColorSource, ms_activeMaterialAmbientColor, sourceColor0, hasColor0, sourceColor1, hasColor1, ambientSource);
			selectMaterialSourceColor(ms_activeLightingEmissiveColorSource, ms_activeMaterialEmissiveColor, sourceColor0, hasColor0, sourceColor1, hasColor1, emissiveSource);
			selectMaterialSourceColor(ms_activeLightingSpecularColorSource, ms_activeMaterialSpecularColor, sourceColor0, hasColor0, sourceColor1, hasColor1, specularSource);
		}

		float nx = readFloat(vertex, ms_activeVertexDescriptor->offsetNormal, 0.0f);
		float ny = readFloat(vertex, ms_activeVertexDescriptor->offsetNormal + static_cast<int>(sizeof(float)), 0.0f);
		float nz = readFloat(vertex, ms_activeVertexDescriptor->offsetNormal + static_cast<int>(sizeof(float)) * 2, 1.0f);
		float worldX = nx * ms_objectToWorldMatrix.m[0][0] + ny * ms_objectToWorldMatrix.m[1][0] + nz * ms_objectToWorldMatrix.m[2][0];
		float worldY = nx * ms_objectToWorldMatrix.m[0][1] + ny * ms_objectToWorldMatrix.m[1][1] + nz * ms_objectToWorldMatrix.m[2][1];
		float worldZ = nx * ms_objectToWorldMatrix.m[0][2] + ny * ms_objectToWorldMatrix.m[1][2] + nz * ms_objectToWorldMatrix.m[2][2];
		normalize3(worldX, worldY, worldZ);

		float ambientRgb[3] = { ms_lightAmbient[0], ms_lightAmbient[1], ms_lightAmbient[2] };
		if (ms_activeFullAmbient)
		{
			ambientRgb[0] = clamp01(ambientRgb[0] + 1.0f);
			ambientRgb[1] = clamp01(ambientRgb[1] + 1.0f);
			ambientRgb[2] = clamp01(ambientRgb[2] + 1.0f);
		}

		float parallelRgb[3] = { 0.0f, 0.0f, 0.0f };
		float specularRgb[3] = { 0.0f, 0.0f, 0.0f };
		int enabledParallelLights = 0;
		Matrix4x4 const objectToCamera = multiply(ms_objectToWorldMatrix, ms_worldToCameraMatrix);
		float normalCameraX = nx * objectToCamera.m[0][0] + ny * objectToCamera.m[1][0] + nz * objectToCamera.m[2][0];
		float normalCameraY = nx * objectToCamera.m[0][1] + ny * objectToCamera.m[1][1] + nz * objectToCamera.m[2][1];
		float normalCameraZ = nx * objectToCamera.m[0][2] + ny * objectToCamera.m[1][2] + nz * objectToCamera.m[2][2];
		normalize3(normalCameraX, normalCameraY, normalCameraZ);
		float px = readFloat(vertex, ms_activeVertexDescriptor->offsetPosition, 0.0f);
		float py = readFloat(vertex, ms_activeVertexDescriptor->offsetPosition + static_cast<int>(sizeof(float)), 0.0f);
		float pz = readFloat(vertex, ms_activeVertexDescriptor->offsetPosition + static_cast<int>(sizeof(float)) * 2, 0.0f);
		float cameraX = px * objectToCamera.m[0][0] + py * objectToCamera.m[1][0] + pz * objectToCamera.m[2][0] + objectToCamera.m[3][0];
		float cameraY = px * objectToCamera.m[0][1] + py * objectToCamera.m[1][1] + pz * objectToCamera.m[2][1] + objectToCamera.m[3][1];
		float cameraZ = px * objectToCamera.m[0][2] + py * objectToCamera.m[1][2] + pz * objectToCamera.m[2][2] + objectToCamera.m[3][2];
		float viewX = -cameraX;
		float viewY = -cameraY;
		float viewZ = -cameraZ;
		normalize3(viewX, viewY, viewZ);

		for (int i = 0; i < 3; ++i)
		{
			if (ms_parallelLightDirection[i][3] <= 0.5f)
				continue;
			++enabledParallelLights;
			float lightX = -ms_parallelLightDirection[i][0];
			float lightY = -ms_parallelLightDirection[i][1];
			float lightZ = -ms_parallelLightDirection[i][2];
			normalize3(lightX, lightY, lightZ);
			float const diffuse = clamp01((worldX * lightX) + (worldY * lightY) + (worldZ * lightZ));
			parallelRgb[0] += ms_parallelLightDiffuse[i][0] * diffuse;
			parallelRgb[1] += ms_parallelLightDiffuse[i][1] * diffuse;
			parallelRgb[2] += ms_parallelLightDiffuse[i][2] * diffuse;
			if (ms_activeLightingSpecularEnabled && diffuse > 0.0f && ms_activeMaterialSpecularPower > 0.0f)
			{
				float lightCameraX = lightX * ms_worldToCameraMatrix.m[0][0] + lightY * ms_worldToCameraMatrix.m[1][0] + lightZ * ms_worldToCameraMatrix.m[2][0];
				float lightCameraY = lightX * ms_worldToCameraMatrix.m[0][1] + lightY * ms_worldToCameraMatrix.m[1][1] + lightZ * ms_worldToCameraMatrix.m[2][1];
				float lightCameraZ = lightX * ms_worldToCameraMatrix.m[0][2] + lightY * ms_worldToCameraMatrix.m[1][2] + lightZ * ms_worldToCameraMatrix.m[2][2];
				normalize3(lightCameraX, lightCameraY, lightCameraZ);
				float halfX = lightCameraX + viewX;
				float halfY = lightCameraY + viewY;
				float halfZ = lightCameraZ + viewZ;
				normalize3(halfX, halfY, halfZ);
				float const specular = powf(clamp01((normalCameraX * halfX) + (normalCameraY * halfY) + (normalCameraZ * halfZ)), ms_activeMaterialSpecularPower);
				specularRgb[0] += ms_parallelLightSpecular[i][0] * specularSource[0] * specular;
				specularRgb[1] += ms_parallelLightSpecular[i][1] * specularSource[1] * specular;
				specularRgb[2] += ms_parallelLightSpecular[i][2] * specularSource[2] * specular;
			}
		}
		if (enabledParallelLights == 0 && ms_lightDirectionalEnabled)
		{
			float lightX = -ms_lightDirection[0];
			float lightY = -ms_lightDirection[1];
			float lightZ = -ms_lightDirection[2];
			normalize3(lightX, lightY, lightZ);
			float const diffuse = clamp01((worldX * lightX) + (worldY * lightY) + (worldZ * lightZ));
			parallelRgb[0] = ms_lightDiffuse[0] * diffuse;
			parallelRgb[1] = ms_lightDiffuse[1] * diffuse;
			parallelRgb[2] = ms_lightDiffuse[2] * diffuse;
		}

		float litRgb[3];
		for (int i = 0; i < 3; ++i)
		{
			if (specularOut)
			{
				specularOut[i] = clamp01(specularRgb[i]);
				litRgb[i] = clamp01(emissiveSource[i] + (ambientSource[i] * ambientRgb[i]) + (diffuseSource[i] * parallelRgb[i]));
			}
			else
			{
				litRgb[i] = clamp01(emissiveSource[i] + (ambientSource[i] * ambientRgb[i]) + (diffuseSource[i] * parallelRgb[i]) + specularRgb[i]);
			}
		}

		uint32 const a = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(diffuseSource[3] * 255.0f + 0.5f))));
		uint32 const r = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(litRgb[0] * 255.0f + 0.5f))));
		uint32 const g = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(litRgb[1] * 255.0f + 0.5f))));
		uint32 const b = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(litRgb[2] * 255.0f + 0.5f))));
		return (a << 24) | (r << 16) | (g << 8) | b;
	}

	uint32 applyFixedFunctionVertexLighting(byte const *vertex, uint32 color, float *specularOut)
	{
		return applyFixedFunctionVertexLighting(vertex, color, specularOut, false);
	}

	uint32 applyFixedFunctionVertexLighting(byte const *vertex, uint32 color)
	{
		return applyFixedFunctionVertexLighting(vertex, color, 0, false);
	}

	uint32 makeActorTextureFactorColor(byte const *vertex)
	{
		return scaleColorRgb(makeTextureFactorColor(), computeActorBodyLightScale(vertex));
	}

	uint32 makeActorNeutralOverlayColor(uint32 color)
	{
		uint32 const r = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(static_cast<float>((color >> 16) & 0xff) * 0.45f + 0.5f))));
		uint32 const g = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(static_cast<float>((color >> 8) & 0xff) * 0.45f + 0.5f))));
		uint32 const b = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(static_cast<float>(color & 0xff) * 0.45f + 0.5f))));
		return 0xff000000 | (r << 16) | (g << 8) | b;
	}

	void fillRect(int minX, int minY, int maxX, int maxY, uint32 color)
	{
		ensureFramePixels();
		if (ms_framePixels.empty())
			return;

		int const width = static_cast<int>(ms_swapchainExtent.width);
		int const height = static_cast<int>(ms_swapchainExtent.height);
		minX = std::max(0, std::min(width, minX));
		maxX = std::max(0, std::min(width, maxX));
		minY = std::max(0, std::min(height, minY));
		maxY = std::max(0, std::min(height, maxY));
		intersectScissor(minX, minY, maxX, maxY);
		if (maxX <= minX || maxY <= minY)
			return;

		for (int y = minY; y < maxY; ++y)
		{
			uint32 *row = &ms_framePixels[static_cast<size_t>(y) * static_cast<size_t>(width)];
			for (int x = minX; x < maxX; ++x)
				row[x] = blendPixel(row[x], color);
		}
	}

	struct QuadCorner
	{
		float x;
		float y;
		float z;
		float u;
		float v;
		uint32 color;
	};

	float lerp(float a, float b, float t)
	{
		return a + ((b - a) * t);
	}

	float edgeFunction(QuadCorner const &a, QuadCorner const &b, float x, float y)
	{
		return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
	}

	uint32 interpolateColor(QuadCorner const &a, QuadCorner const &b, QuadCorner const &c, float wa, float wb, float wc)
	{
		float const aa = static_cast<float>((a.color >> 24) & 0xff) * wa + static_cast<float>((b.color >> 24) & 0xff) * wb + static_cast<float>((c.color >> 24) & 0xff) * wc;
		float const ar = static_cast<float>((a.color >> 16) & 0xff) * wa + static_cast<float>((b.color >> 16) & 0xff) * wb + static_cast<float>((c.color >> 16) & 0xff) * wc;
		float const ag = static_cast<float>((a.color >> 8) & 0xff) * wa + static_cast<float>((b.color >> 8) & 0xff) * wb + static_cast<float>((c.color >> 8) & 0xff) * wc;
		float const ab = static_cast<float>(a.color & 0xff) * wa + static_cast<float>(b.color & 0xff) * wb + static_cast<float>(c.color & 0xff) * wc;
		uint32 const ia = std::max(0, std::min(255, static_cast<int>(aa + 0.5f)));
		uint32 const ir = std::max(0, std::min(255, static_cast<int>(ar + 0.5f)));
		uint32 const ig = std::max(0, std::min(255, static_cast<int>(ag + 0.5f)));
		uint32 const ib = std::max(0, std::min(255, static_cast<int>(ab + 0.5f)));
		return (ia << 24) | (ir << 16) | (ig << 8) | ib;
	}

	void fillTexturedRect(int minX, int minY, int maxX, int maxY, float minU, float minV, float maxU, float maxV, uint32 color, VulkanTextureData const &texture)
	{
		ensureFramePixels();
		if (ms_framePixels.empty())
			return;

		int const width = static_cast<int>(ms_swapchainExtent.width);
		int const height = static_cast<int>(ms_swapchainExtent.height);
		int const rectMinX = minX;
		int const rectMinY = minY;
		int const rectMaxX = maxX;
		int const rectMaxY = maxY;
		int drawMinX = std::max(0, std::min(width, minX));
		int drawMaxX = std::max(0, std::min(width, maxX));
		int drawMinY = std::max(0, std::min(height, minY));
		int drawMaxY = std::max(0, std::min(height, maxY));
		intersectScissor(drawMinX, drawMinY, drawMaxX, drawMaxY);
		if (drawMaxX <= drawMinX || drawMaxY <= drawMinY)
			return;

		float const invW = rectMaxX > rectMinX + 1 ? 1.0f / static_cast<float>(rectMaxX - rectMinX - 1) : 0.0f;
		float const invH = rectMaxY > rectMinY + 1 ? 1.0f / static_cast<float>(rectMaxY - rectMinY - 1) : 0.0f;
		for (int y = drawMinY; y < drawMaxY; ++y)
		{
			float const ty = static_cast<float>(y - rectMinY) * invH;
			float const v = minV + ((maxV - minV) * ty);
			uint32 *row = &ms_framePixels[static_cast<size_t>(y) * static_cast<size_t>(width)];
			for (int x = drawMinX; x < drawMaxX; ++x)
			{
				float const tx = static_cast<float>(x - rectMinX) * invW;
				float const u = minU + ((maxU - minU) * tx);
				uint32 const sampled = texture.sample(u, v);
				row[x] = blendPixel(row[x], modulateColor(sampled, color));
			}
		}
	}

	void fillTexturedQuad(QuadCorner const (&corner)[4], VulkanTextureData const &texture)
	{
		ensureFramePixels();
		if (ms_framePixels.empty())
			return;

		struct TriangleRasterizer
		{
			static void draw(QuadCorner const &a, QuadCorner const &b, QuadCorner const &c, VulkanTextureData const &texture)
			{
				float const area = edgeFunction(a, b, c.x, c.y);
				if (fabsf(area) < 0.0001f)
					return;

				int const width = static_cast<int>(ms_swapchainExtent.width);
				int const height = static_cast<int>(ms_swapchainExtent.height);
				int drawMinX = std::max(0, std::min(width, static_cast<int>(floorf(std::min(std::min(a.x, b.x), c.x)))));
				int drawMaxX = std::max(0, std::min(width, static_cast<int>(ceilf(std::max(std::max(a.x, b.x), c.x)))));
				int drawMinY = std::max(0, std::min(height, static_cast<int>(floorf(std::min(std::min(a.y, b.y), c.y)))));
				int drawMaxY = std::max(0, std::min(height, static_cast<int>(ceilf(std::max(std::max(a.y, b.y), c.y)))));
				intersectScissor(drawMinX, drawMinY, drawMaxX, drawMaxY);
				if (drawMaxX <= drawMinX || drawMaxY <= drawMinY)
					return;

				for (int y = drawMinY; y < drawMaxY; ++y)
				{
					uint32 *row = &ms_framePixels[static_cast<size_t>(y) * static_cast<size_t>(width)];
					for (int x = drawMinX; x < drawMaxX; ++x)
					{
						float const px = static_cast<float>(x) + 0.5f;
						float const py = static_cast<float>(y) + 0.5f;
						float const wa = edgeFunction(b, c, px, py) / area;
						float const wb = edgeFunction(c, a, px, py) / area;
						float const wc = edgeFunction(a, b, px, py) / area;
						if (wa < -0.001f || wb < -0.001f || wc < -0.001f)
							continue;

						float const u = (a.u * wa) + (b.u * wb) + (c.u * wc);
						float const v = (a.v * wa) + (b.v * wb) + (c.v * wc);
						uint32 const vertexColor = interpolateColor(a, b, c, wa, wb, wc);
						uint32 const sampled = texture.sample(u, v);
						row[x] = blendPixel(row[x], modulateColor(sampled, vertexColor));
					}
				}
			}
		};

		TriangleRasterizer::draw(corner[0], corner[1], corner[2], texture);
		TriangleRasterizer::draw(corner[0], corner[2], corner[3], texture);
		return;

		float minX = corner[0].x;
		float minY = corner[0].y;
		float maxX = corner[0].x;
		float maxY = corner[0].y;
		for (int i = 1; i < 4; ++i)
		{
			minX = std::min(minX, corner[i].x);
			minY = std::min(minY, corner[i].y);
			maxX = std::max(maxX, corner[i].x);
			maxY = std::max(maxY, corner[i].y);
		}

		int const width = static_cast<int>(ms_swapchainExtent.width);
		int const height = static_cast<int>(ms_swapchainExtent.height);
		int const rectMinX = static_cast<int>(floorf(minX));
		int const rectMinY = static_cast<int>(floorf(minY));
		int const rectMaxX = static_cast<int>(ceilf(maxX));
		int const rectMaxY = static_cast<int>(ceilf(maxY));
		int drawMinX = std::max(0, std::min(width, rectMinX));
		int drawMaxX = std::max(0, std::min(width, rectMaxX));
		int drawMinY = std::max(0, std::min(height, rectMinY));
		int drawMaxY = std::max(0, std::min(height, rectMaxY));
		intersectScissor(drawMinX, drawMinY, drawMaxX, drawMaxY);
		if (drawMaxX <= drawMinX || drawMaxY <= drawMinY)
			return;

		float const centerX = (minX + maxX) * 0.5f;
		float const centerY = (minY + maxY) * 0.5f;
		int topLeft = 0;
		int topRight = 0;
		int bottomLeft = 0;
		int bottomRight = 0;
		for (int i = 0; i < 4; ++i)
		{
			bool const right = corner[i].x >= centerX;
			bool const bottom = corner[i].y >= centerY;
			if (!right && !bottom)
				topLeft = i;
			else if (right && !bottom)
				topRight = i;
			else if (!right && bottom)
				bottomLeft = i;
			else
				bottomRight = i;
		}

		uint32 color = corner[0].color;
		for (int i = 1; i < 4; ++i)
			color = alphaBlend(color, corner[i].color);

		float const invW = rectMaxX > rectMinX ? 1.0f / static_cast<float>(rectMaxX - rectMinX) : 0.0f;
		float const invH = rectMaxY > rectMinY ? 1.0f / static_cast<float>(rectMaxY - rectMinY) : 0.0f;
		for (int y = drawMinY; y < drawMaxY; ++y)
		{
			float const t = std::max(0.0f, std::min(1.0f, (static_cast<float>(y) + 0.5f - static_cast<float>(rectMinY)) * invH));
			float const uLeft = lerp(corner[topLeft].u, corner[bottomLeft].u, t);
			float const uRight = lerp(corner[topRight].u, corner[bottomRight].u, t);
			float const vLeft = lerp(corner[topLeft].v, corner[bottomLeft].v, t);
			float const vRight = lerp(corner[topRight].v, corner[bottomRight].v, t);
			uint32 *row = &ms_framePixels[static_cast<size_t>(y) * static_cast<size_t>(width)];
			for (int x = drawMinX; x < drawMaxX; ++x)
			{
				float const s = std::max(0.0f, std::min(1.0f, (static_cast<float>(x) + 0.5f - static_cast<float>(rectMinX)) * invW));
				float const u = lerp(uLeft, uRight, s);
				float const v = lerp(vLeft, vRight, s);
				uint32 const sampled = texture.sample(u, v);
				row[x] = blendPixel(row[x], modulateColor(sampled, color));
			}
		}
	}

	struct ScreenVertex
	{
		float x;
		float y;
		float z;
		float w;
		float clipX;
		float clipY;
		float clipZ;
		float clipW;
		float objectX;
		float objectY;
		float objectZ;
		float u;
		float v;
		float u2;
		float v2;
		float specularR;
		float specularG;
		float specularB;
		uint32 color;
		bool valid;
	};

	void includeActorGpuBounds(float minX, float minY, float maxX, float maxY)
	{
		ms_actorGpuMinX = std::min(ms_actorGpuMinX, minX);
		ms_actorGpuMinY = std::min(ms_actorGpuMinY, minY);
		ms_actorGpuMaxX = std::max(ms_actorGpuMaxX, maxX);
		ms_actorGpuMaxY = std::max(ms_actorGpuMaxY, maxY);
	}

	float packStageArgumentForShader(int argument, bool complement, bool alphaReplicate)
	{
		if (argument < 0)
			return -1.0f;
		int packed = argument;
		if (alphaReplicate)
			packed += 16;
		if (complement)
			packed += 32;
		return static_cast<float>(packed);
	}

	float packStageArgumentsForShader(int argument0, bool complement0, bool alphaReplicate0, int argument1, bool complement1, bool alphaReplicate1, int argument2, bool complement2, bool alphaReplicate2)
	{
		int const packed0 = static_cast<int>(packStageArgumentForShader(argument0, complement0, alphaReplicate0));
		int const packed1 = static_cast<int>(packStageArgumentForShader(argument1, complement1, alphaReplicate1));
		int const packed2 = static_cast<int>(packStageArgumentForShader(argument2, complement2, alphaReplicate2));
		if (packed0 < 0 && packed1 < 0 && packed2 < 0)
			return -1.0f;
		int const safe0 = std::max(0, packed0);
		int const safe1 = std::max(0, packed1);
		int const safe2 = std::max(0, packed2);
		return static_cast<float>(safe0 | (safe1 << 6) | (safe2 << 12));
	}

	void makeGpuDrawPushConstants(GpuDrawBatch const *batch, GpuDrawPushConstants &constants, float const *actorBaseColorOverride = 0)
	{
		memset(&constants, 0, sizeof(constants));
		constants.auxParams[0] = gpuActorAuxBlendAlpha();
		constants.auxParams[1] = batch && batch->actorAuxiliary ? 1.0f : 0.0f;
		constants.auxParams[2] = batch && batch->alphaTest ? static_cast<float>(batch->alphaTestCompare) : -1.0f;
		constants.auxParams[3] = 0.0f;
		constants.batchColor[0] = 1.0f;
		constants.batchColor[1] = 1.0f;
		constants.batchColor[2] = 1.0f;
		constants.batchColor[3] = 1.0f;
		constants.textureFactor[0] = 1.0f;
		constants.textureFactor[1] = 1.0f;
		constants.textureFactor[2] = 1.0f;
		constants.textureFactor[3] = 1.0f;
		constants.textureFactor2[0] = 0.0f;
		constants.textureFactor2[1] = 0.0f;
		constants.textureFactor2[2] = 0.0f;
		constants.textureFactor2[3] = 1.0f;
		constants.actorBaseColor[0] = 1.0f;
		constants.actorBaseColor[1] = 1.0f;
		constants.actorBaseColor[2] = 1.0f;
		constants.actorBaseColor[3] = 0.0f;
		for (int i = 0; i < 4; ++i)
		{
			constants.stageOps[i] = -1.0f;
			constants.stageArgs01[i] = -1.0f;
			constants.stageArgs23[i] = -1.0f;
		}
		if (!batch)
			return;
		constants.auxParams[1] = static_cast<float>(batch->pixelProgramMode);
		if (batch->secondaryTextureData && !batch->actorAuxiliary)
			constants.auxParams[0] = 2.0f;
		if (batch->shaderName && strstr(batch->shaderName, "shader/uicanvas_radar.sht"))
		{
			constants.auxParams[1] = 11.0f;
			constants.actorBaseColor[0] = (batch->minX + batch->maxX) * 0.5f;
			constants.actorBaseColor[1] = (batch->minY + batch->maxY) * 0.5f;
			constants.actorBaseColor[2] = std::max(1.0f, (batch->maxX - batch->minX) * 0.5f);
			constants.actorBaseColor[3] = std::max(1.0f, (batch->maxY - batch->minY) * 0.5f);
		}
		if (batch->alphaTest)
			constants.auxParams[3] = static_cast<float>(batch->alphaTestReference) / 255.0f;
		constants.stageOps[0] = static_cast<float>(batch->stageColorOperation[0]);
		constants.stageOps[1] = static_cast<float>(batch->stageAlphaOperation[0]);
		constants.stageOps[2] = static_cast<float>(batch->stageColorOperation[1]);
		constants.stageOps[3] = static_cast<float>(batch->stageAlphaOperation[1]);
		constants.stageArgs01[0] = packStageArgumentsForShader(batch->stageColorArgument0[0], batch->stageColorArgument0Complement[0], batch->stageColorArgument0AlphaReplicate[0], batch->stageColorArgument1[0], batch->stageColorArgument1Complement[0], batch->stageColorArgument1AlphaReplicate[0], batch->stageColorArgument2[0], batch->stageColorArgument2Complement[0], batch->stageColorArgument2AlphaReplicate[0]);
		constants.stageArgs01[1] = packStageArgumentsForShader(batch->stageAlphaArgument0[0], batch->stageAlphaArgument0Complement[0], false, batch->stageAlphaArgument1[0], batch->stageAlphaArgument1Complement[0], false, batch->stageAlphaArgument2[0], batch->stageAlphaArgument2Complement[0], false);
		constants.stageArgs01[2] = static_cast<float>(batch->stageResultArgument[0]);
		constants.stageArgs01[3] = 0.0f;
		constants.stageArgs23[0] = packStageArgumentsForShader(batch->stageColorArgument0[1], batch->stageColorArgument0Complement[1], batch->stageColorArgument0AlphaReplicate[1], batch->stageColorArgument1[1], batch->stageColorArgument1Complement[1], batch->stageColorArgument1AlphaReplicate[1], batch->stageColorArgument2[1], batch->stageColorArgument2Complement[1], batch->stageColorArgument2AlphaReplicate[1]);
		constants.stageArgs23[1] = packStageArgumentsForShader(batch->stageAlphaArgument0[1], batch->stageAlphaArgument0Complement[1], false, batch->stageAlphaArgument1[1], batch->stageAlphaArgument1Complement[1], false, batch->stageAlphaArgument2[1], batch->stageAlphaArgument2Complement[1], false);
		constants.stageArgs23[2] = static_cast<float>(batch->stageResultArgument[1]);
		constants.stageArgs23[3] = 0.0f;

		float const invVertexCount = batch->vertexCount ? 1.0f / static_cast<float>(batch->vertexCount) : 0.0f;
		if (invVertexCount > 0.0f)
		{
			constants.batchColor[0] = std::max(0.0f, std::min(1.0f, batch->colorSum[0] * invVertexCount / 255.0f));
			constants.batchColor[1] = std::max(0.0f, std::min(1.0f, batch->colorSum[1] * invVertexCount / 255.0f));
			constants.batchColor[2] = std::max(0.0f, std::min(1.0f, batch->colorSum[2] * invVertexCount / 255.0f));
			constants.batchColor[3] = std::max(0.0f, std::min(1.0f, batch->colorSum[3] * invVertexCount / 255.0f));
		}
		if (batch->textureFactorValid)
		{
				memcpy(constants.textureFactor, batch->textureFactor, sizeof(constants.textureFactor));
			memcpy(constants.textureFactor2, batch->textureFactor2, sizeof(constants.textureFactor2));
		}
		if (shaderNameLooksCloudLayer(batch->shaderName))
		{
			float const cloudScale = gpuCloudAlphaScale();
			constants.textureFactor[3] *= cloudScale;
			static int s_cloudAlphaScaleLogCount = 0;
			if (gpuBatchDiagnosticsEnabled() && s_cloudAlphaScaleLogCount < 32)
			{
				char text[320];
				_snprintf(text, sizeof(text) - 1, "gpu cloud alpha scale count=%d shader=%s scale=%.3f textureFactorA=%.3f vertices=%u alpha=%d depth=%d/%d bounds=%.1f,%.1f..%.1f,%.1f",
					s_cloudAlphaScaleLogCount + 1,
					batch->shaderName,
					cloudScale,
					constants.textureFactor[3],
					static_cast<unsigned>(batch->vertexCount),
					batch->alphaBlend ? 1 : 0,
					batch->depthTest ? 1 : 0,
					batch->depthWrite ? 1 : 0,
					batch->minX,
					batch->minY,
					batch->maxX,
					batch->maxY);
				text[sizeof(text) - 1] = 0;
				logLine(text);
				++s_cloudAlphaScaleLogCount;
			}
		}
		if (batch->textureSampleCount > 0)
		{
			float const invSampleCount = 1.0f / static_cast<float>(batch->textureSampleCount);
			float const tintR = constants.batchColor[0];
			float const tintG = constants.batchColor[1];
			float const tintB = constants.batchColor[2];
			constants.actorBaseColor[0] = std::max(0.0f, std::min(1.0f, batch->textureSampleSum[0] * invSampleCount / 255.0f * tintR));
			constants.actorBaseColor[1] = std::max(0.0f, std::min(1.0f, batch->textureSampleSum[1] * invSampleCount / 255.0f * tintG));
			constants.actorBaseColor[2] = std::max(0.0f, std::min(1.0f, batch->textureSampleSum[2] * invSampleCount / 255.0f * tintB));
			constants.actorBaseColor[3] = 1.0f;
		}
		if (actorBaseColorOverride)
		{
			constants.actorBaseColor[0] = actorBaseColorOverride[0];
			constants.actorBaseColor[1] = actorBaseColorOverride[1];
			constants.actorBaseColor[2] = actorBaseColorOverride[2];
			constants.actorBaseColor[3] = actorBaseColorOverride[3];
		}
	}

	float gpuBatchOverlapArea(GpuDrawBatch const &a, GpuDrawBatch const &b)
	{
		float const minX = std::max(a.minX, b.minX);
		float const minY = std::max(a.minY, b.minY);
		float const maxX = std::min(a.maxX, b.maxX);
		float const maxY = std::min(a.maxY, b.maxY);
		float const width = maxX - minX;
		float const height = maxY - minY;
		return width > 0.0f && height > 0.0f ? width * height : 0.0f;
	}

	bool computeActorBatchMaterialColor(GpuDrawBatch const &batch, float (&outColor)[4])
	{
		if (!batch.textureSampleCount || !batch.vertexCount)
			return false;

		float const invVertexCount = 1.0f / static_cast<float>(batch.vertexCount);
		float const invSampleCount = 1.0f / static_cast<float>(batch.textureSampleCount);
		float const tintR = std::max(0.0f, std::min(1.0f, batch.colorSum[0] * invVertexCount / 255.0f));
		float const tintG = std::max(0.0f, std::min(1.0f, batch.colorSum[1] * invVertexCount / 255.0f));
		float const tintB = std::max(0.0f, std::min(1.0f, batch.colorSum[2] * invVertexCount / 255.0f));
		outColor[0] = std::max(0.0f, std::min(1.0f, batch.textureSampleSum[0] * invSampleCount / 255.0f * tintR));
		outColor[1] = std::max(0.0f, std::min(1.0f, batch.textureSampleSum[1] * invSampleCount / 255.0f * tintG));
		outColor[2] = std::max(0.0f, std::min(1.0f, batch.textureSampleSum[2] * invSampleCount / 255.0f * tintB));
		outColor[3] = 1.0f;
		return true;
	}

	bool findActorAuxBaseMaterialColor(size_t auxBatchIndex, float (&outColor)[4], size_t &outBaseBatchIndex)
	{
		if (auxBatchIndex >= ms_gpuDrawBatches.size())
			return false;

		GpuDrawBatch const &auxBatch = ms_gpuDrawBatches[auxBatchIndex];
		if (!auxBatch.actorAuxiliary)
			return false;

		float bestScore = 0.0f;
		size_t bestIndex = 0;
		float bestColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		for (size_t candidateIndex = 0; candidateIndex < auxBatchIndex; ++candidateIndex)
		{
			GpuDrawBatch const &candidate = ms_gpuDrawBatches[candidateIndex];
			if (!candidate.actorBody || candidate.actorAuxiliary || !candidate.textureData || candidate.textureTag != TAG(M,A,I,N))
				continue;
			if (candidate.shaderName != auxBatch.shaderName)
				continue;

			float materialColor[4];
			if (!computeActorBatchMaterialColor(candidate, materialColor))
				continue;

			float const overlap = gpuBatchOverlapArea(candidate, auxBatch);
			if (overlap <= 0.0f)
				continue;

			float const score = overlap * (candidate.pixelProgramMode == auxBatch.pixelProgramMode ? 2.0f : 1.0f);
			if (score > bestScore)
			{
				bestScore = score;
				bestIndex = candidateIndex;
				for (int i = 0; i < 4; ++i)
					bestColor[i] = materialColor[i];
			}
		}

		if (bestScore <= 0.0f)
			return false;

		outBaseBatchIndex = bestIndex;
		for (int i = 0; i < 4; ++i)
			outColor[i] = bestColor[i];
		return true;
	}

	VulkanTextureData const *findActorAuxBaseTextureData(size_t auxBatchIndex, size_t &outBaseBatchIndex)
	{
		if (auxBatchIndex >= ms_gpuDrawBatches.size())
			return 0;

		GpuDrawBatch const &auxBatch = ms_gpuDrawBatches[auxBatchIndex];
		if (!auxBatch.actorAuxiliary)
			return 0;

		float bestScore = 0.0f;
		size_t bestIndex = 0;
		for (size_t candidateIndex = 0; candidateIndex < auxBatchIndex; ++candidateIndex)
		{
			GpuDrawBatch const &candidate = ms_gpuDrawBatches[candidateIndex];
			if (!candidate.actorBody || candidate.actorAuxiliary || !candidate.textureData || candidate.textureTag != TAG(M,A,I,N))
				continue;
			if (candidate.shaderName != auxBatch.shaderName)
				continue;

			float const overlap = gpuBatchOverlapArea(candidate, auxBatch);
			if (overlap <= 0.0f)
				continue;

			float const score = overlap * (candidate.pixelProgramMode == auxBatch.pixelProgramMode ? 2.0f : 1.0f);
			if (score > bestScore)
			{
				bestScore = score;
				bestIndex = candidateIndex;
			}
		}

		if (bestScore <= 0.0f)
			return 0;

		outBaseBatchIndex = bestIndex;
		return ms_gpuDrawBatches[bestIndex].textureData;
	}

	VkDescriptorSet getPairedTextureDescriptorSet(VulkanTextureData const *primary, VulkanTextureData const *secondary, bool clampSampler, bool secondaryPointSampler = false)
	{
		if (!primary || !secondary || !ms_device || !ms_gpuDrawDescriptorPool || !ms_whiteTextureDescriptorSetLayout)
			return 0;
		if (!ms_pairedTextureDescriptorMap)
			ms_pairedTextureDescriptorMap = new PairedTextureDescriptorMap;

		PairedTextureDescriptorKey key;
		key.primary = primary;
		key.secondary = secondary;
		key.clampSampler = clampSampler;
		key.secondaryPointSampler = secondaryPointSampler;
		PairedTextureDescriptorMap::iterator found = ms_pairedTextureDescriptorMap->find(key);
		if (found != ms_pairedTextureDescriptorMap->end())
			return found->second;

		VkDescriptorImageInfo primaryDescriptor;
		VkDescriptorImageInfo secondaryDescriptor;
		if (!primary->getGpuDescriptorImageInfo(primaryDescriptor, clampSampler) || !secondary->getGpuDescriptorImageInfo(secondaryDescriptor, clampSampler))
			return 0;
		if (secondaryPointSampler && ms_pointTextureSampler)
		{
			secondaryDescriptor.sampler = ms_pointTextureSampler;
			static int s_pointPairLogCount = 0;
			if (s_pointPairLogCount++ < 16)
				logLine("gpu paired descriptor secondary point sampler");
		}

		VkDescriptorSet descriptorSet = 0;
		VkDescriptorSetAllocateInfo descriptorAllocate;
		memset(&descriptorAllocate, 0, sizeof(descriptorAllocate));
		descriptorAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorAllocate.descriptorPool = ms_gpuDrawDescriptorPool;
		descriptorAllocate.descriptorSetCount = 1;
		descriptorAllocate.pSetLayouts = &ms_whiteTextureDescriptorSetLayout;
		if (vkAllocateDescriptorSetsLocal(ms_device, &descriptorAllocate, &descriptorSet) != VK_SUCCESS)
		{
			static int s_pairLogCount = 0;
			if (s_pairLogCount++ < 32)
				logLine("vkAllocateDescriptorSets paired actor material failed");
			return 0;
		}

		VkWriteDescriptorSet writes[2];
		memset(writes, 0, sizeof(writes));
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = descriptorSet;
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &primaryDescriptor;
		writes[1] = writes[0];
		writes[1].dstBinding = 1;
		writes[1].pImageInfo = &secondaryDescriptor;
		vkUpdateDescriptorSetsLocal(ms_device, 2, writes, 0, 0);

		(*ms_pairedTextureDescriptorMap)[key] = descriptorSet;
		return descriptorSet;
	}

	bool gpuWorldClipEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_WORLD_CLIP", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 0;
		}
		return s_enabled != 0;
	}

	bool gpuNativeCubeSkyEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_NATIVE_CUBE_SKY", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuDropBlackSkyboxEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_DROP_BLACK_SKYBOX", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuCpuCullEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_CPU_CULL", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuCpuCullFlipEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_CPU_CULL_FLIP", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuDropTerrainEdgeOpaqueEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_DROP_TERRAIN_EDGE_OPAQUE", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuDropTerrainWideTriangleEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_DROP_TERRAIN_WIDE_TRI", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuTerrainWideRepairEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_TERRAIN_WIDE_REPAIR", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text) && atoi(text) != 0) ? 1 : 0;
		}
		return s_enabled != 0;
	}

	bool gpuDropTerrainSkyTriangleEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_DROP_TERRAIN_SKY_TRI", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuTextureMipmapsEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_TEXTURE_MIPS", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	bool gpuTextureAnisotropyEnabled()
	{
		static int s_enabled = -1;
		if (s_enabled < 0)
		{
			char text[16];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_TEXTURE_ANISO", text, sizeof(text));
			s_enabled = (length > 0 && length < sizeof(text)) ? (atoi(text) != 0 ? 1 : 0) : 1;
		}
		return s_enabled != 0;
	}

	float gpuTextureAnisotropyLevel()
	{
		static float s_level = -1.0f;
		if (s_level < 0.0f)
		{
			char text[32];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_TEXTURE_ANISO_LEVEL", text, sizeof(text));
			s_level = (length > 0 && length < sizeof(text)) ? static_cast<float>(atof(text)) : 8.0f;
			if (!_finite(s_level) || s_level < 1.0f)
				s_level = 1.0f;
			if (s_level > 16.0f)
				s_level = 16.0f;
		}
		return s_level;
	}

	GpuScreenVertex makeGpuScreenVertex(ScreenVertex const &source)
	{
		GpuScreenVertex vertex;
		int const width = ms_viewportWidth > 0 ? ms_viewportWidth : std::max(1, ms_width);
		int const height = ms_viewportHeight > 0 ? ms_viewportHeight : std::max(1, ms_height);
		float const invWidth = 1.0f / static_cast<float>(std::max(1, width));
		float const invHeight = 1.0f / static_cast<float>(std::max(1, height));
		vertex.x = ((source.x - static_cast<float>(ms_viewportX)) * invWidth) * 2.0f - 1.0f;
		vertex.y = 1.0f - ((source.y - static_cast<float>(ms_viewportY)) * invHeight) * 2.0f;
		vertex.z = source.z;
		vertex.w = source.w;
		vertex.u = source.u;
		vertex.v = source.v;
		vertex.u2 = source.u2;
		vertex.v2 = source.v2;
		vertex.a = static_cast<float>((source.color >> 24) & 0xff) / 255.0f;
		vertex.r = static_cast<float>((source.color >> 16) & 0xff) / 255.0f;
		vertex.g = static_cast<float>((source.color >> 8) & 0xff) / 255.0f;
		vertex.b = static_cast<float>(source.color & 0xff) / 255.0f;
		if (!ms_activeTextureCube)
		{
			vertex.cubeDirX = source.specularR;
			vertex.cubeDirY = source.specularG;
			vertex.cubeDirZ = source.specularB;
		}
		else
		{
			float const lengthSquared = source.objectX * source.objectX + source.objectY * source.objectY + source.objectZ * source.objectZ;
			if (lengthSquared > 0.000001f && _finite(lengthSquared))
			{
				vertex.cubeDirX = source.objectX;
				vertex.cubeDirY = source.objectY;
				vertex.cubeDirZ = source.objectZ;
			}
			else
			{
				vertex.cubeDirX = 0.0f;
				vertex.cubeDirY = 0.0f;
				vertex.cubeDirZ = 1.0f;
			}
		}
		return vertex;
	}

	uint32 makeOpaqueColor(uint32 color)
	{
		return color | 0xff000000;
	}

	enum GpuClipPlane
	{
		GCP_left,
		GCP_right,
		GCP_bottom,
		GCP_top,
		GCP_near,
		GCP_far
	};

	float gpuClipDistance(ScreenVertex const &vertex, GpuClipPlane plane)
	{
		switch (plane)
		{
		case GCP_left:
			return vertex.clipX + vertex.clipW;
		case GCP_right:
			return vertex.clipW - vertex.clipX;
		case GCP_bottom:
			return vertex.clipY + vertex.clipW;
		case GCP_top:
			return vertex.clipW - vertex.clipY;
		case GCP_near:
			return vertex.clipZ;
		case GCP_far:
			return vertex.clipW - vertex.clipZ;
		default:
			return 0.0f;
		}
	}

	uint32 lerpPackedColor(uint32 a, uint32 b, float t)
	{
		uint32 result = 0;
		for (int shift = 0; shift <= 24; shift += 8)
		{
			float const av = static_cast<float>((a >> shift) & 0xff);
			float const bv = static_cast<float>((b >> shift) & 0xff);
			uint32 const value = static_cast<uint32>(std::max(0, std::min(255, static_cast<int>(av + (bv - av) * t + 0.5f))));
			result |= value << shift;
		}
		return result;
	}

	ScreenVertex lerpScreenVertex(ScreenVertex const &a, ScreenVertex const &b, float t)
	{
		ScreenVertex out = a;
		out.x = a.x + (b.x - a.x) * t;
		out.y = a.y + (b.y - a.y) * t;
		out.z = a.z + (b.z - a.z) * t;
		out.w = a.w + (b.w - a.w) * t;
		out.clipX = a.clipX + (b.clipX - a.clipX) * t;
		out.clipY = a.clipY + (b.clipY - a.clipY) * t;
		out.clipZ = a.clipZ + (b.clipZ - a.clipZ) * t;
		out.clipW = a.clipW + (b.clipW - a.clipW) * t;
		out.objectX = a.objectX + (b.objectX - a.objectX) * t;
		out.objectY = a.objectY + (b.objectY - a.objectY) * t;
		out.objectZ = a.objectZ + (b.objectZ - a.objectZ) * t;
		out.u = a.u + (b.u - a.u) * t;
		out.v = a.v + (b.v - a.v) * t;
		out.u2 = a.u2 + (b.u2 - a.u2) * t;
		out.v2 = a.v2 + (b.v2 - a.v2) * t;
		out.specularR = a.specularR + (b.specularR - a.specularR) * t;
		out.specularG = a.specularG + (b.specularG - a.specularG) * t;
		out.specularB = a.specularB + (b.specularB - a.specularB) * t;
		out.color = lerpPackedColor(a.color, b.color, t);
		out.valid = true;
		return out;
	}

	int clipGpuPolygonAgainstPlane(ScreenVertex const *input, int inputCount, ScreenVertex *output, GpuClipPlane plane)
	{
		if (inputCount <= 0)
			return 0;

		int outputCount = 0;
		ScreenVertex previous = input[inputCount - 1];
		float previousDistance = gpuClipDistance(previous, plane);
		bool previousInside = previousDistance >= 0.0f;
		for (int i = 0; i < inputCount; ++i)
		{
			ScreenVertex const current = input[i];
			float const currentDistance = gpuClipDistance(current, plane);
			bool const currentInside = currentDistance >= 0.0f;
			if (currentInside != previousInside)
			{
				float const denominator = previousDistance - currentDistance;
				float const t = fabsf(denominator) > 0.000001f ? previousDistance / denominator : 0.0f;
				if (outputCount < 8)
					output[outputCount++] = lerpScreenVertex(previous, current, std::max(0.0f, std::min(1.0f, t)));
			}
			if (currentInside)
			{
				if (outputCount < 8)
					output[outputCount++] = current;
			}
			previous = current;
			previousDistance = currentDistance;
			previousInside = currentInside;
		}
		return outputCount;
	}

	bool projectClippedGpuVertex(ScreenVertex &vertex)
	{
		if (!_finite(vertex.clipX) || !_finite(vertex.clipY) || !_finite(vertex.clipZ) || !_finite(vertex.clipW) || vertex.clipW <= 0.00001f)
			return false;

		float const invW = 1.0f / vertex.clipW;
		float const ndcX = vertex.clipX * invW;
		float const ndcY = vertex.clipY * invW;
		float const ndcZ = vertex.clipZ * invW;
		if (!_finite(ndcX) || !_finite(ndcY) || !_finite(ndcZ) || ndcZ < 0.0f || ndcZ > 1.0f)
			return false;

		int const viewportWidth = ms_viewportWidth > 0 ? ms_viewportWidth : static_cast<int>(ms_swapchainExtent.width);
		int const viewportHeight = ms_viewportHeight > 0 ? ms_viewportHeight : static_cast<int>(ms_swapchainExtent.height);
		vertex.x = static_cast<float>(ms_viewportX) + ((ndcX + 1.0f) * 0.5f * static_cast<float>(viewportWidth));
		vertex.y = static_cast<float>(ms_viewportY) + ((1.0f - ndcY) * 0.5f * static_cast<float>(viewportHeight));
		vertex.z = std::max(0.0f, std::min(1.0f, ndcZ));
		vertex.w = vertex.clipW;
		return true;
	}

	bool transformVertex(byte const *vertex, ScreenVertex &out);
	bool transformActiveVertex(int vertexIndex, ScreenVertex &out);
	bool readTransformedVertex(byte const *vertex, ScreenVertex &out);
	bool readTransformedActiveVertex(int vertexIndex, ScreenVertex &out);
	uint32 applyFixedFunctionVertexLighting(byte const *vertex, uint32 color);
	uint32 applyFixedFunctionVertexLighting(byte const *vertex, uint32 color, float *specularOut);
	uint32 applyFixedFunctionVertexLighting(byte const *vertex, uint32 color, float *specularOut, bool mobileBodyPass);
	bool isCorruptTerrainVertex(ScreenVertex const &vertex);
	void stageGpuTriangle(ScreenVertex a, ScreenVertex b, ScreenVertex c, bool alphaBlend, bool applyCull, bool actorBody, bool preserveDepthState);
	void stageGpuWorldTriangleClipped(ScreenVertex const &a, ScreenVertex const &b, ScreenVertex const &c, bool alphaBlend, bool actorBody);

	bool activeFixedFunctionAlphaMaskTexture(bool alphaBlend)
	{
		if (ms_activeStaticShaderNamePtr && strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend"))
			return false;
		return alphaBlend &&
			ms_activeTextureData &&
			ms_activeStageTextureCount >= 2 &&
			ms_activeStageColorOperation[1] == 2 &&
			ms_activeStageColorArgument2[1] == 0 &&
			ms_activeStageAlphaOperation[1] == 1 &&
			ms_activeStageAlphaArgument1[1] == 4;
	}

	bool stringContainsNoCase(char const *text, char const *needle)
	{
		if (!text || !needle || !*needle)
			return false;
		size_t const needleLength = strlen(needle);
		for (char const *scan = text; *scan; ++scan)
		{
			size_t i = 0;
			for (; i < needleLength && scan[i]; ++i)
			{
				char const a = static_cast<char>(tolower(static_cast<unsigned char>(scan[i])));
				char const b = static_cast<char>(tolower(static_cast<unsigned char>(needle[i])));
				if (a != b)
					break;
			}
			if (i == needleLength)
				return true;
		}
		return false;
	}

	bool textureNameLooksFoliageCutout(char const *textureName)
	{
		return stringContainsNoCase(textureName, "flora") ||
			stringContainsNoCase(textureName, "grass") ||
			stringContainsNoCase(textureName, "bush") ||
			stringContainsNoCase(textureName, "plant") ||
			stringContainsNoCase(textureName, "leaf") ||
			stringContainsNoCase(textureName, "frond") ||
			stringContainsNoCase(textureName, "foliage") ||
			stringContainsNoCase(textureName, "shadow") ||
			stringContainsNoCase(textureName, "blob");
	}

	bool textureNameLooksSoftParticle(char const *textureName)
	{
		return stringContainsNoCase(textureName, "particle") ||
			stringContainsNoCase(textureName, "dust") ||
			stringContainsNoCase(textureName, "smoke") ||
			stringContainsNoCase(textureName, "ember") ||
			stringContainsNoCase(textureName, "insect") ||
			stringContainsNoCase(textureName, "pt_");
	}

	bool textureNameLooksOpaqueCutout(char const *textureName)
	{
		return stringContainsNoCase(textureName, "flora") ||
			stringContainsNoCase(textureName, "grass") ||
			stringContainsNoCase(textureName, "bush") ||
			stringContainsNoCase(textureName, "plant") ||
			stringContainsNoCase(textureName, "leaf") ||
			stringContainsNoCase(textureName, "frond") ||
			stringContainsNoCase(textureName, "foliage") ||
			stringContainsNoCase(textureName, "shadow") ||
			stringContainsNoCase(textureName, "blob");
	}

	bool textureHasCutoutAlpha(VulkanTextureData const *textureData)
	{
		if (!textureData)
			return false;

		bool sawTransparent = false;
		bool sawOpaque = false;
		for (int y = 0; y < 5; ++y)
		{
			float const v = static_cast<float>(y) * 0.25f;
			for (int x = 0; x < 5; ++x)
			{
				float const u = static_cast<float>(x) * 0.25f;
				uint32 const sample = textureData->sample(u, v, true);
				uint32 const alpha = (sample >> 24) & 0xff;
				if (alpha <= 32)
					sawTransparent = true;
				if (alpha >= 192)
					sawOpaque = true;
			}
		}
		return sawTransparent && sawOpaque;
	}

	bool activeWorldFoliageAlphaFallback(bool alphaBlend, bool applyCull, bool actorBody)
	{
		if (!gpuPresentEnabled() || actorBody || !applyCull || ms_shaderAlphaTestEnabled || !ms_activeTextureData)
			return false;
		if (ms_activeStaticShaderNamePtr && strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend"))
			return false;
		if (ms_activeStaticShaderNamePtr &&
			(strstr(ms_activeStaticShaderNamePtr, "shader/sun_") ||
			strstr(ms_activeStaticShaderNamePtr, "shader/moon_") ||
			strstr(ms_activeStaticShaderNamePtr, "shader/gradient_sky") ||
			shaderNameLooksCloudLayer(ms_activeStaticShaderNamePtr)))
			return false;
		if (alphaBlend)
		{
			if (textureNameLooksSoftParticle(ms_activeTextureData->getName()))
				return false;
			if (!textureNameLooksFoliageCutout(ms_activeTextureData->getName()))
				return false;
		}
		else if (!textureNameLooksOpaqueCutout(ms_activeTextureData->getName()))
			return false;
		return textureHasCutoutAlpha(ms_activeTextureData);
	}

	bool activeWorldWeakAlphaTestRepair(bool alphaBlend, bool applyCull, bool actorBody)
	{
		if (!gpuPresentEnabled() || actorBody || !applyCull || !ms_shaderAlphaTestEnabled || !ms_activeTextureData)
			return false;
		if (ms_shaderAlphaTestCompare != ShaderImplementation::Pass::C_Greater || ms_shaderAlphaTestReference > 1)
			return false;
		if (ms_activeStaticShaderNamePtr && strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend"))
			return false;
		if (ms_activeStaticShaderNamePtr &&
			(strstr(ms_activeStaticShaderNamePtr, "shader/sun_") ||
			strstr(ms_activeStaticShaderNamePtr, "shader/moon_") ||
			strstr(ms_activeStaticShaderNamePtr, "shader/gradient_sky") ||
			shaderNameLooksCloudLayer(ms_activeStaticShaderNamePtr)))
			return false;
		if (alphaBlend)
		{
			if (textureNameLooksSoftParticle(ms_activeTextureData->getName()))
				return false;
			if (!textureNameLooksFoliageCutout(ms_activeTextureData->getName()))
				return false;
		}
		else if (!textureNameLooksOpaqueCutout(ms_activeTextureData->getName()))
			return false;
		return textureHasCutoutAlpha(ms_activeTextureData);
	}

	bool activeTerrainBlendShader()
	{
		return ms_activeStaticShaderNamePtr && strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend");
	}

	bool terrainTriangleNeedsPathologicalClip(ScreenVertex const &a, ScreenVertex const &b, ScreenVertex const &c)
	{
		if (!activeTerrainBlendShader() || !a.valid || !b.valid || !c.valid)
			return false;

		int const diagnosticWidth = ms_viewportWidth > 0 ? ms_viewportWidth : std::max(1, ms_width);
		int const diagnosticHeight = ms_viewportHeight > 0 ? ms_viewportHeight : std::max(1, ms_height);
		float const minX = std::min(a.x, std::min(b.x, c.x));
		float const minY = std::min(a.y, std::min(b.y, c.y));
		float const maxX = std::max(a.x, std::max(b.x, c.x));
		float const maxY = std::max(a.y, std::max(b.y, c.y));
		float const spanX = maxX - minX;
		float const spanY = maxY - minY;
		float const widthF = static_cast<float>(diagnosticWidth);
		float const heightF = static_cast<float>(diagnosticHeight);

		return spanX > widthF * 1.50f ||
			spanY > heightF * 1.50f ||
			minX < -widthF * 0.75f ||
			maxX > widthF * 1.75f ||
			minY < -heightF * 0.75f ||
			maxY > heightF * 1.75f;
	}

	bool shaderNameLooksCloudLayer(char const *shaderName)
	{
		return shaderName &&
			(strstr(shaderName, "shader/cloudtile") ||
			strstr(shaderName, "shader/procedural_cloud") ||
			strstr(shaderName, "shader/terrain_cloud") ||
			strstr(shaderName, "shader/cloudlayer") ||
			strstr(shaderName, "shader/pt_sand_cloud"));
	}

	uint32 averageVertexColor(std::vector<ScreenVertex> const &vertices)
	{
		if (vertices.empty())
			return 0xffffffff;
		uint32 sumA = 0;
		uint32 sumR = 0;
		uint32 sumG = 0;
		uint32 sumB = 0;
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			uint32 const color = vertices[i].color;
			sumA += (color >> 24) & 0xff;
			sumR += (color >> 16) & 0xff;
			sumG += (color >> 8) & 0xff;
			sumB += color & 0xff;
		}
		uint32 const count = static_cast<uint32>(vertices.size());
		return ((sumA / count) << 24) | ((sumR / count) << 16) | ((sumG / count) << 8) | (sumB / count);
	}

	bool readGpuVertexForActiveFormat(int vertexIndex, VertexBufferFormat const &format, ScreenVertex &out)
	{
		if (!ms_activeVertexData || vertexIndex < 0 || vertexIndex >= ms_activeVertexCount)
			return false;
		if (ms_activeVertexVector)
			return format.isTransformed() ? readTransformedActiveVertex(vertexIndex, out) : transformActiveVertex(vertexIndex, out);
		byte const *vertex = ms_activeVertexData + static_cast<size_t>(vertexIndex) * static_cast<size_t>(ms_activeVertexStride);
		bool const ok = format.isTransformed() ? readTransformedVertex(vertex, out) : transformVertex(vertex, out);
		if (ok && gpuPresentEnabled() && !format.isTransformed() && ms_activeLightingEnabled && ms_activePixelProgramMode == 0 && ms_activeVertexDescriptor && ms_activeVertexDescriptor->offsetNormal >= 0)
		{
			float specularRgb[3];
			out.color = applyFixedFunctionVertexLighting(vertex, out.color, specularRgb);
			out.specularR = specularRgb[0];
			out.specularG = specularRgb[1];
			out.specularB = specularRgb[2];
		}
		return ok;
	}

	bool drawTerrainFanWithRepairedCenter(std::vector<int> const &vertexIndices, VertexBufferFormat const &format)
	{
		if (!gpuPresentEnabled() || !activeTerrainBlendShader() || format.isTransformed() || vertexIndices.size() < 4)
			return false;

		ScreenVertex originalCenter;
		bool const originalCenterValid = readGpuVertexForActiveFormat(vertexIndices[0], format, originalCenter);
		if (originalCenterValid && !isCorruptTerrainVertex(originalCenter))
			return false;
		if (!isCorruptTerrainVertex(originalCenter))
			return false;

		std::vector<ScreenVertex> ring;
		ring.reserve(vertexIndices.size() - 1);
		for (size_t i = 1; i < vertexIndices.size(); ++i)
		{
			ScreenVertex vertex;
			if (!readGpuVertexForActiveFormat(vertexIndices[i], format, vertex))
				continue;
			if (isCorruptTerrainVertex(vertex))
				continue;
			ring.push_back(vertex);
		}
		if (ring.size() < 3)
			return false;

		ScreenVertex repairedCenter = originalCenter;
		repairedCenter.valid = true;
		repairedCenter.x = 0.0f;
		repairedCenter.y = 0.0f;
		repairedCenter.z = 0.0f;
		repairedCenter.w = 0.0f;
		repairedCenter.clipX = 0.0f;
		repairedCenter.clipY = 0.0f;
		repairedCenter.clipZ = 0.0f;
		repairedCenter.clipW = 0.0f;
		repairedCenter.objectX = 0.0f;
		repairedCenter.objectY = 0.0f;
		repairedCenter.objectZ = 0.0f;
		repairedCenter.u = _finite(originalCenter.u) ? originalCenter.u : 0.0f;
		repairedCenter.v = _finite(originalCenter.v) ? originalCenter.v : 0.0f;
		repairedCenter.u2 = _finite(originalCenter.u2) ? originalCenter.u2 : repairedCenter.u;
		repairedCenter.v2 = _finite(originalCenter.v2) ? originalCenter.v2 : repairedCenter.v;
		repairedCenter.specularR = 0.0f;
		repairedCenter.specularG = 0.0f;
		repairedCenter.specularB = 0.0f;
		float avgU = 0.0f;
		float avgV = 0.0f;
		float avgU2 = 0.0f;
		float avgV2 = 0.0f;
		for (size_t i = 0; i < ring.size(); ++i)
		{
			repairedCenter.x += ring[i].x;
			repairedCenter.y += ring[i].y;
			repairedCenter.z += ring[i].z;
			repairedCenter.w += ring[i].w;
			repairedCenter.clipX += ring[i].clipX;
			repairedCenter.clipY += ring[i].clipY;
			repairedCenter.clipZ += ring[i].clipZ;
			repairedCenter.clipW += ring[i].clipW;
			repairedCenter.objectX += ring[i].objectX;
			repairedCenter.objectY += ring[i].objectY;
			repairedCenter.objectZ += ring[i].objectZ;
			repairedCenter.specularR += ring[i].specularR;
			repairedCenter.specularG += ring[i].specularG;
			repairedCenter.specularB += ring[i].specularB;
			avgU += ring[i].u;
			avgV += ring[i].v;
			avgU2 += ring[i].u2;
			avgV2 += ring[i].v2;
		}
		float const invCount = 1.0f / static_cast<float>(ring.size());
		repairedCenter.x *= invCount;
		repairedCenter.y *= invCount;
		repairedCenter.z *= invCount;
		repairedCenter.w *= invCount;
		repairedCenter.clipX *= invCount;
		repairedCenter.clipY *= invCount;
		repairedCenter.clipZ *= invCount;
		repairedCenter.clipW *= invCount;
		repairedCenter.objectX *= invCount;
		repairedCenter.objectY *= invCount;
		repairedCenter.objectZ *= invCount;
		repairedCenter.specularR *= invCount;
		repairedCenter.specularG *= invCount;
		repairedCenter.specularB *= invCount;
		if (!_finite(repairedCenter.u) || fabsf(repairedCenter.u) > 8192.0f)
			repairedCenter.u = avgU * invCount;
		if (!_finite(repairedCenter.v) || fabsf(repairedCenter.v) > 8192.0f)
			repairedCenter.v = avgV * invCount;
		if (!_finite(repairedCenter.u2) || fabsf(repairedCenter.u2) > 8192.0f)
			repairedCenter.u2 = avgU2 * invCount;
		if (!_finite(repairedCenter.v2) || fabsf(repairedCenter.v2) > 8192.0f)
			repairedCenter.v2 = avgV2 * invCount;
		repairedCenter.color = averageVertexColor(ring);

		bool const actorBodyFixedFunction = false;
		bool const gpuAlphaBlend = ms_shaderAlphaBlendEnabled;
		for (size_t i = 0; i < ring.size(); ++i)
		{
			ScreenVertex const &b = ring[i];
			ScreenVertex const &c = ring[(i + 1) % ring.size()];
			if (gpuWorldClipEnabled())
				stageGpuWorldTriangleClipped(repairedCenter, b, c, gpuAlphaBlend, actorBodyFixedFunction);
			else
				stageGpuTriangle(repairedCenter, b, c, gpuAlphaBlend, true, actorBodyFixedFunction, false);
		}

		static int s_repairedTerrainFanLogCount = 0;
		if (gpuBatchDiagnosticsEnabled() && s_repairedTerrainFanLogCount < 96)
		{
			char text[640];
			_snprintf(text, sizeof(text) - 1, "gpu repaired terrain fan count=%d shader=%s texture=0x%08x centerIndex=%d ring=%u oldClip=(%.3f,%.3f,%.3f,%.3f) repairedClip=(%.3f,%.3f,%.3f,%.3f) uv=(%.3f,%.3f)",
				s_repairedTerrainFanLogCount + 1,
				ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
				static_cast<unsigned>(ms_activeTextureTag),
				vertexIndices[0],
				static_cast<unsigned>(ring.size()),
				originalCenter.clipX,
				originalCenter.clipY,
				originalCenter.clipZ,
				originalCenter.clipW,
				repairedCenter.clipX,
				repairedCenter.clipY,
				repairedCenter.clipZ,
				repairedCenter.clipW,
				repairedCenter.u,
				repairedCenter.v);
			text[sizeof(text) - 1] = 0;
			logLine(text);
			++s_repairedTerrainFanLogCount;
		}
		return true;
	}

	bool activeAtmosphereShader()
	{
		if (!ms_activeStaticShaderNamePtr)
			return false;
		return strstr(ms_activeStaticShaderNamePtr, "shader/sun_") ||
			strstr(ms_activeStaticShaderNamePtr, "shader/moon_") ||
			strstr(ms_activeStaticShaderNamePtr, "shader/gradient_sky") ||
			shaderNameLooksCloudLayer(ms_activeStaticShaderNamePtr);
	}

	void clampAtmosphereVertex(ScreenVertex &vertex, float minX, float maxX, float minY, float maxY)
	{
		vertex.x = std::max(minX, std::min(maxX, vertex.x));
		vertex.y = std::max(minY, std::min(maxY, vertex.y));
	}

	void stageGpuTriangle(ScreenVertex a, ScreenVertex b, ScreenVertex c, bool alphaBlend, bool applyCull, bool actorBody, bool preserveDepthState = false)
	{
		if (!gpuPresentEnabled() || !ms_gpuTriangleVertexMapped || !ms_gpuTriangleIndexMapped || !ms_gpuTriangleVertexBuffer || !ms_gpuTriangleIndexBuffer)
			return;
		if (!a.valid || !b.valid || !c.valid)
			return;
		if (applyCull && gpuCpuCullEnabled())
		{
			float area = (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
			if (gpuCpuCullFlipEnabled())
				area = -area;
			if (fabsf(area) < 0.0001f)
				return;
			if (ms_cullMode == GCM_clockwise && area > 0.0f)
				return;
			if (ms_cullMode == GCM_counterClockwise && area < 0.0f)
				return;
		}
		int const diagnosticWidth = ms_viewportWidth > 0 ? ms_viewportWidth : std::max(1, ms_width);
		int const diagnosticHeight = ms_viewportHeight > 0 ? ms_viewportHeight : std::max(1, ms_height);
		bool const atmosphereShader = activeAtmosphereShader();
		if (atmosphereShader && applyCull && !ms_depthEnabled && alphaBlend)
		{
			float const clampMarginX = static_cast<float>(diagnosticWidth) * 2.0f;
			float const clampMarginY = static_cast<float>(diagnosticHeight) * 2.0f;
			static int s_atmosphereClampLogCount = 0;
			if (gpuBatchDiagnosticsEnabled() && s_atmosphereClampLogCount < 96)
			{
				float const beforeMinX = std::min(a.x, std::min(b.x, c.x));
				float const beforeMinY = std::min(a.y, std::min(b.y, c.y));
				float const beforeMaxX = std::max(a.x, std::max(b.x, c.x));
				float const beforeMaxY = std::max(a.y, std::max(b.y, c.y));
				if (beforeMinX < -clampMarginX || beforeMaxX > static_cast<float>(diagnosticWidth) + clampMarginX ||
					beforeMinY < -clampMarginY || beforeMaxY > static_cast<float>(diagnosticHeight) + clampMarginY)
				{
					char text[512];
					_snprintf(text, sizeof(text) - 1, "gpu clamp atmosphere tri count=%d shader=%s bounds=%.1f,%.1f..%.1f,%.1f clamp=%.1f,%.1f..%.1f,%.1f",
						s_atmosphereClampLogCount + 1,
						ms_activeStaticShaderNamePtr,
						beforeMinX,
						beforeMinY,
						beforeMaxX,
						beforeMaxY,
						-clampMarginX,
						-clampMarginY,
						static_cast<float>(diagnosticWidth) + clampMarginX,
						static_cast<float>(diagnosticHeight) + clampMarginY);
					text[sizeof(text) - 1] = 0;
					logLine(text);
					++s_atmosphereClampLogCount;
				}
			}
			clampAtmosphereVertex(a, -clampMarginX, static_cast<float>(diagnosticWidth) + clampMarginX, -clampMarginY, static_cast<float>(diagnosticHeight) + clampMarginY);
			clampAtmosphereVertex(b, -clampMarginX, static_cast<float>(diagnosticWidth) + clampMarginX, -clampMarginY, static_cast<float>(diagnosticHeight) + clampMarginY);
			clampAtmosphereVertex(c, -clampMarginX, static_cast<float>(diagnosticWidth) + clampMarginX, -clampMarginY, static_cast<float>(diagnosticHeight) + clampMarginY);
		}
		float const triangleDepth = std::max(a.z, std::max(b.z, c.z));
		float const minX = std::min(a.x, std::min(b.x, c.x));
		float const minY = std::min(a.y, std::min(b.y, c.y));
		float const maxX = std::max(a.x, std::max(b.x, c.x));
		float const maxY = std::max(a.y, std::max(b.y, c.y));
		float const minW = std::min(a.w, std::min(b.w, c.w));
		float const maxW = std::max(a.w, std::max(b.w, c.w));
		float const minU = std::min(a.u, std::min(b.u, c.u));
		float const minV = std::min(a.v, std::min(b.v, c.v));
		float const maxU = std::max(a.u, std::max(b.u, c.u));
		float const maxV = std::max(a.v, std::max(b.v, c.v));
		float const maxAbsUv = std::max(std::max(fabsf(minU), fabsf(maxU)), std::max(fabsf(minV), fabsf(maxV)));
		float const spanX = maxX - minX;
		float const spanY = maxY - minY;
		float const spanU = maxU - minU;
		float const spanV = maxV - minV;
		static int s_dropTerrainWideTriLogCount = 0;
		if (gpuDropTerrainWideTriangleEnabled() &&
			applyCull &&
			ms_activeStaticShaderNamePtr &&
			strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend") &&
			spanX > static_cast<float>(diagnosticWidth) * 0.85f &&
			spanY > static_cast<float>(diagnosticHeight) * 0.20f &&
			maxY > static_cast<float>(diagnosticHeight) * 0.45f)
		{
			if (gpuBatchDiagnosticsEnabled() && s_dropTerrainWideTriLogCount < 96)
			{
				char text[768];
				_snprintf(text, sizeof(text) - 1, "gpu probe drop terrain wide tri count=%d shader=%s textureTag=0x%08x alpha=%d depthTest=%d depthWrite=%d bounds=%.1f,%.1f..%.1f,%.1f span=%.1f,%.1f w=%.4f..%.4f uv=%.3f,%.3f..%.3f,%.3f",
					s_dropTerrainWideTriLogCount + 1,
					ms_activeStaticShaderNamePtr,
					static_cast<unsigned int>(ms_activeTextureTag),
					alphaBlend ? 1 : 0,
					ms_depthEnabled ? 1 : 0,
					ms_depthWriteEnabled ? 1 : 0,
					minX,
					minY,
					maxX,
					maxY,
					spanX,
					spanY,
					minW,
					maxW,
					minU,
					minV,
					maxU,
					maxV);
				text[sizeof(text) - 1] = 0;
				logLine(text);
				++s_dropTerrainWideTriLogCount;
			}
			return;
		}
		static int s_terrainBottomTriangleLogCount = 0;
		if (gpuBatchDiagnosticsEnabled() &&
			s_terrainBottomTriangleLogCount < 96 &&
			ms_activeStaticShaderNamePtr &&
			strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend0") &&
			maxY >= static_cast<float>(diagnosticHeight) - 1.0f &&
			maxX >= static_cast<float>(diagnosticWidth) * 0.85f)
		{
			char text[1024];
			_snprintf(text, sizeof(text) - 1, "terrain bottom tri shader=%s tex=0x%08x alpha=%d depth=%d/%d xy=(%.1f,%.1f)(%.1f,%.1f)(%.1f,%.1f) z=%.5f,%.5f,%.5f w=%.4f,%.4f,%.4f clip=(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f) uv=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f) bounds=%.1f,%.1f..%.1f,%.1f span=%.1f,%.1f",
				ms_activeStaticShaderNamePtr,
				static_cast<unsigned int>(ms_activeTextureTag),
				alphaBlend ? 1 : 0,
				ms_depthEnabled ? 1 : 0,
				ms_depthWriteEnabled ? 1 : 0,
				a.x, a.y, b.x, b.y, c.x, c.y,
				a.z, b.z, c.z,
				a.w, b.w, c.w,
				a.clipX, a.clipY, a.clipZ, a.clipW,
				b.clipX, b.clipY, b.clipZ, b.clipW,
				c.clipX, c.clipY, c.clipZ, c.clipW,
				a.u, a.v, b.u, b.v, c.u, c.v,
				minX, minY, maxX, maxY, spanX, spanY);
			text[sizeof(text) - 1] = 0;
			logLine(text);
			++s_terrainBottomTriangleLogCount;
		}
		bool const terrainBlendShader =
			ms_activeStaticShaderNamePtr &&
			strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend");
		bool const skyboxShader =
			ms_activeStaticShaderNamePtr &&
			(strstr(ms_activeStaticShaderNamePtr, "shader/skybox") ||
			strstr(ms_activeStaticShaderNamePtr, "shader/moon_"));
		if (gpuDropTerrainSkyTriangleEnabled() &&
			applyCull &&
			terrainBlendShader &&
			minY < -2.0f &&
			maxY < static_cast<float>(diagnosticHeight) * 0.38f &&
			spanX > static_cast<float>(diagnosticWidth) * 0.12f &&
			spanY > static_cast<float>(diagnosticHeight) * 0.035f &&
			minW > 128.0f)
		{
			++ms_gpuTrianglePathologicalDropped;
			static int s_dropTerrainSkyTriLogCount = 0;
			if (gpuBatchDiagnosticsEnabled() && s_dropTerrainSkyTriLogCount < 96)
			{
				char text[768];
				_snprintf(text, sizeof(text) - 1, "gpu drop terrain sky tri count=%d shader=%s textureTag=0x%08x alpha=%d depthTest=%d depthWrite=%d bounds=%.1f,%.1f..%.1f,%.1f span=%.1f,%.1f w=%.4f..%.4f uv=%.3f,%.3f..%.3f,%.3f",
					s_dropTerrainSkyTriLogCount + 1,
					ms_activeStaticShaderNamePtr,
					static_cast<unsigned int>(ms_activeTextureTag),
					alphaBlend ? 1 : 0,
					ms_depthEnabled ? 1 : 0,
					ms_depthWriteEnabled ? 1 : 0,
					minX,
					minY,
					maxX,
					maxY,
					spanX,
					spanY,
					minW,
					maxW,
					minU,
					minV,
					maxU,
					maxV);
				text[sizeof(text) - 1] = 0;
				logLine(text);
				++s_dropTerrainSkyTriLogCount;
			}
			return;
		}
		bool const pathologicalUv =
			!terrainBlendShader &&
			!skyboxShader &&
			(maxAbsUv > 8192.0f ||
			 spanU > 8192.0f ||
			 spanV > 8192.0f);
		float const diagnosticWidthF = static_cast<float>(diagnosticWidth);
		float const diagnosticHeightF = static_cast<float>(diagnosticHeight);
		float const worldSpanMultiplier = skyboxShader ? 32.0f : 8.0f;
		float const worldMinMultiplier = skyboxShader ? 32.0f : 4.0f;
		float const worldMaxMultiplier = skyboxShader ? 33.0f : 5.0f;
		float const spanLimitX = diagnosticWidthF * worldSpanMultiplier;
		float const spanLimitY = diagnosticHeightF * worldSpanMultiplier;
		float const minLimitX = -diagnosticWidthF * worldMinMultiplier;
		float const maxLimitX = diagnosticWidthF * worldMaxMultiplier;
		float const minLimitY = -diagnosticHeightF * worldMinMultiplier;
		float const maxLimitY = diagnosticHeightF * worldMaxMultiplier;
		bool const pathologicalValues =
			!_finite(minX) || !_finite(minY) || !_finite(maxX) || !_finite(maxY) || !_finite(minW) || !_finite(maxW) ||
			!_finite(minU) || !_finite(minV) || !_finite(maxU) || !_finite(maxV) ||
			minW <= 0.00001f ||
			maxW > 1000000.0f ||
			pathologicalUv;
		bool const pathologicalScreenBounds =
			!terrainBlendShader &&
			(spanX > spanLimitX ||
			 spanY > spanLimitY ||
			 minX < minLimitX ||
			 maxX > maxLimitX ||
			 minY < minLimitY ||
			 maxY > maxLimitY);
		bool const pathologicalTriangle =
			applyCull &&
			(pathologicalValues || pathologicalScreenBounds);
		if (pathologicalTriangle)
		{
			++ms_gpuTrianglePathologicalDropped;
			if (gpuBatchDiagnosticsEnabled() && ms_gpuTrianglePathologicalDropped <= 64)
			{
				char text[512];
				_snprintf(text, sizeof(text) - 1, "gpu pathological clipped triangle dropped count=%u shader=%s textureTag=0x%08x alpha=%d depthTest=%d depthWrite=%d bounds=%.1f,%.1f..%.1f,%.1f span=%.1f,%.1f w=%.4f..%.4f uv=%.3f,%.3f..%.3f,%.3f",
					ms_gpuTrianglePathologicalDropped,
					ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
					static_cast<unsigned int>(ms_activeTextureTag),
					alphaBlend ? 1 : 0,
					ms_depthEnabled ? 1 : 0,
					ms_depthWriteEnabled ? 1 : 0,
					minX,
					minY,
					maxX,
					maxY,
					spanX,
					spanY,
					minW,
					maxW,
					minU,
					minV,
					maxU,
					maxV);
				text[sizeof(text) - 1] = 0;
				logLine(text);
			}
			return;
		}
		if (ms_gpuTriangleVertexCount + 3 > ms_gpuTriangleVertexCapacity)
		{
			++ms_gpuTriangleDropped;
			return;
		}
		uint32_t const firstVertex = ms_gpuTriangleVertexCount;
		uint32_t const firstIndex = ms_gpuTriangleIndexCount;
		ms_gpuTriangleVertexMapped[ms_gpuTriangleVertexCount++] = makeGpuScreenVertex(a);
		ms_gpuTriangleVertexMapped[ms_gpuTriangleVertexCount++] = makeGpuScreenVertex(b);
		ms_gpuTriangleVertexMapped[ms_gpuTriangleVertexCount++] = makeGpuScreenVertex(c);
		ms_gpuTriangleIndexMapped[ms_gpuTriangleIndexCount++] = firstVertex;
		ms_gpuTriangleIndexMapped[ms_gpuTriangleIndexCount++] = firstVertex + 1;
		ms_gpuTriangleIndexMapped[ms_gpuTriangleIndexCount++] = firstVertex + 2;
		if (actorBody)
		{
			++ms_actorGpuTrianglesStaged;
			includeActorGpuBounds(minX, minY, maxX, maxY);
		}
		bool const suspiciousTriangle = spanX > static_cast<float>(diagnosticWidth) * 0.85f || spanY > static_cast<float>(diagnosticHeight) * 0.85f || minW <= 0.00001f || maxW > 100000.0f;
		bool const alphaMaskTexture = activeFixedFunctionAlphaMaskTexture(alphaBlend);
		float colorSum[4];
		colorSum[0] = static_cast<float>(((a.color >> 16) & 0xff) + ((b.color >> 16) & 0xff) + ((c.color >> 16) & 0xff));
		colorSum[1] = static_cast<float>(((a.color >> 8) & 0xff) + ((b.color >> 8) & 0xff) + ((c.color >> 8) & 0xff));
		colorSum[2] = static_cast<float>((a.color & 0xff) + (b.color & 0xff) + (c.color & 0xff));
		colorSum[3] = static_cast<float>(((a.color >> 24) & 0xff) + ((b.color >> 24) & 0xff) + ((c.color >> 24) & 0xff));
		float textureSampleSum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		unsigned textureSampleCount = 0;
		if (ms_activeTextureData)
		{
			float const sampleU = (a.u + b.u + c.u) * (1.0f / 3.0f);
			float const sampleV = (a.v + b.v + c.v) * (1.0f / 3.0f);
			uint32 const sampled = ms_activeTextureData->sample(sampleU, sampleV, true);
			textureSampleSum[0] = static_cast<float>((sampled >> 16) & 0xff);
			textureSampleSum[1] = static_cast<float>((sampled >> 8) & 0xff);
			textureSampleSum[2] = static_cast<float>(sampled & 0xff);
			textureSampleSum[3] = static_cast<float>((sampled >> 24) & 0xff);
			textureSampleCount = 1;
		}
		bool const activeDepthTest = (applyCull || preserveDepthState) && ms_depthEnabled;
		bool const depthTest = activeDepthTest;
		bool const depthWrite = depthTest && ms_depthWriteEnabled;
		ShaderImplementation::Pass::BlendOperation const effectiveBlendOperation = alphaBlend ? ms_shaderAlphaBlendOperation : ShaderImplementation::Pass::BO_Add;
		ShaderImplementation::Pass::Blend const effectiveBlendSource = alphaBlend ? ms_shaderAlphaBlendSource : ShaderImplementation::Pass::B_SourceAlpha;
		ShaderImplementation::Pass::Blend const effectiveBlendDestination = alphaBlend ? ms_shaderAlphaBlendDestination : ShaderImplementation::Pass::B_InverseSourceAlpha;
		bool const actorAuxiliary = actorBody && ms_activeTextureData != 0 && activeActorVisibleAuxPass();
		VulkanTextureData const *effectiveTextureData = ms_activeTextureData;
		Tag const effectiveTextureTag = ms_activeTextureTag;
		VulkanTextureData const *effectiveSecondaryTextureData = ms_activeSecondaryTextureData;
		Tag const effectiveSecondaryTextureTag = ms_activeSecondaryTextureTag;
		bool effectiveScissorEnabled = false;
		int effectiveScissorX = 0;
		int effectiveScissorY = 0;
		int effectiveScissorWidth = 0;
		int effectiveScissorHeight = 0;
		getCurrentScissorRect(effectiveScissorEnabled, effectiveScissorX, effectiveScissorY, effectiveScissorWidth, effectiveScissorHeight);
		if (effectiveScissorWidth <= 0 || effectiveScissorHeight <= 0)
			return;
		bool const effectiveTerrainAlphaStage = ms_activeTerrainAlphaStage;
		bool const foliageAlphaFallback = activeWorldFoliageAlphaFallback(alphaBlend, applyCull, actorBody);
		bool const weakAlphaTestRepair = activeWorldWeakAlphaTestRepair(alphaBlend, applyCull, actorBody);
		bool const effectiveAlphaTest = ms_shaderAlphaTestEnabled || foliageAlphaFallback || weakAlphaTestRepair;
		ShaderImplementation::Pass::Compare const effectiveAlphaTestCompare = (ms_shaderAlphaTestEnabled && !weakAlphaTestRepair) ? ms_shaderAlphaTestCompare : ShaderImplementation::Pass::C_Greater;
		uint8 const effectiveAlphaTestReference = weakAlphaTestRepair ? 8 : (ms_shaderAlphaTestEnabled ? ms_shaderAlphaTestReference : 8);
		if (foliageAlphaFallback)
		{
			static int s_foliageAlphaFallbackLogCount = 0;
			if (gpuBatchDiagnosticsEnabled() && s_foliageAlphaFallbackLogCount < 96)
			{
				uint32 const centerSample = ms_activeTextureData ? ms_activeTextureData->sample(0.5f, 0.5f, true) : 0xffffffff;
				char text[640];
				_snprintf(text, sizeof(text) - 1, "gpu foliage alpha fallback count=%d shader=%s texture=%s format=%d size=%dx%d centerSample=0x%08x alphaBlend=%d shaderAlphaTest=%d depth=%d/%d bounds=%.1f,%.1f..%.1f,%.1f uv=%.3f,%.3f..%.3f,%.3f",
					s_foliageAlphaFallbackLogCount + 1,
					ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
					ms_activeTextureData ? ms_activeTextureData->getName() : "<none>",
					ms_activeTextureData ? static_cast<int>(ms_activeTextureData->getFormat()) : -1,
					ms_activeTextureData ? ms_activeTextureData->getWidth() : 0,
					ms_activeTextureData ? ms_activeTextureData->getHeight() : 0,
					static_cast<unsigned>(centerSample),
					alphaBlend ? 1 : 0,
					ms_shaderAlphaTestEnabled ? 1 : 0,
					depthTest ? 1 : 0,
					depthWrite ? 1 : 0,
					minX,
					minY,
					maxX,
					maxY,
					minU,
					minV,
					maxU,
					maxV);
				text[sizeof(text) - 1] = 0;
				logLine(text);
				++s_foliageAlphaFallbackLogCount;
			}
		}
		if (weakAlphaTestRepair)
		{
			static int s_weakAlphaTestRepairLogCount = 0;
			if (gpuBatchDiagnosticsEnabled() && s_weakAlphaTestRepairLogCount < 96)
			{
				uint32 const centerSample = ms_activeTextureData ? ms_activeTextureData->sample(0.5f, 0.5f, true) : 0xffffffff;
				char text[640];
				_snprintf(text, sizeof(text) - 1, "gpu weak alpha-test repair count=%d shader=%s texture=%s format=%d size=%dx%d centerSample=0x%08x oldCmp=%d oldRef=%u newCmp=%d newRef=%u depth=%d/%d bounds=%.1f,%.1f..%.1f,%.1f uv=%.3f,%.3f..%.3f,%.3f",
					s_weakAlphaTestRepairLogCount + 1,
					ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
					ms_activeTextureData ? ms_activeTextureData->getName() : "<none>",
					ms_activeTextureData ? static_cast<int>(ms_activeTextureData->getFormat()) : -1,
					ms_activeTextureData ? ms_activeTextureData->getWidth() : 0,
					ms_activeTextureData ? ms_activeTextureData->getHeight() : 0,
					static_cast<unsigned>(centerSample),
					static_cast<int>(ms_shaderAlphaTestCompare),
					static_cast<unsigned>(ms_shaderAlphaTestReference),
					static_cast<int>(effectiveAlphaTestCompare),
					static_cast<unsigned>(effectiveAlphaTestReference),
					depthTest ? 1 : 0,
					depthWrite ? 1 : 0,
					minX,
					minY,
					maxX,
					maxY,
					minU,
					minV,
					maxU,
					maxV);
				text[sizeof(text) - 1] = 0;
				logLine(text);
				++s_weakAlphaTestRepairLogCount;
			}
		}
		if (!ms_gpuDrawBatches.empty() &&
			(alphaBlend || !gpuOpaqueTriangleSortEnabled()) &&
			ms_gpuDrawBatches.back().textureData == effectiveTextureData &&
			ms_gpuDrawBatches.back().secondaryTextureData == effectiveSecondaryTextureData &&
			ms_gpuDrawBatches.back().textureTag == effectiveTextureTag &&
			ms_gpuDrawBatches.back().secondaryTextureTag == effectiveSecondaryTextureTag &&
			ms_gpuDrawBatches.back().shaderName == ms_activeStaticShaderNamePtr &&
			ms_gpuDrawBatches.back().alphaBlend == alphaBlend &&
			ms_gpuDrawBatches.back().alphaBlendOperation == effectiveBlendOperation &&
			ms_gpuDrawBatches.back().alphaBlendSource == effectiveBlendSource &&
			ms_gpuDrawBatches.back().alphaBlendDestination == effectiveBlendDestination &&
			ms_gpuDrawBatches.back().colorWriteMask == ms_shaderColorWriteMask &&
			ms_gpuDrawBatches.back().alphaTest == effectiveAlphaTest &&
			ms_gpuDrawBatches.back().alphaTestCompare == effectiveAlphaTestCompare &&
			ms_gpuDrawBatches.back().alphaTestReference == effectiveAlphaTestReference &&
			ms_gpuDrawBatches.back().depthTest == depthTest &&
			ms_gpuDrawBatches.back().depthWrite == depthWrite &&
			ms_gpuDrawBatches.back().actorBody == actorBody &&
			ms_gpuDrawBatches.back().actorAuxiliary == actorAuxiliary &&
			ms_gpuDrawBatches.back().alphaMaskTexture == alphaMaskTexture &&
			ms_gpuDrawBatches.back().terrainAlphaStage == effectiveTerrainAlphaStage &&
			ms_gpuDrawBatches.back().pixelProgramMode == ms_activePixelProgramMode &&
			ms_gpuDrawBatches.back().pixelProgramName == ms_activePixelProgramName &&
			ms_gpuDrawBatches.back().stageTextureCount == ms_activeStageTextureCount &&
			memcmp(ms_gpuDrawBatches.back().stageColorOperation, ms_activeStageColorOperation, sizeof(ms_activeStageColorOperation)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageAlphaOperation, ms_activeStageAlphaOperation, sizeof(ms_activeStageAlphaOperation)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageColorArgument0, ms_activeStageColorArgument0, sizeof(ms_activeStageColorArgument0)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageColorArgument1, ms_activeStageColorArgument1, sizeof(ms_activeStageColorArgument1)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageColorArgument2, ms_activeStageColorArgument2, sizeof(ms_activeStageColorArgument2)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageAlphaArgument0, ms_activeStageAlphaArgument0, sizeof(ms_activeStageAlphaArgument0)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageAlphaArgument1, ms_activeStageAlphaArgument1, sizeof(ms_activeStageAlphaArgument1)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageAlphaArgument2, ms_activeStageAlphaArgument2, sizeof(ms_activeStageAlphaArgument2)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageResultArgument, ms_activeStageResultArgument, sizeof(ms_activeStageResultArgument)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageColorArgument0Complement, ms_activeStageColorArgument0Complement, sizeof(ms_activeStageColorArgument0Complement)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageColorArgument1Complement, ms_activeStageColorArgument1Complement, sizeof(ms_activeStageColorArgument1Complement)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageColorArgument2Complement, ms_activeStageColorArgument2Complement, sizeof(ms_activeStageColorArgument2Complement)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageAlphaArgument0Complement, ms_activeStageAlphaArgument0Complement, sizeof(ms_activeStageAlphaArgument0Complement)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageAlphaArgument1Complement, ms_activeStageAlphaArgument1Complement, sizeof(ms_activeStageAlphaArgument1Complement)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageAlphaArgument2Complement, ms_activeStageAlphaArgument2Complement, sizeof(ms_activeStageAlphaArgument2Complement)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageColorArgument0AlphaReplicate, ms_activeStageColorArgument0AlphaReplicate, sizeof(ms_activeStageColorArgument0AlphaReplicate)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageColorArgument1AlphaReplicate, ms_activeStageColorArgument1AlphaReplicate, sizeof(ms_activeStageColorArgument1AlphaReplicate)) == 0 &&
			memcmp(ms_gpuDrawBatches.back().stageColorArgument2AlphaReplicate, ms_activeStageColorArgument2AlphaReplicate, sizeof(ms_activeStageColorArgument2AlphaReplicate)) == 0 &&
			ms_gpuDrawBatches.back().textureFactorValid == ms_activeTextureFactorValid &&
			(!ms_activeTextureFactorValid ||
				(memcmp(ms_gpuDrawBatches.back().textureFactor, ms_activeTextureFactor, sizeof(ms_activeTextureFactor)) == 0 &&
				memcmp(ms_gpuDrawBatches.back().textureFactor2, ms_activeTextureFactor2, sizeof(ms_activeTextureFactor2)) == 0)) &&
			ms_gpuDrawBatches.back().lightingEnabled == ms_activeLightingEnabled &&
			ms_gpuDrawBatches.back().lightingSpecularEnabled == ms_activeLightingSpecularEnabled &&
			ms_gpuDrawBatches.back().clampSampler == ms_activeClampSampler &&
			ms_gpuDrawBatches.back().textureCube == ms_activeTextureCube &&
			ms_gpuDrawBatches.back().scissorEnabled == effectiveScissorEnabled &&
			ms_gpuDrawBatches.back().scissorX == effectiveScissorX &&
			ms_gpuDrawBatches.back().scissorY == effectiveScissorY &&
			ms_gpuDrawBatches.back().scissorWidth == effectiveScissorWidth &&
			ms_gpuDrawBatches.back().scissorHeight == effectiveScissorHeight &&
			ms_gpuDrawBatches.back().textureCoordinateSet == ms_activeTextureCoordinateSet &&
			ms_gpuDrawBatches.back().secondaryTextureCoordinateSet == ms_activeSecondaryTextureCoordinateSet &&
			ms_gpuDrawBatches.back().firstVertex + ms_gpuDrawBatches.back().vertexCount == firstIndex)
		{
			GpuDrawBatch &batch = ms_gpuDrawBatches.back();
			ms_gpuDrawBatches.back().vertexCount += 3;
			if (triangleDepth > batch.sortDepth)
				batch.sortDepth = triangleDepth;
			batch.minX = std::min(batch.minX, minX);
			batch.minY = std::min(batch.minY, minY);
			batch.maxX = std::max(batch.maxX, maxX);
			batch.maxY = std::max(batch.maxY, maxY);
			batch.minW = std::min(batch.minW, minW);
			batch.maxW = std::max(batch.maxW, maxW);
			batch.minU = std::min(batch.minU, minU);
			batch.minV = std::min(batch.minV, minV);
			batch.maxU = std::max(batch.maxU, maxU);
			batch.maxV = std::max(batch.maxV, maxV);
			batch.maxTriangleSpanX = std::max(batch.maxTriangleSpanX, spanX);
			batch.maxTriangleSpanY = std::max(batch.maxTriangleSpanY, spanY);
			if (suspiciousTriangle)
				++batch.suspiciousTriangles;
			for (int i = 0; i < 4; ++i)
			{
				batch.colorSum[i] += colorSum[i];
				batch.textureSampleSum[i] += textureSampleSum[i];
			}
			batch.textureSampleCount += textureSampleCount;
		}
		else
		{
			GpuDrawBatch batch;
			batch.textureData = effectiveTextureData;
			batch.secondaryTextureData = effectiveSecondaryTextureData;
			batch.textureTag = effectiveTextureTag;
			batch.secondaryTextureTag = effectiveSecondaryTextureTag;
			batch.shaderName = ms_activeStaticShaderNamePtr;
			batch.alphaBlend = alphaBlend;
			batch.alphaBlendOperation = effectiveBlendOperation;
			batch.alphaBlendSource = effectiveBlendSource;
			batch.alphaBlendDestination = effectiveBlendDestination;
			batch.colorWriteMask = ms_shaderColorWriteMask;
			batch.alphaTest = effectiveAlphaTest;
			batch.alphaTestCompare = effectiveAlphaTestCompare;
			batch.alphaTestReference = effectiveAlphaTestReference;
			batch.depthTest = depthTest;
			batch.depthWrite = depthWrite;
			batch.worldGeometry = applyCull;
			batch.actorBody = actorBody;
			batch.actorAuxiliary = actorAuxiliary;
			batch.alphaMaskTexture = alphaMaskTexture;
			batch.terrainAlphaStage = effectiveTerrainAlphaStage;
			batch.textureFactorValid = ms_activeTextureFactorValid;
			memcpy(batch.textureFactor, ms_activeTextureFactor, sizeof(batch.textureFactor));
			memcpy(batch.textureFactor2, ms_activeTextureFactor2, sizeof(batch.textureFactor2));
			batch.lightingEnabled = ms_activeLightingEnabled;
			batch.lightingSpecularEnabled = ms_activeLightingSpecularEnabled;
			batch.clampSampler = ms_activeClampSampler;
			batch.textureCube = ms_activeTextureCube;
			batch.scissorEnabled = effectiveScissorEnabled;
			batch.scissorX = effectiveScissorX;
			batch.scissorY = effectiveScissorY;
			batch.scissorWidth = effectiveScissorWidth;
			batch.scissorHeight = effectiveScissorHeight;
			batch.pixelProgramMode = ms_activePixelProgramMode;
			batch.pixelProgramName = ms_activePixelProgramName;
			batch.textureCoordinateSet = ms_activeTextureCoordinateSet;
			batch.secondaryTextureCoordinateSet = ms_activeSecondaryTextureCoordinateSet;
			batch.stageTextureCount = ms_activeStageTextureCount;
			memcpy(batch.stageColorOperation, ms_activeStageColorOperation, sizeof(batch.stageColorOperation));
			memcpy(batch.stageAlphaOperation, ms_activeStageAlphaOperation, sizeof(batch.stageAlphaOperation));
			memcpy(batch.stageColorArgument0, ms_activeStageColorArgument0, sizeof(batch.stageColorArgument0));
			memcpy(batch.stageColorArgument1, ms_activeStageColorArgument1, sizeof(batch.stageColorArgument1));
			memcpy(batch.stageColorArgument2, ms_activeStageColorArgument2, sizeof(batch.stageColorArgument2));
			memcpy(batch.stageAlphaArgument0, ms_activeStageAlphaArgument0, sizeof(batch.stageAlphaArgument0));
			memcpy(batch.stageAlphaArgument1, ms_activeStageAlphaArgument1, sizeof(batch.stageAlphaArgument1));
			memcpy(batch.stageAlphaArgument2, ms_activeStageAlphaArgument2, sizeof(batch.stageAlphaArgument2));
			memcpy(batch.stageResultArgument, ms_activeStageResultArgument, sizeof(batch.stageResultArgument));
			memcpy(batch.stageColorArgument0Complement, ms_activeStageColorArgument0Complement, sizeof(batch.stageColorArgument0Complement));
			memcpy(batch.stageColorArgument1Complement, ms_activeStageColorArgument1Complement, sizeof(batch.stageColorArgument1Complement));
			memcpy(batch.stageColorArgument2Complement, ms_activeStageColorArgument2Complement, sizeof(batch.stageColorArgument2Complement));
			memcpy(batch.stageAlphaArgument0Complement, ms_activeStageAlphaArgument0Complement, sizeof(batch.stageAlphaArgument0Complement));
			memcpy(batch.stageAlphaArgument1Complement, ms_activeStageAlphaArgument1Complement, sizeof(batch.stageAlphaArgument1Complement));
			memcpy(batch.stageAlphaArgument2Complement, ms_activeStageAlphaArgument2Complement, sizeof(batch.stageAlphaArgument2Complement));
			memcpy(batch.stageColorArgument0AlphaReplicate, ms_activeStageColorArgument0AlphaReplicate, sizeof(batch.stageColorArgument0AlphaReplicate));
			memcpy(batch.stageColorArgument1AlphaReplicate, ms_activeStageColorArgument1AlphaReplicate, sizeof(batch.stageColorArgument1AlphaReplicate));
			memcpy(batch.stageColorArgument2AlphaReplicate, ms_activeStageColorArgument2AlphaReplicate, sizeof(batch.stageColorArgument2AlphaReplicate));
			batch.firstVertex = firstIndex;
			batch.vertexCount = 3;
			batch.sortDepth = triangleDepth;
			batch.minX = minX;
			batch.minY = minY;
			batch.maxX = maxX;
			batch.maxY = maxY;
			batch.minW = minW;
			batch.maxW = maxW;
			batch.minU = minU;
			batch.minV = minV;
			batch.maxU = maxU;
			batch.maxV = maxV;
			batch.maxTriangleSpanX = spanX;
			batch.maxTriangleSpanY = spanY;
			batch.suspiciousTriangles = suspiciousTriangle ? 1 : 0;
			for (int i = 0; i < 4; ++i)
			{
				batch.colorSum[i] = colorSum[i];
				batch.textureSampleSum[i] = textureSampleSum[i];
			}
			batch.textureSampleCount = textureSampleCount;
			ms_gpuDrawBatches.push_back(batch);
		}
	}

	void stageGpuWorldTriangleClipped(ScreenVertex const &a, ScreenVertex const &b, ScreenVertex const &c, bool alphaBlend, bool actorBody)
	{
		if (!a.valid || !b.valid || !c.valid)
			return;

		ScreenVertex polygonA[8];
		ScreenVertex polygonB[8];
		ScreenVertex *polygon = polygonA;
		ScreenVertex *scratch = polygonB;
		int polygonCount = 3;
		polygon[0] = a;
		polygon[1] = b;
		polygon[2] = c;

		GpuClipPlane const planes[] =
		{
			GCP_left,
			GCP_right,
			GCP_bottom,
			GCP_top,
			GCP_near,
			GCP_far
		};
		for (int planeIndex = 0; planeIndex < static_cast<int>(sizeof(planes) / sizeof(planes[0])); ++planeIndex)
		{
			polygonCount = clipGpuPolygonAgainstPlane(polygon, polygonCount, scratch, planes[planeIndex]);
			if (polygonCount < 3)
				return;
			std::swap(polygon, scratch);
		}

		for (int i = 0; i < polygonCount; ++i)
		{
			if (!projectClippedGpuVertex(polygon[i]))
				return;
		}

		for (int i = 1; i + 1 < polygonCount; ++i)
			stageGpuTriangle(polygon[0], polygon[i], polygon[i + 1], alphaBlend, true, actorBody);
	}

	bool isFiniteScreenVertex(ScreenVertex const &vertex)
	{
		return _finite(vertex.x) &&
			_finite(vertex.y) &&
			_finite(vertex.z) &&
			_finite(vertex.w) &&
			_finite(vertex.clipX) &&
			_finite(vertex.clipY) &&
			_finite(vertex.clipZ) &&
			_finite(vertex.clipW) &&
			_finite(vertex.u) &&
			_finite(vertex.v);
	}

	bool isCorruptTerrainVertex(ScreenVertex const &vertex)
	{
		if (!isFiniteScreenVertex(vertex))
			return true;
		float const maxClipMagnitude = 1000000.0f;
		float const maxUvMagnitude = 8192.0f;
		return fabsf(vertex.clipX) > maxClipMagnitude ||
			fabsf(vertex.clipY) > maxClipMagnitude ||
			fabsf(vertex.clipZ) > maxClipMagnitude ||
			fabsf(vertex.clipW) > maxClipMagnitude ||
			fabsf(vertex.u) > maxUvMagnitude ||
			fabsf(vertex.v) > maxUvMagnitude;
	}

	bool projectObjectVertex(ScreenVertex &vertex)
	{
		if (!_finite(vertex.objectX) || !_finite(vertex.objectY) || !_finite(vertex.objectZ))
			return false;
		Matrix4x4 const objectToCamera = multiply(ms_objectToWorldMatrix, ms_worldToCameraMatrix);
		Matrix4x4 const objectToProjection = multiply(objectToCamera, ms_projectionMatrix);
		float const px = vertex.objectX;
		float const py = vertex.objectY;
		float const pz = vertex.objectZ;
		float const cameraX = px * objectToCamera.m[0][0] + py * objectToCamera.m[1][0] + pz * objectToCamera.m[2][0] + objectToCamera.m[3][0];
		float const cameraY = px * objectToCamera.m[0][1] + py * objectToCamera.m[1][1] + pz * objectToCamera.m[2][1] + objectToCamera.m[3][1];
		float const cameraZ = px * objectToCamera.m[0][2] + py * objectToCamera.m[1][2] + pz * objectToCamera.m[2][2] + objectToCamera.m[3][2];
		float const fogDistance = sqrtf(cameraX * cameraX + cameraY * cameraY + cameraZ * cameraZ);
		float const cx = px * objectToProjection.m[0][0] + py * objectToProjection.m[1][0] + pz * objectToProjection.m[2][0] + objectToProjection.m[3][0];
		float const cy = px * objectToProjection.m[0][1] + py * objectToProjection.m[1][1] + pz * objectToProjection.m[2][1] + objectToProjection.m[3][1];
		float const cz = px * objectToProjection.m[0][2] + py * objectToProjection.m[1][2] + pz * objectToProjection.m[2][2] + objectToProjection.m[3][2];
		float const cw = px * objectToProjection.m[0][3] + py * objectToProjection.m[1][3] + pz * objectToProjection.m[2][3] + objectToProjection.m[3][3];
		if (!_finite(cx) || !_finite(cy) || !_finite(cz) || !_finite(cw) || cw <= 0.00001f)
			return false;
		float const invW = 1.0f / cw;
		float const ndcX = cx * invW;
		float const ndcY = cy * invW;
		float const ndcZ = cz * invW;
		if (!_finite(ndcX) || !_finite(ndcY) || !_finite(ndcZ) || ndcZ < 0.0f || ndcZ > 1.0f)
			return false;
		int const viewportWidth = ms_viewportWidth > 0 ? ms_viewportWidth : static_cast<int>(ms_swapchainExtent.width);
		int const viewportHeight = ms_viewportHeight > 0 ? ms_viewportHeight : static_cast<int>(ms_swapchainExtent.height);
		vertex.clipX = cx;
		vertex.clipY = cy;
		vertex.clipZ = cz;
		vertex.clipW = cw;
		vertex.x = static_cast<float>(ms_viewportX) + ((ndcX + 1.0f) * 0.5f * static_cast<float>(viewportWidth));
		vertex.y = static_cast<float>(ms_viewportY) + ((1.0f - ndcY) * 0.5f * static_cast<float>(viewportHeight));
		vertex.z = std::max(0.0f, std::min(1.0f, ndcZ));
		vertex.w = cw;
		vertex.color = applyFogColor(vertex.color, fogDistance);
		vertex.valid = true;
		return true;
	}

	bool repairCorruptTerrainFanCenterFromLocalRing(ScreenVertex &bad, VertexBufferFormat const &format, int centerIndex)
	{
		if (centerIndex < 0 || centerIndex + 3 >= ms_activeVertexCount)
			return false;

		std::vector<ScreenVertex> ring;
		ring.reserve(16);
		int const maxRingVertex = std::min(ms_activeVertexCount, centerIndex + 17);
		for (int i = centerIndex + 1; i < maxRingVertex; ++i)
		{
			ScreenVertex vertex;
			IGNORE_RETURN(readGpuVertexForActiveFormat(i, format, vertex));
			if (!_finite(vertex.objectX) || !_finite(vertex.objectY) || !_finite(vertex.objectZ))
				continue;
			float const maxObjectMagnitude = 1000000.0f;
			if (fabsf(vertex.objectX) > maxObjectMagnitude || fabsf(vertex.objectY) > maxObjectMagnitude || fabsf(vertex.objectZ) > maxObjectMagnitude)
				continue;
			if (!_finite(vertex.u) || !_finite(vertex.v) || fabsf(vertex.u) > 8192.0f || fabsf(vertex.v) > 8192.0f)
				continue;
			ring.push_back(vertex);
			if (ring.size() >= 8)
				break;
		}
		if (ring.size() < 3)
			return false;

		ScreenVertex repaired = bad;
		repaired.valid = true;
		repaired.x = 0.0f;
		repaired.y = 0.0f;
		repaired.z = 0.0f;
		repaired.w = 0.0f;
		repaired.clipX = 0.0f;
		repaired.clipY = 0.0f;
		repaired.clipZ = 0.0f;
		repaired.clipW = 0.0f;
		repaired.objectX = 0.0f;
		repaired.objectY = 0.0f;
		repaired.objectZ = 0.0f;
		repaired.u = 0.0f;
		repaired.v = 0.0f;
		repaired.u2 = 0.0f;
		repaired.v2 = 0.0f;
		repaired.specularR = 0.0f;
		repaired.specularG = 0.0f;
		repaired.specularB = 0.0f;
		for (size_t i = 0; i < ring.size(); ++i)
		{
			repaired.objectX += ring[i].objectX;
			repaired.objectY += ring[i].objectY;
			repaired.objectZ += ring[i].objectZ;
			repaired.x += ring[i].x;
			repaired.y += ring[i].y;
			repaired.z += ring[i].z;
			repaired.w += ring[i].w;
			repaired.clipX += ring[i].clipX;
			repaired.clipY += ring[i].clipY;
			repaired.clipZ += ring[i].clipZ;
			repaired.clipW += ring[i].clipW;
			repaired.u += ring[i].u;
			repaired.v += ring[i].v;
			repaired.u2 += ring[i].u2;
			repaired.v2 += ring[i].v2;
			repaired.specularR += ring[i].specularR;
			repaired.specularG += ring[i].specularG;
			repaired.specularB += ring[i].specularB;
		}
		float const invCount = 1.0f / static_cast<float>(ring.size());
		repaired.objectX *= invCount;
		repaired.objectY *= invCount;
		repaired.objectZ *= invCount;
		repaired.x *= invCount;
		repaired.y *= invCount;
		repaired.z *= invCount;
		repaired.w *= invCount;
		repaired.clipX *= invCount;
		repaired.clipY *= invCount;
		repaired.clipZ *= invCount;
		repaired.clipW *= invCount;
		repaired.u *= invCount;
		repaired.v *= invCount;
		repaired.u2 *= invCount;
		repaired.v2 *= invCount;
		repaired.specularR *= invCount;
		repaired.specularG *= invCount;
		repaired.specularB *= invCount;
		repaired.color = averageVertexColor(ring);
		if (!isFiniteScreenVertex(repaired) || isCorruptTerrainVertex(repaired))
			return false;

		bad = repaired;
		++ms_terrainLocalFanCenterRepairs;
		static int s_repairedTerrainLocalFanCenterLogCount = 0;
		if (gpuBatchDiagnosticsEnabled() && s_repairedTerrainLocalFanCenterLogCount < 96)
		{
			char buffer[640];
			_snprintf(buffer, sizeof(buffer) - 1, "gpu repaired local terrain fan center count=%d shader=%s tex=0x%08x center=%d ring=%u object=(%.3f,%.3f,%.3f) clip=(%.3f,%.3f,%.3f,%.3f) uv=(%.3f,%.3f)",
				s_repairedTerrainLocalFanCenterLogCount + 1,
				ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
				static_cast<unsigned int>(ms_activeTextureTag),
				centerIndex,
				static_cast<unsigned>(ring.size()),
				bad.objectX,
				bad.objectY,
				bad.objectZ,
				bad.clipX,
				bad.clipY,
				bad.clipZ,
				bad.clipW,
				bad.u,
				bad.v);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_repairedTerrainLocalFanCenterLogCount;
		}
		return true;
	}

	bool repairCorruptTerrainTriangleVertex(ScreenVertex &a, ScreenVertex &b, ScreenVertex &c, VertexBufferFormat const &format, int ia, int ib, int ic)
	{
		bool const corruptA = isCorruptTerrainVertex(a);
		bool const corruptB = isCorruptTerrainVertex(b);
		bool const corruptC = isCorruptTerrainVertex(c);
		int const corruptCount = (corruptA ? 1 : 0) + (corruptB ? 1 : 0) + (corruptC ? 1 : 0);
		if (corruptCount != 1)
			return false;

		ScreenVertex &bad = corruptA ? a : (corruptB ? b : c);
		ScreenVertex const &good0 = corruptA ? b : a;
		ScreenVertex const &good1 = corruptC ? b : c;
		if (corruptA && (ia == 0 || (ia > 0 && ib == ia + 1 && ic == ia + 2)) && repairCorruptTerrainFanCenterFromLocalRing(bad, format, ia))
			return true;
		if (!good0.valid || !good1.valid || isCorruptTerrainVertex(good0) || isCorruptTerrainVertex(good1))
			return false;

		float centerU = _finite(bad.u) && fabsf(bad.u) <= 8192.0f ? bad.u : (good0.u + good1.u) * 0.5f;
		float centerV = _finite(bad.v) && fabsf(bad.v) <= 8192.0f ? bad.v : (good0.v + good1.v) * 0.5f;
		float localRadius = std::max(std::max(fabsf(good0.u - centerU), fabsf(good1.u - centerU)), std::max(fabsf(good0.v - centerV), fabsf(good1.v - centerV)));
		localRadius = std::max(1.0f, std::min(8192.0f, localRadius * 1.35f));
		if (!gpuTerrainWideRepairEnabled() && localRadius > 512.0f)
		{
			static int s_skippedWideTerrainRepairLogCount = 0;
			if (gpuBatchDiagnosticsEnabled() && s_skippedWideTerrainRepairLogCount < 96)
			{
				char buffer[512];
				_snprintf(buffer, sizeof(buffer) - 1, "gpu skip wide terrain repair count=%d shader=%s tex=0x%08x ia=%d ib=%d ic=%d bad=%c radius=%.3f centerUv=(%.3f,%.3f)",
					s_skippedWideTerrainRepairLogCount + 1,
					ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
					static_cast<unsigned int>(ms_activeTextureTag),
					ia,
					ib,
					ic,
					corruptA ? 'a' : (corruptB ? 'b' : 'c'),
					localRadius,
					centerU,
					centerV);
				buffer[sizeof(buffer) - 1] = 0;
				logLine(buffer);
				++s_skippedWideTerrainRepairLogCount;
			}
			return false;
		}

		std::vector<ScreenVertex> candidates;
		candidates.reserve(16);
		for (int i = 0; i < ms_activeVertexCount; ++i)
		{
			ScreenVertex vertex;
			if (!readGpuVertexForActiveFormat(i, format, vertex))
				continue;
			if (!vertex.valid || isCorruptTerrainVertex(vertex))
				continue;
			if (fabsf(vertex.u - centerU) > localRadius || fabsf(vertex.v - centerV) > localRadius)
				continue;
			candidates.push_back(vertex);
		}

		if (candidates.size() < 3)
		{
			if (!gpuTerrainWideRepairEnabled())
				return false;
			candidates.clear();
			candidates.push_back(good0);
			candidates.push_back(good1);
			for (int i = 0; i < ms_activeVertexCount && candidates.size() < 16; ++i)
			{
				ScreenVertex vertex;
				if (!readGpuVertexForActiveFormat(i, format, vertex))
					continue;
				if (!vertex.valid || isCorruptTerrainVertex(vertex))
					continue;
				candidates.push_back(vertex);
			}
		}
		if (candidates.size() < 3)
			return false;

		ScreenVertex repaired = bad;
		repaired.valid = true;
		repaired.x = 0.0f;
		repaired.y = 0.0f;
		repaired.z = 0.0f;
		repaired.w = 0.0f;
		repaired.clipX = 0.0f;
		repaired.clipY = 0.0f;
		repaired.clipZ = 0.0f;
		repaired.clipW = 0.0f;
		repaired.objectX = 0.0f;
		repaired.objectY = 0.0f;
		repaired.objectZ = 0.0f;
		repaired.specularR = 0.0f;
		repaired.specularG = 0.0f;
		repaired.specularB = 0.0f;
		float avgU = 0.0f;
		float avgV = 0.0f;
		float avgU2 = 0.0f;
		float avgV2 = 0.0f;
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			repaired.x += candidates[i].x;
			repaired.y += candidates[i].y;
			repaired.z += candidates[i].z;
			repaired.w += candidates[i].w;
			repaired.clipX += candidates[i].clipX;
			repaired.clipY += candidates[i].clipY;
			repaired.clipZ += candidates[i].clipZ;
			repaired.clipW += candidates[i].clipW;
			repaired.objectX += candidates[i].objectX;
			repaired.objectY += candidates[i].objectY;
			repaired.objectZ += candidates[i].objectZ;
			repaired.specularR += candidates[i].specularR;
			repaired.specularG += candidates[i].specularG;
			repaired.specularB += candidates[i].specularB;
			avgU += candidates[i].u;
			avgV += candidates[i].v;
			avgU2 += candidates[i].u2;
			avgV2 += candidates[i].v2;
		}
		float const invCount = 1.0f / static_cast<float>(candidates.size());
		repaired.x *= invCount;
		repaired.y *= invCount;
		repaired.z *= invCount;
		repaired.w *= invCount;
		repaired.clipX *= invCount;
		repaired.clipY *= invCount;
		repaired.clipZ *= invCount;
		repaired.clipW *= invCount;
		repaired.objectX *= invCount;
		repaired.objectY *= invCount;
		repaired.objectZ *= invCount;
		repaired.specularR *= invCount;
		repaired.specularG *= invCount;
		repaired.specularB *= invCount;
		repaired.u = centerU;
		repaired.v = centerV;
		if (!_finite(repaired.u) || fabsf(repaired.u) > 8192.0f)
			repaired.u = avgU * invCount;
		if (!_finite(repaired.v) || fabsf(repaired.v) > 8192.0f)
			repaired.v = avgV * invCount;
		if (!_finite(repaired.u2) || fabsf(repaired.u2) > 8192.0f)
			repaired.u2 = avgU2 * invCount;
		if (!_finite(repaired.v2) || fabsf(repaired.v2) > 8192.0f)
			repaired.v2 = avgV2 * invCount;
		repaired.color = averageVertexColor(candidates);
		if (isCorruptTerrainVertex(repaired))
			return false;

		bad = repaired;
		++ms_terrainCorruptTriangleRepairs;
		static int s_repairedCorruptTerrainTriangleLogCount = 0;
		if (gpuBatchDiagnosticsEnabled() && s_repairedCorruptTerrainTriangleLogCount < 128)
		{
			char buffer[1024];
			_snprintf(buffer, sizeof(buffer) - 1, "gpu repaired corrupt terrain tri count=%d shader=%s tex=0x%08x ia=%d ib=%d ic=%d bad=%c candidates=%u radius=%.3f repairedClip=(%.3f,%.3f,%.3f,%.3f) repairedUv=(%.3f,%.3f)",
				s_repairedCorruptTerrainTriangleLogCount + 1,
				ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
				static_cast<unsigned int>(ms_activeTextureTag),
				ia,
				ib,
				ic,
				corruptA ? 'a' : (corruptB ? 'b' : 'c'),
				static_cast<unsigned>(candidates.size()),
				localRadius,
				repaired.clipX,
				repaired.clipY,
				repaired.clipZ,
				repaired.clipW,
				repaired.u,
				repaired.v);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_repairedCorruptTerrainTriangleLogCount;
		}
		return true;
	}

	int getActiveTextureCoordinateOffset()
	{
		if (!ms_activeVertexDescriptor)
			return -1;

		int const requestedSet = ms_activeTextureCoordinateSet >= 0 && ms_activeTextureCoordinateSet < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS ? ms_activeTextureCoordinateSet : 0;
		int const requestedOffset = ms_activeVertexDescriptor->offsetTextureCoordinateSet[requestedSet];
		if (requestedOffset >= 0)
			return requestedOffset;

		for (int i = 0; i < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS; ++i)
		{
			int const fallbackOffset = ms_activeVertexDescriptor->offsetTextureCoordinateSet[i];
			if (fallbackOffset >= 0)
				return fallbackOffset;
		}

		return -1;
	}

	int getTextureCoordinateOffset(VertexBufferDescriptor const *descriptor, int textureCoordinateSet)
	{
		if (!descriptor)
			return -1;

		int const requestedSet = textureCoordinateSet >= 0 && textureCoordinateSet < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS ? textureCoordinateSet : 0;
		int const requestedOffset = descriptor->offsetTextureCoordinateSet[requestedSet];
		if (requestedOffset >= 0)
			return requestedOffset;

		for (int i = 0; i < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS; ++i)
		{
			int const fallbackOffset = descriptor->offsetTextureCoordinateSet[i];
			if (fallbackOffset >= 0)
				return fallbackOffset;
		}

		return -1;
	}

	enum VertexAttributeKind
	{
		VAK_position,
		VAK_normal,
		VAK_pointSize,
		VAK_color0,
		VAK_color1,
		VAK_texcoord
	};

	int getAttributeOffset(VertexBufferDescriptor const &descriptor, VertexAttributeKind kind, int textureCoordinateSet)
	{
		switch (kind)
		{
		case VAK_position:
			return descriptor.offsetPosition;
		case VAK_normal:
			return descriptor.offsetNormal;
		case VAK_pointSize:
			return descriptor.offsetPointSize;
		case VAK_color0:
			return descriptor.offsetColor0;
		case VAK_color1:
			return descriptor.offsetColor1;
		case VAK_texcoord:
			{
				int const requestedSet = textureCoordinateSet >= 0 && textureCoordinateSet < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS ? textureCoordinateSet : 0;
				int const requestedOffset = descriptor.offsetTextureCoordinateSet[requestedSet];
				if (requestedOffset >= 0)
					return requestedOffset;
				for (int i = 0; i < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS; ++i)
				{
					int const fallbackOffset = descriptor.offsetTextureCoordinateSet[i];
					if (fallbackOffset >= 0)
						return fallbackOffset;
				}
			}
			break;
		}
		return -1;
	}

	byte const *getActiveVertexBytesForAttribute(int vertexIndex, VertexAttributeKind kind, int textureCoordinateSet, int &offsetOut, VertexBufferDescriptor const *&descriptorOut)
	{
		offsetOut = -1;
		descriptorOut = 0;
		if (vertexIndex < 0)
			return 0;

		if (ms_activeVertexVector)
		{
			for (int stream = 0; stream < ms_activeVertexStreamCount && stream < 2; ++stream)
			{
				VertexBufferDescriptor const *descriptor = ms_activeVertexStreamDescriptors[stream];
				byte const *data = ms_activeVertexStreamData[stream];
				int const stride = ms_activeVertexStreamStrides[stream];
				if (!descriptor || !data || stride <= 0 || vertexIndex >= ms_activeVertexStreamCounts[stream])
					continue;
				int const offset = getAttributeOffset(*descriptor, kind, textureCoordinateSet);
				if (offset < 0)
					continue;
				offsetOut = offset;
				descriptorOut = descriptor;
				return data + static_cast<size_t>(vertexIndex) * static_cast<size_t>(stride);
			}
			return 0;
		}

		if (!ms_activeVertexData || !ms_activeVertexDescriptor || ms_activeVertexStride <= 0 || vertexIndex >= ms_activeVertexCount)
			return 0;
		int const offset = getAttributeOffset(*ms_activeVertexDescriptor, kind, textureCoordinateSet);
		if (offset < 0)
			return 0;
		offsetOut = offset;
		descriptorOut = ms_activeVertexDescriptor;
		return ms_activeVertexData + static_cast<size_t>(vertexIndex) * static_cast<size_t>(ms_activeVertexStride);
	}

	bool activeVertexAttributeAvailable(VertexAttributeKind kind, int textureCoordinateSet = 0)
	{
		int offset = -1;
		VertexBufferDescriptor const *descriptor = 0;
		return getActiveVertexBytesForAttribute(0, kind, textureCoordinateSet, offset, descriptor) != 0;
	}

	bool transformVertex(byte const *vertex, ScreenVertex &out)
	{
		out.valid = false;
		out.x = out.y = out.z = out.w = 0.0f;
		out.clipX = out.clipY = out.clipZ = out.clipW = 0.0f;
		out.objectX = out.objectY = out.objectZ = 0.0f;
		out.u = out.v = 0.0f;
		out.u2 = out.v2 = 0.0f;
		out.specularR = out.specularG = out.specularB = 0.0f;
		out.color = 0xffffffff;

		if (!vertex || !ms_activeVertexDescriptor || ms_activeVertexDescriptor->offsetPosition < 0)
			return false;

		float const px = readFloat(vertex, ms_activeVertexDescriptor->offsetPosition, 0.0f);
		float const py = readFloat(vertex, ms_activeVertexDescriptor->offsetPosition + static_cast<int>(sizeof(float)), 0.0f);
		float const pz = readFloat(vertex, ms_activeVertexDescriptor->offsetPosition + static_cast<int>(sizeof(float)) * 2, 0.0f);
		out.objectX = px;
		out.objectY = py;
		out.objectZ = pz;
		Matrix4x4 const objectToCamera = multiply(ms_objectToWorldMatrix, ms_worldToCameraMatrix);
		Matrix4x4 const objectToProjection = multiply(objectToCamera, ms_projectionMatrix);
		float const cameraX = px * objectToCamera.m[0][0] + py * objectToCamera.m[1][0] + pz * objectToCamera.m[2][0] + objectToCamera.m[3][0];
		float const cameraY = px * objectToCamera.m[0][1] + py * objectToCamera.m[1][1] + pz * objectToCamera.m[2][1] + objectToCamera.m[3][1];
		float const cameraZ = px * objectToCamera.m[0][2] + py * objectToCamera.m[1][2] + pz * objectToCamera.m[2][2] + objectToCamera.m[3][2];
		float const fogDistance = sqrtf(cameraX * cameraX + cameraY * cameraY + cameraZ * cameraZ);
		float const cx = px * objectToProjection.m[0][0] + py * objectToProjection.m[1][0] + pz * objectToProjection.m[2][0] + objectToProjection.m[3][0];
		float const cy = px * objectToProjection.m[0][1] + py * objectToProjection.m[1][1] + pz * objectToProjection.m[2][1] + objectToProjection.m[3][1];
		float const cz = px * objectToProjection.m[0][2] + py * objectToProjection.m[1][2] + pz * objectToProjection.m[2][2] + objectToProjection.m[3][2];
		float const cw = px * objectToProjection.m[0][3] + py * objectToProjection.m[1][3] + pz * objectToProjection.m[2][3] + objectToProjection.m[3][3];
		out.clipX = cx;
		out.clipY = cy;
		out.clipZ = cz;
		out.clipW = cw;
		if (gpuPresentEnabled())
		{
			if (!_finite(cx) || !_finite(cy) || !_finite(cz) || !_finite(cw))
				return false;

			if (gpuWorldClipEnabled())
			{
				out.x = 0.0f;
				out.y = 0.0f;
				out.z = 0.0f;
				out.w = cw;
			}
			else
			{
				if (cw <= 0.00001f)
					return false;

				float const invW = 1.0f / cw;
				float const ndcX = cx * invW;
				float const ndcY = cy * invW;
				float const ndcZ = cz / cw;
				if (ndcZ < 0.0f || ndcZ > 1.0f)
					return false;

				int const viewportWidth = ms_viewportWidth > 0 ? ms_viewportWidth : static_cast<int>(ms_swapchainExtent.width);
				int const viewportHeight = ms_viewportHeight > 0 ? ms_viewportHeight : static_cast<int>(ms_swapchainExtent.height);
				out.x = static_cast<float>(ms_viewportX) + ((ndcX + 1.0f) * 0.5f * static_cast<float>(viewportWidth));
				out.y = static_cast<float>(ms_viewportY) + ((1.0f - ndcY) * 0.5f * static_cast<float>(viewportHeight));
				out.z = std::max(0.0f, std::min(1.0f, ndcZ));
				out.w = cw;
			}
		}
		else
		{
			if (cw <= 0.00001f)
				return false;

			float const invW = 1.0f / cw;
			float const ndcX = cx * invW;
			float const ndcY = cy * invW;
			float const ndcZ = cz * invW;
			if (ndcZ < 0.0f || ndcZ > 1.0f)
				return false;

			int const viewportWidth = ms_viewportWidth > 0 ? ms_viewportWidth : static_cast<int>(ms_swapchainExtent.width);
			int const viewportHeight = ms_viewportHeight > 0 ? ms_viewportHeight : static_cast<int>(ms_swapchainExtent.height);
			out.x = static_cast<float>(ms_viewportX) + ((ndcX + 1.0f) * 0.5f * static_cast<float>(viewportWidth));
			out.y = static_cast<float>(ms_viewportY) + ((1.0f - ndcY) * 0.5f * static_cast<float>(viewportHeight));
			out.z = std::max(0.0f, std::min(1.0f, ndcZ));
			out.w = cw;
		}

		int const uvOffset = getActiveTextureCoordinateOffset();
		if (uvOffset >= 0)
		{
			out.u = readFloat(vertex, uvOffset, 0.0f);
			out.v = readFloat(vertex, uvOffset + static_cast<int>(sizeof(float)), 0.0f);
			applyActiveTextureTransform(out.u, out.v);
			out.u2 = out.u;
			out.v2 = out.v;
		}
		if (ms_activeSecondaryTextureData)
		{
			int const uv2Offset = getTextureCoordinateOffset(ms_activeVertexDescriptor, ms_activeSecondaryTextureCoordinateSet);
			if (uv2Offset >= 0)
			{
				out.u2 = readFloat(vertex, uv2Offset, 0.0f);
				out.v2 = readFloat(vertex, uv2Offset + static_cast<int>(sizeof(float)), 0.0f);
				applyTextureTransformForStage(1, out.u2, out.v2);
			}
		}
		out.color = readColor(vertex, ms_activeVertexDescriptor->offsetColor0);
		if (((out.color >> 24) & 0xff) == 0)
			out.color |= 0xff000000;
		bool const skipGpuTexturedMaterialTint = gpuPresentEnabled() && !gpuMaterialTintEnabled() && ms_activeTextureData && ms_activeVertexDescriptor->offsetColor0 < 0;
		if ((ms_activeVertexDescriptor->offsetColor0 < 0 || ms_activeMaterialColorValid) && !skipGpuTexturedMaterialTint)
			out.color = applyActiveMaterialColor(out.color);
		out.color = applyFogColor(out.color, fogDistance);
		out.valid = true;
		return true;
	}

	bool transformActiveVertex(int vertexIndex, ScreenVertex &out)
	{
		out.valid = false;
		out.x = out.y = out.z = out.w = 0.0f;
		out.clipX = out.clipY = out.clipZ = out.clipW = 0.0f;
		out.objectX = out.objectY = out.objectZ = 0.0f;
		out.u = out.v = 0.0f;
		out.u2 = out.v2 = 0.0f;
		out.specularR = out.specularG = out.specularB = 0.0f;
		out.color = 0xffffffff;

		int positionOffset = -1;
		VertexBufferDescriptor const *positionDescriptor = 0;
		byte const *positionVertex = getActiveVertexBytesForAttribute(vertexIndex, VAK_position, 0, positionOffset, positionDescriptor);
		if (!positionVertex)
			return false;

		float const px = readFloat(positionVertex, positionOffset, 0.0f);
		float const py = readFloat(positionVertex, positionOffset + static_cast<int>(sizeof(float)), 0.0f);
		float const pz = readFloat(positionVertex, positionOffset + static_cast<int>(sizeof(float)) * 2, 0.0f);
		out.objectX = px;
		out.objectY = py;
		out.objectZ = pz;
		Matrix4x4 const objectToCamera = multiply(ms_objectToWorldMatrix, ms_worldToCameraMatrix);
		Matrix4x4 const objectToProjection = multiply(objectToCamera, ms_projectionMatrix);
		float const cameraX = px * objectToCamera.m[0][0] + py * objectToCamera.m[1][0] + pz * objectToCamera.m[2][0] + objectToCamera.m[3][0];
		float const cameraY = px * objectToCamera.m[0][1] + py * objectToCamera.m[1][1] + pz * objectToCamera.m[2][1] + objectToCamera.m[3][1];
		float const cameraZ = px * objectToCamera.m[0][2] + py * objectToCamera.m[1][2] + pz * objectToCamera.m[2][2] + objectToCamera.m[3][2];
		float const fogDistance = sqrtf(cameraX * cameraX + cameraY * cameraY + cameraZ * cameraZ);
		float const cx = px * objectToProjection.m[0][0] + py * objectToProjection.m[1][0] + pz * objectToProjection.m[2][0] + objectToProjection.m[3][0];
		float const cy = px * objectToProjection.m[0][1] + py * objectToProjection.m[1][1] + pz * objectToProjection.m[2][1] + objectToProjection.m[3][1];
		float const cz = px * objectToProjection.m[0][2] + py * objectToProjection.m[1][2] + pz * objectToProjection.m[2][2] + objectToProjection.m[3][2];
		float const cw = px * objectToProjection.m[0][3] + py * objectToProjection.m[1][3] + pz * objectToProjection.m[2][3] + objectToProjection.m[3][3];
		out.clipX = cx;
		out.clipY = cy;
		out.clipZ = cz;
		out.clipW = cw;
		if (gpuPresentEnabled())
		{
			if (!_finite(cx) || !_finite(cy) || !_finite(cz) || !_finite(cw))
				return false;

			if (gpuWorldClipEnabled())
			{
				out.x = 0.0f;
				out.y = 0.0f;
				out.z = 0.0f;
				out.w = cw;
			}
			else
			{
				if (cw <= 0.00001f)
					return false;

				float const invW = 1.0f / cw;
				float const ndcX = cx * invW;
				float const ndcY = cy * invW;
				float const ndcZ = cz / cw;
				if (ndcZ < 0.0f || ndcZ > 1.0f)
					return false;

				int const viewportWidth = ms_viewportWidth > 0 ? ms_viewportWidth : static_cast<int>(ms_swapchainExtent.width);
				int const viewportHeight = ms_viewportHeight > 0 ? ms_viewportHeight : static_cast<int>(ms_swapchainExtent.height);
				out.x = static_cast<float>(ms_viewportX) + ((ndcX + 1.0f) * 0.5f * static_cast<float>(viewportWidth));
				out.y = static_cast<float>(ms_viewportY) + ((1.0f - ndcY) * 0.5f * static_cast<float>(viewportHeight));
				out.z = std::max(0.0f, std::min(1.0f, ndcZ));
				out.w = cw;
			}
		}
		else
		{
			if (cw <= 0.00001f)
				return false;

			float const invW = 1.0f / cw;
			float const ndcX = cx * invW;
			float const ndcY = cy * invW;
			float const ndcZ = cz * invW;
			if (ndcZ < 0.0f || ndcZ > 1.0f)
				return false;

			int const viewportWidth = ms_viewportWidth > 0 ? ms_viewportWidth : static_cast<int>(ms_swapchainExtent.width);
			int const viewportHeight = ms_viewportHeight > 0 ? ms_viewportHeight : static_cast<int>(ms_swapchainExtent.height);
			out.x = static_cast<float>(ms_viewportX) + ((ndcX + 1.0f) * 0.5f * static_cast<float>(viewportWidth));
			out.y = static_cast<float>(ms_viewportY) + ((1.0f - ndcY) * 0.5f * static_cast<float>(viewportHeight));
			out.z = std::max(0.0f, std::min(1.0f, ndcZ));
			out.w = cw;
		}

		int textureOffset = -1;
		VertexBufferDescriptor const *textureDescriptor = 0;
		byte const *textureVertex = getActiveVertexBytesForAttribute(vertexIndex, VAK_texcoord, ms_activeTextureCoordinateSet, textureOffset, textureDescriptor);
		if (textureVertex)
		{
			out.u = readFloat(textureVertex, textureOffset, 0.0f);
			out.v = readFloat(textureVertex, textureOffset + static_cast<int>(sizeof(float)), 0.0f);
			applyActiveTextureTransform(out.u, out.v);
			out.u2 = out.u;
			out.v2 = out.v;
		}
		if (ms_activeSecondaryTextureData)
		{
			int texture2Offset = -1;
			VertexBufferDescriptor const *texture2Descriptor = 0;
			byte const *texture2Vertex = getActiveVertexBytesForAttribute(vertexIndex, VAK_texcoord, ms_activeSecondaryTextureCoordinateSet, texture2Offset, texture2Descriptor);
			if (texture2Vertex)
			{
				out.u2 = readFloat(texture2Vertex, texture2Offset, 0.0f);
				out.v2 = readFloat(texture2Vertex, texture2Offset + static_cast<int>(sizeof(float)), 0.0f);
				applyTextureTransformForStage(1, out.u2, out.v2);
			}
		}

		int colorOffset = -1;
		VertexBufferDescriptor const *colorDescriptor = 0;
		byte const *colorVertex = getActiveVertexBytesForAttribute(vertexIndex, VAK_color0, 0, colorOffset, colorDescriptor);
		out.color = colorVertex ? readColor(colorVertex, colorOffset) : 0xffffffff;
		if (((out.color >> 24) & 0xff) == 0)
			out.color |= 0xff000000;
		bool const hasColor0 = colorVertex != 0;
		bool const skipGpuTexturedMaterialTint = gpuPresentEnabled() && !gpuMaterialTintEnabled() && ms_activeTextureData && !hasColor0;
		if ((!hasColor0 || ms_activeMaterialColorValid) && !skipGpuTexturedMaterialTint)
			out.color = applyActiveMaterialColor(out.color);
		out.color = applyFogColor(out.color, fogDistance);
		out.valid = true;
		return true;
	}

	bool readTransformedVertex(byte const *vertex, ScreenVertex &out)
	{
		out.valid = false;
		out.x = out.y = out.z = out.w = 0.0f;
		out.clipX = out.clipY = out.clipZ = out.clipW = 0.0f;
		out.objectX = out.objectY = out.objectZ = 0.0f;
		out.u = out.v = 0.0f;
		out.u2 = out.v2 = 0.0f;
		out.specularR = out.specularG = out.specularB = 0.0f;
		out.color = 0xffffffff;

		if (!vertex || !ms_activeVertexDescriptor || ms_activeVertexDescriptor->offsetPosition < 0)
			return false;

		out.x = readFloat(vertex, ms_activeVertexDescriptor->offsetPosition, 0.0f);
		out.y = readFloat(vertex, ms_activeVertexDescriptor->offsetPosition + static_cast<int>(sizeof(float)), 0.0f);
		out.z = std::max(0.0f, std::min(1.0f, readFloat(vertex, ms_activeVertexDescriptor->offsetPosition + static_cast<int>(sizeof(float)) * 2, 0.0f)));
		out.w = 1.0f;
		out.objectX = out.x;
		out.objectY = out.y;
		out.objectZ = out.z;
		out.clipX = out.x;
		out.clipY = out.y;
		out.clipZ = out.z;
		out.clipW = out.w;

		int const uvOffset = getActiveTextureCoordinateOffset();
		if (uvOffset >= 0)
		{
			out.u = readFloat(vertex, uvOffset, 0.0f);
			out.v = readFloat(vertex, uvOffset + static_cast<int>(sizeof(float)), 0.0f);
			applyActiveTextureTransform(out.u, out.v);
			out.u2 = out.u;
			out.v2 = out.v;
		}
		if (ms_activeSecondaryTextureData)
		{
			int const uv2Offset = getTextureCoordinateOffset(ms_activeVertexDescriptor, ms_activeSecondaryTextureCoordinateSet);
			if (uv2Offset >= 0)
			{
				out.u2 = readFloat(vertex, uv2Offset, 0.0f);
				out.v2 = readFloat(vertex, uv2Offset + static_cast<int>(sizeof(float)), 0.0f);
				applyTextureTransformForStage(1, out.u2, out.v2);
			}
		}
		out.color = readColor(vertex, ms_activeVertexDescriptor->offsetColor0);
		out.color = normalizeLegacyVertexColor(out.color);
		out.valid = true;
		return true;
	}

	bool readTransformedActiveVertex(int vertexIndex, ScreenVertex &out)
	{
		out.valid = false;
		out.x = out.y = out.z = out.w = 0.0f;
		out.clipX = out.clipY = out.clipZ = out.clipW = 0.0f;
		out.objectX = out.objectY = out.objectZ = 0.0f;
		out.u = out.v = 0.0f;
		out.u2 = out.v2 = 0.0f;
		out.specularR = out.specularG = out.specularB = 0.0f;
		out.color = 0xffffffff;

		int positionOffset = -1;
		VertexBufferDescriptor const *positionDescriptor = 0;
		byte const *positionVertex = getActiveVertexBytesForAttribute(vertexIndex, VAK_position, 0, positionOffset, positionDescriptor);
		if (!positionVertex)
			return false;

		out.x = readFloat(positionVertex, positionOffset, 0.0f);
		out.y = readFloat(positionVertex, positionOffset + static_cast<int>(sizeof(float)), 0.0f);
		out.z = std::max(0.0f, std::min(1.0f, readFloat(positionVertex, positionOffset + static_cast<int>(sizeof(float)) * 2, 0.0f)));
		out.w = 1.0f;
		out.objectX = out.x;
		out.objectY = out.y;
		out.objectZ = out.z;
		out.clipX = out.x;
		out.clipY = out.y;
		out.clipZ = out.z;
		out.clipW = out.w;

		int textureOffset = -1;
		VertexBufferDescriptor const *textureDescriptor = 0;
		byte const *textureVertex = getActiveVertexBytesForAttribute(vertexIndex, VAK_texcoord, ms_activeTextureCoordinateSet, textureOffset, textureDescriptor);
		if (textureVertex)
		{
			out.u = readFloat(textureVertex, textureOffset, 0.0f);
			out.v = readFloat(textureVertex, textureOffset + static_cast<int>(sizeof(float)), 0.0f);
			applyActiveTextureTransform(out.u, out.v);
			out.u2 = out.u;
			out.v2 = out.v;
		}
		if (ms_activeSecondaryTextureData)
		{
			int texture2Offset = -1;
			VertexBufferDescriptor const *texture2Descriptor = 0;
			byte const *texture2Vertex = getActiveVertexBytesForAttribute(vertexIndex, VAK_texcoord, ms_activeSecondaryTextureCoordinateSet, texture2Offset, texture2Descriptor);
			if (texture2Vertex)
			{
				out.u2 = readFloat(texture2Vertex, texture2Offset, 0.0f);
				out.v2 = readFloat(texture2Vertex, texture2Offset + static_cast<int>(sizeof(float)), 0.0f);
				applyTextureTransformForStage(1, out.u2, out.v2);
			}
		}

		int colorOffset = -1;
		VertexBufferDescriptor const *colorDescriptor = 0;
		byte const *colorVertex = getActiveVertexBytesForAttribute(vertexIndex, VAK_color0, 0, colorOffset, colorDescriptor);
		out.color = normalizeLegacyVertexColor(colorVertex ? readColor(colorVertex, colorOffset) : 0xffffffff);
		out.valid = true;
		return true;
	}

	float edgeFunction(ScreenVertex const &a, ScreenVertex const &b, float x, float y)
	{
		return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
	}

	uint32 interpolateColor(ScreenVertex const &a, ScreenVertex const &b, ScreenVertex const &c, float wa, float wb, float wc)
	{
		float const aa = static_cast<float>((a.color >> 24) & 0xff) * wa + static_cast<float>((b.color >> 24) & 0xff) * wb + static_cast<float>((c.color >> 24) & 0xff) * wc;
		float const ar = static_cast<float>((a.color >> 16) & 0xff) * wa + static_cast<float>((b.color >> 16) & 0xff) * wb + static_cast<float>((c.color >> 16) & 0xff) * wc;
		float const ag = static_cast<float>((a.color >> 8) & 0xff) * wa + static_cast<float>((b.color >> 8) & 0xff) * wb + static_cast<float>((c.color >> 8) & 0xff) * wc;
		float const ab = static_cast<float>(a.color & 0xff) * wa + static_cast<float>(b.color & 0xff) * wb + static_cast<float>(c.color & 0xff) * wc;
		uint32 const ia = std::max(0, std::min(255, static_cast<int>(aa + 0.5f)));
		uint32 const ir = std::max(0, std::min(255, static_cast<int>(ar + 0.5f)));
		uint32 const ig = std::max(0, std::min(255, static_cast<int>(ag + 0.5f)));
		uint32 const ib = std::max(0, std::min(255, static_cast<int>(ab + 0.5f)));
		return (ia << 24) | (ir << 16) | (ig << 8) | ib;
	}

	unsigned drawProjectedTriangle(ScreenVertex const &a, ScreenVertex const &b, ScreenVertex const &c, bool depthTest, bool depthWrite, bool wrapTexture, bool useShaderAlpha, bool actorBodyFixedFunction)
	{
		if (!a.valid || !b.valid || !c.valid)
			return 0;
		float const area = edgeFunction(a, b, c.x, c.y);
		if (fabsf(area) < 0.0001f)
			return 0;
		if (ms_cullMode == GCM_clockwise && area > 0.0f)
			return 0;
		if (ms_cullMode == GCM_counterClockwise && area < 0.0f)
			return 0;

		ensureFramePixels();
		if (ms_framePixels.empty() || ms_depthPixels.empty())
			return 0;

		int const width = static_cast<int>(ms_swapchainExtent.width);
		int const height = static_cast<int>(ms_swapchainExtent.height);
		int const viewportMinX = std::max(0, std::min(width, ms_viewportX));
		int const viewportMinY = std::max(0, std::min(height, ms_viewportY));
		int const viewportMaxX = std::max(viewportMinX, std::min(width, ms_viewportX + (ms_viewportWidth > 0 ? ms_viewportWidth : width)));
		int const viewportMaxY = std::max(viewportMinY, std::min(height, ms_viewportY + (ms_viewportHeight > 0 ? ms_viewportHeight : height)));
		int drawMinX = std::max(viewportMinX, std::min(viewportMaxX, static_cast<int>(floorf(std::min(std::min(a.x, b.x), c.x)))));
		int drawMaxX = std::max(viewportMinX, std::min(viewportMaxX, static_cast<int>(ceilf(std::max(std::max(a.x, b.x), c.x)))));
		int drawMinY = std::max(viewportMinY, std::min(viewportMaxY, static_cast<int>(floorf(std::min(std::min(a.y, b.y), c.y)))));
		int drawMaxY = std::max(viewportMinY, std::min(viewportMaxY, static_cast<int>(ceilf(std::max(std::max(a.y, b.y), c.y)))));
		intersectScissor(drawMinX, drawMinY, drawMaxX, drawMaxY);
		if (drawMaxX <= drawMinX || drawMaxY <= drawMinY)
			return 0;

		unsigned pixelsDrawn = 0;
		for (int y = drawMinY; y < drawMaxY; ++y)
		{
			uint32 *colorRow = &ms_framePixels[static_cast<size_t>(y) * static_cast<size_t>(width)];
			float *depthRow = &ms_depthPixels[static_cast<size_t>(y) * static_cast<size_t>(width)];
			for (int x = drawMinX; x < drawMaxX; ++x)
			{
				float const px = static_cast<float>(x) + 0.5f;
				float const py = static_cast<float>(y) + 0.5f;
				float const wa = edgeFunction(b, c, px, py) / area;
				float const wb = edgeFunction(c, a, px, py) / area;
				float const wc = edgeFunction(a, b, px, py) / area;
				if (wa < -0.001f || wb < -0.001f || wc < -0.001f)
					continue;

				float const z = a.z * wa + b.z * wb + c.z * wc;
				if (z < 0.0f || z > 1.0f)
					continue;
				if (depthTest && z >= depthRow[x])
					continue;

				float const u = a.u * wa + b.u * wb + c.u * wc;
				float const v = a.v * wa + b.v * wb + c.v * wc;
				uint32 color = interpolateColor(a, b, c, wa, wb, wc);
				if (ms_activeTextureData)
				{
					uint32 const sampled = ms_activeTextureData->sample(u, v, wrapTexture);
					color = modulateColor(sampled, color);
				}
				if (actorBodyFixedFunction)
					color = scaleColorRgb(color, 0.72f);
				if (useShaderAlpha)
				{
					if (!alphaTestPassWith(color, ms_shaderAlphaTestEnabled, ms_shaderAlphaTestCompare, ms_shaderAlphaTestReference))
						continue;
				}
				else if (!alphaTestPass(color))
					continue;
				bool const blendEnabled = useShaderAlpha ? ms_shaderAlphaBlendEnabled : ms_alphaBlendEnabled;
				colorRow[x] = blendEnabled ? alphaBlend(colorRow[x], color) : (0xff000000 | (color & 0x00ffffff));
				if (depthWrite)
					depthRow[x] = z;
				++pixelsDrawn;
			}
		}
		return pixelsDrawn;
	}

	void drawVertexTriangle(int ia, int ib, int ic)
	{
		if (!ms_activeVertexData || !ms_activeVertexDescriptor || ms_activeVertexStride <= 0)
			return;
		if (ia < 0 || ib < 0 || ic < 0 || ia >= ms_activeVertexCount || ib >= ms_activeVertexCount || ic >= ms_activeVertexCount)
			return;
		ScreenVertex a;
		ScreenVertex b;
		ScreenVertex c;
		VertexBufferFormat format;
		format.setFlags(ms_activeVertexFormatFlags);
		readGpuVertexForActiveFormat(ia, format, a);
		readGpuVertexForActiveFormat(ib, format, b);
		readGpuVertexForActiveFormat(ic, format, c);
		bool const transformed = format.isTransformed();
		bool const terrainBlendShader =
			ms_activeStaticShaderNamePtr &&
			strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend");
		bool const atmosphereShader = activeAtmosphereShader();
		if (!transformed && terrainBlendShader &&
			(isCorruptTerrainVertex(a) || isCorruptTerrainVertex(b) || isCorruptTerrainVertex(c)))
		{
			if (repairCorruptTerrainTriangleVertex(a, b, c, format, ia, ib, ic))
			{
				// Continue through the normal terrain path with the repaired fan center.
			}
			else
			{
			++ms_terrainCorruptTriangleDrops;
			static int s_corruptTerrainTriangleLogCount = 0;
			if (gpuBatchDiagnosticsEnabled() && s_corruptTerrainTriangleLogCount < 96)
			{
				char buffer[1024];
				_snprintf(buffer, sizeof(buffer) - 1, "gpu drop corrupt terrain tri count=%d shader=%s tex=0x%08x ia=%d ib=%d ic=%d clip=(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f) uv=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)",
					s_corruptTerrainTriangleLogCount + 1,
					ms_activeStaticShaderNamePtr,
					static_cast<unsigned int>(ms_activeTextureTag),
					ia,
					ib,
					ic,
					a.clipX, a.clipY, a.clipZ, a.clipW,
					b.clipX, b.clipY, b.clipZ, b.clipW,
					c.clipX, c.clipY, c.clipZ, c.clipW,
					a.u, a.v,
					b.u, b.v,
					c.u, c.v);
				buffer[sizeof(buffer) - 1] = 0;
				logLine(buffer);
				++s_corruptTerrainTriangleLogCount;
			}
			return;
			}
		}
		if (!transformed)
		{
			++ms_worldTrianglesAttempted;
			if (a.valid && b.valid && c.valid)
				++ms_worldTrianglesValid;
			else if (verboseGeometryLoggingEnabled() && ms_worldTrianglesOffscreen < 12)
			{
				++ms_worldTrianglesOffscreen;
				char buffer[256];
				_snprintf(buffer, sizeof(buffer) - 1, "world triangle invalid ia=%d ib=%d ic=%d valid=%d/%d/%d", ia, ib, ic, a.valid ? 1 : 0, b.valid ? 1 : 0, c.valid ? 1 : 0);
				buffer[sizeof(buffer) - 1] = 0;
				logLine(buffer);
			}
			if (verboseGeometryLoggingEnabled() && ms_worldTrianglesValid <= 8 && a.valid && b.valid && c.valid)
			{
				char buffer[320];
				_snprintf(buffer, sizeof(buffer) - 1, "world tri sample screen=(%.1f,%.1f,%.3f)(%.1f,%.1f,%.3f)(%.1f,%.1f,%.3f) uv=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f) color=0x%08x tex=%p", a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, a.u, a.v, b.u, b.v, c.u, c.v, a.color, ms_activeTextureData);
				buffer[sizeof(buffer) - 1] = 0;
				logLine(buffer);
			}
		}
		bool const depthAllowed = !transformed;
		bool const actorBodyFixedFunction = isActorBodyFixedFunction(format);
		int const actorGpuSkipReason = actorBodyFixedFunction ? activeActorGpuSkipReason() : 0;
		if (actorBodyFixedFunction && a.valid && b.valid && c.valid)
		{
			++ms_actorGpuTrianglesSeen;
			if (actorGpuSkipReason == 1)
				++ms_actorGpuTrianglesSkippedNrml;
			else if (actorGpuSkipReason == 2)
				++ms_actorGpuTrianglesSkippedSpec;
			else if (actorGpuSkipReason == 3)
				++ms_actorGpuTrianglesSkippedBlackSpec;
		}
		else if (!transformed && ms_activeTextureData && a.valid && b.valid && c.valid)
		{
			++ms_actorGpuTrianglesNonActorTextured;
			static int s_actorMissProbeLogCount = 0;
			if (gpuBatchDiagnosticsEnabled() && s_actorMissProbeLogCount < 80)
			{
				float const minX = std::min(a.x, std::min(b.x, c.x));
				float const minY = std::min(a.y, std::min(b.y, c.y));
				float const maxX = std::max(a.x, std::max(b.x, c.x));
				float const maxY = std::max(a.y, std::max(b.y, c.y));
				char buffer[640];
				_snprintf(buffer, sizeof(buffer) - 1, "actor miss textured tri flags=0x%08x transformed=%d normal=%d color0=%d color1=%d texSets=%d stride=%d shader=%s texture=%s passDepth=%d/%d alphaBlend=%d alphaTest=%d bounds=%.1f,%.1f..%.1f,%.1f",
					static_cast<unsigned>(format.getFlags()),
					format.isTransformed() ? 1 : 0,
					format.hasNormal() ? 1 : 0,
					format.hasColor0() ? 1 : 0,
					format.hasColor1() ? 1 : 0,
					format.getNumberOfTextureCoordinateSets(),
					ms_activeVertexStride,
					ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
					ms_activeTextureData ? ms_activeTextureData->getName() : "<none>",
					ms_depthEnabled ? 1 : 0,
					ms_depthWriteEnabled ? 1 : 0,
					ms_shaderAlphaBlendEnabled ? 1 : 0,
					ms_shaderAlphaTestEnabled ? 1 : 0,
					minX,
					minY,
					maxX,
					maxY);
				buffer[sizeof(buffer) - 1] = 0;
				logLine(buffer);
				++s_actorMissProbeLogCount;
			}
		}
		bool const stageGpuPass = !actorBodyFixedFunction || actorGpuSkipReason == 0;
		if (stageGpuPass)
		{
			ScreenVertex gpuA = a;
			ScreenVertex gpuB = b;
			ScreenVertex gpuC = c;
			if (gpuPresentEnabled() && !transformed && ms_activeLightingEnabled && ms_activePixelProgramMode == 0 && ms_activeVertexDescriptor && ms_activeVertexDescriptor->offsetNormal >= 0)
			{
				byte const *vertexA = ms_activeVertexData + static_cast<size_t>(ia) * static_cast<size_t>(ms_activeVertexStride);
				byte const *vertexB = ms_activeVertexData + static_cast<size_t>(ib) * static_cast<size_t>(ms_activeVertexStride);
				byte const *vertexC = ms_activeVertexData + static_cast<size_t>(ic) * static_cast<size_t>(ms_activeVertexStride);
				float specularA[3];
				float specularB[3];
				float specularC[3];
				if (actorBodyFixedFunction)
				{
					gpuA.color = scaleColorRgb(applyFixedFunctionVertexLighting(vertexA, gpuA.color, specularA, true), computeActorBodyLightScale(vertexA));
					gpuB.color = scaleColorRgb(applyFixedFunctionVertexLighting(vertexB, gpuB.color, specularB, true), computeActorBodyLightScale(vertexB));
					gpuC.color = scaleColorRgb(applyFixedFunctionVertexLighting(vertexC, gpuC.color, specularC, true), computeActorBodyLightScale(vertexC));
				}
				else
				{
					gpuA.color = applyFixedFunctionVertexLighting(vertexA, gpuA.color, specularA);
					gpuB.color = applyFixedFunctionVertexLighting(vertexB, gpuB.color, specularB);
					gpuC.color = applyFixedFunctionVertexLighting(vertexC, gpuC.color, specularC);
				}
				gpuA.specularR = specularA[0];
				gpuA.specularG = specularA[1];
				gpuA.specularB = specularA[2];
				gpuB.specularR = specularB[0];
				gpuB.specularG = specularB[1];
				gpuB.specularB = specularB[2];
				gpuC.specularR = specularC[0];
				gpuC.specularG = specularC[1];
				gpuC.specularB = specularC[2];
			}
			if (gpuPresentEnabled() && activeTextureFactorModulateStage())
			{
				uint32 const textureFactorColor = makeTextureFactorColor();
				gpuA.color = textureFactorColor;
				gpuB.color = textureFactorColor;
				gpuC.color = textureFactorColor;
				gpuA.specularR = gpuA.specularG = gpuA.specularB = 0.0f;
				gpuB.specularR = gpuB.specularG = gpuB.specularB = 0.0f;
				gpuC.specularR = gpuC.specularG = gpuC.specularB = 0.0f;
			}
			if (gpuPresentEnabled() && actorBodyFixedFunction && activeActorVisibleAuxPass())
			{
				gpuA.color = makeActorNeutralOverlayColor(gpuA.color);
				gpuB.color = makeActorNeutralOverlayColor(gpuB.color);
				gpuC.color = makeActorNeutralOverlayColor(gpuC.color);
			}
			bool const gpuAlphaBlend = transformed ? ms_alphaBlendEnabled : (actorBodyFixedFunction ? (activeActorVisibleAuxPass() || activeActorDxtAlphaPass()) : ms_shaderAlphaBlendEnabled);
			bool const terrainPathologicalClip = !transformed && terrainTriangleNeedsPathologicalClip(gpuA, gpuB, gpuC);
			if (terrainPathologicalClip)
			{
				static int s_terrainPathologicalClipLogCount = 0;
				if (gpuBatchDiagnosticsEnabled() && s_terrainPathologicalClipLogCount < 96)
				{
					float const minX = std::min(gpuA.x, std::min(gpuB.x, gpuC.x));
					float const minY = std::min(gpuA.y, std::min(gpuB.y, gpuC.y));
					float const maxX = std::max(gpuA.x, std::max(gpuB.x, gpuC.x));
					float const maxY = std::max(gpuA.y, std::max(gpuB.y, gpuC.y));
					char buffer[768];
					_snprintf(buffer, sizeof(buffer) - 1, "gpu terrain pathological clip count=%d shader=%s tex=0x%08x bounds=%.1f,%.1f..%.1f,%.1f w=%.4f,%.4f,%.4f clip=(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f)",
						s_terrainPathologicalClipLogCount + 1,
						ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
						static_cast<unsigned int>(ms_activeTextureTag),
						minX,
						minY,
						maxX,
						maxY,
						gpuA.w,
						gpuB.w,
						gpuC.w,
						gpuA.clipX, gpuA.clipY, gpuA.clipZ, gpuA.clipW,
						gpuB.clipX, gpuB.clipY, gpuB.clipZ, gpuB.clipW,
						gpuC.clipX, gpuC.clipY, gpuC.clipZ, gpuC.clipW);
					buffer[sizeof(buffer) - 1] = 0;
					logLine(buffer);
					++s_terrainPathologicalClipLogCount;
				}
			}
			if (!transformed &&
				(gpuWorldClipEnabled() ||
				 terrainPathologicalClip ||
				 (terrainBlendShader && gpuTerrainFrustumClipEnabled()) ||
				 (atmosphereShader && gpuAtmosphereFrustumClipEnabled())))
			{
				if (atmosphereShader && gpuAtmosphereNearRejectEnabled() &&
					(gpuA.clipW <= 0.0001f || gpuB.clipW <= 0.0001f || gpuC.clipW <= 0.0001f))
				{
					static int s_atmosphereNearRejectLogCount = 0;
					if (gpuBatchDiagnosticsEnabled() && s_atmosphereNearRejectLogCount < 96)
					{
						float const minX = std::min(gpuA.x, std::min(gpuB.x, gpuC.x));
						float const minY = std::min(gpuA.y, std::min(gpuB.y, gpuC.y));
						float const maxX = std::max(gpuA.x, std::max(gpuB.x, gpuC.x));
						float const maxY = std::max(gpuA.y, std::max(gpuB.y, gpuC.y));
						char buffer[768];
						_snprintf(buffer, sizeof(buffer) - 1, "gpu atmosphere near reject count=%d shader=%s tex=0x%08x ia=%d ib=%d ic=%d bounds=%.1f,%.1f..%.1f,%.1f w=%.4f,%.4f,%.4f clipW=%.6f,%.6f,%.6f clipZ=%.6f,%.6f,%.6f",
							s_atmosphereNearRejectLogCount + 1,
							ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
							static_cast<unsigned int>(ms_activeTextureTag),
							ia,
							ib,
							ic,
							minX,
							minY,
							maxX,
							maxY,
							gpuA.w,
							gpuB.w,
							gpuC.w,
							gpuA.clipW,
							gpuB.clipW,
							gpuC.clipW,
							gpuA.clipZ,
							gpuB.clipZ,
							gpuC.clipZ);
						buffer[sizeof(buffer) - 1] = 0;
						logLine(buffer);
						++s_atmosphereNearRejectLogCount;
					}
					return;
				}
				if ((terrainBlendShader || atmosphereShader) && !gpuWorldClipEnabled())
				{
					static int s_terrainFrustumClipLogCount = 0;
					static int s_atmosphereFrustumClipLogCount = 0;
					int &clipLogCount = terrainBlendShader ? s_terrainFrustumClipLogCount : s_atmosphereFrustumClipLogCount;
					bool const clipLogEnabled = terrainBlendShader ? gpuTerrainFrustumClipEnabled() : gpuAtmosphereFrustumClipEnabled();
					if (clipLogEnabled && gpuBatchDiagnosticsEnabled() && clipLogCount < 96)
					{
						float const minX = std::min(gpuA.x, std::min(gpuB.x, gpuC.x));
						float const minY = std::min(gpuA.y, std::min(gpuB.y, gpuC.y));
						float const maxX = std::max(gpuA.x, std::max(gpuB.x, gpuC.x));
						float const maxY = std::max(gpuA.y, std::max(gpuB.y, gpuC.y));
						int const diagnosticWidth = ms_viewportWidth > 0 ? ms_viewportWidth : std::max(1, ms_width);
						int const diagnosticHeight = ms_viewportHeight > 0 ? ms_viewportHeight : std::max(1, ms_height);
						if (minX < 0.0f || maxX > static_cast<float>(diagnosticWidth) ||
							minY < 0.0f || maxY > static_cast<float>(diagnosticHeight))
						{
							char buffer[768];
							_snprintf(buffer, sizeof(buffer) - 1, "gpu %s frustum clip count=%d shader=%s tex=0x%08x ia=%d ib=%d ic=%d bounds=%.1f,%.1f..%.1f,%.1f w=%.4f,%.4f,%.4f clip=(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f)",
								terrainBlendShader ? "terrain" : "atmosphere",
								clipLogCount + 1,
								ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
								static_cast<unsigned int>(ms_activeTextureTag),
								ia,
								ib,
								ic,
								minX,
								minY,
								maxX,
								maxY,
								gpuA.w,
								gpuB.w,
								gpuC.w,
								gpuA.clipX, gpuA.clipY, gpuA.clipZ, gpuA.clipW,
								gpuB.clipX, gpuB.clipY, gpuB.clipZ, gpuB.clipW,
								gpuC.clipX, gpuC.clipY, gpuC.clipZ, gpuC.clipW);
							buffer[sizeof(buffer) - 1] = 0;
							logLine(buffer);
							++clipLogCount;
						}
					}
				}
				stageGpuWorldTriangleClipped(gpuA, gpuB, gpuC, gpuAlphaBlend, actorBodyFixedFunction);
			}
			else
				stageGpuTriangle(gpuA, gpuB, gpuC, gpuAlphaBlend, !transformed, actorBodyFixedFunction);
		}
		if (gpuPresentEnabled() && ms_gpuTriangleVertexMapped && ms_gpuTriangleVertexBuffer)
			return;
		unsigned const pixels = drawProjectedTriangle(a, b, c, depthAllowed && ms_depthEnabled, depthAllowed && ms_depthWriteEnabled, !transformed, !transformed, actorBodyFixedFunction);
		if (!transformed)
			ms_worldTrianglePixels += pixels;
	}

	void drawTriangleListRange(int firstVertex, int vertexCount)
	{
		int const clampedFirst = std::max(0, firstVertex);
		int const clampedCount = std::max(0, std::min(vertexCount, ms_activeVertexCount - clampedFirst));
		int const triangleVertexCount = (clampedCount / 3) * 3;
		for (int i = 0; i < triangleVertexCount; i += 3)
			drawVertexTriangle(clampedFirst + i, clampedFirst + i + 1, clampedFirst + i + 2);
	}

	float readActivePointSize(int vertexIndex)
	{
		float result = ms_pointSize;
		int pointSizeOffset = -1;
		VertexBufferDescriptor const *pointSizeDescriptor = 0;
		byte const *pointSizeVertex = getActiveVertexBytesForAttribute(vertexIndex, VAK_pointSize, 0, pointSizeOffset, pointSizeDescriptor);
		if (pointSizeVertex)
			result = readFloat(pointSizeVertex, pointSizeOffset, result);
		if (!_finite(result) || result <= 0.0f)
			result = 1.0f;
		float const minSize = (_finite(ms_pointSizeMin) && ms_pointSizeMin > 0.0f) ? ms_pointSizeMin : 1.0f;
		float const maxSize = (_finite(ms_pointSizeMax) && ms_pointSizeMax >= minSize) ? ms_pointSizeMax : std::max(minSize, 64.0f);
		return std::max(minSize, std::min(maxSize, result));
	}

	bool readActivePointVertex(int vertexIndex, ScreenVertex &out)
	{
		VertexBufferFormat format;
		format.setFlags(ms_activeVertexFormatFlags);
		return format.isTransformed() ? readTransformedActiveVertex(vertexIndex, out) : transformActiveVertex(vertexIndex, out);
	}

	void stageGpuPointSprite(int vertexIndex)
	{
		ScreenVertex center;
		if (!readActivePointVertex(vertexIndex, center) || !center.valid)
			return;
		if (!_finite(center.x) || !_finite(center.y) || !_finite(center.z))
			return;

		float const halfSize = std::max(0.5f, readActivePointSize(vertexIndex) * 0.5f);
		ScreenVertex a = center;
		ScreenVertex b = center;
		ScreenVertex c = center;
		ScreenVertex d = center;
		a.x -= halfSize; a.y -= halfSize; a.u = 0.0f; a.v = 0.0f; a.u2 = 0.0f; a.v2 = 0.0f;
		b.x += halfSize; b.y -= halfSize; b.u = 1.0f; b.v = 0.0f; b.u2 = 1.0f; b.v2 = 0.0f;
		c.x += halfSize; c.y += halfSize; c.u = 1.0f; c.v = 1.0f; c.u2 = 1.0f; c.v2 = 1.0f;
		d.x -= halfSize; d.y += halfSize; d.u = 0.0f; d.v = 1.0f; d.u2 = 0.0f; d.v2 = 1.0f;

		bool const alphaBlend = ms_shaderAlphaBlendEnabled || ms_alphaBlendEnabled || ms_pointSpriteEnable;
		stageGpuTriangle(a, b, c, alphaBlend, false, false, true);
		stageGpuTriangle(a, c, d, alphaBlend, false, false, true);
	}

	void drawPointListRange(int firstVertex, int vertexCount)
	{
		int const clampedFirst = std::max(0, firstVertex);
		int const clampedCount = std::max(0, std::min(vertexCount, ms_activeVertexCount - clampedFirst));
		static int s_pointSpriteLogCount = 0;
		if (clampedCount > 0 && s_pointSpriteLogCount < 48)
		{
			char buffer[320];
			_snprintf(buffer, sizeof(buffer) - 1, "gpu point-list expand first=%d count=%d pointSize=%.2f sprite=%d shader=%s texture=%s alpha=%d depth=%d/%d",
				clampedFirst,
				clampedCount,
				ms_pointSize,
				ms_pointSpriteEnable ? 1 : 0,
				ms_activeStaticShaderNamePtr ? ms_activeStaticShaderNamePtr : "<null>",
				ms_activeTextureData ? ms_activeTextureData->getName() : "<none>",
				ms_shaderAlphaBlendEnabled ? 1 : 0,
				ms_depthEnabled ? 1 : 0,
				ms_depthWriteEnabled ? 1 : 0);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_pointSpriteLogCount;
		}
		for (int i = 0; i < clampedCount; ++i)
			stageGpuPointSprite(clampedFirst + i);
	}

	void drawTriangleStripRange(int firstVertex, int vertexCount)
	{
		int const clampedFirst = std::max(0, firstVertex);
		int const clampedCount = std::max(0, std::min(vertexCount, ms_activeVertexCount - clampedFirst));
		for (int i = 0; i + 2 < clampedCount; ++i)
		{
			if ((i & 1) == 0)
				drawVertexTriangle(clampedFirst + i, clampedFirst + i + 1, clampedFirst + i + 2);
			else
				drawVertexTriangle(clampedFirst + i + 1, clampedFirst + i, clampedFirst + i + 2);
		}
	}

	void drawTriangleFanRange(int firstVertex, int vertexCount)
	{
		int const clampedFirst = std::max(0, firstVertex);
		int const clampedCount = std::max(0, std::min(vertexCount, ms_activeVertexCount - clampedFirst));
		if (activeTerrainBlendShader() && clampedCount >= 4)
		{
			VertexBufferFormat format;
			format.setFlags(ms_activeVertexFormatFlags);
			std::vector<int> vertexIndices;
			vertexIndices.reserve(static_cast<size_t>(clampedCount));
			for (int i = 0; i < clampedCount; ++i)
				vertexIndices.push_back(clampedFirst + i);
			if (drawTerrainFanWithRepairedCenter(vertexIndices, format))
				return;
		}
		for (int i = 1; i + 1 < clampedCount; ++i)
			drawVertexTriangle(clampedFirst, clampedFirst + i, clampedFirst + i + 1);
	}

	void drawIndexedTriangleListRange(int baseIndex, int firstIndex, int indexCount)
	{
		if (!ms_activeIndexData || ms_activeIndexCount <= 0)
			return;
		int const clampedFirst = std::max(0, firstIndex);
		int const clampedCount = std::max(0, std::min(indexCount, ms_activeIndexCount - clampedFirst));
		int const triangleIndexCount = (clampedCount / 3) * 3;
		static int s_3dLogCount = 0;
		if (triangleIndexCount > 0 && s_3dLogCount < 40)
		{
			char buffer[256];
			_snprintf(buffer, sizeof(buffer) - 1, "draw3d indexedTriangles base=%d first=%d indices=%d activeV=%d stride=%d textureData=%p viewport=%d,%d %dx%d", baseIndex, clampedFirst, triangleIndexCount, ms_activeVertexCount, ms_activeVertexStride, ms_activeTextureData, ms_viewportX, ms_viewportY, ms_viewportWidth, ms_viewportHeight);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_3dLogCount;
		}
		for (int i = 0; i < triangleIndexCount; i += 3)
		{
			int const ia = baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst + i]);
			int const ib = baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst + i + 1]);
			int const ic = baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst + i + 2]);
			drawVertexTriangle(ia, ib, ic);
		}
	}

	void drawIndexedTriangleStripRange(int baseIndex, int firstIndex, int indexCount)
	{
		if (!ms_activeIndexData || ms_activeIndexCount <= 0)
			return;
		int const clampedFirst = std::max(0, firstIndex);
		int const clampedCount = std::max(0, std::min(indexCount, ms_activeIndexCount - clampedFirst));
		for (int i = 0; i + 2 < clampedCount; ++i)
		{
			int const ia = baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst + i]);
			int const ib = baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst + i + 1]);
			int const ic = baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst + i + 2]);
			if ((i & 1) == 0)
				drawVertexTriangle(ia, ib, ic);
			else
				drawVertexTriangle(ib, ia, ic);
		}
	}

	void drawIndexedTriangleFanRange(int baseIndex, int firstIndex, int indexCount)
	{
		if (!ms_activeIndexData || ms_activeIndexCount <= 0)
			return;
		int const clampedFirst = std::max(0, firstIndex);
		int const clampedCount = std::max(0, std::min(indexCount, ms_activeIndexCount - clampedFirst));
		if (clampedCount < 3)
			return;
		if (activeTerrainBlendShader() && clampedCount >= 4)
		{
			VertexBufferFormat format;
			format.setFlags(ms_activeVertexFormatFlags);
			std::vector<int> vertexIndices;
			vertexIndices.reserve(static_cast<size_t>(clampedCount));
			for (int i = 0; i < clampedCount; ++i)
				vertexIndices.push_back(baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst + i]));
			if (drawTerrainFanWithRepairedCenter(vertexIndices, format))
				return;
		}
		int const baseVertex = baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst]);
		for (int i = 1; i + 1 < clampedCount; ++i)
		{
			int const ib = baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst + i]);
			int const ic = baseIndex + static_cast<int>(ms_activeIndexData[clampedFirst + i + 1]);
			drawVertexTriangle(baseVertex, ib, ic);
		}
	}

	void drawTransformedQuadList(int firstVertex, int vertexCount)
	{
		if (!ms_activeVertexData || !ms_activeVertexDescriptor || ms_activeVertexStride <= 0)
			return;

		VertexBufferFormat format;
		format.setFlags(ms_activeVertexFormatFlags);
		if (!format.isTransformed())
			return;

		int const clampedFirst = std::max(0, firstVertex);
		int const clampedCount = std::max(0, std::min(vertexCount, ms_activeVertexCount - clampedFirst));
		int const quadVertexCount = (clampedCount / 4) * 4;
		static int s_quadLogCount = 0;
		if (quadVertexCount > 0 && s_quadLogCount < 80)
		{
			char buffer[192];
			_snprintf(buffer, sizeof(buffer) - 1, "drawTransformedQuadList first=%d vertices=%d stride=%d textureData=%p texCoordSet=%d", clampedFirst, quadVertexCount, ms_activeVertexStride, ms_activeTextureData, ms_activeTextureCoordinateSet);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_quadLogCount;
		}
		for (int i = 0; i < quadVertexCount; i += 4)
		{
			float minX = 1000000.0f;
			float minY = 1000000.0f;
			float maxX = -1000000.0f;
			float maxY = -1000000.0f;
			float minU = 1000000.0f;
			float minV = 1000000.0f;
			float maxU = -1000000.0f;
			float maxV = -1000000.0f;
			uint32 color = 0;
			QuadCorner quadCorner[4];
			int const uvOffset = getActiveTextureCoordinateOffset();
			bool validQuad = true;
			for (int corner = 0; corner < 4; ++corner)
			{
				ScreenVertex transformedVertex;
				if (!readTransformedActiveVertex(clampedFirst + i + corner, transformedVertex))
				{
					validQuad = false;
					break;
				}
				quadCorner[corner].x = transformedVertex.x;
				quadCorner[corner].y = transformedVertex.y;
				quadCorner[corner].z = transformedVertex.z;
				quadCorner[corner].u = transformedVertex.u;
				quadCorner[corner].v = transformedVertex.v;
				minX = std::min(minX, transformedVertex.x);
				minY = std::min(minY, transformedVertex.y);
				maxX = std::max(maxX, transformedVertex.x);
				maxY = std::max(maxY, transformedVertex.y);
				quadCorner[corner].color = transformedVertex.color;
				color = color ? alphaBlend(color, transformedVertex.color) : transformedVertex.color;
				if (uvOffset >= 0)
				{
					minU = std::min(minU, transformedVertex.u);
					minV = std::min(minV, transformedVertex.v);
					maxU = std::max(maxU, transformedVertex.u);
					maxV = std::max(maxV, transformedVertex.v);
				}
			}
			if (!validQuad)
				continue;
			int const ix0 = static_cast<int>(floorf(minX));
			int const iy0 = static_cast<int>(floorf(minY));
			int const ix1 = static_cast<int>(ceilf(maxX));
			int const iy1 = static_cast<int>(ceilf(maxY));
			ScreenVertex stagedCorner[4];
			for (int corner = 0; corner < 4; ++corner)
			{
				stagedCorner[corner].x = quadCorner[corner].x;
				stagedCorner[corner].y = quadCorner[corner].y;
				stagedCorner[corner].z = quadCorner[corner].z;
				stagedCorner[corner].w = 1.0f;
				stagedCorner[corner].clipX = quadCorner[corner].x;
				stagedCorner[corner].clipY = quadCorner[corner].y;
				stagedCorner[corner].clipZ = quadCorner[corner].z;
				stagedCorner[corner].clipW = 1.0f;
				stagedCorner[corner].objectX = quadCorner[corner].x;
				stagedCorner[corner].objectY = quadCorner[corner].y;
				stagedCorner[corner].objectZ = quadCorner[corner].z;
				stagedCorner[corner].u = quadCorner[corner].u;
				stagedCorner[corner].v = quadCorner[corner].v;
				stagedCorner[corner].u2 = quadCorner[corner].u;
				stagedCorner[corner].v2 = quadCorner[corner].v;
				stagedCorner[corner].color = quadCorner[corner].color;
				stagedCorner[corner].valid = true;
			}
			stageGpuTriangle(stagedCorner[0], stagedCorner[1], stagedCorner[2], ms_shaderAlphaBlendEnabled, false, false, true);
			stageGpuTriangle(stagedCorner[0], stagedCorner[2], stagedCorner[3], ms_shaderAlphaBlendEnabled, false, false, true);
			if (gpuPresentEnabled() && ms_gpuTriangleVertexMapped && ms_gpuTriangleVertexBuffer)
				continue;
			if (ms_activeTextureData && uvOffset >= 0)
				fillTexturedQuad(quadCorner, *ms_activeTextureData);
			else
				fillRect(ix0, iy0, ix1, iy1, color);
		}
	}

	void clearViewport(bool clearColor, uint32 colorValue, bool clearDepth, real depthValue, bool clearStencil, uint32 stencilValue)
	{
		static int s_worldSummaryLogCount = 0;
		if (verboseGeometryLoggingEnabled() && (ms_worldTrianglesAttempted || ms_worldTrianglesValid || ms_worldTrianglePixels) && s_worldSummaryLogCount < 120)
		{
			char buffer[192];
			_snprintf(buffer, sizeof(buffer) - 1, "world frame summary triangles=%u valid=%u pixels=%u", ms_worldTrianglesAttempted, ms_worldTrianglesValid, ms_worldTrianglePixels);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_worldSummaryLogCount;
		}
		ms_worldTrianglesAttempted = 0;
		ms_worldTrianglesValid = 0;
		ms_worldTrianglePixels = 0;
		ms_worldTrianglesOffscreen = 0;
		static int s_clearLogCount = 0;
		if (gpuBatchDiagnosticsEnabled() && s_clearLogCount < 160)
		{
			char buffer[192];
			_snprintf(buffer, sizeof(buffer) - 1, "clearViewport color=%d value=0x%08x depth=%d depthValue=%f stencil=%d stencilValue=%u previousClear=0x%08x",
				clearColor ? 1 : 0,
				static_cast<unsigned>(colorValue),
				clearDepth ? 1 : 0,
				static_cast<float>(depthValue),
				clearStencil ? 1 : 0,
				static_cast<unsigned>(stencilValue),
				static_cast<unsigned>(ms_clearColor));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_clearLogCount;
		}
		if (clearColor)
		{
			ms_clearColor = makeOpaqueColor(colorValue);
			ensureFramePixels();
			std::fill(ms_framePixels.begin(), ms_framePixels.end(), ms_clearColor);
		}
		ensureFramePixels();
		if (!ms_depthPixels.empty())
			std::fill(ms_depthPixels.begin(), ms_depthPixels.end(), 1.0f);
	}

	bool present()
	{
		if (ms_deviceLost)
			return false;
		static int s_presentLogCount = 0;
		static bool s_autoCaptureDone = false;
		static int s_autoCaptureCount = 0;
		static int s_autoCaptureLastPresent = -1;
		static ULONGLONG s_firstPresentMs = 0;
		int const presentLogIndex = s_presentLogCount;
		int const nextPresentCount = presentLogIndex + 1;
		bool const tracePresent = presentLogIndex < 40;
		ULONGLONG const presentStartMs = nowMilliseconds();
		if (!s_firstPresentMs)
			s_firstPresentMs = presentStartMs;
		ms_perfTextureUploadsThisPresent = 0;
		ms_perfTextureUploadBytesThisPresent = 0;
		if (tracePresent)
		{
			char buffer[128];
			_snprintf(buffer, sizeof(buffer) - 1, "present begin %d", presentLogIndex);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
		}
		++s_presentLogCount;

		if (!ms_installed || !ms_swapchain || ms_commandBuffers.empty())
		{
			logLine("present failed not installed/swapchain");
			return false;
		}

		if (!s_autoCaptureDone)
		{
			char captureBase[MAX_PATH];
			DWORD const length = GetEnvironmentVariableA("SWG_VULKAN_AUTOCAPTURE", captureBase, sizeof(captureBase));
			if (length > 0 && length < sizeof(captureBase))
			{
				int const capturePresent = getEnvInt("SWG_VULKAN_AUTOCAPTURE_PRESENT", 30, 1);
				int const captureMax = getEnvInt("SWG_RENDERER_AUTOCAPTURE_MAX", 1, 1);
				int const captureInterval = getEnvInt("SWG_RENDERER_AUTOCAPTURE_INTERVAL", 1, 1);
				bool const intervalReady = s_autoCaptureLastPresent < 0 || (nextPresentCount - s_autoCaptureLastPresent) >= captureInterval;
				if (nextPresentCount >= capturePresent && intervalReady)
				{
					char captureSeriesBase[MAX_PATH];
					_snprintf(captureSeriesBase, sizeof(captureSeriesBase) - 1, "%s-present%04d", captureBase, nextPresentCount);
					captureSeriesBase[sizeof(captureSeriesBase) - 1] = 0;
					bool const captured = writeFrameShot(GSSF_bmp, captureSeriesBase);
					if (captured)
					{
						char sourcePath[MAX_PATH];
						char latestPath[MAX_PATH];
						_snprintf(sourcePath, sizeof(sourcePath) - 1, "%s.bmp", captureSeriesBase);
						sourcePath[sizeof(sourcePath) - 1] = 0;
						_snprintf(latestPath, sizeof(latestPath) - 1, "%s.bmp", captureBase);
						latestPath[sizeof(latestPath) - 1] = 0;
						CopyFileA(sourcePath, latestPath, FALSE);
					}

					++s_autoCaptureCount;
					s_autoCaptureLastPresent = nextPresentCount;
					if (s_autoCaptureCount >= captureMax)
						s_autoCaptureDone = true;

					char buffer[MAX_PATH + 128];
					_snprintf(buffer, sizeof(buffer) - 1, "autocapture %s pre-present=%d capture=%d/%d base=%s", captured ? "ok" : "failed", nextPresentCount, s_autoCaptureCount, captureMax, captureBase);
					buffer[sizeof(buffer) - 1] = 0;
					logLine(buffer);
				}
			}
		}

		VkResult waitResult = VK_SUCCESS;
		if (ms_frameFenceSubmitted)
			waitResult = vkWaitForFencesLocal(ms_device, 1, &ms_frameFence, VK_TRUE, 16000000);
		if (waitResult == VK_TIMEOUT)
		{
			if (tracePresent)
				logLine("present fence wait timeout");
			return true;
		}
		if (waitResult != VK_SUCCESS)
		{
			char fenceFailure[128];
			_snprintf(fenceFailure, sizeof(fenceFailure) - 1, "present fence wait failed result=%d", static_cast<int>(waitResult));
			fenceFailure[sizeof(fenceFailure) - 1] = 0;
			logLine(fenceFailure);
			if (waitResult == VK_ERROR_DEVICE_LOST)
				ms_deviceLost = true;
			return false;
		}
		ms_frameFenceSubmitted = false;
		releasePendingTextureStaging("present");
		if (tracePresent)
			logLine("present fence ready");

		uint32_t imageIndex = 0;
		VkResult acquire = vkAcquireNextImageKHRLocal(ms_device, ms_swapchain, 16000000, ms_imageAvailableSemaphore, 0, &imageIndex);
		if (tracePresent)
		{
			char buffer[128];
			_snprintf(buffer, sizeof(buffer) - 1, "present acquire result=%d image=%u", static_cast<int>(acquire), static_cast<unsigned>(imageIndex));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
		}
		if (acquire == VK_TIMEOUT)
			return true;
		if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
		{
			destroySwapchain();
			return createSwapchain();
		}
		if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
			return false;

		VkCommandBuffer commandBuffer = ms_commandBuffers[imageIndex];
		vkResetCommandBufferLocal(commandBuffer, 0);
		ULONGLONG const recordStartMs = nowMilliseconds();

		VkCommandBufferBeginInfo beginInfo;
		memset(&beginInfo, 0, sizeof(beginInfo));
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBufferLocal(commandBuffer, &beginInfo);

		bool const useGpuRenderPass = gpuPresentEnabled() && imageIndex < ms_swapchainFramebuffers.size() && ms_swapchainRenderPass;
		int const gpuDrawBatchLimit = getEnvInt("SWG_VULKAN_DRAW_BATCH_LIMIT", 0, 0);
		bool const forceWhiteTextures = gpuForceWhiteTexturesEnabled();
		bool const opaqueTriangleSort = gpuOpaqueTriangleSortEnabled();
		bool const opaqueBatchSort = gpuOpaqueBatchSortEnabled() || opaqueTriangleSort;
		char gpuCaptureSeriesPath[MAX_PATH];
		char gpuCaptureLatestPath[MAX_PATH];
		gpuCaptureSeriesPath[0] = 0;
		gpuCaptureLatestPath[0] = 0;
		bool const gpuCaptureRequested = useGpuRenderPass && ms_gpuReadbackBuffer && ms_gpuReadbackMapped && shouldGpuAutocapture(nextPresentCount, gpuCaptureSeriesPath, sizeof(gpuCaptureSeriesPath), gpuCaptureLatestPath, sizeof(gpuCaptureLatestPath));
		if (useGpuRenderPass && ms_whiteTextureNeedsLayoutTransition && ms_whiteTextureImage)
		{
			VkImageMemoryBarrier textureBarrier;
			memset(&textureBarrier, 0, sizeof(textureBarrier));
			textureBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			textureBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			textureBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			textureBarrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
			textureBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			textureBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			textureBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			textureBarrier.image = ms_whiteTextureImage;
			textureBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			textureBarrier.subresourceRange.levelCount = 1;
			textureBarrier.subresourceRange.layerCount = 1;
			vkCmdPipelineBarrierLocal(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &textureBarrier);
			ms_whiteTextureNeedsLayoutTransition = false;
		}
		if (tracePresent)
		{
			char buffer[192];
			_snprintf(buffer, sizeof(buffer) - 1, "present path=%s image=%u framebuffer=%p extent=%ux%u",
				useGpuRenderPass ? "native-gpu-renderpass" : "cpu-upload-transfer",
				static_cast<unsigned>(imageIndex),
				useGpuRenderPass ? reinterpret_cast<void *>(ms_swapchainFramebuffers[imageIndex]) : 0,
				static_cast<unsigned>(ms_swapchainExtent.width),
				static_cast<unsigned>(ms_swapchainExtent.height));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
		}
		if (useGpuRenderPass)
		{
			float a = static_cast<float>((ms_clearColor >> 24) & 0xff) / 255.0f;
			float r = static_cast<float>((ms_clearColor >> 16) & 0xff) / 255.0f;
			float g = static_cast<float>((ms_clearColor >> 8) & 0xff) / 255.0f;
			float b = static_cast<float>(ms_clearColor & 0xff) / 255.0f;
			VkClearValue clearValues[2];
			memset(clearValues, 0, sizeof(clearValues));
			clearValues[0].color.float32[0] = r;
			clearValues[0].color.float32[1] = g;
			clearValues[0].color.float32[2] = b;
			clearValues[0].color.float32[3] = a;
			clearValues[1].depthStencil.depth = 1.0f;
			clearValues[1].depthStencil.stencil = 0;

			VkRenderPassBeginInfo renderPassBegin;
			memset(&renderPassBegin, 0, sizeof(renderPassBegin));
			renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBegin.renderPass = ms_swapchainRenderPass;
			renderPassBegin.framebuffer = ms_swapchainFramebuffers[imageIndex];
			renderPassBegin.renderArea.offset.x = 0;
			renderPassBegin.renderArea.offset.y = 0;
			renderPassBegin.renderArea.extent = ms_swapchainExtent;
			renderPassBegin.clearValueCount = 2;
			renderPassBegin.pClearValues = clearValues;
			bool const nativeCubeSky = gpuNativeCubeSkyEnabled();
			if (ms_gpuDrawPipeline && ms_gpuDrawPipelineOpaque && ms_gpuDrawPipelineOpaqueNoWrite && ms_gpuDrawPipelineAlphaNoDepth && ms_gpuDrawPipelineAlphaAdd && ms_gpuDrawPipelineAlphaAddNoDepth && ms_gpuDrawPipelineAlphaOneOne && ms_gpuDrawPipelineAlphaOneOneNoDepth && ms_gpuDrawPipelineAlphaSrcColorOne && ms_gpuDrawPipelineAlphaSrcColorOneNoDepth && ms_gpuDrawPipelineActorAux && ms_gpuDrawPipelineCubeAlphaNoDepth && ms_gpuDrawPipelineOpaqueNoDepth && !ms_gpuDrawBatches.empty() && !forceWhiteTextures)
			{
				for (size_t batchIndex = 0; batchIndex < ms_gpuDrawBatches.size(); ++batchIndex)
				{
					GpuDrawBatch const &batch = ms_gpuDrawBatches[batchIndex];
					VulkanTextureData const *textureData = batch.textureData;
					VkDescriptorSet const textureSet = textureData ? ((nativeCubeSky && batch.textureCube) ? textureData->getGpuCubeDescriptorSet(batch.clampSampler) : (batch.alphaMaskTexture ? textureData->getGpuAlphaMaskDescriptorSet() : textureData->getGpuDescriptorSet(batch.clampSampler))) : 0;
					if (textureData && textureSet)
						textureData->recordGpuLayoutTransition(commandBuffer);
					if (batch.actorAuxiliary)
					{
						size_t baseBatchIndex = 0;
						VulkanTextureData const *baseTextureData = findActorAuxBaseTextureData(batchIndex, baseBatchIndex);
						if (baseTextureData && getPairedTextureDescriptorSet(textureData, baseTextureData, batch.clampSampler))
							baseTextureData->recordGpuLayoutTransition(commandBuffer);
					}
				}
			}
			if (ms_gpuTriangleVertexStagingBuffer && ms_gpuTriangleIndexStagingBuffer && ms_gpuTriangleVertexBuffer && ms_gpuTriangleIndexBuffer && ms_gpuTriangleVertexCount > 0 && ms_gpuTriangleIndexCount > 0)
			{
				VkBufferCopy vertexCopy;
				memset(&vertexCopy, 0, sizeof(vertexCopy));
				vertexCopy.size = static_cast<VkDeviceSize>(ms_gpuTriangleVertexCount) * static_cast<VkDeviceSize>(sizeof(GpuScreenVertex));
				vkCmdCopyBufferLocal(commandBuffer, ms_gpuTriangleVertexStagingBuffer, ms_gpuTriangleVertexBuffer, 1, &vertexCopy);

				VkBufferCopy indexCopy;
				memset(&indexCopy, 0, sizeof(indexCopy));
				indexCopy.size = static_cast<VkDeviceSize>(ms_gpuTriangleIndexCount) * static_cast<VkDeviceSize>(sizeof(uint32_t));
				vkCmdCopyBufferLocal(commandBuffer, ms_gpuTriangleIndexStagingBuffer, ms_gpuTriangleIndexBuffer, 1, &indexCopy);

				VkBufferMemoryBarrier barriers[2];
				memset(barriers, 0, sizeof(barriers));
				barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
				barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
				barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[0].buffer = ms_gpuTriangleVertexBuffer;
				barriers[0].size = vertexCopy.size;
				barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
				barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barriers[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
				barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[1].buffer = ms_gpuTriangleIndexBuffer;
				barriers[1].size = indexCopy.size;
				vkCmdPipelineBarrierLocal(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, 0, 2, barriers, 0, 0);
			}
			vkCmdBeginRenderPassLocal(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
			if (ms_gpuDrawPipeline && ms_gpuDrawPipelineOpaque && ms_gpuDrawPipelineOpaqueNoWrite && ms_gpuDrawPipelineAlphaNoDepth && ms_gpuDrawPipelineAlphaAdd && ms_gpuDrawPipelineAlphaAddNoDepth && ms_gpuDrawPipelineAlphaOneOne && ms_gpuDrawPipelineAlphaOneOneNoDepth && ms_gpuDrawPipelineAlphaSrcColorOne && ms_gpuDrawPipelineAlphaSrcColorOneNoDepth && ms_gpuDrawPipelineActorAux && ms_gpuDrawPipelineCubeAlphaNoDepth && ms_gpuDrawPipelineOpaqueNoDepth && ms_gpuTriangleVertexBuffer && ms_gpuTriangleIndexBuffer && ms_gpuTriangleVertexCount > 0 && ms_gpuTriangleIndexCount > 0 && ms_uiConstantsDescriptorSet && ms_whiteTextureDescriptorSet)
			{
				VkViewport viewport;
				memset(&viewport, 0, sizeof(viewport));
				viewport.x = 0.0f;
				viewport.y = 0.0f;
				viewport.width = static_cast<float>(ms_swapchainExtent.width);
				viewport.height = static_cast<float>(ms_swapchainExtent.height);
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				VkRect2D scissor;
				memset(&scissor, 0, sizeof(scissor));
				scissor.extent = ms_swapchainExtent;

				VkDeviceSize vertexOffset = 0;
				VkPipeline currentPipeline = 0;
				vkCmdSetViewportLocal(commandBuffer, 0, 1, &viewport);
				vkCmdSetScissorLocal(commandBuffer, 0, 1, &scissor);
				vkCmdBindDescriptorSetsLocal(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ms_gpuDrawPipelineLayout, 1, 1, &ms_uiConstantsDescriptorSet, 0, 0);
				vkCmdBindVertexBuffersLocal(commandBuffer, 0, 1, &ms_gpuTriangleVertexBuffer, &vertexOffset);
				vkCmdBindIndexBufferLocal(commandBuffer, ms_gpuTriangleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
				unsigned realTextureBatches = 0;
				unsigned fallbackBatches = 0;
				unsigned missingTextureBatches = 0;
				unsigned pendingTextureBatches = 0;
				unsigned untexturedColorBatches = 0;
				unsigned alphaMaskTextureBatches = 0;
				unsigned alphaBatches = 0;
				unsigned opaqueBatches = 0;
				unsigned actorBatches = 0;
				unsigned actorAuxiliaryBatches = 0;
				unsigned drawCalls = 0;
				static int s_fallbackBatchLogCount = 0;
				static int s_suspiciousBatchLogCount = 0;
				static int s_actorAuxLinkLogCount = 0;
				static int s_edgeWorldBatchLogCount = 0;
				static int s_uiRadarBatchLogCount = 0;
				static int s_topWorldBatchLogCount = 0;
				static int s_alphaBlendStateLogCount = 0;
				static bool s_batchAuditLogged = false;
				bool const batchDiagnostics = gpuBatchDiagnosticsEnabled();
				if (ms_gpuDrawBatches.empty())
				{
					currentPipeline = ms_gpuDrawPipeline;
					vkCmdBindPipelineLocal(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline);
					vkCmdBindDescriptorSetsLocal(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ms_gpuDrawPipelineLayout, 2, 1, &ms_whiteTextureDescriptorSet, 0, 0);
					GpuDrawPushConstants constants;
					makeGpuDrawPushConstants(0, constants);
					vkCmdPushConstantsLocal(commandBuffer, ms_gpuDrawPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
					vkCmdDrawIndexedLocal(commandBuffer, ms_gpuTriangleIndexCount, 1, 0, 0, 0);
					++fallbackBatches;
					++alphaBatches;
					++drawCalls;
				}
				else
				{
					size_t const batchCount = gpuDrawBatchLimit > 0 ? std::min<size_t>(ms_gpuDrawBatches.size(), static_cast<size_t>(gpuDrawBatchLimit)) : ms_gpuDrawBatches.size();
					std::vector<size_t> sortedDrawOrder;
					if (opaqueBatchSort)
					{
						sortedDrawOrder.reserve(batchCount);
						for (size_t batchIndex = 0; batchIndex < batchCount; ++batchIndex)
							sortedDrawOrder.push_back(batchIndex);
						std::stable_sort(sortedDrawOrder.begin(), sortedDrawOrder.end(), [](size_t lhs, size_t rhs) {
							GpuDrawBatch const &a = ms_gpuDrawBatches[lhs];
							GpuDrawBatch const &b = ms_gpuDrawBatches[rhs];
							if (a.alphaBlend || b.alphaBlend)
								return lhs < rhs;
							return a.sortDepth > b.sortDepth;
						});
					}
					for (size_t orderIndex = 0; orderIndex < batchCount; ++orderIndex)
					{
						size_t const batchIndex = opaqueBatchSort ? sortedDrawOrder[orderIndex] : orderIndex;
						GpuDrawBatch const &batch = ms_gpuDrawBatches[batchIndex];
						float const diagnosticViewportWidth = static_cast<float>(ms_viewportWidth > 0 ? ms_viewportWidth : std::max(1, ms_width));
						float const diagnosticViewportHeight = static_cast<float>(ms_viewportHeight > 0 ? ms_viewportHeight : std::max(1, ms_height));
						if (batch.worldGeometry &&
							!batch.textureData &&
							batch.shaderName &&
							strstr(batch.shaderName, "shader/3d_vertexcolorac.sht"))
						{
							static int s_dropUntexturedParticleLogCount = 0;
							if (batchDiagnostics && s_dropUntexturedParticleLogCount < 64)
							{
								char dropText[512];
								_snprintf(dropText, sizeof(dropText) - 1, "gpu drop untextured particle marker present=%d order=%u batch=%u shader=%s vertices=%u alpha=%d depth=%d/%d bounds=%.1f,%.1f..%.1f,%.1f avgRgba=%.1f,%.1f,%.1f,%.1f",
									nextPresentCount,
									static_cast<unsigned>(orderIndex),
									static_cast<unsigned>(batchIndex),
									batch.shaderName,
									static_cast<unsigned>(batch.vertexCount),
									batch.alphaBlend ? 1 : 0,
									batch.depthTest ? 1 : 0,
									batch.depthWrite ? 1 : 0,
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY,
									batch.vertexCount ? batch.colorSum[0] / static_cast<float>(batch.vertexCount) : 0.0f,
									batch.vertexCount ? batch.colorSum[1] / static_cast<float>(batch.vertexCount) : 0.0f,
									batch.vertexCount ? batch.colorSum[2] / static_cast<float>(batch.vertexCount) : 0.0f,
									batch.vertexCount ? batch.colorSum[3] / static_cast<float>(batch.vertexCount) : 0.0f);
								dropText[sizeof(dropText) - 1] = 0;
								logLine(dropText);
								++s_dropUntexturedParticleLogCount;
							}
							continue;
						}
						if (gpuDropBlackSkyboxEnabled() &&
							batch.worldGeometry &&
							batch.textureCube &&
							batch.textureData &&
							batch.shaderName &&
							strstr(batch.shaderName, "shader/skybox.sht") &&
							batch.textureData->getWidth() <= 64 &&
							batch.textureData->getHeight() <= 64)
						{
							uint32 const skyboxSample = batch.textureData->sample(0.5f, 0.5f, true);
							uint32 const skyboxR = (skyboxSample >> 16) & 0xff;
							uint32 const skyboxG = (skyboxSample >> 8) & 0xff;
							uint32 const skyboxB = skyboxSample & 0xff;
							if (skyboxR <= 4 && skyboxG <= 4 && skyboxB <= 4)
							{
								static int s_dropBlackSkyboxLogCount = 0;
								if (batchDiagnostics && s_dropBlackSkyboxLogCount < 64)
								{
									char dropText[640];
									_snprintf(dropText, sizeof(dropText) - 1, "gpu drop black skybox present=%d order=%u batch=%u sample=0x%08x textureSize=%dx%d vertices=%u bounds=%.1f,%.1f..%.1f,%.1f uv=%.3f,%.3f..%.3f,%.3f",
										nextPresentCount,
										static_cast<unsigned>(orderIndex),
										static_cast<unsigned>(batchIndex),
										static_cast<unsigned>(skyboxSample),
										batch.textureData->getWidth(),
										batch.textureData->getHeight(),
										static_cast<unsigned>(batch.vertexCount),
										batch.minX,
										batch.minY,
										batch.maxX,
										batch.maxY,
										batch.minU,
										batch.minV,
										batch.maxU,
										batch.maxV);
									dropText[sizeof(dropText) - 1] = 0;
									logLine(dropText);
									++s_dropBlackSkyboxLogCount;
								}
								continue;
							}
						}
						if (gpuDropTerrainWideTriangleEnabled() &&
							batch.worldGeometry &&
							batch.shaderName &&
							strstr(batch.shaderName, "shader/terrain_blend") &&
							batch.maxX > diagnosticViewportWidth * 0.90f &&
							batch.maxY > diagnosticViewportHeight * 0.95f &&
							batch.minY > diagnosticViewportHeight * 0.25f)
						{
							static int s_dropTerrainWideBatchLogCount = 0;
							if (batchDiagnostics && s_dropTerrainWideBatchLogCount < 96)
							{
								char dropText[640];
								_snprintf(dropText, sizeof(dropText) - 1, "gpu probe drop terrain wide batch present=%d order=%u batch=%u shader=%s texture=%s vertices=%u alpha=%d depthWrite=%d bounds=%.1f,%.1f..%.1f,%.1f w=%.4f..%.4f uv=%.3f,%.3f..%.3f,%.3f",
									nextPresentCount,
									static_cast<unsigned>(orderIndex),
									static_cast<unsigned>(batchIndex),
									batch.shaderName,
									batch.textureData ? batch.textureData->getName() : "<none>",
									static_cast<unsigned>(batch.vertexCount),
									batch.alphaBlend ? 1 : 0,
									batch.depthWrite ? 1 : 0,
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY,
									batch.minW,
									batch.maxW,
									batch.minU,
									batch.minV,
									batch.maxU,
									batch.maxV);
								dropText[sizeof(dropText) - 1] = 0;
								logLine(dropText);
								++s_dropTerrainWideBatchLogCount;
							}
							continue;
						}
						if (gpuDropTerrainEdgeOpaqueEnabled() &&
							batch.worldGeometry &&
							!batch.alphaBlend &&
							batch.depthWrite &&
							batch.shaderName &&
							strstr(batch.shaderName, "shader/terrain_blend0") &&
							batch.maxX > diagnosticViewportWidth * 0.95f &&
							batch.maxY > diagnosticViewportHeight * 0.95f)
						{
							static int s_dropTerrainEdgeOpaqueLogCount = 0;
							if (batchDiagnostics && s_dropTerrainEdgeOpaqueLogCount < 64)
							{
								char dropText[512];
								_snprintf(dropText, sizeof(dropText) - 1, "gpu probe drop terrain edge opaque present=%d order=%u batch=%u texture=%s vertices=%u bounds=%.1f,%.1f..%.1f,%.1f depth=%.4f uv=%.3f,%.3f..%.3f,%.3f",
									nextPresentCount,
									static_cast<unsigned>(orderIndex),
									static_cast<unsigned>(batchIndex),
									batch.textureData ? batch.textureData->getName() : "<none>",
									static_cast<unsigned>(batch.vertexCount),
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY,
									batch.sortDepth,
									batch.minU,
									batch.minV,
									batch.maxU,
									batch.maxV);
								dropText[sizeof(dropText) - 1] = 0;
								logLine(dropText);
								++s_dropTerrainEdgeOpaqueLogCount;
							}
							continue;
						}
						VkPipeline batchPipeline = 0;
						if (nativeCubeSky && batch.textureCube)
							batchPipeline = ms_gpuDrawPipelineCubeAlphaNoDepth;
						else if (batch.actorAuxiliary)
							batchPipeline = ms_gpuDrawPipelineActorAux;
						else if (batch.alphaBlend)
						{
							char const *alphaPipelineName = 0;
							batchPipeline = selectGpuAlphaPipeline(batch, &alphaPipelineName);
							if (batchDiagnostics &&
								s_alphaBlendStateLogCount < 192 &&
								(!isStandardAlphaBlend(batch.alphaBlendOperation, batch.alphaBlendSource, batch.alphaBlendDestination) ||
								 strstr(alphaPipelineName ? alphaPipelineName : "", "one")))
							{
								char blendText[512];
								_snprintf(blendText, sizeof(blendText) - 1, "gpu alpha blend state present=%d order=%u batch=%u shader=%s texture=%s op=%d src=%d dst=%d pipeline=%s depth=%d vertices=%u bounds=%.1f,%.1f..%.1f,%.1f",
									nextPresentCount,
									static_cast<unsigned>(orderIndex),
									static_cast<unsigned>(batchIndex),
									batch.shaderName ? batch.shaderName : "<null>",
									batch.textureData ? batch.textureData->getName() : "<none>",
									static_cast<int>(batch.alphaBlendOperation),
									static_cast<int>(batch.alphaBlendSource),
									static_cast<int>(batch.alphaBlendDestination),
									alphaPipelineName ? alphaPipelineName : "<null>",
									batch.depthTest ? 1 : 0,
									static_cast<unsigned>(batch.vertexCount),
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY);
								blendText[sizeof(blendText) - 1] = 0;
								logLine(blendText);
								++s_alphaBlendStateLogCount;
							}
						}
						else if (!batch.depthTest)
							batchPipeline = ms_gpuDrawPipelineOpaqueNoDepth;
						else
							batchPipeline = batch.depthWrite ? ms_gpuDrawPipelineOpaque : ms_gpuDrawPipelineOpaqueNoWrite;
						if (currentPipeline != batchPipeline)
						{
							currentPipeline = batchPipeline;
							vkCmdBindPipelineLocal(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline);
						}
						VkRect2D batchScissor;
						memset(&batchScissor, 0, sizeof(batchScissor));
						batchScissor.offset.x = std::max(0, batch.scissorX);
						batchScissor.offset.y = std::max(0, batch.scissorY);
						batchScissor.extent.width = static_cast<uint32_t>(std::max(0, batch.scissorWidth));
						batchScissor.extent.height = static_cast<uint32_t>(std::max(0, batch.scissorHeight));
						if (batchScissor.extent.width == 0 || batchScissor.extent.height == 0)
							continue;
						vkCmdSetScissorLocal(commandBuffer, 0, 1, &batchScissor);
						if (batch.alphaBlend)
							++alphaBatches;
						else
							++opaqueBatches;
						if (batch.actorBody)
							++actorBatches;
						if (batch.actorAuxiliary)
							++actorAuxiliaryBatches;
						if (batch.alphaMaskTexture)
							++alphaMaskTextureBatches;
						if (batchDiagnostics && s_topWorldBatchLogCount < 192 && nextPresentCount >= 240 && batch.worldGeometry)
						{
							float const viewportWidth = static_cast<float>(ms_viewportWidth > 0 ? ms_viewportWidth : std::max(1, ms_width));
							float const viewportHeight = static_cast<float>(ms_viewportHeight > 0 ? ms_viewportHeight : std::max(1, ms_height));
							bool const skyboxBatch = batch.shaderName && strstr(batch.shaderName, "shader/skybox.sht");
							bool const touchesTopBand =
								!skyboxBatch &&
								batch.minY < viewportHeight * 0.38f &&
								batch.maxX > viewportWidth * 0.35f &&
								batch.minX < viewportWidth * 0.95f &&
								(batch.maxTriangleSpanX > viewportWidth * 0.25f || batch.maxX - batch.minX > viewportWidth * 0.35f);
							if (touchesTopBand)
							{
								float const invVertexCount = batch.vertexCount ? 1.0f / static_cast<float>(batch.vertexCount) : 0.0f;
								float const invTextureSampleCount = batch.textureSampleCount ? 1.0f / static_cast<float>(batch.textureSampleCount) : 0.0f;
								char topText[1120];
								_snprintf(topText, sizeof(topText) - 1, "gpu top world candidate present=%d order=%u batch=%u shader=%s pixelMode=%d textureTag=0x%08x texture=%p textureName=%s textureSize=%dx%d vertices=%u alpha=%d alphaTest=%d ref=%u depthTest=%d depthWrite=%d lighting=%d spec=%d texSet=%d clamp=%d cube=%d bounds=%.1f,%.1f..%.1f,%.1f span=%.1f,%.1f w=%.4f..%.4f depth=%.4f uv=%.3f,%.3f..%.3f,%.3f avgRgba=%.1f,%.1f,%.1f,%.1f avgTex=%.1f,%.1f,%.1f,%.1f",
									nextPresentCount,
									static_cast<unsigned>(orderIndex),
									static_cast<unsigned>(batchIndex),
									batch.shaderName ? batch.shaderName : "<null>",
									batch.pixelProgramMode,
									static_cast<unsigned int>(batch.textureTag),
									static_cast<void const *>(batch.textureData),
									batch.textureData ? batch.textureData->getName() : "<none>",
									batch.textureData ? batch.textureData->getWidth() : 0,
									batch.textureData ? batch.textureData->getHeight() : 0,
									static_cast<unsigned>(batch.vertexCount),
									batch.alphaBlend ? 1 : 0,
									batch.alphaTest ? 1 : 0,
									static_cast<unsigned>(batch.alphaTestReference),
									batch.depthTest ? 1 : 0,
									batch.depthWrite ? 1 : 0,
									batch.lightingEnabled ? 1 : 0,
									batch.lightingSpecularEnabled ? 1 : 0,
									batch.textureCoordinateSet,
									batch.clampSampler ? 1 : 0,
									batch.textureCube ? 1 : 0,
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY,
									batch.maxTriangleSpanX,
									batch.maxTriangleSpanY,
									batch.minW,
									batch.maxW,
									batch.sortDepth,
									batch.minU,
									batch.minV,
									batch.maxU,
									batch.maxV,
									batch.colorSum[0] * invVertexCount,
									batch.colorSum[1] * invVertexCount,
									batch.colorSum[2] * invVertexCount,
									batch.colorSum[3] * invVertexCount,
									batch.textureSampleSum[0] * invTextureSampleCount,
									batch.textureSampleSum[1] * invTextureSampleCount,
									batch.textureSampleSum[2] * invTextureSampleCount,
									batch.textureSampleSum[3] * invTextureSampleCount);
								topText[sizeof(topText) - 1] = 0;
								logLine(topText);
								++s_topWorldBatchLogCount;
							}
						}
						if (batchDiagnostics && s_uiRadarBatchLogCount < 192 && nextPresentCount >= 180 && !batch.worldGeometry)
						{
							float const viewportWidth = static_cast<float>(ms_viewportWidth > 0 ? ms_viewportWidth : std::max(1, ms_width));
							float const viewportHeight = static_cast<float>(ms_viewportHeight > 0 ? ms_viewportHeight : std::max(1, ms_height));
							float const centerX = (batch.minX + batch.maxX) * 0.5f;
							float const centerY = (batch.minY + batch.maxY) * 0.5f;
							float const width = batch.maxX - batch.minX;
							float const height = batch.maxY - batch.minY;
							bool const topRightUi =
								batch.maxX > viewportWidth * 0.72f &&
								batch.minY < viewportHeight * 0.28f &&
								batch.maxY < viewportHeight * 0.38f &&
								width > 8.0f &&
								height > 8.0f;
							bool const radarSized =
								width > viewportWidth * 0.035f &&
								width < viewportWidth * 0.20f &&
								height > viewportHeight * 0.035f &&
								height < viewportHeight * 0.24f;
							if (topRightUi && radarSized)
							{
								float const invVertexCount = batch.vertexCount ? 1.0f / static_cast<float>(batch.vertexCount) : 0.0f;
								float const invTextureSampleCount = batch.textureSampleCount ? 1.0f / static_cast<float>(batch.textureSampleCount) : 0.0f;
								uint32 const centerSample = batch.textureData ? batch.textureData->sample(0.5f, 0.5f, true) : 0xffffffff;
								uint32 const cornerSample = batch.textureData ? batch.textureData->sample(batch.minU, batch.minV, true) : 0xffffffff;
								char radarText[1280];
								_snprintf(radarText, sizeof(radarText) - 1, "gpu ui radar candidate present=%d order=%u batch=%u shader=%s pixelMode=%d pixelProgram=%s textureTag=0x%08x texture=%p textureName=%s textureSize=%dx%d textureFormat=%d centerSample=0x%08x cornerSample=0x%08x vertices=%u alpha=%d alphaTest=%d cmp=%d ref=%u alphaMask=%d depthTest=%d depthWrite=%d lighting=%d spec=%d texSet=%d clamp=%d cube=%d stageCount=%d stage0=%d/%d,c=%d,%d,%d,a=%d,%d,%d bounds=%.1f,%.1f..%.1f,%.1f center=%.1f,%.1f size=%.1fx%.1f depth=%.4f uv=%.3f,%.3f..%.3f,%.3f avgRgba=%.1f,%.1f,%.1f,%.1f avgTex=%.1f,%.1f,%.1f,%.1f",
									nextPresentCount,
									static_cast<unsigned>(orderIndex),
									static_cast<unsigned>(batchIndex),
									batch.shaderName ? batch.shaderName : "<null>",
									batch.pixelProgramMode,
									batch.pixelProgramName ? batch.pixelProgramName : "<none>",
									static_cast<unsigned int>(batch.textureTag),
									static_cast<void const *>(batch.textureData),
									batch.textureData ? batch.textureData->getName() : "<none>",
									batch.textureData ? batch.textureData->getWidth() : 0,
									batch.textureData ? batch.textureData->getHeight() : 0,
									batch.textureData ? static_cast<int>(batch.textureData->getFormat()) : -1,
									static_cast<unsigned int>(centerSample),
									static_cast<unsigned int>(cornerSample),
									static_cast<unsigned>(batch.vertexCount),
									batch.alphaBlend ? 1 : 0,
									batch.alphaTest ? 1 : 0,
									static_cast<int>(batch.alphaTestCompare),
									static_cast<unsigned>(batch.alphaTestReference),
									batch.alphaMaskTexture ? 1 : 0,
									batch.depthTest ? 1 : 0,
									batch.depthWrite ? 1 : 0,
									batch.lightingEnabled ? 1 : 0,
									batch.lightingSpecularEnabled ? 1 : 0,
									batch.textureCoordinateSet,
									batch.clampSampler ? 1 : 0,
									batch.textureCube ? 1 : 0,
									batch.stageTextureCount,
									batch.stageColorOperation[0],
									batch.stageAlphaOperation[0],
									batch.stageColorArgument0[0],
									batch.stageColorArgument1[0],
									batch.stageColorArgument2[0],
									batch.stageAlphaArgument0[0],
									batch.stageAlphaArgument1[0],
									batch.stageAlphaArgument2[0],
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY,
									centerX,
									centerY,
									width,
									height,
									batch.sortDepth,
									batch.minU,
									batch.minV,
									batch.maxU,
									batch.maxV,
									batch.colorSum[0] * invVertexCount,
									batch.colorSum[1] * invVertexCount,
									batch.colorSum[2] * invVertexCount,
									batch.colorSum[3] * invVertexCount,
									batch.textureSampleSum[0] * invTextureSampleCount,
									batch.textureSampleSum[1] * invTextureSampleCount,
									batch.textureSampleSum[2] * invTextureSampleCount,
									batch.textureSampleSum[3] * invTextureSampleCount);
								radarText[sizeof(radarText) - 1] = 0;
								logLine(radarText);
								++s_uiRadarBatchLogCount;
							}
						}
						if (batchDiagnostics && s_edgeWorldBatchLogCount < 160 && nextPresentCount >= 240 && batch.worldGeometry)
						{
							float const viewportWidth = static_cast<float>(ms_viewportWidth > 0 ? ms_viewportWidth : std::max(1, ms_width));
							float const viewportHeight = static_cast<float>(ms_viewportHeight > 0 ? ms_viewportHeight : std::max(1, ms_height));
							bool const touchesLowerRight =
								batch.maxX > viewportWidth * 0.72f &&
								batch.maxY > viewportHeight * 0.62f &&
								(batch.minX > viewportWidth * 0.35f || batch.maxTriangleSpanX > viewportWidth * 0.22f || batch.maxTriangleSpanY > viewportHeight * 0.22f);
							if (touchesLowerRight)
							{
								float const invVertexCount = batch.vertexCount ? 1.0f / static_cast<float>(batch.vertexCount) : 0.0f;
								float const invTextureSampleCount = batch.textureSampleCount ? 1.0f / static_cast<float>(batch.textureSampleCount) : 0.0f;
								char edgeText[960];
								_snprintf(edgeText, sizeof(edgeText) - 1, "gpu edge world batch present=%d order=%u batch=%u shader=%s pixelMode=%d pixelProgram=%s textureTag=0x%08x texture=%p textureName=%s textureSize=%dx%d vertices=%u alpha=%d alphaTest=%d ref=%u depthTest=%d depthWrite=%d lighting=%d spec=%d texSet=%d clamp=%d cube=%d bounds=%.1f,%.1f..%.1f,%.1f span=%.1f,%.1f w=%.4f..%.4f depth=%.4f uv=%.3f,%.3f..%.3f,%.3f avgRgba=%.1f,%.1f,%.1f,%.1f avgTex=%.1f,%.1f,%.1f,%.1f",
									nextPresentCount,
									static_cast<unsigned>(orderIndex),
									static_cast<unsigned>(batchIndex),
									batch.shaderName ? batch.shaderName : "<null>",
									batch.pixelProgramMode,
									batch.pixelProgramName ? batch.pixelProgramName : "<none>",
									static_cast<unsigned int>(batch.textureTag),
									static_cast<void const *>(batch.textureData),
									batch.textureData ? batch.textureData->getName() : "<none>",
									batch.textureData ? batch.textureData->getWidth() : 0,
									batch.textureData ? batch.textureData->getHeight() : 0,
									static_cast<unsigned>(batch.vertexCount),
									batch.alphaBlend ? 1 : 0,
									batch.alphaTest ? 1 : 0,
									static_cast<unsigned>(batch.alphaTestReference),
									batch.depthTest ? 1 : 0,
									batch.depthWrite ? 1 : 0,
									batch.lightingEnabled ? 1 : 0,
									batch.lightingSpecularEnabled ? 1 : 0,
									batch.textureCoordinateSet,
									batch.clampSampler ? 1 : 0,
									batch.textureCube ? 1 : 0,
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY,
									batch.maxTriangleSpanX,
									batch.maxTriangleSpanY,
									batch.minW,
									batch.maxW,
									batch.sortDepth,
									batch.minU,
									batch.minV,
									batch.maxU,
									batch.maxV,
									batch.colorSum[0] * invVertexCount,
									batch.colorSum[1] * invVertexCount,
									batch.colorSum[2] * invVertexCount,
									batch.colorSum[3] * invVertexCount,
									batch.textureSampleSum[0] * invTextureSampleCount,
									batch.textureSampleSum[1] * invTextureSampleCount,
									batch.textureSampleSum[2] * invTextureSampleCount,
									batch.textureSampleSum[3] * invTextureSampleCount);
								edgeText[sizeof(edgeText) - 1] = 0;
								logLine(edgeText);
								++s_edgeWorldBatchLogCount;
							}
						}
						if (batchDiagnostics && batch.suspiciousTriangles > 0 && s_suspiciousBatchLogCount < 220 && (nextPresentCount >= 90 || batch.worldGeometry))
						{
							float const invVertexCount = batch.vertexCount ? 1.0f / static_cast<float>(batch.vertexCount) : 0.0f;
							char suspiciousText[512];
							_snprintf(suspiciousText, sizeof(suspiciousText) - 1, "gpu suspicious batch present=%d batch=%u shader=%s textureTag=0x%08x texture=%p vertices=%u suspicious=%u world=%d alpha=%d depthTest=%d depthWrite=%d texSet=%d depth=%.4f bounds=%.1f,%.1f..%.1f,%.1f span=%.1f,%.1f w=%.4f..%.4f uv=%.3f,%.3f..%.3f,%.3f avgRgba=%.1f,%.1f,%.1f,%.1f",
								nextPresentCount,
								static_cast<unsigned>(batchIndex),
								batch.shaderName ? batch.shaderName : "<null>",
								static_cast<unsigned int>(batch.textureTag),
								static_cast<void const *>(batch.textureData),
								static_cast<unsigned>(batch.vertexCount),
								static_cast<unsigned>(batch.suspiciousTriangles),
								batch.worldGeometry ? 1 : 0,
								batch.alphaBlend ? 1 : 0,
								batch.depthTest ? 1 : 0,
								batch.depthWrite ? 1 : 0,
								batch.textureCoordinateSet,
								batch.sortDepth,
								batch.minX,
								batch.minY,
								batch.maxX,
								batch.maxY,
								batch.maxTriangleSpanX,
								batch.maxTriangleSpanY,
								batch.minW,
								batch.maxW,
								batch.minU,
								batch.minV,
								batch.maxU,
								batch.maxV,
								batch.colorSum[0] * invVertexCount,
								batch.colorSum[1] * invVertexCount,
								batch.colorSum[2] * invVertexCount,
								batch.colorSum[3] * invVertexCount);
							suspiciousText[sizeof(suspiciousText) - 1] = 0;
							logLine(suspiciousText);
							++s_suspiciousBatchLogCount;
						}
						static int s_darkWorldBatchLogCount = 0;
						if (batchDiagnostics && s_darkWorldBatchLogCount < 160 && batch.worldGeometry && !batch.actorBody && batch.vertexCount >= 6)
						{
							float const invVertexCount = batch.vertexCount ? 1.0f / static_cast<float>(batch.vertexCount) : 0.0f;
							float const avgR = batch.colorSum[0] * invVertexCount;
							float const avgG = batch.colorSum[1] * invVertexCount;
							float const avgB = batch.colorSum[2] * invVertexCount;
							float const avgA = batch.colorSum[3] * invVertexCount;
							bool const darkOrPurple =
								(avgA > 8.0f) &&
								((avgR < 100.0f && avgG < 100.0f && avgB < 120.0f) ||
								 (avgR > avgG + 12.0f && avgB > avgG + 12.0f));
							if (darkOrPurple)
							{
								float const invTextureSampleCount = batch.textureSampleCount ? 1.0f / static_cast<float>(batch.textureSampleCount) : 0.0f;
								char darkText[1024];
								_snprintf(darkText, sizeof(darkText) - 1, "gpu dark world batch present=%d order=%u batch=%u shader=%s pixelProgram=%s textureTag=0x%08x textureName=%s vertices=%u alpha=%d alphaTest=%d ref=%u depthTest=%d depthWrite=%d lighting=%d spec=%d texSet=%d clamp=%d bounds=%.1f,%.1f..%.1f,%.1f depth=%.4f uv=%.3f,%.3f..%.3f,%.3f avgRgba=%.1f,%.1f,%.1f,%.1f avgTex=%.1f,%.1f,%.1f,%.1f tf=%d %.3f,%.3f,%.3f,%.3f",
									nextPresentCount,
									static_cast<unsigned>(orderIndex),
									static_cast<unsigned>(batchIndex),
									batch.shaderName ? batch.shaderName : "<null>",
									batch.pixelProgramName ? batch.pixelProgramName : "<none>",
									static_cast<unsigned int>(batch.textureTag),
									batch.textureData ? batch.textureData->getName() : "<none>",
									static_cast<unsigned>(batch.vertexCount),
									batch.alphaBlend ? 1 : 0,
									batch.alphaTest ? 1 : 0,
									static_cast<unsigned>(batch.alphaTestReference),
									batch.depthTest ? 1 : 0,
									batch.depthWrite ? 1 : 0,
									batch.lightingEnabled ? 1 : 0,
									batch.lightingSpecularEnabled ? 1 : 0,
									batch.textureCoordinateSet,
									batch.clampSampler ? 1 : 0,
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY,
									batch.sortDepth,
									batch.minU,
									batch.minV,
									batch.maxU,
									batch.maxV,
									avgR,
									avgG,
									avgB,
									avgA,
									batch.textureSampleSum[0] * invTextureSampleCount,
									batch.textureSampleSum[1] * invTextureSampleCount,
									batch.textureSampleSum[2] * invTextureSampleCount,
									batch.textureSampleSum[3] * invTextureSampleCount,
									batch.textureFactorValid ? 1 : 0,
									batch.textureFactor[0],
									batch.textureFactor[1],
									batch.textureFactor[2],
									batch.textureFactor[3]);
								darkText[sizeof(darkText) - 1] = 0;
								logLine(darkText);
								++s_darkWorldBatchLogCount;
							}
						}
						bool const hadTextureData = batch.textureData != 0;
						VkDescriptorSet textureSet = (!forceWhiteTextures && batch.textureData) ? ((nativeCubeSky && batch.textureCube) ? batch.textureData->getGpuCubeDescriptorSet(batch.clampSampler) : (batch.alphaMaskTexture ? batch.textureData->getGpuAlphaMaskDescriptorSet() : batch.textureData->getGpuDescriptorSet(batch.clampSampler))) : 0;
						if (!forceWhiteTextures && batch.textureData && batch.secondaryTextureData)
						{
							VkDescriptorSet const pairedTextureSet = getPairedTextureDescriptorSet(batch.textureData, batch.secondaryTextureData, batch.clampSampler, batch.terrainAlphaStage);
							if (pairedTextureSet)
								textureSet = pairedTextureSet;
						}
						size_t pairedBaseBatchIndex = 0;
						VulkanTextureData const *pairedBaseTextureData = 0;
						if (!forceWhiteTextures && batch.actorAuxiliary && batch.textureData)
						{
							pairedBaseTextureData = findActorAuxBaseTextureData(batchIndex, pairedBaseBatchIndex);
							VkDescriptorSet const pairedTextureSet = getPairedTextureDescriptorSet(batch.textureData, pairedBaseTextureData, batch.clampSampler);
							if (pairedTextureSet)
								textureSet = pairedTextureSet;
						}
						if (batchDiagnostics && !s_batchAuditLogged && nextPresentCount >= 240)
						{
							s_batchAuditLogged = true;
							unsigned const auditCount = static_cast<unsigned>(std::min<size_t>(batchCount, 96));
							char auditHeader[160];
							_snprintf(auditHeader, sizeof(auditHeader) - 1, "gpu batch audit present=%d batches=%u auditCount=%u", nextPresentCount, static_cast<unsigned>(batchCount), auditCount);
							auditHeader[sizeof(auditHeader) - 1] = 0;
							logLine(auditHeader);
							for (size_t auditIndex = 0; auditIndex < auditCount; ++auditIndex)
							{
								GpuDrawBatch const &auditBatch = ms_gpuDrawBatches[auditIndex];
								float const invVertexCount = auditBatch.vertexCount ? 1.0f / static_cast<float>(auditBatch.vertexCount) : 0.0f;
								bool const auditHadTextureData = auditBatch.textureData != 0;
								VkDescriptorSet const auditTextureSet = (!forceWhiteTextures && auditBatch.textureData) ? ((nativeCubeSky && auditBatch.textureCube) ? auditBatch.textureData->getGpuCubeDescriptorSet(auditBatch.clampSampler) : (auditBatch.alphaMaskTexture ? auditBatch.textureData->getGpuAlphaMaskDescriptorSet() : auditBatch.textureData->getGpuDescriptorSet(auditBatch.clampSampler))) : 0;
								uint32 const auditSample = auditBatch.textureData ? auditBatch.textureData->sample(0.5f, 0.5f, true) : 0xffffffff;
								char auditText[1280];
								_snprintf(auditText, sizeof(auditText) - 1, "gpu batch audit entry batch=%u shader=%s pixelMode=%d pixelProgram=%s stageCount=%d stage0=%d/%d,c=%d,%d,%d,a=%d,%d,%d stage1=%d/%d,c=%d,%d,%d,a=%d,%d,%d textureTag=0x%08x texture=%p textureName=%s textureSize=%dx%d textureFormat=%d textureSample=0x%08x descriptor=%d clamp=%d cube=%d texSet=%d vertices=%u world=%d actor=%d actorAux=%d alpha=%d alphaTest=%d cmp=%d ref=%u alphaMask=%d depthTest=%d depthWrite=%d lighting=%d spec=%d tf=%d %.3f,%.3f,%.3f,%.3f tf2=%.3f,%.3f,%.3f,%.3f bounds=%.1f,%.1f..%.1f,%.1f depth=%.4f uv=%.3f,%.3f..%.3f,%.3f avgRgba=%.1f,%.1f,%.1f,%.1f hadTexture=%d",
									static_cast<unsigned>(auditIndex),
									auditBatch.shaderName ? auditBatch.shaderName : "<null>",
									auditBatch.pixelProgramMode,
									auditBatch.pixelProgramName ? auditBatch.pixelProgramName : "<none>",
									auditBatch.stageTextureCount,
									auditBatch.stageColorOperation[0],
									auditBatch.stageAlphaOperation[0],
									auditBatch.stageColorArgument0[0],
									auditBatch.stageColorArgument1[0],
									auditBatch.stageColorArgument2[0],
									auditBatch.stageAlphaArgument0[0],
									auditBatch.stageAlphaArgument1[0],
									auditBatch.stageAlphaArgument2[0],
									auditBatch.stageColorOperation[1],
									auditBatch.stageAlphaOperation[1],
									auditBatch.stageColorArgument0[1],
									auditBatch.stageColorArgument1[1],
									auditBatch.stageColorArgument2[1],
									auditBatch.stageAlphaArgument0[1],
									auditBatch.stageAlphaArgument1[1],
									auditBatch.stageAlphaArgument2[1],
									static_cast<unsigned int>(auditBatch.textureTag),
									static_cast<void const *>(auditBatch.textureData),
									auditBatch.textureData ? auditBatch.textureData->getName() : "<none>",
									auditBatch.textureData ? auditBatch.textureData->getWidth() : 0,
									auditBatch.textureData ? auditBatch.textureData->getHeight() : 0,
									auditBatch.textureData ? static_cast<int>(auditBatch.textureData->getFormat()) : -1,
									static_cast<unsigned int>(auditSample),
									auditTextureSet ? 1 : 0,
									auditBatch.clampSampler ? 1 : 0,
									auditBatch.textureCube ? 1 : 0,
									auditBatch.textureCoordinateSet,
									static_cast<unsigned>(auditBatch.vertexCount),
									auditBatch.worldGeometry ? 1 : 0,
									auditBatch.actorBody ? 1 : 0,
									auditBatch.actorAuxiliary ? 1 : 0,
									auditBatch.alphaBlend ? 1 : 0,
									auditBatch.alphaTest ? 1 : 0,
									static_cast<int>(auditBatch.alphaTestCompare),
									static_cast<unsigned>(auditBatch.alphaTestReference),
									auditBatch.alphaMaskTexture ? 1 : 0,
									auditBatch.depthTest ? 1 : 0,
									auditBatch.depthWrite ? 1 : 0,
									auditBatch.lightingEnabled ? 1 : 0,
									auditBatch.lightingSpecularEnabled ? 1 : 0,
									auditBatch.textureFactorValid ? 1 : 0,
									auditBatch.textureFactor[0],
									auditBatch.textureFactor[1],
									auditBatch.textureFactor[2],
									auditBatch.textureFactor[3],
									auditBatch.textureFactor2[0],
									auditBatch.textureFactor2[1],
									auditBatch.textureFactor2[2],
									auditBatch.textureFactor2[3],
									auditBatch.minX,
									auditBatch.minY,
									auditBatch.maxX,
									auditBatch.maxY,
									auditBatch.sortDepth,
									auditBatch.minU,
									auditBatch.minV,
									auditBatch.maxU,
									auditBatch.maxV,
									auditBatch.colorSum[0] * invVertexCount,
									auditBatch.colorSum[1] * invVertexCount,
									auditBatch.colorSum[2] * invVertexCount,
									auditBatch.colorSum[3] * invVertexCount,
									auditHadTextureData ? 1 : 0);
								auditText[sizeof(auditText) - 1] = 0;
								logLine(auditText);
							}
						}
						if (textureSet)
							++realTextureBatches;
						else
						{
							textureSet = ms_whiteTextureDescriptorSet;
							if (hadTextureData)
							{
								++fallbackBatches;
								++pendingTextureBatches;
							}
							else
							{
								++untexturedColorBatches;
							}
							if (s_fallbackBatchLogCount < 80 && (nextPresentCount >= 90 || hadTextureData))
							{
								float const invVertexCount = batch.vertexCount ? 1.0f / static_cast<float>(batch.vertexCount) : 0.0f;
								char fallbackText[384];
								_snprintf(fallbackText, sizeof(fallbackText) - 1, "gpu %s batch present=%d batch=%u hadTexture=%d textureTag=0x%08x texture=%p shader=%s vertices=%u actor=%d actorAux=%d alpha=%d depthTest=%d depthWrite=%d bounds=%.1f,%.1f..%.1f,%.1f avgRgba=%.1f,%.1f,%.1f,%.1f depth=%.4f",
									hadTextureData ? "fallback" : "untextured",
									nextPresentCount,
									static_cast<unsigned>(batchIndex),
									hadTextureData ? 1 : 0,
									static_cast<unsigned int>(batch.textureTag),
									static_cast<void const *>(batch.textureData),
									batch.shaderName ? batch.shaderName : "<null>",
									static_cast<unsigned>(batch.vertexCount),
									batch.actorBody ? 1 : 0,
									batch.actorAuxiliary ? 1 : 0,
									batch.alphaBlend ? 1 : 0,
									batch.depthTest ? 1 : 0,
									batch.depthWrite ? 1 : 0,
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY,
									batch.colorSum[0] * invVertexCount,
									batch.colorSum[1] * invVertexCount,
									batch.colorSum[2] * invVertexCount,
									batch.colorSum[3] * invVertexCount,
									batch.sortDepth);
								fallbackText[sizeof(fallbackText) - 1] = 0;
								logLine(fallbackText);
								++s_fallbackBatchLogCount;
							}
						}
						vkCmdBindDescriptorSetsLocal(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ms_gpuDrawPipelineLayout, 2, 1, &textureSet, 0, 0);
						float actorBaseColor[4];
						float const *actorBaseColorOverride = 0;
						size_t actorBaseBatchIndex = 0;
						if (batch.actorAuxiliary && findActorAuxBaseMaterialColor(batchIndex, actorBaseColor, actorBaseBatchIndex))
						{
							actorBaseColorOverride = actorBaseColor;
							if (batchDiagnostics && s_actorAuxLinkLogCount < 80)
							{
								GpuDrawBatch const &baseBatch = ms_gpuDrawBatches[actorBaseBatchIndex];
								char linkText[384];
								_snprintf(linkText, sizeof(linkText) - 1, "gpu actor aux link present=%d auxBatch=%u baseBatch=%u paired=%d auxTex=%s baseTex=%s color=%.3f,%.3f,%.3f,%.3f auxBounds=%.1f,%.1f..%.1f,%.1f baseBounds=%.1f,%.1f..%.1f,%.1f",
									nextPresentCount,
									static_cast<unsigned>(batchIndex),
									static_cast<unsigned>(actorBaseBatchIndex),
									(pairedBaseTextureData && pairedBaseBatchIndex == actorBaseBatchIndex) ? 1 : 0,
									batch.textureData ? batch.textureData->getName() : "<none>",
									baseBatch.textureData ? baseBatch.textureData->getName() : "<none>",
									actorBaseColor[0],
									actorBaseColor[1],
									actorBaseColor[2],
									actorBaseColor[3],
									batch.minX,
									batch.minY,
									batch.maxX,
									batch.maxY,
									baseBatch.minX,
									baseBatch.minY,
									baseBatch.maxX,
									baseBatch.maxY);
								linkText[sizeof(linkText) - 1] = 0;
								logLine(linkText);
								++s_actorAuxLinkLogCount;
							}
						}
						GpuDrawPushConstants constants;
						makeGpuDrawPushConstants(&batch, constants, actorBaseColorOverride);
						vkCmdPushConstantsLocal(commandBuffer, ms_gpuDrawPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
						vkCmdDrawIndexedLocal(commandBuffer, batch.vertexCount, 1, batch.firstVertex, 0, 0);
						++drawCalls;
					}
				}
				if (tracePresent || ((nextPresentCount % 120) == 0) || pendingTextureBatches > 0)
				{
					char drawText[384];
					_snprintf(drawText, sizeof(drawText) - 1, "gpu draw recorded vertices=%u indices=%u triangles=%u drawCalls=%u batches=%u realTextureBatches=%u fallbackBatches=%u missingTextureBatches=%u pendingTextureBatches=%u untexturedColorBatches=%u alphaMaskTextureBatches=%u alphaBatches=%u opaqueBatches=%u actorBatches=%u actorAuxBatches=%u forceWhite=%d batchLimit=%d opaqueTriSort=%d opaqueBatchSort=%d",
						static_cast<unsigned>(ms_gpuTriangleVertexCount),
						static_cast<unsigned>(ms_gpuTriangleIndexCount),
						static_cast<unsigned>(ms_gpuTriangleVertexCount / 3),
						drawCalls,
						static_cast<unsigned>(ms_gpuDrawBatches.size()),
						realTextureBatches,
						fallbackBatches,
						missingTextureBatches,
						pendingTextureBatches,
						untexturedColorBatches,
						alphaMaskTextureBatches,
						alphaBatches,
						opaqueBatches,
						actorBatches,
						actorAuxiliaryBatches,
						forceWhiteTextures ? 1 : 0,
						gpuDrawBatchLimit,
						opaqueTriangleSort ? 1 : 0,
						opaqueBatchSort ? 1 : 0);
					drawText[sizeof(drawText) - 1] = 0;
					logLine(drawText);
				}
				ms_lastRealTextureBatches = realTextureBatches;
				ms_lastPendingTextureBatches = pendingTextureBatches;
				ms_lastMissingTextureBatches = missingTextureBatches;
			}
			vkCmdEndRenderPassLocal(commandBuffer);
			if (gpuCaptureRequested)
			{
				VkImageMemoryBarrier toTransfer;
				memset(&toTransfer, 0, sizeof(toTransfer));
				toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toTransfer.image = ms_swapchainImages[imageIndex];
				toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				toTransfer.subresourceRange.levelCount = 1;
				toTransfer.subresourceRange.layerCount = 1;
				vkCmdPipelineBarrierLocal(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &toTransfer);

				VkBufferImageCopy copyRegion;
				memset(&copyRegion, 0, sizeof(copyRegion));
				copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.imageSubresource.layerCount = 1;
				copyRegion.imageExtent.width = ms_swapchainExtent.width;
				copyRegion.imageExtent.height = ms_swapchainExtent.height;
				copyRegion.imageExtent.depth = 1;
				vkCmdCopyImageToBufferLocal(commandBuffer, ms_swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ms_gpuReadbackBuffer, 1, &copyRegion);

				VkImageMemoryBarrier toPresent = toTransfer;
				toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				toPresent.dstAccessMask = 0;
				toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				vkCmdPipelineBarrierLocal(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, 0, 0, 0, 1, &toPresent);
				if (tracePresent)
					logLine("gpu readback copy recorded");
			}
		}
		else
		{
			VkImageMemoryBarrier toClear;
			memset(&toClear, 0, sizeof(toClear));
			toClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toClear.srcAccessMask = 0;
			toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toClear.image = ms_swapchainImages[imageIndex];
			toClear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			toClear.subresourceRange.levelCount = 1;
			toClear.subresourceRange.layerCount = 1;
			vkCmdPipelineBarrierLocal(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &toClear);

			if (ms_frameUploadMapped && ms_frameUploadBuffer && !ms_framePixels.empty())
			{
				memcpy(ms_frameUploadMapped, &ms_framePixels[0], ms_framePixels.size() * sizeof(uint32));

				VkBufferImageCopy copyRegion;
				memset(&copyRegion, 0, sizeof(copyRegion));
				copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.imageSubresource.layerCount = 1;
				copyRegion.imageExtent.width = ms_swapchainExtent.width;
				copyRegion.imageExtent.height = ms_swapchainExtent.height;
				copyRegion.imageExtent.depth = 1;
				vkCmdCopyBufferToImageLocal(commandBuffer, ms_frameUploadBuffer, ms_swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
			}
			else
			{
				float a = static_cast<float>((ms_clearColor >> 24) & 0xff) / 255.0f;
				float r = static_cast<float>((ms_clearColor >> 16) & 0xff) / 255.0f;
				float g = static_cast<float>((ms_clearColor >> 8) & 0xff) / 255.0f;
				float b = static_cast<float>(ms_clearColor & 0xff) / 255.0f;
				VkClearColorValue clearColor = { r, g, b, a };
				VkImageSubresourceRange clearRange;
				memset(&clearRange, 0, sizeof(clearRange));
				clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				clearRange.levelCount = 1;
				clearRange.layerCount = 1;
				vkCmdClearColorImageLocal(commandBuffer, ms_swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);
			}

			VkImageMemoryBarrier toPresent = toClear;
			toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toPresent.dstAccessMask = 0;
			toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			vkCmdPipelineBarrierLocal(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, 0, 0, 0, 1, &toPresent);
		}

		vkEndCommandBufferLocal(commandBuffer);
		ULONGLONG const recordEndMs = nowMilliseconds();
		if (tracePresent)
			logLine("present commands recorded");

		VkPipelineStageFlags waitStage = useGpuRenderPass ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo submitInfo;
		memset(&submitInfo, 0, sizeof(submitInfo));
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &ms_imageAvailableSemaphore;
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &ms_renderFinishedSemaphore;
		vkResetFencesLocal(ms_device, 1, &ms_frameFence);
		VkResult const submitResult = vkQueueSubmitLocal(ms_queue, 1, &submitInfo, ms_frameFence);
		{
			char buffer[128];
			_snprintf(buffer, sizeof(buffer) - 1, "present submit result=%d", static_cast<int>(submitResult));
			buffer[sizeof(buffer) - 1] = 0;
			if (tracePresent || submitResult != VK_SUCCESS)
				logLine(buffer);
		}
		if (submitResult != VK_SUCCESS)
		{
			if (submitResult == VK_ERROR_DEVICE_LOST)
				ms_deviceLost = true;
			return false;
		}
		ms_frameFenceSubmitted = true;

		if (gpuCaptureRequested)
		{
			VkResult const captureWait = vkWaitForFencesLocal(ms_device, 1, &ms_frameFence, VK_TRUE, 5000000000ull);
			if (captureWait == VK_SUCCESS)
			{
				ms_frameFenceSubmitted = false;
				releasePendingTextureStaging("capture");
			}
			else if (captureWait == VK_ERROR_DEVICE_LOST)
				ms_deviceLost = true;
			bool const wroteGpuCapture = captureWait == VK_SUCCESS && writeGpuReadbackBmp(gpuCaptureSeriesPath);
			if (wroteGpuCapture && gpuCaptureLatestPath[0])
				CopyFileA(gpuCaptureSeriesPath, gpuCaptureLatestPath, FALSE);
			char captureLog[MAX_PATH + 160];
			_snprintf(captureLog, sizeof(captureLog) - 1, "gpu readback capture %s wait=%d path=%s latest=%s", wroteGpuCapture ? "ok" : "failed", static_cast<int>(captureWait), gpuCaptureSeriesPath, gpuCaptureLatestPath);
			captureLog[sizeof(captureLog) - 1] = 0;
			logLine(captureLog);
		}

		VkPresentInfoKHR presentInfo;
		memset(&presentInfo, 0, sizeof(presentInfo));
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &ms_renderFinishedSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &ms_swapchain;
		presentInfo.pImageIndices = &imageIndex;
		VkResult presentResult = vkQueuePresentKHRLocal(ms_queue, &presentInfo);
		if (tracePresent)
		{
			char buffer[128];
			_snprintf(buffer, sizeof(buffer) - 1, "present queue result=%d", static_cast<int>(presentResult));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
		}
		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
		{
			destroySwapchain();
			return createSwapchain();
		}
		bool const presentOk = presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR;
		ULONGLONG const presentEndMs = nowMilliseconds();
		if (tracePresent || (nextPresentCount % 60) == 0 || ms_perfTextureUploadsThisPresent > 0)
		{
			char perfText[640];
			ULONGLONG const elapsedMs = presentEndMs > s_firstPresentMs ? presentEndMs - s_firstPresentMs : 1;
			unsigned const avgFps10 = static_cast<unsigned>((static_cast<ULONGLONG>(nextPresentCount) * 10000ull) / elapsedMs);
			float const actorMinX = ms_actorGpuTrianglesSeen ? ms_actorGpuMinX : 0.0f;
			float const actorMinY = ms_actorGpuTrianglesSeen ? ms_actorGpuMinY : 0.0f;
			float const actorMaxX = ms_actorGpuTrianglesSeen ? ms_actorGpuMaxX : 0.0f;
			float const actorMaxY = ms_actorGpuTrianglesSeen ? ms_actorGpuMaxY : 0.0f;
			_snprintf(perfText, sizeof(perfText) - 1, "gpu perf present=%d ok=%d totalMs=%u recordMs=%u avgFps10=%u vertices=%u indices=%u batches=%u dropped=%u pathDrop=%u terrainDrop=%u terrainRepair=%u terrainLocalRepair=%u textureUploads=%u textureBytes=%u textureUploadsTotal=%u textureBytesTotal=%u textureBudget=%d actorTriSeen=%u actorTriStaged=%u actorNonActorTextured=%u actorSkipNrml=%u actorSkipSpec=%u actorSkipBlackSpec=%u actorBounds=%.1f,%.1f..%.1f,%.1f",
				nextPresentCount,
				presentOk ? 1 : 0,
				static_cast<unsigned>(presentEndMs - presentStartMs),
				static_cast<unsigned>(recordEndMs - recordStartMs),
				avgFps10,
				static_cast<unsigned>(ms_gpuTriangleVertexCount),
				static_cast<unsigned>(ms_gpuTriangleIndexCount),
				static_cast<unsigned>(ms_gpuDrawBatches.size()),
				ms_gpuTriangleDropped,
				ms_gpuTrianglePathologicalDropped,
				ms_terrainCorruptTriangleDrops,
				ms_terrainCorruptTriangleRepairs,
				ms_terrainLocalFanCenterRepairs,
				ms_perfTextureUploadsThisPresent,
				ms_perfTextureUploadBytesThisPresent,
				ms_perfTextureUploadsTotal,
				ms_perfTextureUploadBytesTotal,
				gpuTextureUploadBudgetPerPresent(),
				ms_actorGpuTrianglesSeen,
				ms_actorGpuTrianglesStaged,
				ms_actorGpuTrianglesNonActorTextured,
				ms_actorGpuTrianglesSkippedNrml,
				ms_actorGpuTrianglesSkippedSpec,
				ms_actorGpuTrianglesSkippedBlackSpec,
				actorMinX,
				actorMinY,
				actorMaxX,
				actorMaxY);
			perfText[sizeof(perfText) - 1] = 0;
			logLine(perfText);
		}
		if (presentOk && !ms_framePixels.empty())
		{
			std::fill(ms_framePixels.begin(), ms_framePixels.end(), ms_clearColor);
			if (!ms_depthPixels.empty())
				std::fill(ms_depthPixels.begin(), ms_depthPixels.end(), 1.0f);
		}
		return presentOk;
	}

	bool presentToWindow(HWND, int, int) { return present(); }
	bool lockBackBuffer(Gl_pixelRect &pixels, const RECT *lockRect)
	{
		if (ms_backBufferLocked)
			return false;
		ensureFramePixels();
		if (ms_framePixels.empty())
			return false;

		int left = 0;
		int top = 0;
		if (lockRect)
		{
			left = std::max<int>(0, std::min<int>(ms_width, lockRect->left));
			top = std::max<int>(0, std::min<int>(ms_height, lockRect->top));
			int const right = std::max<int>(left, std::min<int>(ms_width, lockRect->right));
			int const bottom = std::max<int>(top, std::min<int>(ms_height, lockRect->bottom));
			if (right <= left || bottom <= top)
				return false;
		}

		pixels.pixels = &ms_framePixels[static_cast<size_t>(top) * static_cast<size_t>(ms_width) + static_cast<size_t>(left)];
		pixels.pitch = static_cast<unsigned>(ms_width * sizeof(uint32));
		pixels.colorBits = 24;
		pixels.alphaBits = 8;
		ms_backBufferLocked = true;
		return true;
	}
	bool unlockBackBuffer()
	{
		if (!ms_backBufferLocked)
			return false;
		ms_backBufferLocked = false;
		return true;
	}
	void update(float) {}
	void beginScene()
	{
		if (gpuPresentEnabled())
			waitForSubmittedFrame("beginScene", 5000000000ull);
		if (ms_deviceLost)
			return;
		ms_gpuTriangleVertexCount = 0;
		ms_gpuTriangleIndexCount = 0;
		ms_gpuTriangleDropped = 0;
		ms_gpuTrianglePathologicalDropped = 0;
		ms_terrainCorruptTriangleDrops = 0;
		ms_terrainCorruptTriangleRepairs = 0;
		ms_terrainLocalFanCenterRepairs = 0;
		resetActorGpuTelemetry();
		ms_gpuDrawBatches.clear();
	}

	void endScene()
	{
		if (!gpuPresentEnabled() || !ms_gpuTriangleVertexMapped)
			return;

		static unsigned s_gpuStageLogCount = 0;
		if (ms_gpuTriangleVertexCount > 0)
		{
			++ms_gpuTriangleFramesWithData;
			if (ms_gpuTriangleVertexCount > ms_gpuTriangleMaxFrameVertices)
				ms_gpuTriangleMaxFrameVertices = ms_gpuTriangleVertexCount;
		}

		if (s_gpuStageLogCount < 120 || (ms_gpuTriangleFramesWithData > 0 && (ms_gpuTriangleFramesWithData % 60) == 0))
		{
			char buffer[256];
			_snprintf(buffer, sizeof(buffer) - 1, "gpu stage frame vertices=%u indices=%u triangles=%u dropped=%u pathDrop=%u framesWithData=%u maxVertices=%u buffer=%p",
				static_cast<unsigned>(ms_gpuTriangleVertexCount),
				static_cast<unsigned>(ms_gpuTriangleIndexCount),
				static_cast<unsigned>(ms_gpuTriangleVertexCount / 3),
				ms_gpuTriangleDropped,
				ms_gpuTrianglePathologicalDropped,
				ms_gpuTriangleFramesWithData,
				ms_gpuTriangleMaxFrameVertices,
				reinterpret_cast<void *>(ms_gpuTriangleVertexBuffer));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_gpuStageLogCount;
		}
	}
	void updateWindowSettings()
	{
		if (!ms_engineOwnsWindow || !ms_window)
			return;

		DWORD const windowStyleWindowed = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_BORDER | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
		DWORD const windowStyleFullscreen = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

		if (ms_windowed)
		{
			RECT rect;
			rect.left = 0;
			rect.top = 0;
			rect.right = ms_width;
			rect.bottom = ms_height;

			DWORD const windowStyle = ms_borderlessWindow ? windowStyleFullscreen : windowStyleWindowed;
			SetWindowLong(ms_window, GWL_STYLE, windowStyle);
			AdjustWindowRect(&rect, windowStyle, FALSE);

			HMONITOR monitor = MonitorFromWindow(ms_window, MONITOR_DEFAULTTONEAREST);
			MONITORINFO monitorInfo;
			ZeroMemory(&monitorInfo, sizeof(monitorInfo));
			monitorInfo.cbSize = sizeof(monitorInfo);
			GetMonitorInfo(monitor, &monitorInfo);

			if (ms_borderlessWindow)
			{
				ms_windowX = monitorInfo.rcMonitor.left;
				ms_windowY = monitorInfo.rcMonitor.top;
			}
			else if (ms_windowX == INT_MAX || ms_windowY == INT_MAX)
			{
				ms_windowX = monitorInfo.rcMonitor.left + (((monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left) - ms_width) / 2);
				ms_windowY = monitorInfo.rcMonitor.top + (((monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top) - ms_height) / 2);
			}

			int const windowWidth = rect.right - rect.left;
			int const windowHeight = rect.bottom - rect.top;
			SetWindowPos(ms_window, HWND_NOTOPMOST, ms_windowX, ms_windowY, windowWidth, windowHeight, SWP_NOCOPYBITS | SWP_SHOWWINDOW);

			char buffer[256];
			_snprintf(buffer, sizeof(buffer) - 1, "show windowed hwnd=0x%p client=%dx%d window=%dx%d pos=%d,%d", ms_window, ms_width, ms_height, windowWidth, windowHeight, ms_windowX, ms_windowY);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
		}
		else
		{
			SetWindowLong(ms_window, GWL_STYLE, windowStyleFullscreen);

			HMONITOR monitor = MonitorFromWindow(ms_window, MONITOR_DEFAULTTONEAREST);
			MONITORINFO monitorInfo;
			ZeroMemory(&monitorInfo, sizeof(monitorInfo));
			monitorInfo.cbSize = sizeof(monitorInfo);
			GetMonitorInfo(monitor, &monitorInfo);
			RECT const &r = monitorInfo.rcMonitor;

			SetWindowPos(ms_window, HWND_TOPMOST, r.left, r.top, ms_width, ms_height, SWP_NOCOPYBITS | SWP_SHOWWINDOW);

			char buffer[256];
			_snprintf(buffer, sizeof(buffer) - 1, "show fullscreen hwnd=0x%p size=%dx%d pos=%ld,%ld", ms_window, ms_width, ms_height, r.left, r.top);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
		}
	}
	void resize(int newWidth, int newHeight) { ms_width = newWidth; ms_height = newHeight; destroySwapchain(); createSwapchain(); updateWindowSettings(); }
	void setWindowedMode(bool windowed) { ms_windowed = windowed; updateWindowSettings(); }
	void displayModeChanged() {}
	int getShaderCapability() { return gpuShaderCapability(); }
	bool requiresVertexAndPixelShaders() { return true; }
	void getOtherAdapterRects(stdvector<RECT>::fwd &) {}
	int getVideoMemoryInMegabytes() { return 512; }
	void flushResources(bool) {}
	bool isGdiVisible() { return false; }
	bool wasDeviceReset() { return false; }
	void addDeviceLostCallback(Gl_api::CallbackFunction) {}
	void removeDeviceLostCallback(Gl_api::CallbackFunction) {}
	void addDeviceRestoredCallback(Gl_api::CallbackFunction) {}
	void removeDeviceRestoredCallback(Gl_api::CallbackFunction) {}
	bool supportsMipmappedCubeMaps() { return true; }
	bool supportsScissorRect() { return true; }
	bool supportsHardwareMouseCursor() { return false; }
	bool supportsTwoSidedStencil() { return false; }
	bool supportsStreamOffsets() { return false; }
	bool supportsDynamicTextures() { return true; }
	void setBrightnessContrastGamma(float, float, float) {}
	void setFillMode(GlFillMode) {}
	void setCullMode(GlCullMode cullMode)
	{
		ms_cullMode = cullMode;
		static int s_cullLogCount = 0;
		if (s_cullLogCount < 24)
		{
			char buffer[96];
			_snprintf(buffer, sizeof(buffer) - 1, "setCullMode mode=%d", static_cast<int>(cullMode));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_cullLogCount;
		}
	}
	void setPointSize(real size) { ms_pointSize = static_cast<float>(size); }
	void setPointSizeMax(real size) { ms_pointSizeMax = static_cast<float>(size); }
	void setPointSizeMin(real size) { ms_pointSizeMin = static_cast<float>(size); }
	void setPointScaleEnable(bool) {}
	void setPointScaleFactor(real, real, real) {}
	void setPointSpriteEnable(bool enabled) { ms_pointSpriteEnable = enabled; }
	bool vrIsWorldRenderingEnabled() { return false; }
	bool vrBeginWorldFrame() { return false; }
	bool vrGetEyeInfo(int, Gl_vrEyeInfo *) { return false; }
	bool vrBeginEye(int) { return false; }
	void vrEndEye(int) {}
	void vrEndWorldFrame() {}
	bool vrBeginHudCapture() { return false; }
	void vrEndHudCapture() {}
	void vrSubmitHudPanelRect(const char *, int, int, int, int, int, int) {}
	void setRenderTarget(Texture *, CubeFace, int) {}
	bool copyRenderTargetToNonRenderTargetTexture() { return false; }
	bool screenShot(GlScreenShotFormat format, int, const char *fileName) { return writeFrameShot(format, fileName); }
	ShaderImplementationGraphicsData *createShaderImplementationGraphicsData(const ShaderImplementation &) { return new VulkanShaderImplementationData; }
	StaticShaderGraphicsData *createStaticShaderGraphicsData(const StaticShader &shader) { return new ::VulkanStaticShaderData(shader); }
	void setBadVertexShaderStaticShader(const StaticShader *) {}
	void setStaticShader(const StaticShader &shader, int pass)
	{
		ms_activeTextureData = 0;
		ms_activeSecondaryTextureData = 0;
		ms_activeTextureTag = 0;
		ms_activeSecondaryTextureTag = 0;
		ms_activeTerrainAlphaStage = false;
		char const *shaderName = shader.getStaticShaderTemplate().getName().getString();
		ms_activeStaticShaderNamePtr = shaderName ? shaderName : "<null>";
		strncpy(ms_activeStaticShaderName, shaderName ? shaderName : "<null>", sizeof(ms_activeStaticShaderName) - 1);
		ms_activeStaticShaderName[sizeof(ms_activeStaticShaderName) - 1] = 0;
		ms_activeTextureCoordinateSet = 0;
		ms_activeSecondaryTextureCoordinateSet = 0;
		ms_activeTextureStage = 0;
		ms_activePixelProgramMode = 0;
		ms_activePixelProgramName = 0;
		ms_activeTextureCube = false;
		ms_activeStageTextureCount = 0;
		for (int i = 0; i < 2; ++i)
		{
			ms_activeStageColorOperation[i] = -1;
			ms_activeStageAlphaOperation[i] = -1;
			ms_activeStageColorArgument0[i] = -1;
			ms_activeStageColorArgument1[i] = -1;
			ms_activeStageColorArgument2[i] = -1;
			ms_activeStageAlphaArgument0[i] = -1;
			ms_activeStageAlphaArgument1[i] = -1;
			ms_activeStageAlphaArgument2[i] = -1;
			ms_activeStageResultArgument[i] = ShaderImplementation::Pass::Stage::TA_current;
			ms_activeStageColorArgument0Complement[i] = false;
			ms_activeStageColorArgument1Complement[i] = false;
			ms_activeStageColorArgument2Complement[i] = false;
			ms_activeStageAlphaArgument0Complement[i] = false;
			ms_activeStageAlphaArgument1Complement[i] = false;
			ms_activeStageAlphaArgument2Complement[i] = false;
			ms_activeStageColorArgument0AlphaReplicate[i] = false;
			ms_activeStageColorArgument1AlphaReplicate[i] = false;
			ms_activeStageColorArgument2AlphaReplicate[i] = false;
		}
		ms_activeStaticShaderPass = pass;
		ms_activeTextureFactorValid = false;
		ms_activeTextureFactor[0] = 1.0f;
		ms_activeTextureFactor[1] = 1.0f;
		ms_activeTextureFactor[2] = 1.0f;
		ms_activeTextureFactor[3] = 1.0f;
		ms_activeTextureFactor2[0] = 0.0f;
		ms_activeTextureFactor2[1] = 0.0f;
		ms_activeTextureFactor2[2] = 0.0f;
		ms_activeTextureFactor2[3] = 1.0f;
		ms_activeLightingEnabled = false;
		ms_activeLightingColorVertex = false;
		ms_activeLightingSpecularEnabled = false;
		ms_activeLightingAmbientColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeLightingDiffuseColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeLightingSpecularColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_activeLightingEmissiveColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_obeysLightScale = false;
		ms_activeFullAmbient = false;
		ms_activeMaterialColorValid = false;
		ms_activeMaterialAmbientColor[0] = 1.0f;
		ms_activeMaterialAmbientColor[1] = 1.0f;
		ms_activeMaterialAmbientColor[2] = 1.0f;
		ms_activeMaterialAmbientColor[3] = 1.0f;
		ms_activeMaterialColor[0] = 1.0f;
		ms_activeMaterialColor[1] = 1.0f;
		ms_activeMaterialColor[2] = 1.0f;
		ms_activeMaterialColor[3] = 1.0f;
		ms_activeMaterialEmissiveColor[0] = 0.0f;
		ms_activeMaterialEmissiveColor[1] = 0.0f;
		ms_activeMaterialEmissiveColor[2] = 0.0f;
		ms_activeMaterialEmissiveColor[3] = 0.0f;
		ms_activeMaterialSpecularColor[0] = 0.0f;
		ms_activeMaterialSpecularColor[1] = 0.0f;
		ms_activeMaterialSpecularColor[2] = 0.0f;
		ms_activeMaterialSpecularColor[3] = 0.0f;
		ms_activeMaterialSpecularPower = 0.0f;
		ms_depthEnabled = true;
		ms_depthWriteEnabled = true;
		IGNORE_RETURN(::VulkanStaticShaderData::getPassDepth(shader, pass, ms_depthEnabled, ms_depthWriteEnabled));
		::VulkanStaticShaderData::PassAlpha passAlpha;
		IGNORE_RETURN(::VulkanStaticShaderData::getPassAlpha(shader, pass, passAlpha));
		ms_shaderAlphaBlendEnabled = passAlpha.alphaBlendEnable;
		ms_shaderAlphaBlendOperation = passAlpha.alphaBlendOperation;
		ms_shaderAlphaBlendSource = passAlpha.alphaBlendSource;
		ms_shaderAlphaBlendDestination = passAlpha.alphaBlendDestination;
		ms_shaderColorWriteMask = passAlpha.writeEnable;
		ms_shaderAlphaTestEnabled = passAlpha.alphaTestEnable;
		ms_shaderAlphaTestCompare = passAlpha.alphaTestCompare;
		ms_shaderAlphaTestReference = passAlpha.alphaTestReference;
		ms_alphaBlendEnabled = true;
		ms_alphaTestEnabled = false;
		ms_alphaTestCompare = ShaderImplementation::Pass::C_Always;
		ms_alphaTestReference = 0;

		::VulkanStaticShaderData::PassTexture passTexture;
		if (::VulkanStaticShaderData::getPassTexture(shader, pass, passTexture))
		{
			setActiveFogColor(passTexture.fogMode);
			ms_activeMaterialColorValid = passTexture.materialColorValid;
			for (int i = 0; i < 4; ++i)
			{
				ms_activeMaterialAmbientColor[i] = passTexture.materialAmbientColor[i];
				ms_activeMaterialColor[i] = passTexture.materialColor[i];
				ms_activeMaterialEmissiveColor[i] = passTexture.materialEmissiveColor[i];
				ms_activeMaterialSpecularColor[i] = passTexture.materialSpecularColor[i];
			}
			ms_activeMaterialSpecularPower = passTexture.materialSpecularPower;
			ms_activeTextureFactorValid = passTexture.textureFactorValid;
			ms_activeLightingEnabled = passTexture.lightingEnabled;
			ms_activeLightingColorVertex = passTexture.lightingColorVertex;
			ms_activeLightingSpecularEnabled = passTexture.lightingSpecularEnabled;
			ms_activeLightingAmbientColorSource = passTexture.lightingAmbientColorSource;
			ms_activeLightingDiffuseColorSource = passTexture.lightingDiffuseColorSource;
			ms_activeLightingSpecularColorSource = passTexture.lightingSpecularColorSource;
			ms_activeLightingEmissiveColorSource = passTexture.lightingEmissiveColorSource;
			bool const uiShader =
				(ms_activeStaticShaderNamePtr &&
					(strstr(ms_activeStaticShaderNamePtr, "uicanvas") ||
					 strstr(ms_activeStaticShaderNamePtr, "ui_shader") ||
					 strstr(ms_activeStaticShaderNamePtr, "font") ||
					 strstr(ms_activeStaticShaderNamePtr, "text"))) ||
				(passTexture.pixelProgramName && strstr(passTexture.pixelProgramName, "ui.psh"));
			ms_activeClampSampler = uiShader || textureAddressUsesClamp(passTexture.addressU) || textureAddressUsesClamp(passTexture.addressV);
			ms_obeysLightScale = passTexture.obeysLightScale;
			ms_activeFullAmbient = passTexture.fullAmbient;
			ms_activePixelProgramMode = passTexture.pixelProgramMode;
			ms_activePixelProgramName = passTexture.pixelProgramName;
			ms_activeStageTextureCount = passTexture.stageTextureCount;
			for (int i = 0; i < 2; ++i)
			{
				ms_activeStageColorOperation[i] = passTexture.stageColorOperation[i];
				ms_activeStageAlphaOperation[i] = passTexture.stageAlphaOperation[i];
				ms_activeStageColorArgument0[i] = passTexture.stageColorArgument0[i];
				ms_activeStageColorArgument1[i] = passTexture.stageColorArgument1[i];
				ms_activeStageColorArgument2[i] = passTexture.stageColorArgument2[i];
				ms_activeStageAlphaArgument0[i] = passTexture.stageAlphaArgument0[i];
				ms_activeStageAlphaArgument1[i] = passTexture.stageAlphaArgument1[i];
				ms_activeStageAlphaArgument2[i] = passTexture.stageAlphaArgument2[i];
				ms_activeStageResultArgument[i] = passTexture.stageResultArgument[i];
				ms_activeStageColorArgument0Complement[i] = passTexture.stageColorArgument0Complement[i];
				ms_activeStageColorArgument1Complement[i] = passTexture.stageColorArgument1Complement[i];
				ms_activeStageColorArgument2Complement[i] = passTexture.stageColorArgument2Complement[i];
				ms_activeStageAlphaArgument0Complement[i] = passTexture.stageAlphaArgument0Complement[i];
				ms_activeStageAlphaArgument1Complement[i] = passTexture.stageAlphaArgument1Complement[i];
				ms_activeStageAlphaArgument2Complement[i] = passTexture.stageAlphaArgument2Complement[i];
				ms_activeStageColorArgument0AlphaReplicate[i] = passTexture.stageColorArgument0AlphaReplicate[i];
				ms_activeStageColorArgument1AlphaReplicate[i] = passTexture.stageColorArgument1AlphaReplicate[i];
				ms_activeStageColorArgument2AlphaReplicate[i] = passTexture.stageColorArgument2AlphaReplicate[i];
			}
			for (int i = 0; i < 4; ++i)
			{
				ms_activeTextureFactor[i] = passTexture.textureFactor[i];
				ms_activeTextureFactor2[i] = passTexture.textureFactor2[i];
			}
			StaticShaderTemplate::TextureData textureData;
			ms_activeTextureStage = passTexture.textureStage >= 0 && passTexture.textureStage < 8 ? passTexture.textureStage : 0;
			if (passTexture.textureTag && shader.getTextureData(passTexture.textureTag, textureData) && textureData.texture)
			{
				ms_activeTextureData = getTextureData(textureData.texture);
				ms_activeTextureTag = passTexture.textureTag;
				ms_activeTextureCube = ms_activeTextureData && ms_activeTextureData->isCubeMap();
				ms_activeTextureCoordinateSet = passTexture.textureCoordinateSet < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS ? passTexture.textureCoordinateSet : 0;
			}
			else if (isGlobalTextureTag(passTexture.textureTag))
			{
				ms_activeTextureData = getGlobalTextureData(passTexture.textureTag);
				ms_activeTextureTag = passTexture.textureTag;
				ms_activeTextureCube = ms_activeTextureData && ms_activeTextureData->isCubeMap();
				ms_activeTextureCoordinateSet = passTexture.textureCoordinateSet < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS ? passTexture.textureCoordinateSet : 0;
			}
			if (ms_activeStaticShaderNamePtr &&
				strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend") &&
				passTexture.stageTextureCount >= 2 &&
				passTexture.stageTextureTag[1])
			{
				StaticShaderTemplate::TextureData secondaryTextureData;
				if (shader.getTextureData(passTexture.stageTextureTag[1], secondaryTextureData) && secondaryTextureData.texture)
				{
					ms_activeSecondaryTextureData = getTextureData(secondaryTextureData.texture);
					ms_activeSecondaryTextureTag = passTexture.stageTextureTag[1];
					ms_activeSecondaryTextureCoordinateSet = passTexture.stageTextureCoordinateSet[1] < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS ? passTexture.stageTextureCoordinateSet[1] : 0;
				}
				else if (isGlobalTextureTag(passTexture.stageTextureTag[1]))
				{
					ms_activeSecondaryTextureData = getGlobalTextureData(passTexture.stageTextureTag[1]);
					ms_activeSecondaryTextureTag = passTexture.stageTextureTag[1];
					ms_activeSecondaryTextureCoordinateSet = passTexture.stageTextureCoordinateSet[1] < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS ? passTexture.stageTextureCoordinateSet[1] : 0;
				}
			}
			ms_activeTerrainAlphaStage =
				gpuTerrainAlphaStageEnabled() &&
				ms_activeSecondaryTextureData &&
				ms_activeStaticShaderNamePtr &&
				strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend") &&
				passTexture.stageTextureCount >= 2 &&
				passTexture.stageAlphaOperation[1] == ShaderImplementation::Pass::Stage::TO_selectArg1 &&
				passTexture.stageAlphaArgument1[1] == ShaderImplementation::Pass::Stage::TA_texture;
		}

		static int s_setStaticShaderLogCount = 0;
		if (s_setStaticShaderLogCount < 80)
		{
			char buffer[640];
			_snprintf(buffer, sizeof(buffer) - 1, "setStaticShader pass=%d name=%s textureTag=0x%08x textureData=%p cube=%d texCoordSet=%d texStage=%d pixelMode=%d pixelProgram=%s lighting=%d spec=%d obeyScale=%d fullAmbient=%d depth=%d write=%d passAlphaBlend=%d blendOp=%d blendSrc=%d blendDst=%d passAlphaTest=%d cmp=%d ref=%u mat=%d %.3f,%.3f,%.3f,%.3f tf=%d %.3f,%.3f,%.3f,%.3f tf2=%.3f,%.3f,%.3f,%.3f fogMode=%d activeFog=0x%08x", pass, ms_activeStaticShaderName, static_cast<unsigned int>(passTexture.textureTag), ms_activeTextureData, ms_activeTextureCube ? 1 : 0, ms_activeTextureCoordinateSet, ms_activeTextureStage, passTexture.pixelProgramMode, passTexture.pixelProgramName ? passTexture.pixelProgramName : "<none>", passTexture.lightingEnabled ? 1 : 0, passTexture.lightingSpecularEnabled ? 1 : 0, passTexture.obeysLightScale ? 1 : 0, passTexture.fullAmbient ? 1 : 0, ms_depthEnabled ? 1 : 0, ms_depthWriteEnabled ? 1 : 0, passAlpha.alphaBlendEnable ? 1 : 0, static_cast<int>(passAlpha.alphaBlendOperation), static_cast<int>(passAlpha.alphaBlendSource), static_cast<int>(passAlpha.alphaBlendDestination), passAlpha.alphaTestEnable ? 1 : 0, static_cast<int>(passAlpha.alphaTestCompare), static_cast<unsigned int>(passAlpha.alphaTestReference), passTexture.materialColorValid ? 1 : 0, passTexture.materialColor[0], passTexture.materialColor[1], passTexture.materialColor[2], passTexture.materialColor[3], passTexture.textureFactorValid ? 1 : 0, passTexture.textureFactor[0], passTexture.textureFactor[1], passTexture.textureFactor[2], passTexture.textureFactor[3], passTexture.textureFactor2[0], passTexture.textureFactor2[1], passTexture.textureFactor2[2], passTexture.textureFactor2[3], static_cast<int>(passTexture.fogMode), static_cast<unsigned>(ms_activeFogColorPacked));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_setStaticShaderLogCount;
		}
		static int s_materialDetailLogCount = 0;
		if (passTexture.materialColorValid && s_materialDetailLogCount < 80)
		{
			char buffer[512];
			_snprintf(buffer, sizeof(buffer) - 1, "material detail pass=%d name=%s diffuse=%.3f,%.3f,%.3f,%.3f ambient=%.3f,%.3f,%.3f,%.3f emissive=%.3f,%.3f,%.3f,%.3f specular=%.3f,%.3f,%.3f,%.3f power=%.3f tint=%d",
				pass,
				ms_activeStaticShaderName,
				passTexture.materialColor[0],
				passTexture.materialColor[1],
				passTexture.materialColor[2],
				passTexture.materialColor[3],
				passTexture.materialAmbientColor[0],
				passTexture.materialAmbientColor[1],
				passTexture.materialAmbientColor[2],
				passTexture.materialAmbientColor[3],
				passTexture.materialEmissiveColor[0],
				passTexture.materialEmissiveColor[1],
				passTexture.materialEmissiveColor[2],
				passTexture.materialEmissiveColor[3],
				passTexture.materialSpecularColor[0],
				passTexture.materialSpecularColor[1],
				passTexture.materialSpecularColor[2],
				passTexture.materialSpecularColor[3],
				passTexture.materialSpecularPower,
				gpuMaterialTintEnabled() ? 1 : 0);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_materialDetailLogCount;
		}
		static int s_terrainStageLogCount = 0;
		if (ms_activeStaticShaderNamePtr && strstr(ms_activeStaticShaderNamePtr, "shader/terrain_blend") && s_terrainStageLogCount < 80)
		{
			uint32 const secondarySample = ms_activeSecondaryTextureData ? ms_activeSecondaryTextureData->sample(0.5f, 0.5f, true) : 0xffffffff;
			char buffer[1024];
			_snprintf(buffer, sizeof(buffer) - 1, "terrain pass vulkan pass=%d name=%s chosenTex=0x%08x chosenSet=%d stageCount=%d s0 tex=0x%08x set=%u gen=%d op=%d/%d arg=%d,%d,%d s1 tex=0x%08x set=%u gen=%d op=%d/%d arg=%d,%d,%d secondary=%p secondaryName=%s secondaryFormat=%d secondarySample=0x%08x secondaryAlpha=%u terrainAlphaStage=%d pixelMode=%d depth=%d/%d alpha=%d/%d",
				pass,
				ms_activeStaticShaderNamePtr,
				static_cast<unsigned int>(passTexture.textureTag),
				ms_activeTextureCoordinateSet,
				passTexture.stageTextureCount,
				static_cast<unsigned int>(passTexture.stageTextureTag[0]),
				static_cast<unsigned int>(passTexture.stageTextureCoordinateSet[0]),
				static_cast<int>(passTexture.stageTextureCoordinateGeneration[0]),
				passTexture.stageColorOperation[0],
				passTexture.stageAlphaOperation[0],
				passTexture.stageColorArgument0[0],
				passTexture.stageColorArgument1[0],
				passTexture.stageColorArgument2[0],
				static_cast<unsigned int>(passTexture.stageTextureTag[1]),
				static_cast<unsigned int>(passTexture.stageTextureCoordinateSet[1]),
				static_cast<int>(passTexture.stageTextureCoordinateGeneration[1]),
				passTexture.stageColorOperation[1],
				passTexture.stageAlphaOperation[1],
				passTexture.stageColorArgument0[1],
				passTexture.stageColorArgument1[1],
				passTexture.stageColorArgument2[1],
				static_cast<void const *>(ms_activeSecondaryTextureData),
				ms_activeSecondaryTextureData ? ms_activeSecondaryTextureData->getName() : "<none>",
				ms_activeSecondaryTextureData ? static_cast<int>(ms_activeSecondaryTextureData->getFormat()) : -1,
				static_cast<unsigned int>(secondarySample),
				static_cast<unsigned int>((secondarySample >> 24) & 0xff),
				ms_activeTerrainAlphaStage ? 1 : 0,
				passTexture.pixelProgramMode,
				ms_depthEnabled ? 1 : 0,
				ms_depthWriteEnabled ? 1 : 0,
				passAlpha.alphaBlendEnable ? 1 : 0,
				passAlpha.alphaTestEnable ? 1 : 0);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_terrainStageLogCount;
		}
	}
	bool setMouseCursor(const Texture &, int, int) { return false; }
	bool showMouseCursor(bool) { return false; }
	void setViewport(int x, int y, int width, int height, real minZ, real maxZ)
	{
		ms_viewportX = x;
		ms_viewportY = y;
		ms_viewportWidth = width;
		ms_viewportHeight = height;
		ms_viewportMinZ = minZ;
		ms_viewportMaxZ = maxZ;
		static int s_viewportLogCount = 0;
		if (s_viewportLogCount < 32)
		{
			char buffer[160];
			_snprintf(buffer, sizeof(buffer) - 1, "viewport %d,%d %dx%d z=%f..%f", x, y, width, height, static_cast<float>(minZ), static_cast<float>(maxZ));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_viewportLogCount;
		}
	}
	void setScissorRect(bool enabled, int x, int y, int width, int height)
	{
		ms_scissorEnabled = enabled;
		ms_scissorX = x;
		ms_scissorY = y;
		ms_scissorWidth = width;
		ms_scissorHeight = height;
		static int s_scissorLogCount = 0;
		if (s_scissorLogCount < 80)
		{
			char buffer[160];
			_snprintf(buffer, sizeof(buffer) - 1, "scissor enabled=%d rect=%d,%d %dx%d", enabled ? 1 : 0, x, y, width, height);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_scissorLogCount;
		}
	}
	void setWorldToCameraTransform(const Transform &transform, const Vector &) { setMatrixFromTransform(ms_worldToCameraMatrix, transform, 0); }
	void setProjectionMatrix(const GlMatrix4x4 &projectionMatrix) { setMatrixFromGl(ms_projectionMatrix, projectionMatrix); }
	void setFog(bool enabled, real density, const PackedArgb &color)
	{
		if (density < 0.0f)
			density = 0.0f;

		ms_fogEnabled = enabled;
		ms_fogDensity = static_cast<float>(density);
		ms_fogColor[0] = static_cast<float>(color.getR()) / 255.0f;
		ms_fogColor[1] = static_cast<float>(color.getG()) / 255.0f;
		ms_fogColor[2] = static_cast<float>(color.getB()) / 255.0f;
		ms_fogColor[3] = static_cast<float>(color.getA()) / 255.0f;
		ms_fogColorPacked = color.getArgb();
		setActiveFogColor(ShaderImplementation::Pass::FM_Normal);

		static int s_fogLogCount = 0;
		if (s_fogLogCount < 160)
		{
			char buffer[192];
			_snprintf(buffer, sizeof(buffer) - 1, "fog set vulkan enabled=%d density=%f color=%u,%u,%u,%u packed=0x%08x",
				enabled ? 1 : 0,
				static_cast<float>(density),
				static_cast<unsigned>(color.getA()),
				static_cast<unsigned>(color.getR()),
				static_cast<unsigned>(color.getG()),
				static_cast<unsigned>(color.getB()),
				static_cast<unsigned>(ms_fogColorPacked));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_fogLogCount;
		}
	}
	void setObjectToWorldTransformAndScale(const Transform &transform, const Vector &scale) { setMatrixFromTransform(ms_objectToWorldMatrix, transform, &scale); }
	void setGlobalTexture(Tag tag, const Texture &texture)
	{
		static int s_setGlobalTextureLogCount = 0;
		if (!isGlobalTextureTag(tag))
		{
			ms_activeTextureData = getTextureData(&texture);
			ms_activeSecondaryTextureData = 0;
			ms_activeTextureTag = tag;
			ms_activeSecondaryTextureTag = 0;
			ms_activeTerrainAlphaStage = false;
			ms_activeTextureCube = ms_activeTextureData && ms_activeTextureData->isCubeMap();
			ms_activeTextureCoordinateSet = 0;
			ms_activeSecondaryTextureCoordinateSet = 0;
			if (s_setGlobalTextureLogCount < 80)
			{
				char buffer[192];
				_snprintf(buffer, sizeof(buffer) - 1, "setGlobalTexture direct tag=0x%08x textureData=%p", static_cast<unsigned int>(tag), ms_activeTextureData);
				buffer[sizeof(buffer) - 1] = 0;
				logLine(buffer);
				++s_setGlobalTextureLogCount;
			}
			return;
		}

		if (!ms_globalTextureMap)
			ms_globalTextureMap = new GlobalTextureMap;

		GlobalTextureMap::iterator iter = ms_globalTextureMap->find(tag);
		if (iter != ms_globalTextureMap->end())
		{
			if (iter->second != &texture)
			{
				texture.fetch();
				if (iter->second)
					iter->second->release();
				iter->second = &texture;
			}
		}
		else
		{
			texture.fetch();
			(*ms_globalTextureMap)[tag] = &texture;
		}
		if (s_setGlobalTextureLogCount < 80)
		{
			VulkanTextureData const *textureData = getTextureData(&texture);
			char buffer[192];
			_snprintf(buffer, sizeof(buffer) - 1, "setGlobalTexture global tag=0x%08x textureData=%p", static_cast<unsigned int>(tag), textureData);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_setGlobalTextureLogCount;
		}
	}
	void releaseAllGlobalTextures()
	{
		ms_activeTextureData = 0;
		ms_activeSecondaryTextureData = 0;
		ms_activeTextureTag = 0;
		ms_activeSecondaryTextureTag = 0;
		ms_activeTerrainAlphaStage = false;
		ms_activeTextureCube = false;
		ms_activeTextureFactorValid = false;
		if (!ms_globalTextureMap)
			return;

		for (GlobalTextureMap::iterator iter = ms_globalTextureMap->begin(); iter != ms_globalTextureMap->end(); ++iter)
		{
			if (iter->second)
				iter->second->release();
		}
		ms_globalTextureMap->clear();
	}
	void setTextureTransform(int stage, bool enabled, int dimension, bool projected, const real *transform)
	{
		if (stage < 0 || stage >= 8)
			return;

		if (!enabled)
		{
			resetTextureTransform(stage);
			return;
		}

		if (!transform)
			return;

		if (dimension < 1)
			dimension = 1;
		else if (dimension > 4)
			dimension = 4;

		memcpy(&ms_textureTransform[stage], transform, sizeof(Matrix4x4));
		ms_textureTransformEnabled[stage] = true;
		ms_textureTransformDimension[stage] = dimension;
		ms_textureTransformProjected[stage] = projected;

		static int s_textureTransformLogCount = 0;
		if (s_textureTransformLogCount < 80)
		{
			char buffer[256];
			_snprintf(buffer, sizeof(buffer) - 1, "textureTransform stage=%d enabled=%d dimension=%d projected=%d row0=%.4f,%.4f,%.4f,%.4f row1=%.4f,%.4f,%.4f,%.4f",
				stage,
				enabled ? 1 : 0,
				dimension,
				projected ? 1 : 0,
				ms_textureTransform[stage].m[0][0],
				ms_textureTransform[stage].m[0][1],
				ms_textureTransform[stage].m[0][2],
				ms_textureTransform[stage].m[0][3],
				ms_textureTransform[stage].m[1][0],
				ms_textureTransform[stage].m[1][1],
				ms_textureTransform[stage].m[1][2],
				ms_textureTransform[stage].m[1][3]);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_textureTransformLogCount;
		}
	}
	void setVertexShaderUserConstants(int index, float c0, float c1, float c2, float c3)
	{
		if (index >= 0 && index < static_cast<int>(sizeof(ms_vertexUserConstants) / sizeof(ms_vertexUserConstants[0])))
		{
			ms_vertexUserConstants[index][0] = c0;
			ms_vertexUserConstants[index][1] = c1;
			ms_vertexUserConstants[index][2] = c2;
			ms_vertexUserConstants[index][3] = c3;
		}
		static int s_vertexConstantLogCount = 0;
		if (s_vertexConstantLogCount < 48)
		{
			char buffer[192];
			_snprintf(buffer, sizeof(buffer) - 1, "vertexUserConstant index=%d value=%.4f,%.4f,%.4f,%.4f", index, c0, c1, c2, c3);
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_vertexConstantLogCount;
		}
	}
	void setPixelShaderUserConstants(VectorRgba const *, int) {}
	void setAlphaFadeOpacity(bool, float) {}
	void setLights(const stdvector<const Light*>::fwd &lightList)
	{
		updateLightsFromList(lightList);
	}
	StaticVertexBufferGraphicsData *createStaticVertexBufferData(const StaticVertexBuffer &vertexBuffer)
	{
		if (!ms_staticVertexBufferDataMap)
			ms_staticVertexBufferDataMap = new StaticVertexBufferDataMap;
		VulkanStaticVertexBufferData *data = new VulkanStaticVertexBufferData(vertexBuffer);
		(*ms_staticVertexBufferDataMap)[&vertexBuffer] = data;
		return data;
	}
	DynamicVertexBufferGraphicsData *createDynamicVertexBufferData(const DynamicVertexBuffer &vertexBuffer)
	{
		if (!ms_dynamicVertexBufferDataMap)
			ms_dynamicVertexBufferDataMap = new DynamicVertexBufferDataMap;
		VulkanDynamicVertexBufferData *data = new VulkanDynamicVertexBufferData(vertexBuffer);
		(*ms_dynamicVertexBufferDataMap)[&vertexBuffer] = data;
		return data;
	}
	VertexBufferVectorGraphicsData *createVertexBufferVectorData(VertexBufferVector const &vertexBufferVector) { return new VulkanVertexBufferVectorData(vertexBufferVector); }
	void setVertexBuffer(HardwareVertexBuffer const &vertexBuffer)
	{
		ms_activeVertexData = 0;
		ms_activeDynamicVertexSnapshot.clear();
		for (int stream = 0; stream < 2; ++stream)
			ms_activeDynamicVertexStreamSnapshot[stream].clear();
		ms_activeVertexDescriptor = 0;
		ms_activeVertexFormatFlags = 0;
		ms_activeVertexStride = 0;
		ms_activeVertexCount = 0;
		ms_activeVertexVector = false;
		ms_activeVertexStreamCount = 0;
		for (int stream = 0; stream < 2; ++stream)
		{
			ms_activeVertexStreamData[stream] = 0;
			ms_activeVertexStreamStrides[stream] = 0;
			ms_activeVertexStreamDescriptors[stream] = 0;
			ms_activeVertexStreamFormatFlags[stream] = 0;
			ms_activeVertexStreamCounts[stream] = 0;
		}

		if (vertexBuffer.getType() == HardwareVertexBuffer::T_static)
		{
			StaticVertexBuffer const *staticVertexBuffer = static_cast<StaticVertexBuffer const *>(&vertexBuffer);
			if (ms_staticVertexBufferDataMap)
			{
				StaticVertexBufferDataMap::iterator iter = ms_staticVertexBufferDataMap->find(staticVertexBuffer);
				if (iter != ms_staticVertexBufferDataMap->end())
				{
					ms_activeVertexData = iter->second->getData();
					ms_activeVertexDescriptor = &iter->second->getDescriptor();
					ms_activeVertexFormatFlags = iter->second->getFormatFlags();
					ms_activeVertexStride = ms_activeVertexDescriptor->vertexSize;
					ms_activeVertexCount = iter->second->getVertexCount();
				}
			}
		}
		else
		{
			DynamicVertexBuffer const *dynamicVertexBuffer = static_cast<DynamicVertexBuffer const *>(&vertexBuffer);
			VulkanDynamicVertexBufferData *data = 0;
			if (ms_dynamicVertexBufferDataMap)
			{
				DynamicVertexBufferDataMap::iterator iter = ms_dynamicVertexBufferDataMap->find(dynamicVertexBuffer);
				if (iter != ms_dynamicVertexBufferDataMap->end())
					data = iter->second;
			}
			if (!data)
				data = ms_lastDynamicVertexBufferData;
			if (data)
			{
				ms_activeVertexDescriptor = &data->getDescriptor();
				ms_activeVertexFormatFlags = data->getFormatFlags();
				ms_activeVertexStride = ms_activeVertexDescriptor->vertexSize;
				ms_activeVertexCount = data->getVertexCount();
				size_t const byteCount = (ms_activeVertexCount > 0 && ms_activeVertexStride > 0) ? static_cast<size_t>(ms_activeVertexCount) * static_cast<size_t>(ms_activeVertexStride) : 0;
				byte const *sourceData = data->getData();
				if (sourceData && byteCount > 0)
				{
					ms_activeDynamicVertexSnapshot.assign(sourceData, sourceData + byteCount);
					ms_activeVertexData = &ms_activeDynamicVertexSnapshot[0];
				}
			}
		}
	}
	void setVertexBufferVector(VertexBufferVector const &vertexBufferVector)
	{
		VulkanVertexBufferVectorData const *data = VulkanVertexBufferVectorData::getData(vertexBufferVector);
		if (!data)
			return;

		std::vector<HardwareVertexBuffer const *> const &vertexBuffers = data->getVertexBuffers();
		if (vertexBuffers.empty() || !vertexBuffers[0])
			return;
		int const streamCount = static_cast<int>(std::min<size_t>(vertexBuffers.size(), 2));
		if (streamCount <= 0)
			return;

		static int s_vertexVectorLogCount = 0;
		if (s_vertexVectorLogCount < 40)
		{
			char buffer[256];
			_snprintf(buffer, sizeof(buffer) - 1, "setVertexBufferVector streams=%u", static_cast<unsigned>(vertexBuffers.size()));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			for (size_t i = 0; i < vertexBuffers.size() && i < 4; ++i)
			{
				HardwareVertexBuffer const *vertexBuffer = vertexBuffers[i];
				if (!vertexBuffer)
					continue;
				VertexBufferFormat const &format = vertexBuffer->getFormat();
				VertexBufferDescriptor const &descriptor = getVertexBufferDescriptor(format);
				_snprintf(buffer, sizeof(buffer) - 1, "  stream%u type=%d stride=%d flags=0x%08x pos=%d normal=%d color0=%d texSets=%d", static_cast<unsigned>(i), static_cast<int>(vertexBuffer->getType()), descriptor.vertexSize, static_cast<unsigned>(format.getFlags()), format.hasPosition() ? 1 : 0, format.hasNormal() ? 1 : 0, format.hasColor0() ? 1 : 0, format.getNumberOfTextureCoordinateSets());
				buffer[sizeof(buffer) - 1] = 0;
				logLine(buffer);
			}
		}
		ms_activeVertexData = 0;
		ms_activeDynamicVertexSnapshot.clear();
		for (int stream = 0; stream < 2; ++stream)
			ms_activeDynamicVertexStreamSnapshot[stream].clear();
		ms_activeVertexDescriptor = 0;
		ms_activeVertexFormatFlags = 0;
		ms_activeVertexStride = 0;
		ms_activeVertexCount = 0;
		ms_activeVertexStreamCount = streamCount;
		for (int stream = 0; stream < 2; ++stream)
		{
			ms_activeVertexStreamData[stream] = 0;
			ms_activeVertexStreamStrides[stream] = 0;
			ms_activeVertexStreamDescriptors[stream] = 0;
			ms_activeVertexStreamFormatFlags[stream] = 0;
			ms_activeVertexStreamCounts[stream] = 0;
		}
		for (int stream = 0; stream < streamCount; ++stream)
		{
			HardwareVertexBuffer const *vertexBuffer = vertexBuffers[static_cast<size_t>(stream)];
			if (!vertexBuffer)
				continue;

			byte const *vertexData = 0;
			VertexBufferDescriptor const *descriptor = 0;
			uint32 formatFlags = 0;
			int vertexCount = 0;
			if (vertexBuffer->getType() == HardwareVertexBuffer::T_static)
			{
				StaticVertexBuffer const *staticVertexBuffer = static_cast<StaticVertexBuffer const *>(vertexBuffer);
				if (ms_staticVertexBufferDataMap)
				{
					StaticVertexBufferDataMap::iterator iter = ms_staticVertexBufferDataMap->find(staticVertexBuffer);
					if (iter != ms_staticVertexBufferDataMap->end())
					{
						vertexData = iter->second->getData();
						descriptor = &iter->second->getDescriptor();
						formatFlags = iter->second->getFormatFlags();
						vertexCount = iter->second->getVertexCount();
					}
				}
			}
			else
			{
				DynamicVertexBuffer const *dynamicVertexBuffer = static_cast<DynamicVertexBuffer const *>(vertexBuffer);
				VulkanDynamicVertexBufferData *dynamicData = 0;
				if (ms_dynamicVertexBufferDataMap)
				{
					DynamicVertexBufferDataMap::iterator iter = ms_dynamicVertexBufferDataMap->find(dynamicVertexBuffer);
					if (iter != ms_dynamicVertexBufferDataMap->end())
						dynamicData = iter->second;
				}
				if (!dynamicData)
					dynamicData = ms_lastDynamicVertexBufferData;
				if (dynamicData)
				{
					descriptor = &dynamicData->getDescriptor();
					formatFlags = dynamicData->getFormatFlags();
					vertexCount = dynamicData->getVertexCount();
					size_t const byteCount = (vertexCount > 0 && descriptor && descriptor->vertexSize > 0) ? static_cast<size_t>(vertexCount) * static_cast<size_t>(descriptor->vertexSize) : 0;
					byte const *sourceData = dynamicData->getData();
					if (sourceData && byteCount > 0)
					{
						ms_activeDynamicVertexStreamSnapshot[stream].assign(sourceData, sourceData + byteCount);
						vertexData = &ms_activeDynamicVertexStreamSnapshot[stream][0];
					}
				}
			}

			ms_activeVertexStreamData[stream] = vertexData;
			ms_activeVertexStreamDescriptors[stream] = descriptor;
			ms_activeVertexStreamFormatFlags[stream] = formatFlags;
			ms_activeVertexStreamStrides[stream] = descriptor ? descriptor->vertexSize : 0;
			ms_activeVertexStreamCounts[stream] = vertexCount;
			if (stream == 0)
			{
				ms_activeVertexData = vertexData;
				ms_activeVertexDescriptor = descriptor;
				ms_activeVertexFormatFlags = formatFlags;
				ms_activeVertexStride = descriptor ? descriptor->vertexSize : 0;
				ms_activeVertexCount = vertexCount;
			}
		}
		ms_activeVertexVector = true;
		if (s_vertexVectorLogCount < 40)
		{
			char buffer[192];
			_snprintf(buffer, sizeof(buffer) - 1, "setVertexBufferVector activeV=%d stride=%d flags=0x%08x streams=%d s1V=%d s1stride=%d s1flags=0x%08x", ms_activeVertexCount, ms_activeVertexStride, static_cast<unsigned>(ms_activeVertexFormatFlags), ms_activeVertexStreamCount, ms_activeVertexStreamCounts[1], ms_activeVertexStreamStrides[1], static_cast<unsigned>(ms_activeVertexStreamFormatFlags[1]));
			buffer[sizeof(buffer) - 1] = 0;
			logLine(buffer);
			++s_vertexVectorLogCount;
		}
	}
	StaticIndexBufferGraphicsData *createStaticIndexBufferData(const StaticIndexBuffer &indexBuffer)
	{
		if (!ms_staticIndexBufferDataMap)
			ms_staticIndexBufferDataMap = new StaticIndexBufferDataMap;
		VulkanStaticIndexBufferData *data = new VulkanStaticIndexBufferData(indexBuffer);
		(*ms_staticIndexBufferDataMap)[&indexBuffer] = data;
		return data;
	}
	DynamicIndexBufferGraphicsData *createDynamicIndexBufferData() { return new VulkanDynamicIndexBufferData; }
	void setIndexBuffer(const HardwareIndexBuffer &indexBuffer)
	{
		ms_activeIndexData = 0;
		ms_activeIndexCount = 0;
		if (indexBuffer.getType() == HardwareIndexBuffer::T_static)
		{
			StaticIndexBuffer const *staticIndexBuffer = static_cast<StaticIndexBuffer const *>(&indexBuffer);
			if (ms_staticIndexBufferDataMap)
			{
				StaticIndexBufferDataMap::iterator iter = ms_staticIndexBufferDataMap->find(staticIndexBuffer);
				if (iter != ms_staticIndexBufferDataMap->end())
				{
					ms_activeIndexData = iter->second->getData();
					ms_activeIndexCount = iter->second->getIndexCount();
				}
			}
		}
		else if (ms_lastDynamicIndexBufferData)
		{
			ms_activeIndexData = ms_lastDynamicIndexBufferData->getData();
			ms_activeIndexCount = ms_lastDynamicIndexBufferData->getIndexCount();
		}
	}
	void setDynamicIndexBufferSize(int) {}
	void getOneToOneUVMapping(int textureWidth, int textureHeight, real &u0, real &v0, real &u1, real &v1)
	{
		u0 = CONST_REAL(0);
		v0 = CONST_REAL(0);
		u1 = textureWidth > 0 ? CONST_REAL(1) : CONST_REAL(0);
		v1 = textureHeight > 0 ? CONST_REAL(1) : CONST_REAL(0);
	}
	TextureGraphicsData *createTextureData(const Texture &texture, const TextureFormat *runtimeFormats, int numberOfRuntimeFormats) { return new VulkanTextureData(texture, runtimeFormats, numberOfRuntimeFormats); }
	ShaderImplementationPassVertexShaderGraphicsData *createVertexShaderData(ShaderImplementationPassVertexShader const &) { return new VulkanVertexShaderData; }
	ShaderImplementationPassPixelShaderProgramGraphicsData *createPixelShaderProgramData(ShaderImplementationPassPixelShaderProgram const &) { return new VulkanPixelShaderData; }
	void logDrawCall(char const *kind, int firstVertex, int vertexCount, int baseIndex, int firstIndex, int indexCount)
	{
		static int s_drawLogCount = 0;
		if (s_drawLogCount >= 160)
			return;
		++s_drawLogCount;
		VertexBufferFormat format;
		format.setFlags(ms_activeVertexFormatFlags);
		char buffer[256];
		_snprintf(buffer, sizeof(buffer) - 1, "draw %s fv=%d vc=%d bi=%d fi=%d ic=%d activeV=%d activeI=%d stride=%d flags=0x%08x transformed=%d color0=%d texSets=%d", kind, firstVertex, vertexCount, baseIndex, firstIndex, indexCount, ms_activeVertexCount, ms_activeIndexCount, ms_activeVertexStride, static_cast<unsigned>(ms_activeVertexFormatFlags), format.isTransformed() ? 1 : 0, format.hasColor0() ? 1 : 0, format.getNumberOfTextureCoordinateSets());
		buffer[sizeof(buffer) - 1] = 0;
		logLine(buffer);
	}
	void drawPointList()
	{
		logDrawCall("pointList", 0, ms_activeVertexCount, 0, 0, 0);
		drawPointListRange(0, ms_activeVertexCount);
	}
	void drawLineList() { logDrawCall("lineList", 0, ms_activeVertexCount, 0, 0, 0); }
	void drawLineStrip() { logDrawCall("lineStrip", 0, ms_activeVertexCount, 0, 0, 0); }
	void drawTriangleList()
	{
		int const vertexCount = (ms_activeVertexCount / 3) * 3;
		logDrawCall("triangleList", 0, vertexCount, 0, 0, 0);
		drawTriangleListRange(0, vertexCount);
	}
	void drawTriangleStrip()
	{
		logDrawCall("triangleStrip", 0, ms_activeVertexCount, 0, 0, 0);
		drawTriangleStripRange(0, ms_activeVertexCount);
	}
	void drawTriangleFan()
	{
		logDrawCall("triangleFan", 0, ms_activeVertexCount, 0, 0, 0);
		drawTriangleFanRange(0, ms_activeVertexCount);
	}
	void drawQuadList()
	{
		int const vertexCount = (ms_activeVertexCount / 4) * 4;
		logDrawCall("quadList", 0, vertexCount, 0, 0, 0);
		drawTransformedQuadList(0, vertexCount);
	}
	void drawIndexedPointList()
	{
		logDrawCall("indexedPointList", 0, 0, 0, 0, ms_activeIndexCount);
		if (!ms_activeIndexData)
			return;
		for (int i = 0; i < ms_activeIndexCount; ++i)
			stageGpuPointSprite(ms_activeIndexData[i]);
	}
	void drawIndexedLineList() { logDrawCall("indexedLineList", 0, 0, 0, 0, ms_activeIndexCount); }
	void drawIndexedLineStrip() { logDrawCall("indexedLineStrip", 0, 0, 0, 0, ms_activeIndexCount); }
	void drawIndexedTriangleList()
	{
		int const indexCount = (ms_activeIndexCount / 3) * 3;
		logDrawCall("indexedTriangleList", 0, 0, 0, 0, indexCount);
		drawIndexedTriangleListRange(0, 0, indexCount);
	}
	void drawIndexedTriangleStrip()
	{
		logDrawCall("indexedTriangleStrip", 0, 0, 0, 0, ms_activeIndexCount);
		drawIndexedTriangleStripRange(0, 0, ms_activeIndexCount);
	}
	void drawIndexedTriangleFan()
	{
		logDrawCall("indexedTriangleFan", 0, 0, 0, 0, ms_activeIndexCount);
		drawIndexedTriangleFanRange(0, 0, ms_activeIndexCount);
	}
	void drawPartialPointList(int firstVertex, int vertexCount)
	{
		logDrawCall("partialPointList", firstVertex, vertexCount, 0, 0, 0);
		drawPointListRange(firstVertex, vertexCount);
	}
	void drawPartialLineList(int firstVertex, int vertexCount) { logDrawCall("partialLineList", firstVertex, vertexCount, 0, 0, 0); }
	void drawPartialLineStrip(int firstVertex, int vertexCount) { logDrawCall("partialLineStrip", firstVertex, vertexCount, 0, 0, 0); }
	void drawPartialTriangleList(int firstVertex, int primitiveCount)
	{
		int const vertexCount = primitiveCount * 3;
		logDrawCall("partialTriangleList", firstVertex, vertexCount, 0, 0, 0);
		drawTriangleListRange(firstVertex, vertexCount);
	}
	void drawPartialTriangleStrip(int firstVertex, int primitiveCount)
	{
		int const vertexCount = primitiveCount + 2;
		logDrawCall("partialTriangleStrip", firstVertex, vertexCount, 0, 0, 0);
		drawTriangleStripRange(firstVertex, vertexCount);
	}
	void drawPartialTriangleFan(int firstVertex, int primitiveCount)
	{
		int const vertexCount = primitiveCount + 2;
		logDrawCall("partialTriangleFan", firstVertex, vertexCount, 0, 0, 0);
		drawTriangleFanRange(firstVertex, vertexCount);
	}
	void drawPartialIndexedPointList(int baseIndex, int firstVertex, int vertexCount, int firstIndex, int indexCount)
	{
		logDrawCall("partialIndexedPointList", firstVertex, vertexCount, baseIndex, firstIndex, indexCount);
		UNREF(vertexCount);
		if (!ms_activeIndexData)
			return;
		int const clampedFirst = std::max(0, firstIndex);
		int const clampedCount = std::max(0, std::min(indexCount, ms_activeIndexCount - clampedFirst));
		for (int i = 0; i < clampedCount; ++i)
			stageGpuPointSprite(baseIndex + ms_activeIndexData[clampedFirst + i]);
	}
	void drawPartialIndexedLineList(int baseIndex, int firstVertex, int vertexCount, int firstIndex, int indexCount) { logDrawCall("partialIndexedLineList", firstVertex, vertexCount, baseIndex, firstIndex, indexCount); }
	void drawPartialIndexedLineStrip(int baseIndex, int firstVertex, int vertexCount, int firstIndex, int indexCount) { logDrawCall("partialIndexedLineStrip", firstVertex, vertexCount, baseIndex, firstIndex, indexCount); }
	void drawPartialIndexedTriangleList(int baseIndex, int firstVertex, int vertexCount, int firstIndex, int indexCount)
	{
		logDrawCall("partialIndexedTriangleList", firstVertex, vertexCount, baseIndex, firstIndex, indexCount);
		drawIndexedTriangleListRange(baseIndex, firstIndex, indexCount * 3);
	}
	void drawPartialIndexedTriangleStrip(int baseIndex, int firstVertex, int vertexCount, int firstIndex, int indexCount)
	{
		logDrawCall("partialIndexedTriangleStrip", firstVertex, vertexCount, baseIndex, firstIndex, indexCount);
		drawIndexedTriangleStripRange(baseIndex, firstIndex, indexCount + 2);
	}
	void drawPartialIndexedTriangleFan(int baseIndex, int firstVertex, int vertexCount, int firstIndex, int indexCount)
	{
		logDrawCall("partialIndexedTriangleFan", firstVertex, vertexCount, baseIndex, firstIndex, indexCount);
		drawIndexedTriangleFanRange(baseIndex, firstIndex, indexCount + 2);
	}
	void optimizeIndexBuffer(WORD *, int) {}
	int getMaximumVertexBufferStreamCount() { return 2; }
	void setBloomEnabled(bool) {}
	void pixSetMarker(WCHAR const *) {}
	void pixBeginEvent(WCHAR const *) {}
	void pixEndEvent(WCHAR const *) {}
	bool writeImage(char const *, int, int, int, int const *, bool, Gl_imageFormat, Rectangle2d const *) { return false; }
	bool supportsAntialias() { return false; }
	void setAntialiasEnabled(bool) {}

#ifdef _DEBUG
	void setTexturesEnabled(bool) {}
	void showMipmapLevels(bool) {}
	bool getShowMipmapLevels() { return false; }
	void setBadVertexBufferVertexShaderCombination(bool *, const char *) {}
	void getRenderedVerticesPointsLinesTrianglesCalls(int &vertices, int &points, int &lines, int &triangles, int &calls)
	{
		vertices = points = lines = triangles = calls = 0;
	}
#endif

#if PRODUCTION == 0
	bool createVideoBuffers(int, int) { return false; }
	void fillVideoBuffers() {}
	bool getVideoBufferData(void *, size_t) { return false; }
	void releaseVideoBuffers() {}
#endif

	void fillApi()
	{
		ZeroMemory(&ms_glApi, sizeof(ms_glApi));
		ms_glApi.verify = verify;
		ms_glApi.install = install;
		ms_glApi.remove = remove;
		ms_glApi.displayModeChanged = displayModeChanged;
		ms_glApi.getShaderCapability = getShaderCapability;
		ms_glApi.requiresVertexAndPixelShaders = requiresVertexAndPixelShaders;
		ms_glApi.getOtherAdapterRects = getOtherAdapterRects;
		ms_glApi.getVideoMemoryInMegabytes = getVideoMemoryInMegabytes;
		ms_glApi.flushResources = flushResources;
		ms_glApi.isGdiVisible = isGdiVisible;
		ms_glApi.wasDeviceReset = wasDeviceReset;
		ms_glApi.addDeviceLostCallback = addDeviceLostCallback;
		ms_glApi.removeDeviceLostCallback = removeDeviceLostCallback;
		ms_glApi.addDeviceRestoredCallback = addDeviceRestoredCallback;
		ms_glApi.removeDeviceRestoredCallback = removeDeviceRestoredCallback;
#ifdef _DEBUG
		ms_glApi.setTexturesEnabled = setTexturesEnabled;
		ms_glApi.showMipmapLevels = showMipmapLevels;
		ms_glApi.getShowMipmapLevels = getShowMipmapLevels;
		ms_glApi.setBadVertexBufferVertexShaderCombination = setBadVertexBufferVertexShaderCombination;
		ms_glApi.getRenderedVerticesPointsLinesTrianglesCalls = getRenderedVerticesPointsLinesTrianglesCalls;
#endif
		ms_glApi.supportsMipmappedCubeMaps = supportsMipmappedCubeMaps;
		ms_glApi.supportsScissorRect = supportsScissorRect;
		ms_glApi.supportsHardwareMouseCursor = supportsHardwareMouseCursor;
		ms_glApi.supportsTwoSidedStencil = supportsTwoSidedStencil;
		ms_glApi.supportsStreamOffsets = supportsStreamOffsets;
		ms_glApi.supportsDynamicTextures = supportsDynamicTextures;
		ms_glApi.setBrightnessContrastGamma = setBrightnessContrastGamma;
		ms_glApi.resize = resize;
		ms_glApi.setWindowedMode = setWindowedMode;
		ms_glApi.setFillMode = setFillMode;
		ms_glApi.setCullMode = setCullMode;
		ms_glApi.setPointSize = setPointSize;
		ms_glApi.setPointSizeMax = setPointSizeMax;
		ms_glApi.setPointSizeMin = setPointSizeMin;
		ms_glApi.setPointScaleEnable = setPointScaleEnable;
		ms_glApi.setPointScaleFactor = setPointScaleFactor;
		ms_glApi.setPointSpriteEnable = setPointSpriteEnable;
		ms_glApi.clearViewport = clearViewport;
		ms_glApi.update = update;
		ms_glApi.beginScene = beginScene;
		ms_glApi.endScene = endScene;
		ms_glApi.lockBackBuffer = lockBackBuffer;
		ms_glApi.unlockBackBuffer = unlockBackBuffer;
		ms_glApi.present = present;
		ms_glApi.presentToWindow = presentToWindow;
		ms_glApi.vrIsWorldRenderingEnabled = vrIsWorldRenderingEnabled;
		ms_glApi.vrBeginWorldFrame = vrBeginWorldFrame;
		ms_glApi.vrGetEyeInfo = vrGetEyeInfo;
		ms_glApi.vrBeginEye = vrBeginEye;
		ms_glApi.vrEndEye = vrEndEye;
		ms_glApi.vrEndWorldFrame = vrEndWorldFrame;
		ms_glApi.vrBeginHudCapture = vrBeginHudCapture;
		ms_glApi.vrEndHudCapture = vrEndHudCapture;
		ms_glApi.vrSubmitHudPanelRect = vrSubmitHudPanelRect;
		ms_glApi.setRenderTarget = setRenderTarget;
		ms_glApi.copyRenderTargetToNonRenderTargetTexture = copyRenderTargetToNonRenderTargetTexture;
		ms_glApi.screenShot = screenShot;
		ms_glApi.createShaderImplementationGraphicsData = createShaderImplementationGraphicsData;
		ms_glApi.createStaticShaderGraphicsData = createStaticShaderGraphicsData;
		ms_glApi.setBadVertexShaderStaticShader = setBadVertexShaderStaticShader;
		ms_glApi.setStaticShader = setStaticShader;
		ms_glApi.setMouseCursor = setMouseCursor;
		ms_glApi.showMouseCursor = showMouseCursor;
		ms_glApi.setViewport = setViewport;
		ms_glApi.setScissorRect = setScissorRect;
		ms_glApi.setWorldToCameraTransform = setWorldToCameraTransform;
		ms_glApi.setProjectionMatrix = setProjectionMatrix;
		ms_glApi.setFog = setFog;
		ms_glApi.setObjectToWorldTransformAndScale = setObjectToWorldTransformAndScale;
		ms_glApi.setGlobalTexture = setGlobalTexture;
		ms_glApi.releaseAllGlobalTextures = releaseAllGlobalTextures;
		ms_glApi.setTextureTransform = setTextureTransform;
		ms_glApi.setVertexShaderUserConstants = setVertexShaderUserConstants;
		ms_glApi.setPixelShaderUserConstants = setPixelShaderUserConstants;
		ms_glApi.setAlphaFadeOpacity = setAlphaFadeOpacity;
		ms_glApi.setLights = setLights;
		ms_glApi.createStaticVertexBufferData = createStaticVertexBufferData;
		ms_glApi.createDynamicVertexBufferData = createDynamicVertexBufferData;
		ms_glApi.createVertexBufferVectorData = createVertexBufferVectorData;
		ms_glApi.setVertexBuffer = setVertexBuffer;
		ms_glApi.setVertexBufferVector = setVertexBufferVector;
		ms_glApi.createStaticIndexBufferData = createStaticIndexBufferData;
		ms_glApi.createDynamicIndexBufferData = createDynamicIndexBufferData;
		ms_glApi.setIndexBuffer = setIndexBuffer;
		ms_glApi.setDynamicIndexBufferSize = setDynamicIndexBufferSize;
		ms_glApi.getOneToOneUVMapping = getOneToOneUVMapping;
		ms_glApi.createTextureData = createTextureData;
		ms_glApi.createVertexShaderData = createVertexShaderData;
		ms_glApi.createPixelShaderProgramData = createPixelShaderProgramData;
		ms_glApi.drawPointList = drawPointList;
		ms_glApi.drawLineList = drawLineList;
		ms_glApi.drawLineStrip = drawLineStrip;
		ms_glApi.drawTriangleList = drawTriangleList;
		ms_glApi.drawTriangleStrip = drawTriangleStrip;
		ms_glApi.drawTriangleFan = drawTriangleFan;
		ms_glApi.drawQuadList = drawQuadList;
		ms_glApi.drawIndexedPointList = drawIndexedPointList;
		ms_glApi.drawIndexedLineList = drawIndexedLineList;
		ms_glApi.drawIndexedLineStrip = drawIndexedLineStrip;
		ms_glApi.drawIndexedTriangleList = drawIndexedTriangleList;
		ms_glApi.drawIndexedTriangleStrip = drawIndexedTriangleStrip;
		ms_glApi.drawIndexedTriangleFan = drawIndexedTriangleFan;
		ms_glApi.drawPartialPointList = drawPartialPointList;
		ms_glApi.drawPartialLineList = drawPartialLineList;
		ms_glApi.drawPartialLineStrip = drawPartialLineStrip;
		ms_glApi.drawPartialTriangleList = drawPartialTriangleList;
		ms_glApi.drawPartialTriangleStrip = drawPartialTriangleStrip;
		ms_glApi.drawPartialTriangleFan = drawPartialTriangleFan;
		ms_glApi.drawPartialIndexedPointList = drawPartialIndexedPointList;
		ms_glApi.drawPartialIndexedLineList = drawPartialIndexedLineList;
		ms_glApi.drawPartialIndexedLineStrip = drawPartialIndexedLineStrip;
		ms_glApi.drawPartialIndexedTriangleList = drawPartialIndexedTriangleList;
		ms_glApi.drawPartialIndexedTriangleStrip = drawPartialIndexedTriangleStrip;
		ms_glApi.drawPartialIndexedTriangleFan = drawPartialIndexedTriangleFan;
		ms_glApi.optimizeIndexBuffer = optimizeIndexBuffer;
		ms_glApi.getMaximumVertexBufferStreamCount = getMaximumVertexBufferStreamCount;
		ms_glApi.setBloomEnabled = setBloomEnabled;
		ms_glApi.pixSetMarker = pixSetMarker;
		ms_glApi.pixBeginEvent = pixBeginEvent;
		ms_glApi.pixEndEvent = pixEndEvent;
		ms_glApi.writeImage = writeImage;
		ms_glApi.supportsAntialias = supportsAntialias;
		ms_glApi.setAntialiasEnabled = setAntialiasEnabled;
#if PRODUCTION == 0
		ms_glApi.createVideoBuffers = createVideoBuffers;
		ms_glApi.fillVideoBuffers = fillVideoBuffers;
		ms_glApi.getVideoBufferData = getVideoBufferData;
		ms_glApi.releaseVideoBuffers = releaseVideoBuffers;
#endif
	}
}

// ======================================================================

extern "C" __declspec(dllexport) Gl_api const * GetApi();
extern "C" __declspec(dllexport) void SetVrTvModeEnabled(int);

Gl_api const * GetApi()
{
	VulkanNamespace::fillApi();
	return &VulkanNamespace::ms_glApi;
}

void SetVrTvModeEnabled(int)
{
}

// ======================================================================
