// ======================================================================
//
// Direct3d11.cpp
//
// Clean D3D11 rasterizer skeleton for the original SWG client graphics API.
//
// ======================================================================

#include "FirstDirect3d11.h"

#include "ConfigDirect3d11.h"
#include "Direct3d11_Diagnostics.h"
#include "Direct3d11_InputLayoutCache.h"
#include "Direct3d11_VrBridge.h"
#include "clientGraphics/DynamicIndexBuffer.h"
#include "clientGraphics/DynamicVertexBuffer.h"
#include "clientGraphics/Gl_dll.def"
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

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <math.h>
#include <map>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vector>

// ======================================================================

namespace
{
	template <typename T>
	struct D3d11DescLess
	{
		bool operator()(T const &lhs, T const &rhs) const
		{
			return memcmp(&lhs, &rhs, sizeof(T)) < 0;
		}
	};

	int getTextureLevelDimension(int dimension, int level)
	{
		int result = dimension >> level;
		return result > 0 ? result : 1;
	}

	int getTextureRowPitch(TextureFormat format, int width)
	{
		TextureFormatInfo const &info = TextureFormatInfo::getInfo(format);
		if (info.compressed)
		{
			int blocksWide = (width + info.blockWidth - 1) / info.blockWidth;
			if (blocksWide < 1)
				blocksWide = 1;
			return blocksWide * info.blockSize;
		}

		return width * info.pixelByteCount;
	}

	int getTextureSlicePitch(TextureFormat format, int width, int height)
	{
		TextureFormatInfo const &info = TextureFormatInfo::getInfo(format);
		int const rowPitch = getTextureRowPitch(format, width);
		if (info.compressed)
		{
			int blocksHigh = (height + info.blockHeight - 1) / info.blockHeight;
			if (blocksHigh < 1)
				blocksHigh = 1;
			return rowPitch * blocksHigh;
		}

		return rowPitch * height;
	}

	size_t getTextureLevelSize(TextureFormat format, int width, int height, int depth)
	{
		return static_cast<size_t>(getTextureSlicePitch(format, width, height)) * static_cast<size_t>(depth);
	}

	struct Direct3d11TextureScratch
	{
		TextureFormat format;
		int pitch;
		int slicePitch;
		std::vector<byte> data;
	};

	struct Direct3d11Rgba8
	{
		byte r;
		byte g;
		byte b;
		byte a;
	};

	bool isTextureFormat32BitColor(TextureFormat format)
	{
		return format == TF_ARGB_8888 || format == TF_XRGB_8888;
	}

	Direct3d11Rgba8 readScratchRgba(Direct3d11TextureScratch const &scratch, int x, int y)
	{
		byte const *pixel = &scratch.data[static_cast<size_t>(y * scratch.pitch + x * 4)];
		Direct3d11Rgba8 result;
		result.b = pixel[0];
		result.g = pixel[1];
		result.r = pixel[2];
		result.a = scratch.format == TF_XRGB_8888 ? static_cast<byte>(255) : pixel[3];
		return result;
	}

	unsigned short packRgba565(Direct3d11Rgba8 const &color)
	{
		return static_cast<unsigned short>(((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3));
	}

	Direct3d11Rgba8 unpackRgba565(unsigned short packed)
	{
		Direct3d11Rgba8 result;
		result.r = static_cast<byte>(((packed >> 11) & 0x1f) * 255 / 31);
		result.g = static_cast<byte>(((packed >> 5) & 0x3f) * 255 / 63);
		result.b = static_cast<byte>((packed & 0x1f) * 255 / 31);
		result.a = 255;
		return result;
	}

	int colorDistanceSquared(Direct3d11Rgba8 const &lhs, Direct3d11Rgba8 const &rhs)
	{
		int const dr = static_cast<int>(lhs.r) - static_cast<int>(rhs.r);
		int const dg = static_cast<int>(lhs.g) - static_cast<int>(rhs.g);
		int const db = static_cast<int>(lhs.b) - static_cast<int>(rhs.b);
		return dr * dr + dg * dg + db * db;
	}

	Direct3d11Rgba8 lerpRgba(Direct3d11Rgba8 const &a, Direct3d11Rgba8 const &b, int aWeight, int bWeight, int divisor)
	{
		Direct3d11Rgba8 result;
		result.r = static_cast<byte>((static_cast<int>(a.r) * aWeight + static_cast<int>(b.r) * bWeight) / divisor);
		result.g = static_cast<byte>((static_cast<int>(a.g) * aWeight + static_cast<int>(b.g) * bWeight) / divisor);
		result.b = static_cast<byte>((static_cast<int>(a.b) * aWeight + static_cast<int>(b.b) * bWeight) / divisor);
		result.a = 255;
		return result;
	}

	void encodeDxt1Block(Direct3d11TextureScratch const &scratch, int sourceX, int sourceY, int width, int height, byte *block)
	{
		Direct3d11Rgba8 minColor = { 255, 255, 255, 255 };
		Direct3d11Rgba8 maxColor = { 0, 0, 0, 255 };
		bool hasOpaque = false;
		bool hasAlpha = false;

		for (int y = 0; y < 4; ++y)
		{
			int const py = std::min(sourceY + y, height - 1);
			for (int x = 0; x < 4; ++x)
			{
				int const px = std::min(sourceX + x, width - 1);
				Direct3d11Rgba8 const c = readScratchRgba(scratch, px, py);
				if (c.a < 128)
				{
					hasAlpha = true;
					continue;
				}

				hasOpaque = true;
				minColor.r = std::min(minColor.r, c.r);
				minColor.g = std::min(minColor.g, c.g);
				minColor.b = std::min(minColor.b, c.b);
				maxColor.r = std::max(maxColor.r, c.r);
				maxColor.g = std::max(maxColor.g, c.g);
				maxColor.b = std::max(maxColor.b, c.b);
			}
		}

		if (!hasOpaque)
		{
			memset(block, 0, 8);
			return;
		}

		unsigned short color0 = packRgba565(maxColor);
		unsigned short color1 = packRgba565(minColor);
		if (hasAlpha)
		{
			if (color0 > color1)
				std::swap(color0, color1);
		}
		else if (color0 < color1)
			std::swap(color0, color1);

		Direct3d11Rgba8 palette[4];
		palette[0] = unpackRgba565(color0);
		palette[1] = unpackRgba565(color1);
		if (color0 > color1)
		{
			palette[2] = lerpRgba(palette[0], palette[1], 2, 1, 3);
			palette[3] = lerpRgba(palette[0], palette[1], 1, 2, 3);
		}
		else
		{
			palette[2] = lerpRgba(palette[0], palette[1], 1, 1, 2);
			palette[3].r = 0;
			palette[3].g = 0;
			palette[3].b = 0;
			palette[3].a = 0;
		}

		unsigned int indices = 0;
		for (int y = 0; y < 4; ++y)
		{
			int const py = std::min(sourceY + y, height - 1);
			for (int x = 0; x < 4; ++x)
			{
				int const px = std::min(sourceX + x, width - 1);
				Direct3d11Rgba8 const c = readScratchRgba(scratch, px, py);
				int bestIndex = (hasAlpha && c.a < 128 && color0 <= color1) ? 3 : 0;
				if (bestIndex != 3)
				{
					int bestDistance = colorDistanceSquared(c, palette[0]);
					int const paletteCount = color0 > color1 ? 4 : 3;
					for (int i = 1; i < paletteCount; ++i)
					{
						int const distance = colorDistanceSquared(c, palette[i]);
						if (distance < bestDistance)
						{
							bestDistance = distance;
							bestIndex = i;
						}
					}
				}
				indices |= static_cast<unsigned int>(bestIndex & 3) << (2 * (y * 4 + x));
			}
		}

		block[0] = static_cast<byte>(color0 & 0xff);
		block[1] = static_cast<byte>((color0 >> 8) & 0xff);
		block[2] = static_cast<byte>(color1 & 0xff);
		block[3] = static_cast<byte>((color1 >> 8) & 0xff);
		block[4] = static_cast<byte>(indices & 0xff);
		block[5] = static_cast<byte>((indices >> 8) & 0xff);
		block[6] = static_cast<byte>((indices >> 16) & 0xff);
		block[7] = static_cast<byte>((indices >> 24) & 0xff);
	}

	DXGI_FORMAT getDxgiTextureFormat(TextureFormat format)
	{
		switch (format)
		{
			case TF_ARGB_8888:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			case TF_XRGB_8888:
				return DXGI_FORMAT_B8G8R8X8_UNORM;
			case TF_DXT1:
				return DXGI_FORMAT_BC1_UNORM;
			case TF_DXT3:
				return DXGI_FORMAT_BC2_UNORM;
			case TF_DXT5:
				return DXGI_FORMAT_BC3_UNORM;
			case TF_A_8:
				return DXGI_FORMAT_A8_UNORM;
			case TF_ABGR_16F:
				return DXGI_FORMAT_R16G16B16A16_FLOAT;
			case TF_ABGR_32F:
				return DXGI_FORMAT_R32G32B32A32_FLOAT;
			default:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}

	bool needsBgraTextureSwizzle(TextureFormat format)
	{
		UNREF(format);
		return false;
	}
}

namespace Direct3d11Namespace
{
	int const cms_maxStageTextures = 12;

	struct ShaderPassState
	{
		bool depthEnable;
		bool depthWrite;
		D3D11_COMPARISON_FUNC depthFunc;
		bool alphaBlendEnable;
		D3D11_BLEND_OP blendOp;
		D3D11_BLEND sourceBlend;
		D3D11_BLEND destinationBlend;
		UINT8 colorWriteMask;
		bool lightingEnable;
		bool lightingColorVertex;
		bool lightingSpecularEnable;
		int lightingAmbientColorSource;
		int lightingDiffuseColorSource;
		int lightingSpecularColorSource;
		int lightingEmissiveColorSource;
		bool stencilEnable;
		bool stencilTwoSidedMode;
		D3D11_COMPARISON_FUNC stencilFunc;
		D3D11_STENCIL_OP stencilFailOp;
		D3D11_STENCIL_OP stencilDepthFailOp;
		D3D11_STENCIL_OP stencilPassOp;
		D3D11_COMPARISON_FUNC ccwStencilFunc;
		D3D11_STENCIL_OP ccwStencilFailOp;
		D3D11_STENCIL_OP ccwStencilDepthFailOp;
		D3D11_STENCIL_OP ccwStencilPassOp;
		UINT8 stencilReadMask;
		UINT8 stencilWriteMask;
		Tag stencilReferenceValueTag;

		ShaderPassState()
		: depthEnable(true),
		  depthWrite(true),
		  depthFunc(D3D11_COMPARISON_LESS_EQUAL),
		  alphaBlendEnable(false),
		  blendOp(D3D11_BLEND_OP_ADD),
		  sourceBlend(D3D11_BLEND_ONE),
		  destinationBlend(D3D11_BLEND_ZERO),
		  colorWriteMask(D3D11_COLOR_WRITE_ENABLE_ALL),
		  lightingEnable(false),
		  lightingColorVertex(false),
		  lightingSpecularEnable(false),
		  lightingAmbientColorSource(0),
		  lightingDiffuseColorSource(0),
		  lightingSpecularColorSource(0),
		  lightingEmissiveColorSource(0),
		  stencilEnable(false),
		  stencilTwoSidedMode(false),
		  stencilFunc(D3D11_COMPARISON_ALWAYS),
		  stencilFailOp(D3D11_STENCIL_OP_KEEP),
		  stencilDepthFailOp(D3D11_STENCIL_OP_KEEP),
		  stencilPassOp(D3D11_STENCIL_OP_KEEP),
		  ccwStencilFunc(D3D11_COMPARISON_ALWAYS),
		  ccwStencilFailOp(D3D11_STENCIL_OP_KEEP),
		  ccwStencilDepthFailOp(D3D11_STENCIL_OP_KEEP),
		  ccwStencilPassOp(D3D11_STENCIL_OP_KEEP),
		  stencilReadMask(0xff),
		  stencilWriteMask(0xff),
		  stencilReferenceValueTag(0)
		{
		}
	};

	template <typename T>
	void releaseCom(T *&object);
	bool isDiagnosticsEnabled();
	void diag(char const *format, ...);
	void applyShaderPassState(ShaderPassState const &state);
	bool ensureModernShadowResources();
	void releaseModernShadowResources();

#ifdef _DEBUG
	void setTexturesEnabled(bool);
	void showMipmapLevels(bool);
	bool getShowMipmapLevels();
	void setBadVertexBufferVertexShaderCombination(bool *flag, const char *debugAppearanceName);
	void getRenderedVerticesPointsLinesTrianglesCalls(int &vertices, int &points, int &lines, int &triangles, int &calls);
#endif

	#ifdef _DEBUG
		bool                   ms_debugTexturesEnabled = true;
		bool                   ms_showMipmapLevelsEnabled = false;
	#endif
	extern ID3D11Device *ms_device;
	extern ID3D11DeviceContext *ms_context;
}

// This class name is intentionally global: ShaderImplementation only grants
// renderer-private pass access to the legacy D3D implementation-data class.
class Direct3d11_ShaderImplementationData : public ShaderImplementationGraphicsData
{
public:
	explicit Direct3d11_ShaderImplementationData(ShaderImplementation const &implementation);
	virtual ~Direct3d11_ShaderImplementationData();

	void apply(int passNumber) const;

	static Direct3d11_ShaderImplementationData const *find(ShaderImplementation const &implementation);

	Tag getStencilReferenceValueTag(int passNumber) const
	{
		if (passNumber >= 0 && passNumber < static_cast<int>(m_pass.size()))
			return m_pass[static_cast<size_t>(passNumber)].stencilReferenceValueTag;
		return 0;
	}

private:
	typedef std::map<ShaderImplementation const *, Direct3d11_ShaderImplementationData const *> DataMap;

private:
	ShaderImplementation const *m_implementation;
	std::vector<Direct3d11Namespace::ShaderPassState> m_pass;

	static DataMap *ms_dataMap;
};

// This friend-name shim reads StaticShaderTemplate/ShaderEffect private links
// without calling non-exported accessor functions.
class Direct3d11_StaticShaderData : public StaticShaderGraphicsData
{
public:
	explicit Direct3d11_StaticShaderData(StaticShader const &shader);
	virtual ~Direct3d11_StaticShaderData();

	virtual void update(StaticShader const &shader);
	virtual uintptr_t getTextureSortKey() const;

	bool getTextureTag(int passNumber, Tag &textureTag) const;
	struct PassTexture;
	bool getPassTexture(int passNumber, PassTexture &passTexture) const;

	static ShaderImplementation const *getImplementation(StaticShader const &shader);
	static bool getPassTexture(StaticShader const &shader, int passNumber, PassTexture &passTexture);
	static bool getTextureTag(StaticShader const &shader, int passNumber, Tag &textureTag);

private:
	void construct(StaticShader const &shader);

private:
public:
	struct PassTexture
	{
		struct StageTexture
		{
			StageTexture()
			: textureTag(0),
			  textureStage(0),
			  textureCoordinateSet(0),
			  addressU(StaticShaderTemplate::TA_wrap),
			  addressV(StaticShaderTemplate::TA_wrap),
			  addressW(StaticShaderTemplate::TA_wrap),
			  mipFilter(StaticShaderTemplate::TF_linear),
			  minificationFilter(StaticShaderTemplate::TF_linear),
			  magnificationFilter(StaticShaderTemplate::TF_linear),
			  maxAnisotropy(1),
			  colorOperation(ShaderImplementation::Pass::Stage::TO_disable),
			  colorArgument0(ShaderImplementation::Pass::Stage::TA_current),
			  colorArgument1(ShaderImplementation::Pass::Stage::TA_current),
			  colorArgument2(ShaderImplementation::Pass::Stage::TA_texture),
			  colorArgumentFlags(0),
			  alphaOperation(ShaderImplementation::Pass::Stage::TO_disable),
			  alphaArgument0(ShaderImplementation::Pass::Stage::TA_current),
			  alphaArgument1(ShaderImplementation::Pass::Stage::TA_current),
			  alphaArgument2(ShaderImplementation::Pass::Stage::TA_texture),
			  alphaArgumentFlags(0),
			  resultArgument(ShaderImplementation::Pass::Stage::TA_current),
			  coordinateGeneration(ShaderImplementation::Pass::Stage::CG_passThru)
			{
				textureScroll[0] = 0.0f;
				textureScroll[1] = 0.0f;
				textureScroll[2] = 0.0f;
				textureScroll[3] = 0.0f;
			}

			Tag textureTag;
			int textureStage;
			uint8 textureCoordinateSet;
			StaticShaderTemplate::TextureAddress addressU;
			StaticShaderTemplate::TextureAddress addressV;
			StaticShaderTemplate::TextureAddress addressW;
			StaticShaderTemplate::TextureFilter mipFilter;
			StaticShaderTemplate::TextureFilter minificationFilter;
			StaticShaderTemplate::TextureFilter magnificationFilter;
			int maxAnisotropy;
			ShaderImplementation::Pass::Stage::TextureOperation colorOperation;
			ShaderImplementation::Pass::Stage::TextureArgument colorArgument0;
			ShaderImplementation::Pass::Stage::TextureArgument colorArgument1;
			ShaderImplementation::Pass::Stage::TextureArgument colorArgument2;
			int colorArgumentFlags;
			ShaderImplementation::Pass::Stage::TextureOperation alphaOperation;
			ShaderImplementation::Pass::Stage::TextureArgument alphaArgument0;
			ShaderImplementation::Pass::Stage::TextureArgument alphaArgument1;
			ShaderImplementation::Pass::Stage::TextureArgument alphaArgument2;
			int alphaArgumentFlags;
			ShaderImplementation::Pass::Stage::TextureArgument resultArgument;
			ShaderImplementation::Pass::Stage::CoordinateGeneration coordinateGeneration;
			float textureScroll[4];
		};

		PassTexture()
		: textureTag(0),
		  textureCoordinateSet(0),
		  addressU(StaticShaderTemplate::TA_wrap),
		  addressV(StaticShaderTemplate::TA_wrap),
		  addressW(StaticShaderTemplate::TA_wrap),
		  mipFilter(StaticShaderTemplate::TF_linear),
		  minificationFilter(StaticShaderTemplate::TF_linear),
		  magnificationFilter(StaticShaderTemplate::TF_linear),
		  maxAnisotropy(1),
		  alphaTestEnable(false),
		  alphaTestCompare(ShaderImplementation::Pass::C_Always),
		  alphaTestReference(0),
		  materialColorValid(false),
		  textureFactorValid(false),
		  textureStage(0),
		  fogMode(ShaderImplementation::Pass::FM_Normal),
		  textureScrollValid(false),
		  fullAmbient(false),
		  terrainDot3(false),
		  terrainBlendCount(0),
		  terrainProgramMode(0),
		  vertexProgramMode(0),
		  pixelProgramMode(0),
		  vertexProgramName(0),
		  pixelProgramName(0)
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
			textureScroll[0] = 0.0f;
			textureScroll[1] = 0.0f;
			textureScroll[2] = 0.0f;
			textureScroll[3] = 0.0f;
		}

		Tag textureTag;
		uint8 textureCoordinateSet;
		StaticShaderTemplate::TextureAddress addressU;
		StaticShaderTemplate::TextureAddress addressV;
		StaticShaderTemplate::TextureAddress addressW;
		StaticShaderTemplate::TextureFilter mipFilter;
		StaticShaderTemplate::TextureFilter minificationFilter;
		StaticShaderTemplate::TextureFilter magnificationFilter;
		int maxAnisotropy;
		bool alphaTestEnable;
		ShaderImplementation::Pass::Compare alphaTestCompare;
		uint8 alphaTestReference;
		bool materialColorValid;
		float materialAmbientColor[4];
		float materialColor[4];
		float materialEmissiveColor[4];
		float materialSpecularColor[4];
		float materialSpecularPower;
		bool textureFactorValid;
		float textureFactor[4];
		float textureFactor2[4];
		int textureStage;
		ShaderImplementation::Pass::FogMode fogMode;
		bool textureScrollValid;
		float textureScroll[4];
		bool fullAmbient;
		std::vector<StageTexture> stageTextures;
		std::vector<StageTexture> shaderTextures;
		bool terrainDot3;
		int terrainBlendCount;
		int terrainProgramMode;
		int vertexProgramMode;
		int pixelProgramMode;
		char const *vertexProgramName;
		char const *pixelProgramName;
	};

private:
	ShaderImplementation const *m_implementation;
	std::vector<PassTexture> m_firstTexture;
};

// VertexBufferVector only grants private stream-list access to the legacy
// D3D vector-data class name.
class Direct3d11_VertexBufferVectorData : public VertexBufferVectorGraphicsData
{
public:
	explicit Direct3d11_VertexBufferVectorData(VertexBufferVector const &vertexBufferVector);
	virtual ~Direct3d11_VertexBufferVectorData();

	std::vector<HardwareVertexBuffer const *> const &getVertexBuffers() const;

	static Direct3d11_VertexBufferVectorData const *getData(VertexBufferVector const &vertexBufferVector);

private:
	std::vector<HardwareVertexBuffer const *> m_vertexBuffers;
};

// This class name is intentionally global: Texture::LockData only grants
// renderer write access to the legacy D3D texture-data class names.
class Direct3d11_TextureData : public TextureGraphicsData
{
public:
	Direct3d11_TextureData(Texture const &texture, TextureFormat const *runtimeFormats, int numberOfRuntimeFormats)
	: TextureGraphicsData(),
	  m_texture(texture),
	  m_nativeFormat(TF_ARGB_8888),
	  m_faceCount(texture.isCubeMap() ? 6 : 1),
	  m_d3dTexture(0),
	  m_shaderResourceView(0),
	  m_renderTargetViews(),
	  m_dirty(true),
	  m_gpuWritten(false)
	{
		if (runtimeFormats && numberOfRuntimeFormats > 0)
			m_nativeFormat = runtimeFormats[0];

		for (int i = 0; runtimeFormats && i < numberOfRuntimeFormats; ++i)
		{
			if (runtimeFormats[i] >= 0 && runtimeFormats[i] < TF_Count && TextureFormatInfo::getInfo(runtimeFormats[i]).supported)
			{
				m_nativeFormat = runtimeFormats[i];
				break;
			}
		}

		int const mipCount = std::max(1, texture.getMipmapLevelCount());
		m_levels.resize(static_cast<size_t>(m_faceCount * mipCount));

		for (int face = 0; face < m_faceCount; ++face)
		{
			for (int level = 0; level < mipCount; ++level)
			{
				int const width = getTextureLevelDimension(texture.getWidth(), level);
				int const height = getTextureLevelDimension(texture.getHeight(), level);
				int const depth = texture.isVolumeMap() ? getTextureLevelDimension(texture.getDepth(), level) : 1;
				m_levels[getLevelIndex(face, level)].resize(getTextureLevelSize(m_nativeFormat, width, height, depth));
			}
		}
	}

	virtual ~Direct3d11_TextureData()
	{
		for (std::vector<ID3D11RenderTargetView *>::iterator i = m_renderTargetViews.begin(); i != m_renderTargetViews.end(); ++i)
			Direct3d11Namespace::releaseCom(*i);
		Direct3d11Namespace::releaseCom(m_shaderResourceView);
		Direct3d11Namespace::releaseCom(m_d3dTexture);
	}

	virtual void copyFrom(int surfaceLevel, TextureGraphicsData const &rhs, int srcX, int srcY, int srcWidth, int srcHeight, int dstX, int dstY, int dstWidth, int dstHeight)
	{
		Direct3d11_TextureData const *rhsTexture = dynamic_cast<Direct3d11_TextureData const *>(&rhs);
		if (!rhsTexture || rhsTexture == this || surfaceLevel < 0 || surfaceLevel >= m_texture.getMipmapLevelCount())
			return;

		if (m_nativeFormat != rhsTexture->m_nativeFormat)
			return;

		int const dstLevelWidth = getTextureLevelDimension(m_texture.getWidth(), surfaceLevel);
		int const dstLevelHeight = getTextureLevelDimension(m_texture.getHeight(), surfaceLevel);
		int const srcLevelWidth = getTextureLevelDimension(rhsTexture->m_texture.getWidth(), surfaceLevel);
		int const srcLevelHeight = getTextureLevelDimension(rhsTexture->m_texture.getHeight(), surfaceLevel);

		if (srcX < 0 || srcY < 0 || dstX < 0 || dstY < 0)
			return;

		int const copyWidth = std::min(std::min(srcWidth, dstWidth), std::min(srcLevelWidth - srcX, dstLevelWidth - dstX));
		int const copyHeight = std::min(std::min(srcHeight, dstHeight), std::min(srcLevelHeight - srcY, dstLevelHeight - dstY));
		if (copyWidth <= 0 || copyHeight <= 0)
			return;

		TextureFormatInfo const &info = TextureFormatInfo::getInfo(m_nativeFormat);
		if (info.compressed)
			return;

		int const bytesPerPixel = info.pixelByteCount;
		int const dstPitch = getTextureRowPitch(m_nativeFormat, dstLevelWidth);
		int const srcPitch = getTextureRowPitch(m_nativeFormat, srcLevelWidth);
		std::vector<byte> &dst = m_levels[getLevelIndex(0, surfaceLevel)];
		std::vector<byte> const &src = rhsTexture->m_levels[rhsTexture->getLevelIndex(0, surfaceLevel)];

		for (int y = 0; y < copyHeight; ++y)
		{
			byte *dstRow = &dst[0] + ((dstY + y) * dstPitch) + (dstX * bytesPerPixel);
			byte const *srcRow = &src[0] + ((srcY + y) * srcPitch) + (srcX * bytesPerPixel);
			memcpy(dstRow, srcRow, static_cast<size_t>(copyWidth * bytesPerPixel));
		}
		m_dirty = true;
		m_gpuWritten = false;
	}

	virtual TextureFormat getNativeFormat() const
	{
		return m_nativeFormat;
	}

	virtual void lock(LockData &lockData)
	{
		if (lockData.m_format == TF_Native)
			lockData.m_format = m_nativeFormat;

		TextureFormat const requestedFormat = lockData.getFormat();
		int const width = lockData.getWidth();
		int const height = lockData.getHeight();
		int const depth = lockData.getDepth();
		int const pitch = getTextureRowPitch(requestedFormat, width);
		int const slicePitch = getTextureSlicePitch(requestedFormat, width, height);

		if (requestedFormat != m_nativeFormat)
		{
			Direct3d11TextureScratch *scratch = new Direct3d11TextureScratch;
			scratch->format = requestedFormat;
			scratch->pitch = pitch;
			scratch->slicePitch = slicePitch;
			scratch->data.resize(static_cast<size_t>(slicePitch) * static_cast<size_t>(depth));
			lockData.m_pitch = pitch;
			lockData.m_slicePitch = slicePitch;
			lockData.m_pixelData = scratch->data.empty() ? 0 : &scratch->data[0];
			lockData.m_reserved = scratch;
			return;
		}

		int const face = getFaceIndex(lockData.m_cubeFace);
		int const level = lockData.getLevel();
		int const levelWidth = getTextureLevelDimension(m_texture.getWidth(), level);
		int const levelHeight = getTextureLevelDimension(m_texture.getHeight(), level);
		std::vector<byte> &levelData = m_levels[getLevelIndex(face, level)];

		TextureFormatInfo const &info = TextureFormatInfo::getInfo(m_nativeFormat);
		int const levelPitch = getTextureRowPitch(m_nativeFormat, levelWidth);
		int const levelSlicePitch = getTextureSlicePitch(m_nativeFormat, levelWidth, levelHeight);
		size_t offset = 0;
		if (info.compressed)
		{
			int const blockX = lockData.getX() / info.blockWidth;
			int const blockY = lockData.getY() / info.blockHeight;
			offset = static_cast<size_t>(lockData.getZ() * levelSlicePitch + blockY * levelPitch + blockX * info.blockSize);
		}
		else
		{
			offset = static_cast<size_t>(lockData.getZ() * levelSlicePitch + lockData.getY() * levelPitch + lockData.getX() * info.pixelByteCount);
		}

		lockData.m_pitch = levelPitch;
		lockData.m_slicePitch = levelSlicePitch;
		lockData.m_pixelData = levelData.empty() ? 0 : &levelData[0] + offset;
		lockData.m_reserved = 0;
	}

	bool copyScratchToNative(LockData const &lockData, Direct3d11TextureScratch const &scratch)
	{
		if (scratch.data.empty())
			return false;

		int const face = getFaceIndex(lockData.m_cubeFace);
		int const level = lockData.getLevel();
		if (level < 0 || level >= m_texture.getMipmapLevelCount())
			return false;

		int const levelWidth = getTextureLevelDimension(m_texture.getWidth(), level);
		int const levelHeight = getTextureLevelDimension(m_texture.getHeight(), level);
		int const x = lockData.getX();
		int const y = lockData.getY();
		int const width = lockData.getWidth();
		int const height = lockData.getHeight();
		if (x < 0 || y < 0 || width <= 0 || height <= 0 || x >= levelWidth || y >= levelHeight)
			return false;

		std::vector<byte> &levelData = m_levels[getLevelIndex(face, level)];
		if (levelData.empty())
			return false;

		if (m_nativeFormat == TF_DXT1 && isTextureFormat32BitColor(scratch.format))
		{
			TextureFormatInfo const &info = TextureFormatInfo::getInfo(m_nativeFormat);
			int const dstPitch = getTextureRowPitch(m_nativeFormat, levelWidth);
			int const blockStartX = x / info.blockWidth;
			int const blockStartY = y / info.blockHeight;
			int const blockCountX = (width + info.blockWidth - 1) / info.blockWidth;
			int const blockCountY = (height + info.blockHeight - 1) / info.blockHeight;

			for (int blockY = 0; blockY < blockCountY; ++blockY)
			{
				for (int blockX = 0; blockX < blockCountX; ++blockX)
				{
					byte encodedBlock[8];
					encodeDxt1Block(scratch, blockX * info.blockWidth, blockY * info.blockHeight, width, height, encodedBlock);
					size_t const dstOffset = static_cast<size_t>((blockStartY + blockY) * dstPitch + (blockStartX + blockX) * info.blockSize);
					if (dstOffset + 8 <= levelData.size())
						memcpy(&levelData[dstOffset], encodedBlock, 8);
				}
			}
			m_dirty = true;
			return true;
		}

		if (isTextureFormat32BitColor(m_nativeFormat) && isTextureFormat32BitColor(scratch.format))
		{
			int const dstPitch = getTextureRowPitch(m_nativeFormat, levelWidth);
			int const copyWidth = std::min(width, levelWidth - x);
			int const copyHeight = std::min(height, levelHeight - y);
			for (int row = 0; row < copyHeight; ++row)
			{
				byte const *srcRow = &scratch.data[static_cast<size_t>(row * scratch.pitch)];
				byte *dstRow = &levelData[static_cast<size_t>((y + row) * dstPitch + x * 4)];
				memcpy(dstRow, srcRow, static_cast<size_t>(copyWidth * 4));
			}
			m_dirty = true;
			return true;
		}

		return false;
	}

	virtual void unlock(LockData &lockData)
	{
		if (lockData.m_reserved)
		{
			Direct3d11TextureScratch *scratch = reinterpret_cast<Direct3d11TextureScratch *>(lockData.m_reserved);
			if (!copyScratchToNative(lockData, *scratch))
				Direct3d11Namespace::diag("texture scratch conversion unsupported native=%d scratch=%d level=%d size=%dx%d", static_cast<int>(m_nativeFormat), static_cast<int>(scratch->format), lockData.getLevel(), lockData.getWidth(), lockData.getHeight());
			delete scratch;
			m_gpuWritten = false;
		}
		else
		{
			m_dirty = true;
			m_gpuWritten = false;
		}

		lockData.m_pitch = 0;
		lockData.m_slicePitch = 0;
		lockData.m_pixelData = 0;
		lockData.m_reserved = 0;
	}

	ID3D11ShaderResourceView *getShaderResourceView()
	{
		upload();
		return m_shaderResourceView;
	}

	bool isCubeMap() const
	{
		return m_texture.isCubeMap();
	}

	ID3D11RenderTargetView *getRenderTargetView(CubeFace face, int level)
	{
		if (!m_texture.isRenderTarget() || level < 0 || level >= m_texture.getMipmapLevelCount())
			return 0;

		upload();
		if (!m_d3dTexture || !Direct3d11Namespace::ms_device)
			return 0;

		int const faceIndex = getFaceIndex(face);
		int const mipCount = std::max(1, m_texture.getMipmapLevelCount());
		size_t const viewIndex = getLevelIndex(faceIndex, level);
		if (m_renderTargetViews.size() < static_cast<size_t>(m_faceCount * mipCount))
			m_renderTargetViews.resize(static_cast<size_t>(m_faceCount * mipCount), 0);
		if (m_renderTargetViews[viewIndex])
			return m_renderTargetViews[viewIndex];

		D3D11_RENDER_TARGET_VIEW_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Format = getDxgiTextureFormat(m_nativeFormat);
		if (m_texture.isCubeMap())
		{
			desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.MipSlice = static_cast<UINT>(level);
			desc.Texture2DArray.FirstArraySlice = static_cast<UINT>(faceIndex);
			desc.Texture2DArray.ArraySize = 1;
		}
		else
		{
			desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = static_cast<UINT>(level);
		}

		if (FAILED(Direct3d11Namespace::ms_device->CreateRenderTargetView(m_d3dTexture, &desc, &m_renderTargetViews[viewIndex])))
			return 0;

		return m_renderTargetViews[viewIndex];
	}

	ID3D11Texture2D *getTexture2D()
	{
		upload();
		return m_d3dTexture;
	}

	UINT getSubresourceIndex(CubeFace face, int level) const
	{
		return static_cast<UINT>(getLevelIndex(getFaceIndex(face), level));
	}

	DXGI_FORMAT getDxgiFormat() const
	{
		return getDxgiTextureFormat(m_nativeFormat);
	}

	bool needsBgraSwizzleForSampling() const
	{
		return needsBgraTextureSwizzle(m_nativeFormat) && !m_gpuWritten;
	}

	void markGpuUpdated()
	{
		m_dirty = false;
		m_gpuWritten = true;
	}

private:
	int getFaceIndex(CubeFace face) const
	{
		if (face >= CF_positiveX && face <= CF_negativeZ)
			return static_cast<int>(face);
		return 0;
	}

	size_t getLevelIndex(int face, int level) const
	{
		int const mipCount = std::max(1, m_texture.getMipmapLevelCount());
		return static_cast<size_t>(face * mipCount + level);
	}

	void upload()
	{
		if (!Direct3d11Namespace::ms_device || m_texture.isVolumeMap() || m_levels.empty())
			return;

		int const mipCount = std::max(1, m_texture.getMipmapLevelCount());
		DXGI_FORMAT const format = getDxgiTextureFormat(m_nativeFormat);

		if (!m_d3dTexture)
		{
			std::vector<D3D11_SUBRESOURCE_DATA> initialData;
			initialData.resize(static_cast<size_t>(m_faceCount * mipCount));

			std::vector<std::vector<byte> > convertedLevels;
			if (m_nativeFormat == TF_L_8)
				convertedLevels.resize(static_cast<size_t>(m_faceCount * mipCount));

			for (int face = 0; face < m_faceCount; ++face)
			{
				for (int level = 0; level < mipCount; ++level)
				{
					int const width = getTextureLevelDimension(m_texture.getWidth(), level);
					int const height = getTextureLevelDimension(m_texture.getHeight(), level);
					std::vector<byte> const &levelData = m_levels[getLevelIndex(face, level)];
					D3D11_SUBRESOURCE_DATA &subresource = initialData[getLevelIndex(face, level)];
					ZeroMemory(&subresource, sizeof(subresource));

					if (m_nativeFormat == TF_L_8)
					{
						std::vector<byte> &converted = convertedLevels[getLevelIndex(face, level)];
						converted.resize(static_cast<size_t>(width * height * 4));
						for (int i = 0; i < width * height; ++i)
						{
							byte const val = levelData.empty() ? static_cast<byte>(0) : levelData[static_cast<size_t>(i)];
							converted[static_cast<size_t>(i * 4 + 0)] = val; // R
							converted[static_cast<size_t>(i * 4 + 1)] = val; // G
							converted[static_cast<size_t>(i * 4 + 2)] = val; // B
							converted[static_cast<size_t>(i * 4 + 3)] = 255;
						}
						subresource.pSysMem = &converted[0];
						subresource.SysMemPitch = static_cast<UINT>(width * 4);
						subresource.SysMemSlicePitch = static_cast<UINT>(width * height * 4);
					}
					else
					{
						subresource.pSysMem = levelData.empty() ? 0 : &levelData[0];
						subresource.SysMemPitch = static_cast<UINT>(getTextureRowPitch(m_nativeFormat, width));
						subresource.SysMemSlicePitch = static_cast<UINT>(getTextureSlicePitch(m_nativeFormat, width, height));
					}
				}
			}

			D3D11_TEXTURE2D_DESC desc;
			ZeroMemory(&desc, sizeof(desc));
			desc.Width = static_cast<UINT>(m_texture.getWidth());
			desc.Height = static_cast<UINT>(m_texture.getHeight());
			desc.MipLevels = static_cast<UINT>(mipCount);
			desc.ArraySize = static_cast<UINT>(m_faceCount);
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (m_texture.isRenderTarget() ? D3D11_BIND_RENDER_TARGET : 0);
			desc.MiscFlags = m_texture.isCubeMap() ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

			if (FAILED(Direct3d11Namespace::ms_device->CreateTexture2D(&desc, initialData.empty() ? 0 : &initialData[0], &m_d3dTexture)))
				return;

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			ZeroMemory(&srvDesc, sizeof(srvDesc));
			srvDesc.Format = format;
			if (m_texture.isCubeMap())
			{
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
				srvDesc.TextureCube.MipLevels = static_cast<UINT>(mipCount);
			}
			else
			{
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = static_cast<UINT>(mipCount);
			}

			if (FAILED(Direct3d11Namespace::ms_device->CreateShaderResourceView(m_d3dTexture, &srvDesc, &m_shaderResourceView)))
				return;
		}
		else if (m_dirty && Direct3d11Namespace::ms_context)
		{
			for (int face = 0; face < m_faceCount; ++face)
			{
				for (int level = 0; level < mipCount; ++level)
				{
					int const width = getTextureLevelDimension(m_texture.getWidth(), level);
					int const height = getTextureLevelDimension(m_texture.getHeight(), level);
					std::vector<byte> const &levelData = m_levels[getLevelIndex(face, level)];
					if (!levelData.empty())
					{
						if (m_nativeFormat == TF_L_8)
						{
							std::vector<byte> converted;
							converted.resize(static_cast<size_t>(width * height * 4));
							for (int i = 0; i < width * height; ++i)
							{
								byte const val = levelData[static_cast<size_t>(i)];
								converted[static_cast<size_t>(i * 4 + 0)] = val;
								converted[static_cast<size_t>(i * 4 + 1)] = val;
								converted[static_cast<size_t>(i * 4 + 2)] = val;
								converted[static_cast<size_t>(i * 4 + 3)] = 255;
							}
							Direct3d11Namespace::ms_context->UpdateSubresource(m_d3dTexture, static_cast<UINT>(getLevelIndex(face, level)), 0, &converted[0], static_cast<UINT>(width * 4), static_cast<UINT>(width * height * 4));
						}
						else
						{
							Direct3d11Namespace::ms_context->UpdateSubresource(m_d3dTexture, static_cast<UINT>(getLevelIndex(face, level)), 0, &levelData[0], static_cast<UINT>(getTextureRowPitch(m_nativeFormat, width)), static_cast<UINT>(getTextureSlicePitch(m_nativeFormat, width, height)));
						}
					}
				}
			}
		}

		m_dirty = false;
	}

private:
	Texture const &m_texture;
	TextureFormat m_nativeFormat;
	int m_faceCount;
	std::vector<std::vector<byte> > m_levels;
	ID3D11Texture2D *m_d3dTexture;
	ID3D11ShaderResourceView *m_shaderResourceView;
	std::vector<ID3D11RenderTargetView *> m_renderTargetViews;
	bool m_dirty;
	bool m_gpuWritten;
};

// ======================================================================

namespace Direct3d11Namespace
{
	Gl_api ms_glApi;

	HWND ms_window;
	int ms_width;
	int ms_height;
	bool ms_windowed;
	bool ms_engineOwnsWindow;
	bool ms_borderlessWindow;
	int ms_windowX;
	int ms_windowY;
	bool ms_installed;
	int ms_shaderCapability;

	ID3D11Device *ms_device;
	ID3D11DeviceContext *ms_context;
	IDXGISwapChain *ms_swapChain;
	ID3D11RenderTargetView *ms_backBufferView;
	ID3D11Texture2D *ms_depthTexture;
	ID3D11DepthStencilView *ms_depthView;
	ID3D11Texture2D *ms_modernShadowDepthTexture;
	ID3D11DepthStencilView *ms_modernShadowDepthView;
	ID3D11ShaderResourceView *ms_modernShadowDepthSrv;
	ID3D11SamplerState *ms_modernShadowSampler;
	int ms_modernShadowMapSizeCached;
	int ms_modernShadowCascadeCountCached;
	ID3D11RenderTargetView *ms_currentRenderTargetView;
	ID3D11DepthStencilView *ms_currentDepthView;
	ID3D11Texture2D *ms_vrEyeTexture;
	ID3D11RenderTargetView *ms_vrEyeRenderTargetViews[2];
	ID3D11Texture2D *ms_vrEyeDepthTexture;
	ID3D11DepthStencilView *ms_vrEyeDepthViews[2];
	int ms_vrEyeTargetWidth;
	int ms_vrEyeTargetHeight;
	int ms_activeVrEye;
	ID3D11VertexShader *ms_defaultVertexShader;
	ID3D11PixelShader *ms_defaultPixelShader;
	ID3D11PixelShader *ms_cubePixelShader;
	ID3DBlob *ms_defaultVertexShaderBytecode;
	ID3D11VertexShader *ms_terrainDot3VertexShader;
	ID3D11PixelShader *ms_terrainDot3PixelShader;
	ID3DBlob *ms_terrainDot3VertexShaderBytecode;
	ID3D11VertexShader *ms_detailBumpVertexShader;
	ID3D11PixelShader *ms_detailBumpPixelShader;
	ID3DBlob *ms_detailBumpVertexShaderBytecode;
	ID3D11Buffer *ms_transformConstantBuffer;
	ID3D11SamplerState *ms_defaultSamplerState;
	ID3D11SamplerState *ms_activeSamplerState;
	ID3D11ShaderResourceView *ms_activeTextureView;
	ID3D11ShaderResourceView *ms_activeStageTextureViews[cms_maxStageTextures];
	ID3D11SamplerState *ms_activeStageSamplerStates[cms_maxStageTextures];
	ID3D11RasterizerState *ms_rasterizerState;
	ID3D11DepthStencilState *ms_depthStencilState;
	ID3D11BlendState *ms_blendState;
	ID3D11Texture2D *ms_copyRenderTargetTexture;
	ID3D11RenderTargetView *ms_copyRenderTargetView;
	DXGI_FORMAT ms_copyRenderTargetFormat;
	int ms_copyRenderTargetWidth;
	int ms_copyRenderTargetHeight;
	Direct3d11_TextureData *ms_copyTargetTextureData;
	UINT ms_copyTargetSubresource;
	ID3D11Texture2D *ms_backBufferLockTexture;
	ID3D11Texture2D *ms_backBufferLockTarget;
	bool ms_backBufferLocked;

	class Direct3d11_StaticVertexBufferData;
	class Direct3d11_DynamicVertexBufferData;
	class Direct3d11_StaticIndexBufferData;
	class Direct3d11_DynamicIndexBufferData;

	typedef std::map<uint32, VertexBufferDescriptor> VertexBufferDescriptorMap;
	VertexBufferDescriptorMap *ms_vertexBufferDescriptorMap;
	typedef std::map<StaticVertexBuffer const *, Direct3d11_StaticVertexBufferData *> StaticVertexBufferDataMap;
	typedef std::map<DynamicVertexBuffer const *, Direct3d11_DynamicVertexBufferData *> DynamicVertexBufferDataMap;
	typedef std::map<StaticIndexBuffer const *, Direct3d11_StaticIndexBufferData *> StaticIndexBufferDataMap;
	typedef std::map<DynamicIndexBuffer const *, Direct3d11_DynamicIndexBufferData *> DynamicIndexBufferDataMap;
	StaticVertexBufferDataMap *ms_staticVertexBufferDataMap;
	DynamicVertexBufferDataMap *ms_dynamicVertexBufferDataMap;
	StaticIndexBufferDataMap *ms_staticIndexBufferDataMap;
	DynamicIndexBufferDataMap *ms_dynamicIndexBufferDataMap;
	typedef std::map<D3D11_RASTERIZER_DESC, ID3D11RasterizerState *, D3d11DescLess<D3D11_RASTERIZER_DESC> > RasterizerStateMap;
	typedef std::map<D3D11_DEPTH_STENCIL_DESC, ID3D11DepthStencilState *, D3d11DescLess<D3D11_DEPTH_STENCIL_DESC> > DepthStencilStateMap;
	typedef std::map<D3D11_BLEND_DESC, ID3D11BlendState *, D3d11DescLess<D3D11_BLEND_DESC> > BlendStateMap;
	RasterizerStateMap *ms_rasterizerStateMap;
	DepthStencilStateMap *ms_depthStencilStateMap;
	BlendStateMap *ms_blendStateMap;
	typedef std::map<Tag, Texture const *> GlobalTextureMap;
	GlobalTextureMap *ms_globalTextureMap;
	int ms_nextSortKey;
	int ms_sliceFirstVertex;
	int ms_sliceNumberOfVertices;
	int ms_sliceFirstIndex;
	int ms_sliceNumberOfIndices;
	ID3D11Buffer *ms_activeVertexBuffer;
	byte const *ms_activeVertexData;
	UINT ms_activeVertexStride;
	VertexBufferDescriptor const *ms_activeVertexDescriptor;
	uint32 ms_activeVertexFormatFlags;
	bool ms_activeVertexVector;
	int ms_activeVertexStreamCount;
	ID3D11Buffer *ms_activeVertexStreamBuffers[2];
	byte const *ms_activeVertexStreamData[2];
	UINT ms_activeVertexStreamStrides[2];
	VertexBufferDescriptor const *ms_activeVertexStreamDescriptors[2];
	uint32 ms_activeVertexStreamFormatFlags[2];
	ID3D11Buffer *ms_activeIndexBuffer;
	Index const *ms_activeIndexData;
	Direct3d11_DynamicIndexBufferData *ms_lastDynamicIndexBufferData;
	int ms_dynamicIndexBufferCapacity;
	ID3D11Buffer *ms_quadListIndexBuffer;
	int ms_quadListIndexBufferNumberOfQuads;
	ID3D11Buffer *ms_triangleFanIndexBuffer;
	int ms_triangleFanIndexBufferNumberOfVertices;
	ID3D11Buffer *ms_indexedTriangleFanIndexBuffer;
	int ms_indexedTriangleFanIndexBufferNumberOfIndices;
	int ms_presentCount;
	int ms_clearColorCount;
	int ms_clearDepthCount;
	int ms_beginSceneCount;
	int ms_endSceneCount;
	int ms_shaderSetCount;
	int ms_stageReplayShaderSetCount;
	int ms_stageReplayMaxStageCount;
	int ms_stageReplayLogCount;
	int ms_coordinateGenerationShaderSetCount;
	int ms_coordinateGenerationLogCount;
	int ms_terrainDot3ShaderSetCount;
	int ms_terrainDot3LogCount;
	int ms_terrainDot3DrawLogCount;
	int ms_skyPixelProgramLogCount;
	int ms_staticShaderLogCount;
	int ms_staticDrawLogCount;
	int ms_mobileDrawLogCount;
	int ms_shadowBlobDrawLogCount;
	int ms_uiShaderLogCount;
	int ms_rendererInventoryLogCount;
	int ms_rendererVectorInventoryLogCount;
	int ms_rendererInventoryEnabled;
	int ms_rendererInventoryLimit;
	int ms_rendererInventoryStartFrame;
	char ms_activeStaticShaderName[256];
	char ms_activeVertexProgramName[256];
	char ms_activePixelProgramName[256];
	Tag ms_activeTextureTag;
	int ms_activeTextureNativeFormat;
	int ms_fogSetCount;
	int ms_fogEnabledSetCount;
	int ms_fogLogCount;
	int ms_lightSetCount;
	int ms_lightNonEmptySetCount;
	int ms_lightLogCount;
	int ms_vertexShaderUserConstantSetCount;
	int ms_pixelShaderUserConstantSetCount;
	int ms_gammaSetCount;
	float ms_brightness = 1.0f;
	float ms_contrast = 1.0f;
	float ms_gamma = 1.0f;
	int ms_bloomSetCount;
	int ms_vertexBufferSetCount;
	int ms_indexBufferSetCount;
	int ms_drawCallCount;
	int ms_drawIndexedCallCount;
	int ms_drawVertexCount;
	int ms_drawIndexCount;
	int ms_drawSkippedNoContext;
	int ms_drawSkippedNoVertexBuffer;
	int ms_drawSkippedNoIndexBuffer;
	int ms_drawSkippedNoPipeline;
	bool ms_autoCaptureDone;
	bool ms_autoCaptureRequested;
	int ms_autoCaptureWorldRows;
	int ms_autoCaptureWorldRowsThreshold;
	int ms_autoCaptureCount;
	int ms_autoCaptureLastPresent;
	int ms_scissorEnableCount;
	int ms_scissorDisableCount;
	int ms_scissorLogCount;
	int ms_transformedDrawCount;
	int ms_transformedDrawLogCount;
	int ms_viewportX;
	int ms_viewportY;
	int ms_viewportWidth;
	int ms_viewportHeight;

	struct Matrix4x4
	{
		float m[4][4];
	};

	struct TransformConstants
	{
		Matrix4x4 objectToProjection;
		Matrix4x4 objectToCamera;
		Matrix4x4 objectToWorld;
		Matrix4x4 objectToWorldRotation;
		float textureEnabled[4];
		float alphaTest[4];
		float materialAmbientColor[4];
		float materialColor[4];
		float materialEmissiveColor[4];
		float materialSpecularColor[4];
		float textureFactor[4];
		float textureFactor2[4];
		Matrix4x4 textureTransform;
		float textureTransformControl[4];
		float fogColor[4];
		float fogControl[4];
		float textureScroll[4];
		float lightAmbient[4];
		float lightDiffuse[4];
		float lightDirection[4];
		float parallelLightDiffuse[3][4];
		float parallelLightSpecular[3][4];
		float parallelLightDirection[3][4];
		float lightControl[4];
		float lightControl2[4];
		float materialSourceControl[4];
		float vertexControl[4];
		float viewportControl[4];
		float stageControl[4];
		float stageColorOp[cms_maxStageTextures][4];
		float stageAlphaOp[cms_maxStageTextures][4];
		float stageMeta[cms_maxStageTextures][4];
		float stageScroll[cms_maxStageTextures][4];
		float dot3LightSpecularColor[4];
		float dot3LightTangentMinusDiffuseColor[4];
		float dot3LightTangentMinusBackColor[4];
		float cameraAndMaterialControl[4];
		float dot3LightLocalDirection[4];
		float dot3CameraLocalPosition[4];
		float userVertexConstants[8][4];
		float userPixelConstants[16][4];
		float stageTextureCoordinateSet[cms_maxStageTextures][4];
	};

	Matrix4x4 ms_objectToWorldMatrix;
	Matrix4x4 ms_objectToWorldRotationMatrix;
	Transform ms_objectToWorldTransform;
	Matrix4x4 ms_worldToCameraMatrix;
	Matrix4x4 ms_projectionMatrix;
	bool ms_transformDirty;
	GlFillMode ms_fillMode;
	GlCullMode ms_cullMode;
	bool ms_alphaBlendEnabled;
	bool ms_depthEnabled;
	bool ms_depthWriteEnabled;
	D3D11_COMPARISON_FUNC ms_depthFunc;
	D3D11_BLEND_OP ms_blendOp;
	D3D11_BLEND ms_sourceBlend;
	D3D11_BLEND ms_destinationBlend;
	UINT8 ms_colorWriteMask;
	bool ms_lightingEnabled;
	bool ms_lightingColorVertex;
	bool ms_lightingSpecularEnabled;
	int ms_lightingAmbientColorSource;
	int ms_lightingDiffuseColorSource;
	int ms_lightingSpecularColorSource;
	int ms_lightingEmissiveColorSource;
	bool ms_stencilEnabled;
	bool ms_stencilTwoSidedMode;
	D3D11_COMPARISON_FUNC ms_stencilFunc;
	D3D11_STENCIL_OP ms_stencilFailOp;
	D3D11_STENCIL_OP ms_stencilDepthFailOp;
	D3D11_STENCIL_OP ms_stencilPassOp;
	D3D11_COMPARISON_FUNC ms_ccwStencilFunc;
	D3D11_STENCIL_OP ms_ccwStencilFailOp;
	D3D11_STENCIL_OP ms_ccwStencilDepthFailOp;
	D3D11_STENCIL_OP ms_ccwStencilPassOp;
	UINT8 ms_stencilReadMask;
	UINT8 ms_stencilWriteMask;
	uint32 ms_stencilReferenceValue;
	bool ms_scissorEnabled;
	D3D11_RECT ms_scissorRect;
	bool ms_alphaFadeOpacityEnabled;
	float ms_alphaFadeOpacity;
	bool ms_activeFullAmbient;
	int ms_activeTextureCoordinateSet;
	bool ms_activeTextureAlphaOnly;
	bool ms_activeTextureBgraSwizzle;
	bool ms_activeTextureCube;
	int ms_activeStageTextureCount;
	int ms_activeStageNativeFormat[cms_maxStageTextures];
	Tag ms_activeStageTextureTag[cms_maxStageTextures];
	bool ms_activeTerrainDot3;
	int ms_activeTerrainBlendCount;
	int ms_activeTerrainProgramMode;
	int ms_activeVertexProgramMode;
	int ms_activePixelProgramMode;
	int ms_activePassFogMode;
	float ms_vertexUserConstants[8][4];
	float ms_pixelUserConstants[16][4];
	float ms_activeStageColorOp[cms_maxStageTextures][4];
	float ms_activeStageAlphaOp[cms_maxStageTextures][4];
	float ms_activeStageMeta[cms_maxStageTextures][4];
	float ms_activeStageScroll[cms_maxStageTextures][4];
	float ms_activeStageTextureCoordinateSet[cms_maxStageTextures][4];
	bool ms_alphaTestEnabled;
	float ms_alphaTestReference;
	int ms_alphaTestCompare;
	float ms_materialAmbientColor[4];
	float ms_materialColor[4];
	float ms_materialEmissiveColor[4];
	float ms_materialSpecularColor[4];
	float ms_materialSpecularPower;
	float ms_textureFactor[4];
	float ms_textureFactor2[4];
	Matrix4x4 ms_textureTransform[8];
	bool ms_textureTransformEnabled[8];
	int ms_textureTransformDimension[8];
	bool ms_textureTransformProjected[8];
	int ms_activeTextureStage;
	bool ms_fogEnabled;
	float ms_fogDensity;
	float ms_fogColor[4];
	float ms_activeFogColor[4];
	uint32 ms_fogColorPacked;
	uint32 ms_activeFogColorPacked;
	float ms_cameraPosition[4];
	bool ms_textureScrollValid;
	float ms_textureScroll[4];
	float ms_currentTime;
	float ms_lightAmbient[4];
	float ms_lightDiffuse[4];
	float ms_lightDirection[4];
	float ms_parallelLightDiffuse[3][4];
	float ms_parallelLightSpecular[3][4];
	float ms_parallelLightDirection[3][4];
	float ms_dot3LightSpecularColor[4];
	float ms_dot3LightTangentMinusDiffuseColor[4];
	float ms_dot3LightTangentMinusBackColor[4];
	bool ms_lightDirectionalEnabled;
	int ms_selectedLightMask;
	bool ms_obeysLightScale;
	stdvector<const Light*>::fwd ms_currentLightList;

	char const *getCoordinateGenerationName(ShaderImplementation::Pass::Stage::CoordinateGeneration coordinateGeneration)
	{
		switch (coordinateGeneration)
		{
			case ShaderImplementation::Pass::Stage::CG_passThru:
				return "passThru";
			case ShaderImplementation::Pass::Stage::CG_cameraSpacePosition:
				return "cameraSpacePosition";
			case ShaderImplementation::Pass::Stage::CG_cameraSpaceNormal:
				return "cameraSpaceNormal";
			case ShaderImplementation::Pass::Stage::CG_cameraSpaceReflectionVector:
				return "cameraSpaceReflectionVector";
			case ShaderImplementation::Pass::Stage::CG_scroll1:
				return "scroll1";
			case ShaderImplementation::Pass::Stage::CG_scroll2:
				return "scroll2";
			default:
				return "unknown";
		}
	}

	unsigned getD3d9TextureCoordinateIndex(uint8 textureCoordinateSet, ShaderImplementation::Pass::Stage::CoordinateGeneration coordinateGeneration)
	{
		unsigned coordinateGenerationBits = 0x00000000;
		switch (coordinateGeneration)
		{
			case ShaderImplementation::Pass::Stage::CG_cameraSpacePosition:
				coordinateGenerationBits = 0x00010000;
				break;
			case ShaderImplementation::Pass::Stage::CG_cameraSpaceNormal:
				coordinateGenerationBits = 0x00020000;
				break;
			case ShaderImplementation::Pass::Stage::CG_cameraSpaceReflectionVector:
				coordinateGenerationBits = 0x00030000;
				break;
			default:
				coordinateGenerationBits = 0x00000000;
				break;
		}

		return coordinateGenerationBits | static_cast<unsigned>(textureCoordinateSet);
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

	void releaseModernShadowResources()
	{
		releaseCom(ms_modernShadowSampler);
		releaseCom(ms_modernShadowDepthSrv);
		releaseCom(ms_modernShadowDepthView);
		releaseCom(ms_modernShadowDepthTexture);
		ms_modernShadowMapSizeCached = 0;
		ms_modernShadowCascadeCountCached = 0;
	}

	bool ensureModernShadowResources()
	{
		if (!ms_device)
			return false;

		if (!ConfigDirect3d11::getModernShadows())
		{
			if (ms_modernShadowDepthTexture)
				diag("modernShadows disabled; releasing D3D11 shadow-map resources");
			releaseModernShadowResources();
			return false;
		}

		int const requestedSize = ConfigDirect3d11::getModernShadowMapSize();
		int const mapSize = std::max(512, std::min(8192, requestedSize));
		int const cascadeCount = std::max(1, std::min(4, ConfigDirect3d11::getModernShadowCascadeCount()));
		if (ms_modernShadowDepthTexture &&
			ms_modernShadowMapSizeCached == mapSize &&
			ms_modernShadowCascadeCountCached == cascadeCount)
		{
			return true;
		}

		releaseModernShadowResources();

		D3D11_TEXTURE2D_DESC textureDesc;
		ZeroMemory(&textureDesc, sizeof(textureDesc));
		textureDesc.Width = static_cast<UINT>(mapSize);
		textureDesc.Height = static_cast<UINT>(mapSize);
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = static_cast<UINT>(cascadeCount);
		textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

		HRESULT hr = ms_device->CreateTexture2D(&textureDesc, 0, &ms_modernShadowDepthTexture);
		if (FAILED(hr))
		{
			diag("modernShadows resource create failed stage=texture hr=0x%08x size=%d cascades=%d", static_cast<unsigned>(hr), mapSize, cascadeCount);
			releaseModernShadowResources();
			return false;
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		ZeroMemory(&dsvDesc, sizeof(dsvDesc));
		dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		if (cascadeCount > 1)
		{
			dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.Texture2DArray.FirstArraySlice = 0;
			dsvDesc.Texture2DArray.ArraySize = static_cast<UINT>(cascadeCount);
		}
		else
		{
			dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			dsvDesc.Texture2D.MipSlice = 0;
		}

		hr = ms_device->CreateDepthStencilView(ms_modernShadowDepthTexture, &dsvDesc, &ms_modernShadowDepthView);
		if (FAILED(hr))
		{
			diag("modernShadows resource create failed stage=dsv hr=0x%08x size=%d cascades=%d", static_cast<unsigned>(hr), mapSize, cascadeCount);
			releaseModernShadowResources();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(srvDesc));
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		if (cascadeCount > 1)
		{
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MostDetailedMip = 0;
			srvDesc.Texture2DArray.MipLevels = 1;
			srvDesc.Texture2DArray.FirstArraySlice = 0;
			srvDesc.Texture2DArray.ArraySize = static_cast<UINT>(cascadeCount);
		}
		else
		{
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
		}

		hr = ms_device->CreateShaderResourceView(ms_modernShadowDepthTexture, &srvDesc, &ms_modernShadowDepthSrv);
		if (FAILED(hr))
		{
			diag("modernShadows resource create failed stage=srv hr=0x%08x size=%d cascades=%d", static_cast<unsigned>(hr), mapSize, cascadeCount);
			releaseModernShadowResources();
			return false;
		}

		D3D11_SAMPLER_DESC samplerDesc;
		ZeroMemory(&samplerDesc, sizeof(samplerDesc));
		samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.BorderColor[0] = 1.0f;
		samplerDesc.BorderColor[1] = 1.0f;
		samplerDesc.BorderColor[2] = 1.0f;
		samplerDesc.BorderColor[3] = 1.0f;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		hr = ms_device->CreateSamplerState(&samplerDesc, &ms_modernShadowSampler);
		if (FAILED(hr))
		{
			diag("modernShadows resource create failed stage=sampler hr=0x%08x size=%d cascades=%d", static_cast<unsigned>(hr), mapSize, cascadeCount);
			releaseModernShadowResources();
			return false;
		}

		ms_modernShadowMapSizeCached = mapSize;
		ms_modernShadowCascadeCountCached = cascadeCount;
		diag("modernShadows resources ready size=%d requestedSize=%d cascades=%d quality=%d filter=%d pcfTaps=%d radius=%0.3f distance=%0.3f contact=%d contactDistance=%0.3f stabilize=%d vrSinglePass=%d",
			mapSize,
			requestedSize,
			cascadeCount,
			ConfigDirect3d11::getModernShadowQuality(),
			ConfigDirect3d11::getModernShadowFilter(),
			ConfigDirect3d11::getModernShadowPcfTaps(),
			ConfigDirect3d11::getModernShadowFilterRadius(),
			ConfigDirect3d11::getModernShadowDistance(),
			ConfigDirect3d11::getModernShadowContactShadows() ? 1 : 0,
			ConfigDirect3d11::getModernShadowContactDistance(),
			ConfigDirect3d11::getModernShadowStabilize() ? 1 : 0,
			ConfigDirect3d11::getModernShadowVrSinglePass() ? 1 : 0);
		return true;
	}

	void releaseBackBufferLock(bool copyBack)
	{
		if (ms_backBufferLockTexture && ms_context && ms_backBufferLocked)
		{
			ms_context->Unmap(ms_backBufferLockTexture, 0);
			if (copyBack && ms_backBufferLockTarget)
				ms_context->CopyResource(ms_backBufferLockTarget, ms_backBufferLockTexture);
		}

		ms_backBufferLocked = false;
		releaseCom(ms_backBufferLockTexture);
		releaseCom(ms_backBufferLockTarget);
	}

	void writeLittle16(FILE *file, unsigned value)
	{
		unsigned char bytes[2];
		bytes[0] = static_cast<unsigned char>(value & 0xff);
		bytes[1] = static_cast<unsigned char>((value >> 8) & 0xff);
		fwrite(bytes, 1, sizeof(bytes), file);
	}

	void writeLittle32(FILE *file, unsigned value)
	{
		unsigned char bytes[4];
		bytes[0] = static_cast<unsigned char>(value & 0xff);
		bytes[1] = static_cast<unsigned char>((value >> 8) & 0xff);
		bytes[2] = static_cast<unsigned char>((value >> 16) & 0xff);
		bytes[3] = static_cast<unsigned char>((value >> 24) & 0xff);
		fwrite(bytes, 1, sizeof(bytes), file);
	}

	void writePixelBgra(FILE *file, uint32 pixel, bool alphaExtend)
	{
		unsigned char const alpha = static_cast<unsigned char>((pixel >> 24) & 0xff);
		unsigned char bytes[4];
		if (alphaExtend)
		{
			bytes[0] = alpha;
			bytes[1] = alpha;
			bytes[2] = alpha;
			bytes[3] = 0;
		}
		else
		{
			bytes[0] = static_cast<unsigned char>((pixel >> 0) & 0xff);
			bytes[1] = static_cast<unsigned char>((pixel >> 8) & 0xff);
			bytes[2] = static_cast<unsigned char>((pixel >> 16) & 0xff);
			bytes[3] = alpha;
		}
		fwrite(bytes, 1, sizeof(bytes), file);
	}

	uint32 readDxgiPixelArgb(byte const *source, DXGI_FORMAT format)
	{
		unsigned r = 0;
		unsigned g = 0;
		unsigned b = 0;
		unsigned a = 0xff;

		if (format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
		{
			b = source[0];
			g = source[1];
			r = source[2];
			a = source[3];
		}
		else if (format == DXGI_FORMAT_B8G8R8X8_UNORM)
		{
			b = source[0];
			g = source[1];
			r = source[2];
			a = 0xff;
		}
		else if (format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
		{
			r = source[0];
			g = source[1];
			b = source[2];
			a = source[3];
		}
		else if (format == DXGI_FORMAT_B5G6R5_UNORM)
		{
			uint16 const pixel = *reinterpret_cast<uint16 const *>(source);
			r = ((pixel >> 11) & 0x1f) * 255 / 31;
			g = ((pixel >> 5) & 0x3f) * 255 / 63;
			b = (pixel & 0x1f) * 255 / 31;
		}

		return static_cast<uint32>((a << 24) | (r << 16) | (g << 8) | b);
	}

	bool getImageRect(int width, int height, Rectangle2d const *subRect, int &left, int &top, int &rectWidth, int &rectHeight)
	{
		left = subRect ? static_cast<int>(std::min(subRect->x0, subRect->x1)) : 0;
		top = subRect ? static_cast<int>(std::min(subRect->y0, subRect->y1)) : 0;
		int const right = subRect ? static_cast<int>(std::max(subRect->x0, subRect->x1)) : width;
		int const bottom = subRect ? static_cast<int>(std::max(subRect->y0, subRect->y1)) : height;

		left = std::max(0, std::min(left, width));
		top = std::max(0, std::min(top, height));
		rectWidth = std::max(0, std::min(right, width) - left);
		rectHeight = std::max(0, std::min(bottom, height) - top);
		return rectWidth > 0 && rectHeight > 0;
	}

	bool writeBmpFileArgb(char const *fileName, int width, int height, int pitch, int const *pixelsARGB, bool alphaExtend, Rectangle2d const *subRect)
	{
		int left = 0;
		int top = 0;
		int rectWidth = 0;
		int rectHeight = 0;
		if (!getImageRect(width, height, subRect, left, top, rectWidth, rectHeight))
			return false;

		FILE *file = fopen(fileName, "wb");
		if (!file)
			return false;

		unsigned const rowBytes = static_cast<unsigned>(rectWidth * 4);
		unsigned const pixelBytes = rowBytes * static_cast<unsigned>(rectHeight);
		unsigned const fileHeaderBytes = 14;
		unsigned const infoHeaderBytes = 40;
		unsigned const pixelOffset = fileHeaderBytes + infoHeaderBytes;

		fwrite("BM", 1, 2, file);
		writeLittle32(file, pixelOffset + pixelBytes);
		writeLittle16(file, 0);
		writeLittle16(file, 0);
		writeLittle32(file, pixelOffset);
		writeLittle32(file, infoHeaderBytes);
		writeLittle32(file, static_cast<unsigned>(rectWidth));
		writeLittle32(file, static_cast<unsigned>(rectHeight));
		writeLittle16(file, 1);
		writeLittle16(file, 32);
		writeLittle32(file, 0);
		writeLittle32(file, pixelBytes);
		writeLittle32(file, 0);
		writeLittle32(file, 0);
		writeLittle32(file, 0);
		writeLittle32(file, 0);

		int const sourcePitch = pitch / static_cast<int>(sizeof(int));
		for (int y = rectHeight - 1; y >= 0; --y)
		{
			int const *source = pixelsARGB + (top + y) * sourcePitch + left;
			for (int x = 0; x < rectWidth; ++x)
				writePixelBgra(file, static_cast<uint32>(source[x]), alphaExtend);
		}

		fclose(file);
		return true;
	}

	bool writeTgaFileArgb(char const *fileName, int width, int height, int pitch, int const *pixelsARGB, bool alphaExtend, Rectangle2d const *subRect)
	{
		int left = 0;
		int top = 0;
		int rectWidth = 0;
		int rectHeight = 0;
		if (!getImageRect(width, height, subRect, left, top, rectWidth, rectHeight))
			return false;

		FILE *file = fopen(fileName, "wb");
		if (!file)
			return false;

		unsigned char header[18];
		memset(header, 0, sizeof(header));
		header[2] = 2;
		header[12] = static_cast<unsigned char>(rectWidth & 0xff);
		header[13] = static_cast<unsigned char>((rectWidth >> 8) & 0xff);
		header[14] = static_cast<unsigned char>(rectHeight & 0xff);
		header[15] = static_cast<unsigned char>((rectHeight >> 8) & 0xff);
		header[16] = 32;
		header[17] = 0x28;
		fwrite(header, 1, sizeof(header), file);

		int const sourcePitch = pitch / static_cast<int>(sizeof(int));
		for (int y = 0; y < rectHeight; ++y)
		{
			int const *source = pixelsARGB + (top + y) * sourcePitch + left;
			for (int x = 0; x < rectWidth; ++x)
				writePixelBgra(file, static_cast<uint32>(source[x]), alphaExtend);
		}

		fclose(file);
		return true;
	}

	bool writeImageFileArgb(char const *fileName, int width, int height, int pitch, int const *pixelsARGB, bool alphaExtend, Gl_imageFormat imageFormat, Rectangle2d const *subRect)
	{
		if (!fileName || width <= 0 || height <= 0 || pitch < width * static_cast<int>(sizeof(int)) || !pixelsARGB)
			return false;

		switch (imageFormat)
		{
			case GLIF_tga:
				return writeTgaFileArgb(fileName, width, height, pitch, pixelsARGB, alphaExtend, subRect);

			case GLIF_bmp:
			case GLIF_jpg:
			default:
				return writeBmpFileArgb(fileName, width, height, pitch, pixelsARGB, alphaExtend, subRect);
		}
	}

	template <typename T>
	void eraseMappedValue(T *mapPointer)
	{
		delete mapPointer;
	}

	ID3D11Buffer *createBuffer(D3D11_BIND_FLAG bindFlag, void const *sourceData, size_t byteCount)
	{
		if (!ms_device || !sourceData || byteCount == 0)
			return 0;

		D3D11_BUFFER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.ByteWidth = static_cast<UINT>(byteCount);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = bindFlag;

		D3D11_SUBRESOURCE_DATA data;
		ZeroMemory(&data, sizeof(data));
		data.pSysMem = sourceData;

		ID3D11Buffer *buffer = 0;
		if (FAILED(ms_device->CreateBuffer(&desc, &data, &buffer)))
			return 0;

		return buffer;
	}

	ID3D11Buffer *createEmptyIndexBuffer(int numberOfIndices)
	{
		if (!ms_device || numberOfIndices <= 0)
			return 0;

		D3D11_BUFFER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.ByteWidth = static_cast<UINT>(numberOfIndices * sizeof(Index));
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		ID3D11Buffer *buffer = 0;
		if (FAILED(ms_device->CreateBuffer(&desc, 0, &buffer)))
			return 0;

		return buffer;
	}

	bool ensureCopyRenderTarget(DXGI_FORMAT format, int width, int height)
	{
		if (!ms_device || width <= 0 || height <= 0)
			return false;

		if (ms_copyRenderTargetTexture && ms_copyRenderTargetView && ms_copyRenderTargetFormat == format && ms_copyRenderTargetWidth == width && ms_copyRenderTargetHeight == height)
			return true;

		releaseCom(ms_copyRenderTargetView);
		releaseCom(ms_copyRenderTargetTexture);
		ms_copyRenderTargetFormat = DXGI_FORMAT_UNKNOWN;
		ms_copyRenderTargetWidth = 0;
		ms_copyRenderTargetHeight = 0;

		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;

		if (FAILED(ms_device->CreateTexture2D(&desc, 0, &ms_copyRenderTargetTexture)))
			return false;

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
		ZeroMemory(&rtvDesc, sizeof(rtvDesc));
		rtvDesc.Format = format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;

		if (FAILED(ms_device->CreateRenderTargetView(ms_copyRenderTargetTexture, &rtvDesc, &ms_copyRenderTargetView)))
		{
			releaseCom(ms_copyRenderTargetTexture);
			return false;
		}

		ms_copyRenderTargetFormat = format;
		ms_copyRenderTargetWidth = width;
		ms_copyRenderTargetHeight = height;
		return true;
	}

	bool isGlobalTextureTag(Tag tag)
	{
		return tag == TAG(E,N,V,M) || (((tag >> 24) & 0xff) == '_');
	}

	void setIdentity(Matrix4x4 &matrix)
	{
		ZeroMemory(&matrix, sizeof(matrix));
		matrix.m[0][0] = 1.0f;
		matrix.m[1][1] = 1.0f;
		matrix.m[2][2] = 1.0f;
		matrix.m[3][3] = 1.0f;
	}

	void setWhiteColor(float (&color)[4])
	{
		color[0] = 1.0f;
		color[1] = 1.0f;
		color[2] = 1.0f;
		color[3] = 1.0f;
	}

	void setBlackColor(float (&color)[4])
	{
		color[0] = 0.0f;
		color[1] = 0.0f;
		color[2] = 0.0f;
		color[3] = 0.0f;
	}

	void copyColor(float (&destination)[4], float const (&source)[4])
	{
		destination[0] = source[0];
		destination[1] = source[1];
		destination[2] = source[2];
		destination[3] = source[3];
	}

	void resetStageTextures()
	{
		ms_activeStageTextureCount = 0;
		for (int i = 0; i < cms_maxStageTextures; ++i)
		{
			ms_activeStageTextureViews[i] = 0;
			releaseCom(ms_activeStageSamplerStates[i]);
			for (int j = 0; j < 4; ++j)
			{
				ms_activeStageColorOp[i][j] = 0.0f;
				ms_activeStageAlphaOp[i][j] = 0.0f;
				ms_activeStageMeta[i][j] = 0.0f;
				ms_activeStageScroll[i][j] = 0.0f;
				ms_activeStageTextureCoordinateSet[i][j] = 0.0f;
			}
			ms_activeStageNativeFormat[i] = -1;
			ms_activeStageTextureTag[i] = 0;
		}
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

	void setColorFromPackedArgb(float (&destination)[4], PackedArgb const &source)
	{
		destination[0] = static_cast<float>(source.getR()) / 255.0f;
		destination[1] = static_cast<float>(source.getG()) / 255.0f;
		destination[2] = static_cast<float>(source.getB()) / 255.0f;
		destination[3] = static_cast<float>(source.getA()) / 255.0f;
	}

	void setColorFromVectorArgb(float (&destination)[4], VectorArgb const &source)
	{
		destination[0] = source.r;
		destination[1] = source.g;
		destination[2] = source.b;
		destination[3] = source.a;
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

	VectorArgb const &getPossiblyScaledDiffuseBackColor(Light const &light)
	{
		return ms_obeysLightScale ? light.getScaledDiffuseBackColor() : light.getDiffuseBackColor();
	}

	VectorArgb const &getPossiblyScaledDiffuseTangentColor(Light const &light)
	{
		return ms_obeysLightScale ? light.getScaledDiffuseTangentColor() : light.getDiffuseTangentColor();
	}

	void resetLighting()
	{
		ms_lightAmbient[0] = 1.0f;
		ms_lightAmbient[1] = 1.0f;
		ms_lightAmbient[2] = 1.0f;
		ms_lightAmbient[3] = 1.0f;
		ms_lightDiffuse[0] = 0.0f;
		ms_lightDiffuse[1] = 0.0f;
		ms_lightDiffuse[2] = 0.0f;
		ms_lightDiffuse[3] = 1.0f;
		ms_lightDirection[0] = 0.0f;
		ms_lightDirection[1] = 0.0f;
		ms_lightDirection[2] = -1.0f;
		ms_lightDirection[3] = 0.0f;
		for (int i = 0; i < 3; ++i)
		{
			ms_parallelLightDiffuse[i][0] = 0.0f;
			ms_parallelLightDiffuse[i][1] = 0.0f;
			ms_parallelLightDiffuse[i][2] = 0.0f;
			ms_parallelLightDiffuse[i][3] = 1.0f;
			ms_parallelLightSpecular[i][0] = 0.0f;
			ms_parallelLightSpecular[i][1] = 0.0f;
			ms_parallelLightSpecular[i][2] = 0.0f;
			ms_parallelLightSpecular[i][3] = 1.0f;
			ms_parallelLightDirection[i][0] = 0.0f;
			ms_parallelLightDirection[i][1] = 0.0f;
			ms_parallelLightDirection[i][2] = -1.0f;
			ms_parallelLightDirection[i][3] = 0.0f;
		}
		setBlackColor(ms_dot3LightSpecularColor);
		setBlackColor(ms_dot3LightTangentMinusDiffuseColor);
		setBlackColor(ms_dot3LightTangentMinusBackColor);
		ms_lightDirectionalEnabled = false;
		ms_selectedLightMask = 0;
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
				copyColor(ms_activeFogColor, ms_fogColor);
				ms_activeFogColorPacked = ms_fogColorPacked;
				break;
		}
	}

	Matrix4x4 multiply(Matrix4x4 const &lhs, Matrix4x4 const &rhs)
	{
		Matrix4x4 result;
		ZeroMemory(&result, sizeof(result));
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

	bool isShadowBlobShaderName(char const *shaderName)
	{
		return shaderName &&
			(strstr(shaderName, "shadowblob.sht") ||
			 strstr(shaderName, "shadowblob"));
	}

	void updateTransformConstants()
	{
		if (!ms_context || !ms_transformConstantBuffer)
			return;

		Matrix4x4 const objectToCamera = multiply(ms_objectToWorldMatrix, ms_worldToCameraMatrix);
		Matrix4x4 const objectToProjection = multiply(objectToCamera, ms_projectionMatrix);
		TransformConstants constants;
		constants.objectToProjection = objectToProjection;
		constants.objectToCamera = objectToCamera;
		constants.objectToWorld = ms_objectToWorldMatrix;
		constants.objectToWorldRotation = ms_objectToWorldRotationMatrix;
		constants.textureEnabled[0] = ms_activeTextureView ? 1.0f : 0.0f;
		constants.textureEnabled[1] = ms_alphaFadeOpacityEnabled ? 1.0f : 0.0f;
		constants.textureEnabled[2] = ms_alphaFadeOpacity;
		constants.textureEnabled[3] = static_cast<float>(ms_activeTextureCoordinateSet);
		constants.alphaTest[0] = ms_alphaTestEnabled ? 1.0f : 0.0f;
		constants.alphaTest[1] = ms_alphaTestReference * (ms_alphaFadeOpacityEnabled ? ms_alphaFadeOpacity : 1.0f);
		constants.alphaTest[2] = static_cast<float>(ms_alphaTestCompare);
		constants.alphaTest[3] = static_cast<float>(ms_activeVertexProgramMode);
		copyColor(constants.materialAmbientColor, ms_materialAmbientColor);
		copyColor(constants.materialColor, ms_materialColor);
		copyColor(constants.materialEmissiveColor, ms_materialEmissiveColor);
		copyColor(constants.materialSpecularColor, ms_materialSpecularColor);
		copyColor(constants.textureFactor, ms_textureFactor);
		copyColor(constants.textureFactor2, ms_textureFactor2);
		if (ms_activeTextureStage >= 0 && ms_activeTextureStage < 8 && ms_textureTransformEnabled[ms_activeTextureStage])
		{
			constants.textureTransform = ms_textureTransform[ms_activeTextureStage];
			constants.textureTransformControl[0] = 1.0f;
			constants.textureTransformControl[1] = static_cast<float>(ms_textureTransformDimension[ms_activeTextureStage]);
			constants.textureTransformControl[2] = ms_textureTransformProjected[ms_activeTextureStage] ? 1.0f : 0.0f;
			constants.textureTransformControl[3] = 0.0f;
		}
		else
		{
			setIdentity(constants.textureTransform);
			constants.textureTransformControl[0] = 0.0f;
			constants.textureTransformControl[1] = 2.0f;
			constants.textureTransformControl[2] = 0.0f;
			constants.textureTransformControl[3] = 0.0f;
		}
		copyColor(constants.fogColor, ms_activeFogColor);
		constants.fogControl[0] = ms_fogEnabled ? 1.0f : 0.0f;
		constants.fogControl[1] = ms_fogDensity;
		constants.fogControl[2] = ms_fogDensity * ms_fogDensity * 1.4426950408889634f;
		constants.fogControl[3] = 0.0f;
		constants.textureScroll[0] = 0.0f;
		constants.textureScroll[1] = 0.0f;
		constants.textureScroll[2] = ms_vertexUserConstants[0][0];
		constants.textureScroll[3] = 0.0f;
		if (ms_textureScrollValid)
		{
			double junk = 0.0;
			int const scrollIndex = ms_activeTextureStage == 1 ? 2 : 0;
			constants.textureScroll[0] = static_cast<float>(modf(ms_textureScroll[scrollIndex] * ms_currentTime, &junk));
			constants.textureScroll[1] = static_cast<float>(modf(ms_textureScroll[scrollIndex + 1] * ms_currentTime, &junk));
		}
		copyColor(constants.lightAmbient, ms_lightAmbient);
		if (ms_activeFullAmbient)
		{
			constants.lightAmbient[0] = std::min(constants.lightAmbient[0] + 1.0f, 1.0f);
			constants.lightAmbient[1] = std::min(constants.lightAmbient[1] + 1.0f, 1.0f);
			constants.lightAmbient[2] = std::min(constants.lightAmbient[2] + 1.0f, 1.0f);
		}
		copyColor(constants.lightDiffuse, ms_lightDiffuse);
		copyColor(constants.lightDirection, ms_lightDirection);
		for (int i = 0; i < 3; ++i)
		{
			copyColor(constants.parallelLightDiffuse[i], ms_parallelLightDiffuse[i]);
			copyColor(constants.parallelLightSpecular[i], ms_parallelLightSpecular[i]);
			copyColor(constants.parallelLightDirection[i], ms_parallelLightDirection[i]);
		}
		constants.lightControl[0] = ms_lightDirectionalEnabled ? 1.0f : 0.0f;
		constants.lightControl2[0] = ms_lightingSpecularEnabled ? 1.0f : 0.0f;
		constants.lightControl2[1] = static_cast<float>(ms_lightingSpecularColorSource);
		constants.lightControl2[2] = 0.0f;
		constants.lightControl2[3] = 0.0f;
		for (int i = 0; i < 8; ++i)
			copyColor(constants.userVertexConstants[i], ms_vertexUserConstants[i]);
		for (int i = 0; i < 16; ++i)
			copyColor(constants.userPixelConstants[i], ms_pixelUserConstants[i]);

		bool hasColor0 = false;
		bool hasColor1 = false;
		bool hasNormal = false;
		bool transformed = false;
		if (ms_activeVertexVector)
		{
			for (int i = 0; i < ms_activeVertexStreamCount; ++i)
			{
				VertexBufferFormat streamFormat;
				streamFormat.setFlags(ms_activeVertexStreamFormatFlags[i]);
				if (streamFormat.hasColor0())
					hasColor0 = true;
				if (streamFormat.hasColor1())
					hasColor1 = true;
				if (streamFormat.hasNormal())
					hasNormal = true;
				if (streamFormat.isTransformed())
					transformed = true;
			}
		}
		else
		{
			VertexBufferFormat activeFormat;
			activeFormat.setFlags(ms_activeVertexFormatFlags);
			if (activeFormat.hasColor0())
				hasColor0 = true;
			if (activeFormat.hasColor1())
				hasColor1 = true;
			if (activeFormat.hasNormal())
				hasNormal = true;
			if (activeFormat.isTransformed())
				transformed = true;
		}
		const bool actorLikeGeometry = ms_activeVertexVector || isActorShaderName(ms_activeStaticShaderName);
		const bool mobileFixedFunctionBodyPass = actorLikeGeometry && hasNormal && !hasColor0 && !hasColor1 && !transformed && ms_lightingEnabled && ms_activeVertexProgramMode == 0 && ms_activePixelProgramMode == 0;
		constants.lightControl2[2] = mobileFixedFunctionBodyPass ? 1.0f : 0.0f;
		const bool usesVertexDiffuse = hasColor0 && (!ms_lightingEnabled || (ms_lightingColorVertex && ms_lightingDiffuseColorSource == ShaderImplementation::Pass::FixedFunctionPipeline::MS_VertexColor0));
		constants.lightControl[1] = usesVertexDiffuse ? 1.0f : 0.0f;
		constants.lightControl[2] = hasNormal ? 1.0f : 0.0f;
		constants.lightControl[3] = ms_lightingEnabled ? 1.0f : 0.0f;
		constants.materialSourceControl[0] = !ms_lightingEnabled && hasColor0 ? 1.0f : (ms_lightingColorVertex ? static_cast<float>(ms_lightingDiffuseColorSource) : 0.0f);
		constants.materialSourceControl[1] = ms_lightingColorVertex ? static_cast<float>(ms_lightingAmbientColorSource) : 0.0f;
		constants.materialSourceControl[2] = ms_lightingColorVertex ? static_cast<float>(ms_lightingEmissiveColorSource) : 0.0f;
		constants.materialSourceControl[3] = (hasColor0 ? 1.0f : 0.0f) + (hasColor1 ? 2.0f : 0.0f);
		VertexBufferFormat activeFormat;
		activeFormat.setFlags(ms_activeVertexFormatFlags);
		constants.vertexControl[0] = activeFormat.isTransformed() ? 1.0f : 0.0f;
		constants.vertexControl[1] = ms_width > 0 ? 2.0f / static_cast<float>(ms_width) : 0.0f;
		constants.vertexControl[2] = ms_height > 0 ? 2.0f / static_cast<float>(ms_height) : 0.0f;
		constants.vertexControl[3] = ms_activeTextureAlphaOnly ? 2.0f : (ms_activeTextureBgraSwizzle ? 1.0f : 0.0f);
		int const viewportWidth = ms_viewportWidth > 0 ? ms_viewportWidth : ms_width;
		int const viewportHeight = ms_viewportHeight > 0 ? ms_viewportHeight : ms_height;
		constants.viewportControl[0] = viewportWidth > 0 ? 2.0f / static_cast<float>(viewportWidth) : 0.0f;
		constants.viewportControl[1] = viewportHeight > 0 ? -2.0f / static_cast<float>(viewportHeight) : 0.0f;
		constants.viewportControl[2] = viewportWidth > 0 ? -1.0f - (2.0f * static_cast<float>(ms_viewportX) / static_cast<float>(viewportWidth)) : -1.0f;
		constants.viewportControl[3] = viewportHeight > 0 ? 1.0f + (2.0f * static_cast<float>(ms_viewportY) / static_cast<float>(viewportHeight)) : 1.0f;
		constants.stageControl[0] = static_cast<float>(ms_activeStageTextureCount);
		constants.stageControl[1] = static_cast<float>(ms_activeTerrainBlendCount);
		constants.stageControl[2] = static_cast<float>(ms_activeTerrainProgramMode);
		constants.stageControl[3] = static_cast<float>(ms_activePixelProgramMode);
		for (int i = 0; i < cms_maxStageTextures; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				constants.stageColorOp[i][j] = ms_activeStageColorOp[i][j];
				constants.stageAlphaOp[i][j] = ms_activeStageAlphaOp[i][j];
				constants.stageMeta[i][j] = ms_activeStageMeta[i][j];
				double stageScrollJunk = 0.0;
				constants.stageScroll[i][j] = static_cast<float>(modf(ms_activeStageScroll[i][j] * ms_currentTime, &stageScrollJunk));
				constants.stageTextureCoordinateSet[i][j] = ms_activeStageTextureCoordinateSet[i][j];
			}
		}
		copyColor(constants.dot3LightSpecularColor, ms_dot3LightSpecularColor);
		copyColor(constants.dot3LightTangentMinusDiffuseColor, ms_dot3LightTangentMinusDiffuseColor);
		copyColor(constants.dot3LightTangentMinusBackColor, ms_dot3LightTangentMinusBackColor);
		constants.cameraAndMaterialControl[0] = ms_cameraPosition[0];
		constants.cameraAndMaterialControl[1] = ms_cameraPosition[1];
		constants.cameraAndMaterialControl[2] = ms_cameraPosition[2];
		constants.cameraAndMaterialControl[3] = ms_materialSpecularPower;
		Vector const lightDirection(ms_lightDirection[0], ms_lightDirection[1], ms_lightDirection[2]);
		Vector const localLightDirection = ms_objectToWorldTransform.rotate_p2l(lightDirection);
		constants.dot3LightLocalDirection[0] = -localLightDirection.x;
		constants.dot3LightLocalDirection[1] = -localLightDirection.y;
		constants.dot3LightLocalDirection[2] = -localLightDirection.z;
		constants.dot3LightLocalDirection[3] = ms_materialSpecularPower;
		Vector const cameraPosition(ms_cameraPosition[0], ms_cameraPosition[1], ms_cameraPosition[2]);
		Vector const localCameraPosition = ms_objectToWorldTransform.rotateTranslate_p2l(cameraPosition);
		constants.dot3CameraLocalPosition[0] = localCameraPosition.x;
		constants.dot3CameraLocalPosition[1] = localCameraPosition.y;
		constants.dot3CameraLocalPosition[2] = localCameraPosition.z;
		constants.dot3CameraLocalPosition[3] = 1.0f;
		ms_context->UpdateSubresource(ms_transformConstantBuffer, 0, 0, &constants, 0, 0);
		ms_transformDirty = false;
	}

	bool compileShader(char const *source, char const *entry, char const *profile, ID3DBlob **bytecode)
	{
		ID3DBlob *errors = 0;
		HRESULT const hr = D3DCompile(source, strlen(source), "Direct3d11DefaultShader", 0, 0, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION, 0, bytecode, &errors);
		if (FAILED(hr))
		{
			if (errors && errors->GetBufferPointer())
				diag("shader compile failed entry=%s profile=%s hr=0x%08X errors=%s", entry, profile, static_cast<unsigned>(hr), static_cast<char const *>(errors->GetBufferPointer()));
			else
				diag("shader compile failed entry=%s profile=%s hr=0x%08X", entry, profile, static_cast<unsigned>(hr));
		}
		releaseCom(errors);
		return SUCCEEDED(hr);
	}

	bool createDefaultShaders()
	{
		if (!ms_device)
			return false;

		static char const *shaderSource =
			"cbuffer TransformConstants : register(b0) { row_major float4x4 objectToProjection; row_major float4x4 objectToCamera; row_major float4x4 objectToWorld; row_major float4x4 objectToWorldRotation; float4 textureEnabled; float4 alphaTest; float4 materialAmbientColor; float4 materialColor; float4 materialEmissiveColor; float4 materialSpecularColor; float4 textureFactor; float4 textureFactor2; row_major float4x4 textureTransform; float4 textureTransformControl; float4 fogColor; float4 fogControl; float4 textureScroll; float4 lightAmbient; float4 lightDiffuse; float4 lightDirection; float4 parallelLightDiffuse[3]; float4 parallelLightSpecular[3]; float4 parallelLightDirection[3]; float4 lightControl; float4 lightControl2; float4 materialSourceControl; float4 vertexControl; float4 viewportControl; float4 stageControl; float4 stageColorOp[12]; float4 stageAlphaOp[12]; float4 stageMeta[12]; float4 stageScroll[12]; float4 dot3LightSpecularColor; float4 dot3LightTangentMinusDiffuseColor; float4 dot3LightTangentMinusBackColor; float4 cameraAndMaterialControl; float4 dot3LightLocalDirection; float4 dot3CameraLocalPosition; float4 userVertexConstants[8]; float4 userPixelConstants[16]; float4 stageTextureCoordinateSet[12]; };"
			"Texture2D texture0 : register(t0);"
			"Texture2D texture1 : register(t1);"
			"Texture2D texture2 : register(t2);"
			"Texture2D texture3 : register(t3);"
			"Texture2D texture4 : register(t4);"
			"Texture2D texture5 : register(t5);"
			"Texture2D texture6 : register(t6);"
			"Texture2D texture7 : register(t7);"
			"Texture2D texture8 : register(t8);"
			"Texture2D texture9 : register(t9);"
			"Texture2D texture10 : register(t10);"
			"Texture2D texture11 : register(t11);"
			"TextureCube textureCube0 : register(t12);"
			"SamplerState sampler0 : register(s0);"
			"SamplerState sampler1 : register(s1);"
			"SamplerState sampler2 : register(s2);"
			"SamplerState sampler3 : register(s3);"
			"SamplerState sampler4 : register(s4);"
			"SamplerState sampler5 : register(s5);"
			"SamplerState sampler6 : register(s6);"
			"SamplerState sampler7 : register(s7);"
			"SamplerState sampler8 : register(s8);"
			"SamplerState sampler9 : register(s9);"
			"SamplerState sampler10 : register(s10);"
			"SamplerState sampler11 : register(s11);"
			"float3 safeNormalize(float3 v, float3 fallback) { return (dot(v, v) > 0.000001f) ? normalize(v) : fallback; }"
			"struct VSInput {"
			"float4 position : POSITION;"
			"float3 normal : NORMAL;"
			"float4 color0 : COLOR0;"
			"float4 color1 : COLOR1;"
			"float4 tex0 : TEXCOORD0;"
			"float4 tex1 : TEXCOORD1;"
			"float4 tex2 : TEXCOORD2;"
			"float4 tex3 : TEXCOORD3;"
			"};"
			"struct VSOutput { float4 position : SV_Position; float4 color : COLOR0; float2 tex0 : TEXCOORD0; float2 tex1 : TEXCOORD1; float2 tex2 : TEXCOORD2; float2 tex3 : TEXCOORD3; float2 activeTex : TEXCOORD4; float fogDistance : TEXCOORD5; float3 normalWorld : TEXCOORD6; float3 dot3VertexDiffuse : TEXCOORD7; float4 dot3VertexSpecular : TEXCOORD8; float3 halfAngleWorld : TEXCOORD9; float3 cloudLightDirection : TEXCOORD10; float3 cameraPosition : TEXCOORD11; float3 cameraNormal : TEXCOORD12; float3 normalObject : TEXCOORD13; float3 halfAngleObject : TEXCOORD14; float3 dot3LightDirectionObject : TEXCOORD15; float4 fixedFunctionColor : TEXCOORD16; };"
			"float4 selectInputTex(float texSet, float4 tex0, float4 tex1, float4 tex2, float4 tex3) {"
			"return (texSet > 2.5f) ? tex3 : ((texSet > 1.5f) ? tex2 : ((texSet > 0.5f) ? tex1 : tex0));"
			"}"
			"VSOutput vsMain(VSInput input) {"
			"VSOutput output;"
			"float4 p = input.position;"
			"p.w = (p.w == 0.0f) ? 1.0f : p.w;"
			"int vertexMode = (int)(alphaTest.w + 0.5f);"
			"float3 normalizedPosition = (dot(p.xyz, p.xyz) > 0.000001f) ? normalize(p.xyz) : p.xyz;"
			"float inversePositionLength = (dot(p.xyz, p.xyz) > 0.000001f) ? rsqrt(dot(p.xyz, p.xyz)) : p.w;"
			"float4 gradientSkyPosition = float4(normalizedPosition.x, normalizedPosition.y * 0.5f, normalizedPosition.z, inversePositionLength);"
			"float4 projected = (vertexMode == 1) ? mul(gradientSkyPosition, objectToProjection) : mul(p, objectToProjection);"
			"float transformedDepth = (p.z > 1.0f && p.w > 0.0f) ? saturate(p.z * p.w) : saturate(p.z);"
			"float4 screenProjected = float4((p.x * viewportControl.x) + viewportControl.z, (p.y * viewportControl.y) + viewportControl.w, transformedDepth, 1.0f);"
			"output.position = (vertexControl.x > 0.5f) ? screenProjected : projected;"
			"float4 cameraPosition = mul(p, objectToCamera);"
			"output.cameraPosition = cameraPosition.xyz;"
			"output.fogDistance = (vertexControl.x > 0.5f) ? 0.0f : length(cameraPosition.xyz);"
			"output.normalObject = input.normal;"
			"output.normalWorld = (lightControl.z > 0.5f) ? mul(input.normal, (float3x3)objectToWorldRotation) : float3(0,0,0);"
			"output.cameraNormal = mul(input.normal, (float3x3)objectToCamera);"
			"float3 positionWorld = mul(p, objectToWorld).xyz;"
			"float hasColor0 = fmod(materialSourceControl.w, 2.0f);"
			"float hasColor1 = floor(materialSourceControl.w / 2.0f);"
			"float4 sourceColor0 = (hasColor0 > 0.5f) ? input.color0.bgra : float4(1,1,1,1);"
			"float4 sourceColor1 = (hasColor1 > 0.5f) ? input.color1.bgra : float4(1,1,1,1);"
			"float4 rawColor = (lightControl.y > 0.5f) ? sourceColor0 : float4(1,1,1,sourceColor0.a);"
			"output.color = rawColor;"
			"bool mobileBodyPass = lightControl2.z > 0.5f;"
			"float4 ffDiffuseSource = mobileBodyPass ? float4(1,1,1,1) : ((materialSourceControl.x > 1.5f) ? sourceColor1 : ((materialSourceControl.x > 0.5f) ? sourceColor0 : materialColor));"
			"float4 ffAmbientSource = mobileBodyPass ? float4(1,1,1,1) : ((materialSourceControl.y > 1.5f) ? sourceColor1 : ((materialSourceControl.y > 0.5f) ? sourceColor0 : materialAmbientColor));"
			"float4 ffEmissiveSource = mobileBodyPass ? float4(0,0,0,0) : ((materialSourceControl.z > 1.5f) ? sourceColor1 : ((materialSourceControl.z > 0.5f) ? sourceColor0 : materialEmissiveColor));"
			"float4 ffSpecularSource = mobileBodyPass ? float4(1,1,1,1) : ((lightControl2.y > 1.5f) ? sourceColor1 : ((lightControl2.y > 0.5f) ? sourceColor0 : materialSpecularColor));"
			"float ffNormalLength = length(output.normalWorld);"
			"float3 ffNormalWorld = (ffNormalLength > 0.0001f) ? output.normalWorld / ffNormalLength : float3(0,0,0);"
			"float3 ffViewerWorld = safeNormalize(cameraAndMaterialControl.xyz - positionWorld, float3(0,0,1));"
			"float3 ffParallelLighting = float3(0,0,0);"
			"float3 ffParallelSpecular = float3(0,0,0);"
			"float ffMobileBodyShade = 0.0f;"
			"float ffEnabledParallelLights = 0.0f;"
			"[unroll] for (int ffLightIndex = 0; ffLightIndex < 3; ++ffLightIndex) {"
			"if (parallelLightDirection[ffLightIndex].w > 0.5f) {"
			"ffEnabledParallelLights += 1.0f;"
			"float3 ffLightDir = -normalize(parallelLightDirection[ffLightIndex].xyz);"
			"float3 ffSpecularLightDir = ffLightDir;"
			"float ffNdotL = saturate(dot(ffNormalWorld, ffLightDir));"
			"ffParallelLighting += parallelLightDiffuse[ffLightIndex].rgb * ffNdotL;"
			"float ffSpecNdotL = saturate(dot(ffNormalWorld, ffSpecularLightDir));"
			"ffMobileBodyShade = max(ffMobileBodyShade, ffSpecNdotL);"
			"float3 ffHalfDir = safeNormalize(ffSpecularLightDir + ffViewerWorld, ffSpecularLightDir);"
			"float ffSpecAmount = (ffSpecNdotL > 0.0f && cameraAndMaterialControl.w > 0.0f) ? pow(saturate(dot(ffNormalWorld, ffHalfDir)), cameraAndMaterialControl.w) : 0.0f;"
			"ffParallelSpecular += parallelLightSpecular[ffLightIndex].rgb * ffSpecAmount;"
			"}}"
			"if (ffEnabledParallelLights < 0.5f && lightControl.x > 0.5f && ffNormalLength > 0.0001f) {"
			"float ffDirectionalAmount = saturate(dot(ffNormalWorld, -normalize(lightDirection.xyz)));"
			"ffParallelLighting = lightDiffuse.rgb * ffDirectionalAmount;"
			"}"
			"float3 ffSpecular = (lightControl2.x > 0.5f && ffNormalLength > 0.0001f) ? ffSpecularSource.rgb * ffParallelSpecular : float3(0,0,0);"
			"float3 ffDiffuse = (lightControl.w > 0.5f && ffNormalLength > 0.0001f) ? saturate(ffEmissiveSource.rgb + ffAmbientSource.rgb * lightAmbient.rgb + ffDiffuseSource.rgb * ffParallelLighting) : ffDiffuseSource.rgb;"
			"output.fixedFunctionColor = float4(ffDiffuse, ffDiffuseSource.a);"
			"float3 dot3ParallelLighting = float3(0,0,0);"
			"[unroll] for (int dot3LightIndex = 1; dot3LightIndex < 3; ++dot3LightIndex) {"
			"if (parallelLightDirection[dot3LightIndex].w > 0.5f && length(output.normalWorld) > 0.0001f) {"
			"float3 dot3LightDir = -normalize(parallelLightDirection[dot3LightIndex].xyz);"
			"dot3ParallelLighting += parallelLightDiffuse[dot3LightIndex].rgb * saturate(dot(normalize(output.normalWorld), dot3LightDir));"
			"}}"
			"output.dot3VertexDiffuse = materialEmissiveColor.rgb + lightAmbient.rgb + dot3ParallelLighting;"
			"output.dot3VertexSpecular = float4(ffSpecular, ffMobileBodyShade);"
			"output.dot3LightDirectionObject = (lightControl.x > 0.5f) ? safeNormalize(dot3LightLocalDirection.xyz, float3(0,1,0)) : float3(0,1,0);"
			"float3 viewerWorld = ffViewerWorld;"
			"float3 viewerObject = safeNormalize(mul(viewerWorld, transpose((float3x3)objectToWorldRotation)), float3(0,0,1));"
			"float3 halfLightDir = (lightControl.x > 0.5f) ? -safeNormalize(lightDirection.xyz, float3(0,-1,0)) : float3(0,1,0);"
			"output.halfAngleWorld = safeNormalize(halfLightDir + viewerWorld, halfLightDir);"
			"output.halfAngleObject = safeNormalize(output.dot3LightDirectionObject + viewerObject, output.dot3LightDirectionObject);"
			"float3 cloudTangent = safeNormalize(input.tex1.xyz, float3(1,0,0));"
			"float cloudHandedness = (input.tex1.w == 0.0f) ? 1.0f : input.tex1.w;"
			"float3 cloudBitangent = cross(cloudTangent, input.normal) * cloudHandedness;"
			"float3 cloudNormal = safeNormalize(input.normal, float3(0,1,0));"
			"float3 cloudLight = safeNormalize(lightDirection.xyz, float3(0,-1,0));"
			"output.cloudLightDirection = float3(dot(cloudLight, cloudTangent), dot(cloudLight, cloudBitangent), dot(cloudLight, cloudNormal)) * 0.5f + 0.5f;"
			"float4 selectedTex = selectInputTex(textureEnabled.w, input.tex0, input.tex1, input.tex2, input.tex3);"
			"selectedTex.xy += textureScroll.xy;"
			"selectedTex.w = (selectedTex.w == 0.0f) ? 1.0f : selectedTex.w;"
			"float4 transformedTex = (textureTransformControl.x > 0.5f) ? mul(selectedTex, textureTransform) : selectedTex;"
			"if (textureTransformControl.z > 0.5f && textureTransformControl.y > 1.5f) {"
			"float denominator = (textureTransformControl.y < 2.5f) ? transformedTex.y : ((textureTransformControl.y < 3.5f) ? transformedTex.z : transformedTex.w);"
			"transformedTex.xy = (abs(denominator) > 0.00001f) ? transformedTex.xy / denominator : transformedTex.xy;"
			"}"
			"output.dot3LightDirectionObject = (stageControl.w > 0.5f && stageControl.w < 1.5f) ? safeNormalize(transformedTex.xyz, normalizedPosition) : output.dot3LightDirectionObject;"
			"output.tex0 = input.tex0.xy;"
			"output.tex1 = input.tex1.xy;"
			"output.tex2 = input.tex2.xy;"
			"output.tex3 = input.tex3.xy;"
			"output.activeTex = (vertexMode == 1) ? float2(userVertexConstants[0].x, 1.0f - normalizedPosition.y) : transformedTex.xy;"
			"return output;"
			"}"
			"float4 fixSampleFormat(float4 rawValue, float mode) {"
			"return (mode > 1.5f) ? float4(1,1,1,rawValue.a) : ((mode > 0.5f) ? rawValue.bgra : rawValue);"
			"}"
			"float4 sampleStageTexture(int slot, float2 uv, float mode) {"
			"if (slot == 0) return fixSampleFormat(texture0.Sample(sampler0, uv), mode);"
			"if (slot == 1) return fixSampleFormat(texture1.Sample(sampler1, uv), mode);"
			"if (slot == 2) return fixSampleFormat(texture2.Sample(sampler2, uv), mode);"
			"if (slot == 3) return fixSampleFormat(texture3.Sample(sampler3, uv), mode);"
			"if (slot == 4) return fixSampleFormat(texture4.Sample(sampler4, uv), mode);"
			"if (slot == 5) return fixSampleFormat(texture5.Sample(sampler5, uv), mode);"
			"if (slot == 6) return fixSampleFormat(texture6.Sample(sampler6, uv), mode);"
			"if (slot == 7) return fixSampleFormat(texture7.Sample(sampler7, uv), mode);"
			"if (slot == 8) return fixSampleFormat(texture8.Sample(sampler8, uv), mode);"
			"if (slot == 9) return fixSampleFormat(texture9.Sample(sampler9, uv), mode);"
			"if (slot == 10) return fixSampleFormat(texture10.Sample(sampler10, uv), mode);"
			"return fixSampleFormat(texture11.Sample(sampler11, uv), mode);"
			"}"
			"float4 stageArg(float selector, float complementFlag, float alphaReplicateFlag, float4 currentValue, float4 diffuseValue, float4 specularValue, float4 tempValue, float4 textureValue, float4 textureFactorValue) {"
			"float4 value = currentValue;"
			"if (selector > 0.5f && selector < 1.5f) value = diffuseValue;"
			"else if (selector > 1.5f && selector < 2.5f) value = specularValue;"
			"else if (selector > 2.5f && selector < 3.5f) value = tempValue;"
			"else if (selector > 3.5f && selector < 4.5f) value = textureValue;"
			"else if (selector > 4.5f) value = textureFactorValue;"
			"if (alphaReplicateFlag > 0.5f) value.rgb = value.aaa;"
			"if (complementFlag > 0.5f) value = 1.0f - value;"
			"return value;"
			"}"
			"float4 applyStageOp(float op, float4 arg0, float4 arg1, float4 arg2, float4 textureValue, float4 diffuseValue, float4 currentValue) {"
			"if (op < 0.5f) return currentValue;"
			"if (op < 1.5f) return arg1;"
			"if (op < 2.5f) return arg2;"
			"if (op < 3.5f) return arg1 * arg2;"
			"if (op < 4.5f) return arg1 * arg2 * 2.0f;"
			"if (op < 5.5f) return arg1 * arg2 * 4.0f;"
			"if (op < 6.5f) return arg1 + arg2;"
			"if (op < 7.5f) return arg1 + arg2 - 0.5f;"
			"if (op < 8.5f) return (arg1 + arg2 - 0.5f) * 2.0f;"
			"if (op < 9.5f) return arg1 - arg2;"
			"if (op < 10.5f) return arg1 + arg2 * (1.0f - arg1);"
			"if (op < 11.5f) return lerp(arg2, arg1, diffuseValue.a);"
			"if (op < 12.5f) return lerp(arg2, arg1, textureValue.a);"
			"if (op < 13.5f) return lerp(arg2, arg1, textureFactor.a);"
			"if (op < 14.5f) return arg1 + arg2 * (1.0f - textureValue.a);"
			"if (op < 15.5f) return lerp(arg2, arg1, currentValue.a);"
			"if (op < 16.5f) return arg1;"
			"if (op < 17.5f) return float4(arg1.rgb + arg1.a * arg2.rgb, arg1.a);"
			"if (op < 18.5f) return float4(arg1.rgb * arg2.rgb + arg1.a, arg1.a);"
			"if (op < 19.5f) return float4((1.0f - arg1.a) * arg2.rgb + arg1.rgb, arg1.a);"
			"if (op < 20.5f) return float4((1.0f - arg1.rgb) * arg2.rgb + arg1.a, arg1.a);"
			"if (op < 21.5f) return arg1;"
			"if (op < 22.5f) return arg1;"
			"if (op < 23.5f) { float dot3 = saturate(dot(arg1.rgb * 2.0f - 1.0f, arg2.rgb * 2.0f - 1.0f)); return float4(dot3, dot3, dot3, dot3); }"
			"if (op > 23.5f && op < 24.5f) return arg0 + arg1 * arg2;"
			"if (op > 24.5f && op < 25.5f) return lerp(arg2, arg1, arg0);"
			"return arg1;"
			"}"
			"float2 selectStageTexCoord(VSOutput input, float texSet) {"
			"return (texSet > 2.5f) ? input.tex3 : ((texSet > 1.5f) ? input.tex2 : ((texSet > 0.5f) ? input.tex1 : input.tex0));"
			"}"
			"float2 generateStageTexCoord(VSOutput input, float texSet, float coordinateGeneration) {"
			"float2 authoredUv = selectStageTexCoord(input, texSet);"
			"if (coordinateGeneration > 0.5f && coordinateGeneration < 1.5f) return input.cameraPosition.xy;"
			"if (coordinateGeneration > 1.5f && coordinateGeneration < 2.5f) return safeNormalize(input.cameraNormal, float3(0,0,1)).xy * 0.5f + 0.5f;"
			"if (coordinateGeneration > 2.5f && coordinateGeneration < 3.5f) { float3 viewDirection = safeNormalize(-input.cameraPosition, float3(0,0,1)); float3 reflectionVector = reflect(-viewDirection, safeNormalize(input.cameraNormal, float3(0,0,1))); return reflectionVector.xy * 0.5f + 0.5f; }"
			"return authoredUv;"
			"}"
			"float4 applyStageCombiner(float4 diffuseValue, VSOutput input) {"
			"float4 currentValue = diffuseValue;"
			"float4 tempValue = float4(0,0,0,0);"
			"float4 specularValue = float4(input.dot3VertexSpecular.rgb, 1);"
			"int stageCount = min((int)(stageControl.x + 0.5f), 12);"
			"[loop] for (int stage = 0; stage < stageCount; ++stage) {"
			"float colorOp = stageColorOp[stage].x;"
			"float alphaOp = stageAlphaOp[stage].x;"
			"if (colorOp < 0.5f && alphaOp < 0.5f) break;"
			"float4 textureValue = sampleStageTexture(stage, generateStageTexCoord(input, stageTextureCoordinateSet[stage].x, stageTextureCoordinateSet[stage].y) + stageScroll[stage].xy, stageMeta[stage].z);"
			"float colorFlags = stageMeta[stage].x;"
			"float alphaFlags = stageMeta[stage].y;"
			"float4 colorArg0 = stageArg(stageColorOp[stage].y, fmod(colorFlags, 2.0f), fmod(floor(colorFlags / 2.0f), 2.0f), currentValue, diffuseValue, specularValue, tempValue, textureValue, textureFactor);"
			"float4 colorArg1 = stageArg(stageColorOp[stage].z, fmod(floor(colorFlags / 4.0f), 2.0f), fmod(floor(colorFlags / 8.0f), 2.0f), currentValue, diffuseValue, specularValue, tempValue, textureValue, textureFactor);"
			"float4 colorArg2 = stageArg(stageColorOp[stage].w, fmod(floor(colorFlags / 16.0f), 2.0f), fmod(floor(colorFlags / 32.0f), 2.0f), currentValue, diffuseValue, specularValue, tempValue, textureValue, textureFactor);"
			"float4 alphaArg0 = stageArg(stageAlphaOp[stage].y, fmod(alphaFlags, 2.0f), 0.0f, currentValue, diffuseValue, specularValue, tempValue, textureValue, textureFactor);"
			"float4 alphaArg1 = stageArg(stageAlphaOp[stage].z, fmod(floor(alphaFlags / 2.0f), 2.0f), 0.0f, currentValue, diffuseValue, specularValue, tempValue, textureValue, textureFactor);"
			"float4 alphaArg2 = stageArg(stageAlphaOp[stage].w, fmod(floor(alphaFlags / 4.0f), 2.0f), 0.0f, currentValue, diffuseValue, specularValue, tempValue, textureValue, textureFactor);"
			"float4 colorResult = applyStageOp(colorOp, colorArg0, colorArg1, colorArg2, textureValue, diffuseValue, currentValue);"
			"float4 alphaResult = applyStageOp(alphaOp, alphaArg0, alphaArg1, alphaArg2, textureValue, diffuseValue, currentValue);"
			"float4 combined = float4(colorResult.rgb, alphaResult.a);"
			"if (stageMeta[stage].w > 2.5f && stageMeta[stage].w < 3.5f) tempValue = combined; else currentValue = combined;"
			"}"
			"return saturate(currentValue);"
			"}"
			"float3 calculateD3d9HemisphericLighting(float3 direction, float3 normal, float3 vertexDiffuse) {"
			"float dotProduct = dot(direction, normal);"
			"float3 light = vertexDiffuse + dot3LightTangentMinusDiffuseColor.rgb + lightDiffuse.rgb + (-max(0.0f, dotProduct) * dot3LightTangentMinusDiffuseColor.rgb);"
			"light += min(0.0f, dotProduct) * dot3LightTangentMinusBackColor.rgb;"
			"return saturate(light);"
			"}"
			"float3 sampleBumpNormal(VSOutput input) {"
			"float3 normalMap = fixSampleFormat(texture1.Sample(sampler1, input.tex1), stageMeta[1].z).rgb * 2.0f - 1.0f;"
			"return safeNormalize(normalMap, safeNormalize(input.normalObject, float3(0,1,0)));"
			"}"
			"float3 calculateDot3Specular(float3 dot3Dir, float3 normalO, float3 halfAngleO, float3 vertexSpecular, float specularMask) {"
			"float dot3LightIntensity = saturate(dot(dot3Dir, normalO));"
			"float specularPower = max(cameraAndMaterialControl.w, 0.0001f);"
			"float dot3SpecularIntensity = (dot3LightIntensity > 0.0f && cameraAndMaterialControl.w > 0.0f) ? pow(saturate(dot(halfAngleO, normalO)), specularPower) : 0.0f;"
			"return (dot3SpecularIntensity * dot3LightSpecularColor.rgb * materialSpecularColor.rgb + vertexSpecular) * specularMask;"
			"}"
			"float4 psMain(VSOutput input) : SV_Target {"
			"float4 sampledRaw = texture0.Sample(sampler0, input.activeTex);"
			"float4 sampled = fixSampleFormat(sampledRaw, vertexControl.w);"
			"int pixelMode = (int)(stageControl.w + 0.5f);"
			"float4 output;"
			"bool legacyPixelProgram = pixelMode > 0;"
			"float3 normalO = safeNormalize(input.normalObject, float3(0,1,0));"
			"float3 normalW = safeNormalize(input.normalWorld, float3(0,1,0));"
			"float3 dot3Dir = safeNormalize(input.dot3LightDirectionObject, float3(0,1,0));"
			"float3 dot3DirW = (lightControl.x > 0.5f) ? -safeNormalize(lightDirection.xyz, float3(0,-1,0)) : float3(0,1,0);"
			"float3 halfAngleO = safeNormalize(input.halfAngleObject, dot3Dir);"
			"float3 halfAngleW = safeNormalize(input.halfAngleWorld, dot3DirW);"
			"float3 dot3Light = calculateD3d9HemisphericLighting(dot3Dir, normalO, input.dot3VertexDiffuse);"
			"float3 dot3LightW = calculateD3d9HemisphericLighting(dot3DirW, normalW, input.dot3VertexDiffuse);"
			"if (pixelMode == 1) output = sampled;"
			"else if (pixelMode == 2) output = input.color * textureFactor;"
			"else if (pixelMode == 3) output = sampled * textureFactor;"
			"else if (pixelMode == 4) output = lerp(texture1.Sample(sampler1, input.activeTex), sampled, input.color.a);"
			"else if (pixelMode == 5) { float a = sampled.a * sampled.a; output = float4(0.2f, 0.2f, 0.2f, a); }"
			"else if (pixelMode == 9) { output = float4(sampled.rgb * dot3Light, sampled.a); }"
			"else if (pixelMode == 10) { float specularMask = texture1.Sample(sampler1, input.activeTex).a; float3 specularRaw = calculateDot3Specular(dot3Dir, normalO, halfAngleO, input.dot3VertexSpecular.rgb, specularMask); output = float4((sampled.rgb * dot3Light) + specularRaw, sampled.a); }"
			"else if (pixelMode == 11) { float4 mask = fixSampleFormat(texture1.Sample(sampler1, input.tex1), stageMeta[1].z); output = float4(sampled.rgb * textureFactor.rgb, mask.a); }"
			"else if (pixelMode == 12) { float3 lightVector = input.cloudLightDirection * 2.0f - 1.0f; float lightLength2 = dot(lightVector, lightVector); float3 normalizedLight = ((input.cloudLightDirection - 0.5f) * (1.0f - lightLength2)) + lightVector; float3 normalMap = fixSampleFormat(texture1.Sample(sampler1, input.activeTex), stageMeta[1].z).rgb * 2.0f - 1.0f; float dot3 = saturate(dot(normalMap, normalizedLight)); float3 lit = (dot3 * lightDiffuse.rgb) + input.color.rgb; output = float4(lit * sampled.rgb, sampled.a) * textureFactor; }"
			"else if (pixelMode == 13) { output = float4(sampled.rgb * dot3Light * textureFactor.rgb, 1.0f); }"
			"else if (pixelMode == 14) { float3 hue = lerp(textureFactor.rgb, textureFactor2.rgb, sampled.a); output = float4(sampled.rgb * hue * dot3Light, 1.0f); }"
			"else if (pixelMode == 15) { float specularMask = texture1.Sample(sampler1, input.activeTex).a; float3 specularRaw = calculateDot3Specular(dot3Dir, normalO, halfAngleO, input.dot3VertexSpecular.rgb, specularMask); float alphaOut = (textureEnabled.y > 0.5f) ? 1.0f : dot(specularRaw, float3(0.333333f, 0.333333f, 0.333333f)); output = float4((sampled.rgb * dot3Light * textureFactor.rgb) + specularRaw, alphaOut); }"
			"else if (pixelMode == 16) { float specularMask = texture1.Sample(sampler1, input.activeTex).a; float3 hue = lerp(textureFactor.rgb, textureFactor2.rgb, sampled.a); float3 specularRaw = calculateDot3Specular(dot3Dir, normalO, halfAngleO, input.dot3VertexSpecular.rgb, specularMask); float alphaOut = (textureEnabled.y > 0.5f) ? 1.0f : dot(specularRaw, float3(0.333333f, 0.333333f, 0.333333f)); output = float4((sampled.rgb * hue * dot3Light) + specularRaw, alphaOut); }"
			"else if (pixelMode == 17) { output = float4(sampled.rgb * textureFactor.rgb * dot3Light, sampled.a); }"
			"else if (pixelMode == 18) { float hueB = texture1.Sample(sampler1, input.activeTex).a; float hueMask = saturate(sampled.a - hueB); float3 hue = lerp(textureFactor2.rgb, textureFactor.rgb, hueMask); output = float4(sampled.rgb * hue * dot3Light, saturate(sampled.a + hueB)); }"
			"else if (pixelMode == 19) { float hueB = texture1.Sample(sampler1, input.activeTex).a; float3 hueMain = lerp(float3(1,1,1), textureFactor.rgb, sampled.a); float3 hueBColor = lerp(float3(1,1,1), textureFactor2.rgb, hueB); output = float4(sampled.rgb * dot3Light * hueMain * hueBColor, 1.0f); }"
			"else if (pixelMode == 20) { float3 specularRaw = calculateDot3Specular(dot3Dir, normalO, halfAngleO, input.dot3VertexSpecular.rgb, 1.0f); float alphaOut = (textureEnabled.y > 0.5f) ? 1.0f : dot(specularRaw, float3(0.333333f, 0.333333f, 0.333333f)); output = float4((sampled.rgb * dot3Light * textureFactor.rgb) + specularRaw, alphaOut); }"
			"else if (pixelMode == 21) { output = sampled; }"
			"else if (pixelMode == 22) { float3 bumpNormal = sampleBumpNormal(input); float3 bumpLight = calculateD3d9HemisphericLighting(dot3Dir, bumpNormal, input.dot3VertexDiffuse); output = float4(sampled.rgb * bumpLight * textureFactor.rgb, 1.0f); }"
			"else if (pixelMode == 23) { float3 bumpNormal = sampleBumpNormal(input); float3 bumpLight = calculateD3d9HemisphericLighting(dot3Dir, bumpNormal, input.dot3VertexDiffuse); float3 hue = lerp(textureFactor.rgb, textureFactor2.rgb, sampled.a); output = float4(sampled.rgb * hue * bumpLight, 1.0f); }"
			"else if (pixelMode == 24) { float3 bumpNormal = sampleBumpNormal(input); float3 bumpLight = calculateD3d9HemisphericLighting(dot3Dir, bumpNormal, input.dot3VertexDiffuse); float3 bumpHalf = safeNormalize(halfAngleO, dot3Dir); float specularMask = sampled.a; float3 specularRaw = calculateDot3Specular(dot3Dir, bumpNormal, bumpHalf, input.dot3VertexSpecular.rgb, specularMask); float alphaOut = (textureEnabled.y > 0.5f) ? 1.0f : dot(specularRaw, float3(0.333333f, 0.333333f, 0.333333f)); output = float4((sampled.rgb * bumpLight * textureFactor.rgb) + specularRaw, alphaOut); }"
			"else if (pixelMode == 25) { float4 normalSample = fixSampleFormat(texture1.Sample(sampler1, input.tex1), stageMeta[1].z); float3 bumpNormal = safeNormalize(normalSample.rgb * 2.0f - 1.0f, normalO); float3 bumpLight = calculateD3d9HemisphericLighting(dot3Dir, bumpNormal, input.dot3VertexDiffuse); float3 bumpHalf = safeNormalize(halfAngleO, dot3Dir); float3 hue = lerp(textureFactor.rgb, textureFactor2.rgb, sampled.a); float3 specularRaw = calculateDot3Specular(dot3Dir, bumpNormal, bumpHalf, input.dot3VertexSpecular.rgb, normalSample.a); float alphaOut = (textureEnabled.y > 0.5f) ? 1.0f : dot(specularRaw, float3(0.333333f, 0.333333f, 0.333333f)); output = float4((sampled.rgb * hue * bumpLight) + specularRaw, alphaOut); }"
			"else if (pixelMode == 26) { float4 normalSample = fixSampleFormat(texture1.Sample(sampler1, input.tex1), stageMeta[1].z); float3 bumpNormal = safeNormalize(normalSample.rgb * 2.0f - 1.0f, normalO); float3 bumpLight = calculateD3d9HemisphericLighting(dot3Dir, bumpNormal, input.dot3VertexDiffuse); float hueMask = saturate(sampled.a - normalSample.a); float3 hue = lerp(textureFactor2.rgb, textureFactor.rgb, hueMask); output = float4(sampled.rgb * hue * bumpLight, saturate(sampled.a + normalSample.a)); }"
			"float4 fixedFunctionColor = input.fixedFunctionColor;"
			"if (pixelMode == 0) output = (stageControl.x > 0.5f) ? applyStageCombiner(fixedFunctionColor, input) : ((textureEnabled.x > 0.5f) ? sampled * fixedFunctionColor : fixedFunctionColor);"
			"float mobileBodySunShade = saturate(input.dot3VertexSpecular.a);"
			"float mobileBodyDirectionalScale = lerp(0.55f, 1.00f, mobileBodySunShade);"
			"float3 mobileBodyLightingScale = (lightControl2.z > 0.5f) ? (float3(0.72f,0.72f,0.72f) * mobileBodyDirectionalScale) : float3(1,1,1);"
			"if (pixelMode == 0) output.rgb *= mobileBodyLightingScale;"
			"if (pixelMode == 0 && lightControl2.x > 0.5f) output.rgb = saturate(output.rgb + input.dot3VertexSpecular.rgb);"
			"output.a *= (textureEnabled.y > 0.5f) ? textureEnabled.z : 1.0f;"
			"if (alphaTest.x > 0.5f) {"
			"float a = output.a;"
			"float r = alphaTest.y;"
			"float c = alphaTest.z;"
			"bool keep = (c < 0.5f) ? false :"
			"(c < 1.5f) ? (a < r) :"
			"(c < 2.5f) ? (abs(a - r) < 0.001f) :"
			"(c < 3.5f) ? (a <= r) :"
			"(c < 4.5f) ? (a > r) :"
			"(c < 5.5f) ? (abs(a - r) >= 0.001f) :"
			"(c < 6.5f) ? (a >= r) : true;"
			"if (!keep) discard;"
			"}"
			"float fogAmount = (fogControl.x > 0.5f) ? saturate(1.0f - exp2(-fogControl.z * input.fogDistance * input.fogDistance)) : 0.0f;"
			"output.rgb = lerp(output.rgb, fogColor.rgb, fogAmount);"
			"return output;"
			"}"
			"float4 psCube(VSOutput input) : SV_Target {"
			"float4 output = fixSampleFormat(textureCube0.Sample(sampler0, input.dot3LightDirectionObject), vertexControl.w);"
			"output.a *= (textureEnabled.y > 0.5f) ? textureEnabled.z : 1.0f;"
			"if (alphaTest.x > 0.5f) {"
			"float a = output.a;"
			"float r = alphaTest.y;"
			"float c = alphaTest.z;"
			"bool keep = (c < 0.5f) ? false :"
			"(c < 1.5f) ? (a < r) :"
			"(c < 2.5f) ? (abs(a - r) < 0.001f) :"
			"(c < 3.5f) ? (a <= r) :"
			"(c < 4.5f) ? (a > r) :"
			"(c < 5.5f) ? (a >= r) :"
			"(c < 6.5f) ? (abs(a - r) >= 0.001f) : true;"
			"if (!keep) discard;"
			"}"
			"float fogAmount = (fogControl.x > 0.5f) ? saturate(1.0f - exp2(-fogControl.z * input.fogDistance * input.fogDistance)) : 0.0f;"
			"output.rgb = lerp(output.rgb, fogColor.rgb, fogAmount);"
			"return output;"
			"}";

		ID3DBlob *pixelShaderBytecode = 0;
		if (!compileShader(shaderSource, "vsMain", "vs_4_0", &ms_defaultVertexShaderBytecode))
			return false;
		if (!compileShader(shaderSource, "psMain", "ps_4_0", &pixelShaderBytecode))
			return false;

		HRESULT hr = ms_device->CreateVertexShader(ms_defaultVertexShaderBytecode->GetBufferPointer(), ms_defaultVertexShaderBytecode->GetBufferSize(), 0, &ms_defaultVertexShader);
		if (FAILED(hr))
		{
			releaseCom(pixelShaderBytecode);
			return false;
		}

		hr = ms_device->CreatePixelShader(pixelShaderBytecode->GetBufferPointer(), pixelShaderBytecode->GetBufferSize(), 0, &ms_defaultPixelShader);
		releaseCom(pixelShaderBytecode);
		if (FAILED(hr))
			return false;

		if (!compileShader(shaderSource, "psCube", "ps_4_0", &pixelShaderBytecode))
			return false;
		hr = ms_device->CreatePixelShader(pixelShaderBytecode->GetBufferPointer(), pixelShaderBytecode->GetBufferSize(), 0, &ms_cubePixelShader);
		releaseCom(pixelShaderBytecode);
		if (FAILED(hr))
			return false;

		static char const *terrainDot3ShaderSource =
			"cbuffer TransformConstants : register(b0) { row_major float4x4 objectToProjection; row_major float4x4 objectToCamera; row_major float4x4 objectToWorld; row_major float4x4 objectToWorldRotation; float4 textureEnabled; float4 alphaTest; float4 materialAmbientColor; float4 materialColor; float4 materialEmissiveColor; float4 materialSpecularColor; float4 textureFactor; float4 textureFactor2; row_major float4x4 textureTransform; float4 textureTransformControl; float4 fogColor; float4 fogControl; float4 textureScroll; float4 lightAmbient; float4 lightDiffuse; float4 lightDirection; float4 parallelLightDiffuse[3]; float4 parallelLightSpecular[3]; float4 parallelLightDirection[3]; float4 lightControl; float4 lightControl2; float4 materialSourceControl; float4 vertexControl; float4 viewportControl; float4 stageControl; float4 stageColorOp[12]; float4 stageAlphaOp[12]; float4 stageMeta[12]; float4 stageScroll[12]; float4 dot3LightSpecularColor; float4 dot3LightTangentMinusDiffuseColor; float4 dot3LightTangentMinusBackColor; float4 cameraAndMaterialControl; float4 dot3LightLocalDirection; float4 dot3CameraLocalPosition; float4 userVertexConstants[8]; float4 userPixelConstants[16]; };"
			"Texture2D texture0 : register(t0); Texture2D texture1 : register(t1); Texture2D texture2 : register(t2); Texture2D texture3 : register(t3);"
			"Texture2D texture4 : register(t4); Texture2D texture5 : register(t5); Texture2D texture6 : register(t6); Texture2D texture7 : register(t7);"
			"Texture2D texture8 : register(t8); Texture2D texture9 : register(t9); Texture2D texture10 : register(t10); Texture2D texture11 : register(t11);"
			"SamplerState sampler0 : register(s0); SamplerState sampler1 : register(s1); SamplerState sampler2 : register(s2); SamplerState sampler3 : register(s3);"
			"SamplerState sampler4 : register(s4); SamplerState sampler5 : register(s5); SamplerState sampler6 : register(s6); SamplerState sampler7 : register(s7);"
			"SamplerState sampler8 : register(s8); SamplerState sampler9 : register(s9); SamplerState sampler10 : register(s10); SamplerState sampler11 : register(s11);"
			"struct VSInput { float4 position : POSITION; float3 normal : NORMAL; float4 color0 : COLOR0; float4 tex0 : TEXCOORD0; float4 tex1 : TEXCOORD1; float4 tex2 : TEXCOORD2; float4 tex3 : TEXCOORD3; };"
			"struct VSOutput { float4 position : SV_Position; float4 vertexLight : COLOR0; float4 dot3LightColor : COLOR1; float3 dot3LightDirection : TEXCOORD0; float2 baseUv : TEXCOORD1; float2 alphaUv1 : TEXCOORD2; float2 alphaUv2 : TEXCOORD3; float2 alphaUv3 : TEXCOORD4; float fogDistance : TEXCOORD5; };"
			"float3 reverseSignAndBias(float3 value) { return value * 0.5f + 0.5f; }"
			"float3 transformTerrainDot3LightDirection(float3 normalObject, float3 lightDirectionObject) { float3 j = cross(normalObject, float3(1.0f, 0.0f, 0.0f)); float3 i = cross(j, normalObject); return reverseSignAndBias(float3(dot(i, lightDirectionObject), dot(j, lightDirectionObject), dot(normalObject, lightDirectionObject))); }"
			"VSOutput vsTerrain(VSInput input) { VSOutput output; float4 p = input.position; p.w = (p.w == 0.0f) ? 1.0f : p.w; output.position = mul(p, objectToProjection); float4 cameraPosition = mul(p, objectToCamera); output.fogDistance = length(cameraPosition.xyz); float3 normalObject = (length(input.normal) > 0.0001f) ? normalize(input.normal) : float3(0,1,0); float3 normalWorld = normalize(mul(normalObject, (float3x3)objectToWorldRotation)); float4 rawColor = input.color0.bgra; float4 vertexColor = (dot(rawColor, rawColor) < 0.0001f) ? float4(1,1,1,1) : rawColor; float3 lightDirWorld = (lightControl.x > 0.5f) ? -normalize(lightDirection.xyz) : float3(0,1,0); float diffuseAmount = (lightControl.x > 0.5f) ? saturate(dot(normalWorld, lightDirWorld)) : 0.0f; output.vertexLight = saturate(float4(lightAmbient.rgb + lightDiffuse.rgb * diffuseAmount, 1.0f)) * vertexColor; output.dot3LightColor = saturate(float4(lightDiffuse.rgb, 1.0f) * vertexColor); output.dot3LightDirection = transformTerrainDot3LightDirection(normalObject, lightDirWorld); output.baseUv = input.tex0.xy; output.alphaUv1 = input.tex1.xy; output.alphaUv2 = input.tex2.xy; output.alphaUv3 = input.tex3.xy; return output; }"
			"float4 sampleTex(int slot, float2 uv) { if (slot == 0) return texture0.Sample(sampler0, uv); if (slot == 1) return texture1.Sample(sampler1, uv); if (slot == 2) return texture2.Sample(sampler2, uv); if (slot == 3) return texture3.Sample(sampler3, uv); if (slot == 4) return texture4.Sample(sampler4, uv); if (slot == 5) return texture5.Sample(sampler5, uv); if (slot == 6) return texture6.Sample(sampler6, uv); if (slot == 7) return texture7.Sample(sampler7, uv); if (slot == 8) return texture8.Sample(sampler8, uv); if (slot == 9) return texture9.Sample(sampler9, uv); if (slot == 10) return texture10.Sample(sampler10, uv); return texture11.Sample(sampler11, uv); }"
			"float3 signAndBias(float3 value) { return value * 2.0f - 1.0f; }"
			"float4 terrainLayer(int diffuseSlot, int normalSlot, float2 uv, float3 normalizedLightDirection, float4 vertexLight, float4 dot3LightColor) { float4 diffuseMap = sampleTex(diffuseSlot, uv); float3 normalMap = signAndBias(sampleTex(normalSlot, uv).rgb); float dot3Intensity = saturate(dot(normalizedLightDirection, normalMap)); float4 light = saturate((dot3LightColor * dot3Intensity) + vertexLight); return diffuseMap * light; }"
			"float terrainSpecWeight(int mode, VSOutput input) { float a1; float a2; float a3; if (mode == 1) return 1.0f; if (mode == 2) { a1 = sampleTex(2, input.alphaUv1).a; return 1.0f - a1; } if (mode == 3) { a1 = sampleTex(2, input.alphaUv1).a; return a1; } if (mode == 4) { a1 = sampleTex(2, input.alphaUv1).a; a2 = sampleTex(3, input.alphaUv2).a; return (1.0f - a1) * (1.0f - a2); } if (mode == 5) { a1 = sampleTex(2, input.alphaUv1).a; a2 = sampleTex(3, input.alphaUv2).a; return a1 * (1.0f - a2); } if (mode == 6) { a2 = sampleTex(2, input.alphaUv2).a; return a2; } if (mode == 7) { a1 = sampleTex(2, input.alphaUv1).a; a2 = sampleTex(3, input.alphaUv2).a; a3 = sampleTex(4, input.alphaUv3).a; return (1.0f - a1) * (1.0f - a2) * (1.0f - a3); } if (mode == 8) { a1 = sampleTex(2, input.alphaUv1).a; a2 = sampleTex(3, input.alphaUv2).a; a3 = sampleTex(4, input.alphaUv3).a; return a1 * (1.0f - a2) * (1.0f - a3); } if (mode == 9) { a2 = sampleTex(2, input.alphaUv2).a; a3 = sampleTex(3, input.alphaUv3).a; return a2 * (1.0f - a3); } if (mode == 10) { a3 = sampleTex(2, input.alphaUv3).a; return a3; } return 1.0f; }"
			"float4 psTerrain(VSOutput input) : SV_Target { int mode = (int)(stageControl.z + 0.5f); int blendCount = (int)(stageControl.y + 0.5f); float3 normalizedLightDirection = normalize(signAndBias(input.dot3LightDirection)); float4 color = terrainLayer(0, 1, input.baseUv, normalizedLightDirection, input.vertexLight, input.dot3LightColor); if (mode > 0) { color *= terrainSpecWeight(mode, input); } else { if (blendCount > 0) { float a = sampleTex(4, input.alphaUv1).a; float4 layer = terrainLayer(2, 3, input.baseUv, normalizedLightDirection, input.vertexLight, input.dot3LightColor); color = lerp(color, layer, a); } if (blendCount > 1) { float a = sampleTex(7, input.alphaUv2).a; float4 layer = terrainLayer(5, 6, input.baseUv, normalizedLightDirection, input.vertexLight, input.dot3LightColor); color = lerp(color, layer, a); } if (blendCount > 2) { float a = sampleTex(10, input.alphaUv3).a; float4 layer = terrainLayer(8, 9, input.baseUv, normalizedLightDirection, input.vertexLight, input.dot3LightColor); color = lerp(color, layer, a); } } color *= materialColor; color *= textureFactor; float fogAmount = (fogControl.x > 0.5f) ? saturate(1.0f - exp2(-fogControl.z * input.fogDistance * input.fogDistance)) : 0.0f; color.rgb = lerp(color.rgb, fogColor.rgb, fogAmount); color.a = 1.0f; return saturate(color); }";

		ID3DBlob *terrainPixelShaderBytecode = 0;
		if (!compileShader(terrainDot3ShaderSource, "vsTerrain", "vs_4_0", &ms_terrainDot3VertexShaderBytecode))
			return false;
		if (!compileShader(terrainDot3ShaderSource, "psTerrain", "ps_4_0", &terrainPixelShaderBytecode))
			return false;
		hr = ms_device->CreateVertexShader(ms_terrainDot3VertexShaderBytecode->GetBufferPointer(), ms_terrainDot3VertexShaderBytecode->GetBufferSize(), 0, &ms_terrainDot3VertexShader);
		if (FAILED(hr))
		{
			releaseCom(terrainPixelShaderBytecode);
			return false;
		}
		hr = ms_device->CreatePixelShader(terrainPixelShaderBytecode->GetBufferPointer(), terrainPixelShaderBytecode->GetBufferSize(), 0, &ms_terrainDot3PixelShader);
		releaseCom(terrainPixelShaderBytecode);
		if (FAILED(hr))
			return false;

		static char const *detailBumpShaderSource =
			"cbuffer TransformConstants : register(b0) { row_major float4x4 objectToProjection; row_major float4x4 objectToCamera; row_major float4x4 objectToWorld; row_major float4x4 objectToWorldRotation; float4 textureEnabled; float4 alphaTest; float4 materialAmbientColor; float4 materialColor; float4 materialEmissiveColor; float4 materialSpecularColor; float4 textureFactor; float4 textureFactor2; row_major float4x4 textureTransform; float4 textureTransformControl; float4 fogColor; float4 fogControl; float4 textureScroll; float4 lightAmbient; float4 lightDiffuse; float4 lightDirection; float4 parallelLightDiffuse[3]; float4 parallelLightSpecular[3]; float4 parallelLightDirection[3]; float4 lightControl; float4 lightControl2; float4 materialSourceControl; float4 vertexControl; float4 viewportControl; float4 stageControl; float4 stageColorOp[12]; float4 stageAlphaOp[12]; float4 stageMeta[12]; float4 stageScroll[12]; float4 dot3LightSpecularColor; float4 dot3LightTangentMinusDiffuseColor; float4 dot3LightTangentMinusBackColor; float4 cameraAndMaterialControl; float4 dot3LightLocalDirection; float4 dot3CameraLocalPosition; float4 userVertexConstants[8]; float4 userPixelConstants[16]; };"
			"Texture2D texture0 : register(t0); Texture2D texture1 : register(t1); Texture2D texture2 : register(t2); Texture2D texture3 : register(t3);"
			"SamplerState sampler0 : register(s0); SamplerState sampler1 : register(s1); SamplerState sampler2 : register(s2); SamplerState sampler3 : register(s3);"
			"struct VSInput { float4 position : POSITION; float3 normal : NORMAL; float4 color0 : COLOR0; float4 tex0 : TEXCOORD0; float4 tex1 : TEXCOORD1; float4 tex2 : TEXCOORD2; float4 tex3 : TEXCOORD3; };"
			"struct VSOutput { float4 position : SV_Position; float3 diffuse : COLOR0; float2 uv0 : TEXCOORD0; float2 uv1 : TEXCOORD1; float2 uv2 : TEXCOORD2; float2 uv3 : TEXCOORD3; float3 lightDirectionTangent : TEXCOORD4; float3 normalWorld : TEXCOORD5; float fogDistance : TEXCOORD6; };"
			"float3 signAndBias(float3 v) { return v * 2.0f - 1.0f; }"
			"float3 safeNormalize(float3 v, float3 fallback) { return (dot(v, v) > 0.000001f) ? normalize(v) : fallback; }"
			"VSOutput vsDetailBump(VSInput input) {"
			"VSOutput output;"
			"float4 p = input.position; p.w = (p.w == 0.0f) ? 1.0f : p.w;"
			"output.position = mul(p, objectToProjection);"
			"float4 cameraPosition = mul(p, objectToCamera);"
			"output.fogDistance = length(cameraPosition.xyz);"
			"float3 normalObject = safeNormalize(input.normal, float3(0,1,0));"
			"float3 normalWorld = safeNormalize(mul(normalObject, (float3x3)objectToWorldRotation), float3(0,1,0));"
			"float3 parallelLighting = float3(0,0,0);"
			"float enabledParallelLights = 0.0f;"
			"[unroll] for (int lightIndex = 0; lightIndex < 3; ++lightIndex) {"
			"if (parallelLightDirection[lightIndex].w > 0.5f) {"
			"enabledParallelLights += 1.0f;"
			"float3 lightDir = -safeNormalize(parallelLightDirection[lightIndex].xyz, float3(0,-1,0));"
			"parallelLighting += parallelLightDiffuse[lightIndex].rgb * saturate(dot(normalWorld, lightDir));"
			"}}"
			"if (enabledParallelLights < 0.5f && lightControl.x > 0.5f) {"
			"float3 lightDir = -safeNormalize(lightDirection.xyz, float3(0,-1,0));"
			"parallelLighting = lightDiffuse.rgb * saturate(dot(normalWorld, lightDir));"
			"}"
			"output.diffuse = saturate(lightAmbient.rgb + parallelLighting);"
			"output.uv0 = input.tex0.xy;"
			"output.uv1 = (stageControl.w > 5.5f && stageControl.w < 6.5f) ? input.tex3.xy : input.tex1.xy;"
			"output.uv2 = (stageControl.w > 5.5f && stageControl.w < 6.5f) ? input.tex1.xy : input.tex2.xy;"
			"output.uv3 = input.tex3.xy;"
			"output.normalWorld = normalWorld;"
			"float4 dot3Basis = (stageControl.w > 5.5f && stageControl.w < 7.5f) ? input.tex2 : input.tex3;"
			"float3 tangent = safeNormalize(dot3Basis.xyz, float3(1,0,0));"
			"float3 bitangent = cross(tangent, normalObject) * ((dot3Basis.w == 0.0f) ? 1.0f : dot3Basis.w);"
			"float3x3 tangentBasis = float3x3(tangent, normalObject, bitangent);"
			"float3 objectLightDir = safeNormalize(dot3LightLocalDirection.xyz, float3(0,1,0));"
			"output.lightDirectionTangent = mul(objectLightDir, tangentBasis);"
			"return output;"
			"}"
			"float3 calculateHemisphericLighting(float3 direction, float3 normal, float3 vertexDiffuse) {"
			"float dotProduct = dot(direction, normal);"
			"float3 diffuseColor = (lightControl.x > 0.5f) ? lightDiffuse.rgb : float3(0,0,0);"
			"float3 tangentColor = saturate(lightAmbient.rgb + diffuseColor);"
			"float3 backColor = lightAmbient.rgb;"
			"float3 tangentMinusDiffuse = tangentColor - diffuseColor;"
			"float3 tangentMinusBack = tangentColor - backColor;"
			"float3 light = vertexDiffuse + tangentMinusDiffuse + diffuseColor + (-max(0.0f, dotProduct) * tangentMinusDiffuse);"
			"light += min(0.0f, dotProduct) * tangentMinusBack;"
			"return saturate(light);"
			"}"
			"float4 psDetailBump(VSOutput input) : SV_Target {"
			"int shellMode = (int)(stageControl.w + 0.5f);"
			"float3 diffuseColor = texture0.Sample(sampler0, input.uv0).rgb;"
			"float3 normalTangent = signAndBias(texture1.Sample(sampler1, input.uv1).rgb);"
			"float3 lightDirectionTangent = safeNormalize(input.lightDirectionTangent, float3(0,1,0));"
			"float3 light = calculateHemisphericLighting(lightDirectionTangent, normalTangent, input.diffuse);"
			"float3 rgb = diffuseColor * light;"
			"if (shellMode == 6) rgb *= texture2.Sample(sampler2, input.uv2).rgb;"
			"else if (shellMode == 8) {"
			"float3 detailColorA = texture0.Sample(sampler0, input.uv0).rgb;"
			"float3 detailColorB = texture1.Sample(sampler1, input.uv1).rgb;"
			"float mask = texture2.Sample(sampler2, input.uv2).a;"
			"float3 dirtColor = texture3.Sample(sampler3, input.uv3).rgb;"
			"float3 diffuseBlend = lerp(detailColorB, detailColorA, mask);"
			"float3 worldLight = calculateHemisphericLighting(-safeNormalize(lightDirection.xyz, float3(0,-1,0)), safeNormalize(input.normalWorld, float3(0,1,0)), input.diffuse);"
			"rgb = diffuseBlend * dirtColor * worldLight;"
			"}"
			"float4 output = float4(rgb, (textureEnabled.y > 0.5f) ? textureEnabled.z : 1.0f);"
			"float fogAmount = (fogControl.x > 0.5f) ? saturate(1.0f - exp2(-fogControl.z * input.fogDistance * input.fogDistance)) : 0.0f;"
			"output.rgb = lerp(output.rgb, fogColor.rgb, fogAmount);"
			"return saturate(output);"
			"}";

		ID3DBlob *detailBumpPixelShaderBytecode = 0;
		if (!compileShader(detailBumpShaderSource, "vsDetailBump", "vs_4_0", &ms_detailBumpVertexShaderBytecode))
			return false;
		if (!compileShader(detailBumpShaderSource, "psDetailBump", "ps_4_0", &detailBumpPixelShaderBytecode))
			return false;
		hr = ms_device->CreateVertexShader(ms_detailBumpVertexShaderBytecode->GetBufferPointer(), ms_detailBumpVertexShaderBytecode->GetBufferSize(), 0, &ms_detailBumpVertexShader);
		if (FAILED(hr))
		{
			releaseCom(detailBumpPixelShaderBytecode);
			return false;
		}
		hr = ms_device->CreatePixelShader(detailBumpPixelShaderBytecode->GetBufferPointer(), detailBumpPixelShaderBytecode->GetBufferSize(), 0, &ms_detailBumpPixelShader);
		releaseCom(detailBumpPixelShaderBytecode);
		if (FAILED(hr))
			return false;

		D3D11_BUFFER_DESC constantBufferDesc;
		ZeroMemory(&constantBufferDesc, sizeof(constantBufferDesc));
		constantBufferDesc.ByteWidth = sizeof(TransformConstants);
		constantBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		hr = ms_device->CreateBuffer(&constantBufferDesc, 0, &ms_transformConstantBuffer);
		if (FAILED(hr))
			return false;

		D3D11_SAMPLER_DESC samplerDesc;
		ZeroMemory(&samplerDesc, sizeof(samplerDesc));
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		hr = ms_device->CreateSamplerState(&samplerDesc, &ms_defaultSamplerState);
		if (FAILED(hr))
			return false;

		return true;
	}

	D3D11_COMPARISON_FUNC getD3dComparison(ShaderImplementation::Pass::Compare compare)
	{
		switch (compare)
		{
			case ShaderImplementation::Pass::C_Never:
				return D3D11_COMPARISON_NEVER;
			case ShaderImplementation::Pass::C_Less:
				return D3D11_COMPARISON_LESS;
			case ShaderImplementation::Pass::C_Equal:
				return D3D11_COMPARISON_EQUAL;
			case ShaderImplementation::Pass::C_LessOrEqual:
				return D3D11_COMPARISON_LESS_EQUAL;
			case ShaderImplementation::Pass::C_Greater:
				return D3D11_COMPARISON_GREATER;
			case ShaderImplementation::Pass::C_GreaterOrEqual:
				return D3D11_COMPARISON_NOT_EQUAL;
			case ShaderImplementation::Pass::C_NotEqual:
				return D3D11_COMPARISON_GREATER_EQUAL;
			case ShaderImplementation::Pass::C_Always:
			default:
				return D3D11_COMPARISON_ALWAYS;
		}
	}

	D3D11_BLEND getD3dBlend(ShaderImplementation::Pass::Blend blend)
	{
		switch (blend)
		{
			case ShaderImplementation::Pass::B_Zero:
				return D3D11_BLEND_ZERO;
			case ShaderImplementation::Pass::B_One:
				return D3D11_BLEND_ONE;
			case ShaderImplementation::Pass::B_SourceColor:
				return D3D11_BLEND_SRC_COLOR;
			case ShaderImplementation::Pass::B_InverseSourceColor:
				return D3D11_BLEND_INV_SRC_COLOR;
			case ShaderImplementation::Pass::B_SourceAlpha:
				return D3D11_BLEND_SRC_ALPHA;
			case ShaderImplementation::Pass::B_InverseSourceAlpha:
				return D3D11_BLEND_INV_SRC_ALPHA;
			case ShaderImplementation::Pass::B_DestinationAlpha:
				return D3D11_BLEND_DEST_ALPHA;
			case ShaderImplementation::Pass::B_InverseDestinationAlpha:
				return D3D11_BLEND_INV_DEST_ALPHA;
			case ShaderImplementation::Pass::B_DestinationColor:
				return D3D11_BLEND_DEST_COLOR;
			case ShaderImplementation::Pass::B_InverseDestinationColor:
				return D3D11_BLEND_INV_DEST_COLOR;
			case ShaderImplementation::Pass::B_SourceAlphaSaturate:
			default:
				return D3D11_BLEND_SRC_ALPHA_SAT;
		}
	}

	D3D11_BLEND getAlphaBlend(D3D11_BLEND blend)
	{
		switch (blend)
		{
			case D3D11_BLEND_SRC_COLOR:
				return D3D11_BLEND_SRC_ALPHA;
			case D3D11_BLEND_INV_SRC_COLOR:
				return D3D11_BLEND_INV_SRC_ALPHA;
			case D3D11_BLEND_DEST_COLOR:
				return D3D11_BLEND_DEST_ALPHA;
			case D3D11_BLEND_INV_DEST_COLOR:
				return D3D11_BLEND_INV_DEST_ALPHA;
			default:
				return blend;
		}
	}

	D3D11_BLEND_OP getD3dBlendOp(ShaderImplementation::Pass::BlendOperation blendOperation)
	{
		switch (blendOperation)
		{
			case ShaderImplementation::Pass::BO_Subtract:
				return D3D11_BLEND_OP_SUBTRACT;
			case ShaderImplementation::Pass::BO_ReverseSubtract:
				return D3D11_BLEND_OP_REV_SUBTRACT;
			case ShaderImplementation::Pass::BO_Min:
				return D3D11_BLEND_OP_MIN;
			case ShaderImplementation::Pass::BO_Max:
				return D3D11_BLEND_OP_MAX;
			case ShaderImplementation::Pass::BO_Add:
			default:
				return D3D11_BLEND_OP_ADD;
		}
	}

	UINT8 getD3dColorWriteMask(uint8 writeEnable)
	{
		UINT8 mask = 0;
		if (writeEnable & 0x4)
			mask |= D3D11_COLOR_WRITE_ENABLE_RED;
		if (writeEnable & 0x2)
			mask |= D3D11_COLOR_WRITE_ENABLE_GREEN;
		if (writeEnable & 0x1)
			mask |= D3D11_COLOR_WRITE_ENABLE_BLUE;
		if (writeEnable & 0x8)
			mask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
		return mask;
	}

	D3D11_STENCIL_OP getD3dStencilOp(ShaderImplementation::Pass::StencilOperation op)
	{
		switch (op)
		{
			case ShaderImplementation::Pass::SO_Keep:
				return D3D11_STENCIL_OP_KEEP;
			case ShaderImplementation::Pass::SO_Zero:
				return D3D11_STENCIL_OP_ZERO;
			case ShaderImplementation::Pass::SO_Replace:
				return D3D11_STENCIL_OP_REPLACE;
			case ShaderImplementation::Pass::SO_IncrementSaturate:
				return D3D11_STENCIL_OP_INCR_SAT;
			case ShaderImplementation::Pass::SO_DecrementSaturate:
				return D3D11_STENCIL_OP_DECR_SAT;
			case ShaderImplementation::Pass::SO_Invert:
				return D3D11_STENCIL_OP_INVERT;
			case ShaderImplementation::Pass::SO_IncrementWrap:
				return D3D11_STENCIL_OP_INCR;
			case ShaderImplementation::Pass::SO_DecrementWrap:
				return D3D11_STENCIL_OP_DECR;
			default:
				return D3D11_STENCIL_OP_KEEP;
		}
	}

	D3D11_TEXTURE_ADDRESS_MODE getD3dTextureAddress(StaticShaderTemplate::TextureAddress address)
	{
		switch (address)
		{
			case StaticShaderTemplate::TA_mirror:
				return D3D11_TEXTURE_ADDRESS_MIRROR;
			case StaticShaderTemplate::TA_clamp:
				return D3D11_TEXTURE_ADDRESS_CLAMP;
			case StaticShaderTemplate::TA_border:
				return D3D11_TEXTURE_ADDRESS_BORDER;
			case StaticShaderTemplate::TA_mirrorOnce:
				return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
			case StaticShaderTemplate::TA_wrap:
			case StaticShaderTemplate::TA_invalid:
			default:
				return D3D11_TEXTURE_ADDRESS_WRAP;
		}
	}

	bool isLinearFilter(StaticShaderTemplate::TextureFilter filter)
	{
		return filter == StaticShaderTemplate::TF_linear || filter == StaticShaderTemplate::TF_anisotropic || filter == StaticShaderTemplate::TF_invalid || filter == StaticShaderTemplate::TF_none || filter == StaticShaderTemplate::TF_flatCubic || filter == StaticShaderTemplate::TF_gaussianCubic;
	}

	D3D11_FILTER getD3dFilter(StaticShaderTemplate::TextureFilter minification, StaticShaderTemplate::TextureFilter magnification, StaticShaderTemplate::TextureFilter mip)
	{
		if (minification == StaticShaderTemplate::TF_anisotropic || magnification == StaticShaderTemplate::TF_anisotropic || mip == StaticShaderTemplate::TF_anisotropic)
			return D3D11_FILTER_ANISOTROPIC;

		bool const minLinear = isLinearFilter(minification);
		bool const magLinear = isLinearFilter(magnification);
		bool const mipLinear = mip == StaticShaderTemplate::TF_linear || mip == StaticShaderTemplate::TF_anisotropic || mip == StaticShaderTemplate::TF_invalid;

		if (!minLinear && !magLinear && !mipLinear)
			return D3D11_FILTER_MIN_MAG_MIP_POINT;
		if (!minLinear && !magLinear && mipLinear)
			return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
		if (!minLinear && magLinear && !mipLinear)
			return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
		if (!minLinear && magLinear && mipLinear)
			return D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
		if (minLinear && !magLinear && !mipLinear)
			return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
		if (minLinear && !magLinear && mipLinear)
			return D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
		if (minLinear && magLinear && !mipLinear)
			return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	}

	ID3D11SamplerState *createSamplerState(Direct3d11_StaticShaderData::PassTexture const &passTexture)
	{
		if (!ms_device)
			return 0;

		D3D11_SAMPLER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Filter = getD3dFilter(passTexture.minificationFilter, passTexture.magnificationFilter, passTexture.mipFilter);
		desc.AddressU = getD3dTextureAddress(passTexture.addressU);
		desc.AddressV = getD3dTextureAddress(passTexture.addressV);
		desc.AddressW = getD3dTextureAddress(passTexture.addressW);
		desc.MaxAnisotropy = static_cast<UINT>(passTexture.maxAnisotropy > 1 ? passTexture.maxAnisotropy : 1);
		if (desc.MaxAnisotropy > 16)
			desc.MaxAnisotropy = 16;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		ID3D11SamplerState *samplerState = 0;
		if (FAILED(ms_device->CreateSamplerState(&desc, &samplerState)))
			return 0;
		return samplerState;
	}

	bool bindDefaultPipeline()
	{
		if (!ms_context || !ms_defaultVertexShader || !ms_defaultPixelShader)
			return false;

		bool const useShellEffect = (ms_activePixelProgramMode == 6 || ms_activePixelProgramMode == 7 || ms_activePixelProgramMode == 8);
		ID3D11InputLayout *layout = 0;
		if (ms_activeTerrainDot3 && ms_activeVertexDescriptor)
		{
			VertexBufferFormat format;
			format.setFlags(ms_activeVertexFormatFlags);
			layout = Direct3d11_InputLayoutCache::getTerrainDot3(ms_device, ms_terrainDot3VertexShaderBytecode, *ms_activeVertexDescriptor, format, diag);
		}
		else if (useShellEffect && ms_activeVertexDescriptor)
		{
			VertexBufferFormat format;
			format.setFlags(ms_activeVertexFormatFlags);
			layout = Direct3d11_InputLayoutCache::getDetailBump(ms_device, ms_detailBumpVertexShaderBytecode, *ms_activeVertexDescriptor, format, diag);
		}
		else if (ms_activeVertexVector)
			layout = Direct3d11_InputLayoutCache::getVertexVector(ms_device, ms_defaultVertexShaderBytecode, ms_activeTextureCoordinateSet, ms_activeVertexStreamCount, ms_activeVertexStreamDescriptors, ms_activeVertexStreamBuffers, ms_activeVertexStreamFormatFlags, diag);
		else if (ms_activeVertexDescriptor)
		{
			VertexBufferFormat format;
			format.setFlags(ms_activeVertexFormatFlags);
			layout = Direct3d11_InputLayoutCache::getDefault(ms_device, ms_defaultVertexShaderBytecode, *ms_activeVertexDescriptor, format, ms_activeTextureCoordinateSet, diag);
		}
		if (!layout)
			return false;

		updateTransformConstants();
		ms_context->IASetInputLayout(layout);
		ms_context->VSSetShader(ms_activeTerrainDot3 ? ms_terrainDot3VertexShader : (useShellEffect ? ms_detailBumpVertexShader : ms_defaultVertexShader), 0, 0);
		bool const useCubeSky = ms_activeTextureCube && ms_activePixelProgramMode == 1 && ms_cubePixelShader;
		ms_context->PSSetShader(useCubeSky ? ms_cubePixelShader : (ms_activeTerrainDot3 ? ms_terrainDot3PixelShader : (useShellEffect ? ms_detailBumpPixelShader : ms_defaultPixelShader)), 0, 0);
		ms_context->VSSetConstantBuffers(0, 1, &ms_transformConstantBuffer);
		ms_context->PSSetConstantBuffers(0, 1, &ms_transformConstantBuffer);
		ID3D11ShaderResourceView *shaderResources[cms_maxStageTextures];
		shaderResources[0] = useCubeSky ? 0 : ((ms_activeTerrainDot3 || useShellEffect || ms_activeStageTextureCount > 0) ? ms_activeStageTextureViews[0] : ms_activeTextureView);
		for (int i = 1; i < cms_maxStageTextures; ++i)
			shaderResources[i] = ms_activeStageTextureViews[i];
		ms_context->PSSetShaderResources(0, cms_maxStageTextures, shaderResources);
		ID3D11ShaderResourceView *cubeResource = useCubeSky ? ms_activeTextureView : 0;
		ms_context->PSSetShaderResources(12, 1, &cubeResource);
		ID3D11SamplerState *samplerState = ms_activeSamplerState ? ms_activeSamplerState : ms_defaultSamplerState;
		ID3D11SamplerState *samplerStates[cms_maxStageTextures];
		samplerStates[0] = (ms_activeTerrainDot3 || useShellEffect || ms_activeStageTextureCount > 0) ? (ms_activeStageSamplerStates[0] ? ms_activeStageSamplerStates[0] : ms_defaultSamplerState) : samplerState;
		for (int i = 1; i < cms_maxStageTextures; ++i)
			samplerStates[i] = ms_activeStageSamplerStates[i] ? ms_activeStageSamplerStates[i] : ms_defaultSamplerState;
		ms_context->PSSetSamplers(0, cms_maxStageTextures, samplerStates);
		return true;
	}

	VertexBufferDescriptor const &getVertexBufferDescriptor(VertexBufferFormat const &vertexFormat)
	{
		if (!ms_vertexBufferDescriptorMap)
			ms_vertexBufferDescriptorMap = new VertexBufferDescriptorMap;

		VertexBufferDescriptorMap::iterator iter = ms_vertexBufferDescriptorMap->find(vertexFormat.getFlags());
		if (iter == ms_vertexBufferDescriptorMap->end())
		{
			VertexBufferDescriptor descriptor;

			if (vertexFormat.hasPosition())
			{
				descriptor.offsetPosition = descriptor.vertexSize;
				descriptor.vertexSize = static_cast<int8>(descriptor.vertexSize + sizeof(float) * 3);

				if (vertexFormat.isTransformed())
				{
					descriptor.offsetOoz = descriptor.vertexSize;
					descriptor.vertexSize = static_cast<int8>(descriptor.vertexSize + sizeof(float));
				}
				else
					descriptor.offsetOoz = -1;
			}
			else
			{
				DEBUG_FATAL(vertexFormat.isTransformed(), ("Transformed data requires XYZ as well"));
				descriptor.offsetPosition = -1;
			}

			if (vertexFormat.hasNormal())
			{
				descriptor.offsetNormal = descriptor.vertexSize;
				descriptor.vertexSize = static_cast<int8>(descriptor.vertexSize + sizeof(float) * 3);
			}
			else
				descriptor.offsetNormal = -1;

			if (vertexFormat.hasPointSize())
			{
				descriptor.offsetPointSize = descriptor.vertexSize;
				descriptor.vertexSize = static_cast<int8>(descriptor.vertexSize + sizeof(float));
			}
			else
				descriptor.offsetPointSize = -1;

			if (vertexFormat.hasColor0())
			{
				descriptor.offsetColor0 = descriptor.vertexSize;
				descriptor.vertexSize = static_cast<int8>(descriptor.vertexSize + sizeof(uint32));
			}
			else
				descriptor.offsetColor0 = -1;

			if (vertexFormat.hasColor1())
			{
				descriptor.offsetColor1 = descriptor.vertexSize;
				descriptor.vertexSize = static_cast<int8>(descriptor.vertexSize + sizeof(uint32));
			}
			else
				descriptor.offsetColor1 = -1;

			int const numberOfTextureCoordinateSets = vertexFormat.getNumberOfTextureCoordinateSets();
			for (int i = 0; i < numberOfTextureCoordinateSets; ++i)
			{
				int const dimension = vertexFormat.getTextureCoordinateSetDimension(i);
				descriptor.offsetTextureCoordinateSet[i] = descriptor.vertexSize;
				descriptor.vertexSize = static_cast<int8>(descriptor.vertexSize + sizeof(float) * dimension);
			}
			for (int i = numberOfTextureCoordinateSets; i < VertexBufferFormat::MAX_TEXTURE_COORDINATE_SETS; ++i)
				descriptor.offsetTextureCoordinateSet[i] = -1;

			DEBUG_FATAL(descriptor.vertexSize == 0, ("Vertex has no data"));

			VertexBufferDescriptorMap::value_type const entry(vertexFormat.getFlags(), descriptor);
			iter = ms_vertexBufferDescriptorMap->insert(entry).first;
		}

		return iter->second;
	}

	class Direct3d11_StaticVertexBufferData : public StaticVertexBufferGraphicsData
	{
	public:
		explicit Direct3d11_StaticVertexBufferData(StaticVertexBuffer const &vertexBuffer)
		: m_descriptor(getVertexBufferDescriptor(vertexBuffer.getFormat())),
		  m_owner(&vertexBuffer),
		  m_formatFlags(vertexBuffer.getFormat().getFlags()),
		  m_sortKey(++ms_nextSortKey),
		  m_vertexCount(vertexBuffer.getNumberOfVertices()),
		  m_data(static_cast<size_t>(vertexBuffer.getNumberOfVertices()) * static_cast<size_t>(m_descriptor.vertexSize)),
		  m_buffer(0)
		{
		}

		virtual ~Direct3d11_StaticVertexBufferData()
		{
			if (ms_staticVertexBufferDataMap)
				ms_staticVertexBufferDataMap->erase(m_owner);
			releaseCom(m_buffer);
		}

		virtual VertexBufferDescriptor const &getDescriptor() const
		{
			return m_descriptor;
		}

		virtual uintptr_t getSortKey()
		{
			return m_sortKey;
		}

		virtual void *lock(bool)
		{
			return m_data.empty() ? 0 : &m_data[0];
		}

		virtual void unlock()
		{
			upload();
		}

		void upload()
		{
			if (m_data.empty() || !ms_device)
				return;

			if (!m_buffer)
				m_buffer = createBuffer(D3D11_BIND_VERTEX_BUFFER, &m_data[0], m_data.size());
			else if (ms_context)
				ms_context->UpdateSubresource(m_buffer, 0, 0, &m_data[0], 0, 0);
		}

		ID3D11Buffer *getBuffer()
		{
			if (!m_buffer)
				upload();
			return m_buffer;
		}

		UINT getStride() const
		{
			return static_cast<UINT>(m_descriptor.vertexSize);
		}

		VertexBufferDescriptor const &getDescriptorRef() const
		{
			return m_descriptor;
		}

		uint32 getFormatFlags() const
		{
			return m_formatFlags;
		}

		byte const *getData() const
		{
			return m_data.empty() ? 0 : &m_data[0];
		}

		int getVertexCount() const
		{
			return m_vertexCount;
		}

	private:
		VertexBufferDescriptor const &m_descriptor;
		StaticVertexBuffer const *m_owner;
		uint32 m_formatFlags;
		int m_sortKey;
		int m_vertexCount;
		std::vector<byte> m_data;
		ID3D11Buffer *m_buffer;
	};

	class Direct3d11_DynamicVertexBufferData : public DynamicVertexBufferGraphicsData
	{
	public:
		explicit Direct3d11_DynamicVertexBufferData(DynamicVertexBuffer const &vertexBuffer)
		: m_descriptor(getVertexBufferDescriptor(vertexBuffer.getFormat())),
		  m_owner(&vertexBuffer),
		  m_formatFlags(vertexBuffer.getFormat().getFlags()),
		  m_sortKey(++ms_nextSortKey),
		  m_numberOfVertices(0),
		  m_buffer(0),
		  m_bufferSize(0)
		{
		}

		virtual ~Direct3d11_DynamicVertexBufferData()
		{
			if (ms_dynamicVertexBufferDataMap)
				ms_dynamicVertexBufferDataMap->erase(m_owner);
			releaseCom(m_buffer);
		}

		virtual void *lock(int numberOfVertices, bool)
		{
			m_numberOfVertices = numberOfVertices;
			m_data.resize(static_cast<size_t>(m_numberOfVertices) * static_cast<size_t>(m_descriptor.vertexSize));
			return m_data.empty() ? 0 : &m_data[0];
		}

		virtual void unlock()
		{
			upload();
		}

		virtual void unlock(int numberOfVertices)
		{
			m_numberOfVertices = numberOfVertices;
			m_data.resize(static_cast<size_t>(m_numberOfVertices) * static_cast<size_t>(m_descriptor.vertexSize));
			upload();
		}

		virtual VertexBufferDescriptor const &getDescriptor() const
		{
			return m_descriptor;
		}

		virtual int getNumberOfLockableDynamicVertices(bool)
		{
			return m_descriptor.vertexSize > 0 ? (2 * 1024 * 1024) / m_descriptor.vertexSize : 0;
		}

		virtual uintptr_t getSortKey()
		{
			return m_sortKey;
		}

		void upload()
		{
			if (m_data.empty() || !ms_device)
				return;

			if (!m_buffer || m_data.size() > m_bufferSize)
			{
				releaseCom(m_buffer);
				m_bufferSize = m_data.size();
				m_buffer = createBuffer(D3D11_BIND_VERTEX_BUFFER, &m_data[0], m_bufferSize);
			}
			else if (ms_context)
			{
				ms_context->UpdateSubresource(m_buffer, 0, 0, &m_data[0], 0, 0);
			}
		}

		ID3D11Buffer *getBuffer()
		{
			if (!m_buffer)
				upload();
			return m_buffer;
		}

		UINT getStride() const
		{
			return static_cast<UINT>(m_descriptor.vertexSize);
		}

		VertexBufferDescriptor const &getDescriptorRef() const
		{
			return m_descriptor;
		}

		uint32 getFormatFlags() const
		{
			return m_formatFlags;
		}

		byte const *getData() const
		{
			return m_data.empty() ? 0 : &m_data[0];
		}

		int getVertexCount() const
		{
			return m_numberOfVertices;
		}

	private:
		VertexBufferDescriptor const &m_descriptor;
		DynamicVertexBuffer const *m_owner;
		uint32 m_formatFlags;
		int m_sortKey;
		int m_numberOfVertices;
		std::vector<byte> m_data;
		ID3D11Buffer *m_buffer;
		size_t m_bufferSize;
	};

	class Direct3d11_StaticIndexBufferData : public StaticIndexBufferGraphicsData
	{
	public:
		explicit Direct3d11_StaticIndexBufferData(StaticIndexBuffer const &indexBuffer)
		: m_owner(&indexBuffer),
		  m_indexCount(indexBuffer.getNumberOfIndices()),
		  m_data(static_cast<size_t>(indexBuffer.getNumberOfIndices())),
		  m_buffer(0)
		{
		}

		virtual ~Direct3d11_StaticIndexBufferData()
		{
			if (ms_staticIndexBufferDataMap)
				ms_staticIndexBufferDataMap->erase(m_owner);
			releaseCom(m_buffer);
		}

		virtual Index *lock(bool)
		{
			return m_data.empty() ? 0 : &m_data[0];
		}

		virtual void unlock()
		{
			upload();
		}

		void upload()
		{
			if (m_data.empty() || !ms_device)
				return;

			if (!m_buffer)
				m_buffer = createBuffer(D3D11_BIND_INDEX_BUFFER, &m_data[0], m_data.size() * sizeof(Index));
			else if (ms_context)
				ms_context->UpdateSubresource(m_buffer, 0, 0, &m_data[0], 0, 0);
		}

		ID3D11Buffer *getBuffer()
		{
			if (!m_buffer)
				upload();
			return m_buffer;
		}

		int getIndexCount() const
		{
			return m_indexCount;
		}

		Index const *getIndexData() const
		{
			return m_data.empty() ? 0 : &m_data[0];
		}

	private:
		StaticIndexBuffer const *m_owner;
		int m_indexCount;
		std::vector<Index> m_data;
		ID3D11Buffer *m_buffer;
	};

	class Direct3d11_DynamicIndexBufferData : public DynamicIndexBufferGraphicsData
	{
	public:
		explicit Direct3d11_DynamicIndexBufferData(DynamicIndexBuffer const *owner)
		: m_owner(owner),
		  m_indexCount(0),
		  m_capacity(0),
		  m_buffer(0)
		{
		}

		virtual ~Direct3d11_DynamicIndexBufferData()
		{
			if (ms_dynamicIndexBufferDataMap && m_owner)
				ms_dynamicIndexBufferDataMap->erase(m_owner);
			releaseCom(m_buffer);
		}

		virtual Index *lock(int numberOfIndices)
		{
			ms_lastDynamicIndexBufferData = this;
			if (numberOfIndices < 0)
				numberOfIndices = 0;
			m_indexCount = numberOfIndices;
			m_data.resize(static_cast<size_t>(numberOfIndices));
			return m_data.empty() ? 0 : &m_data[0];
		}

		virtual void unlock()
		{
			upload();
		}

		void upload()
		{
			if (m_data.empty() || !ms_device)
				return;

			int const desiredCapacity = std::max(m_indexCount, ms_dynamicIndexBufferCapacity);
			if (!m_buffer || desiredCapacity > m_capacity)
			{
				releaseCom(m_buffer);
				m_capacity = desiredCapacity;
				m_buffer = createEmptyIndexBuffer(m_capacity);
			}

			if (m_buffer && ms_context)
			{
				D3D11_BOX box;
				ZeroMemory(&box, sizeof(box));
				box.right = static_cast<UINT>(m_indexCount * sizeof(Index));
				box.bottom = 1;
				box.back = 1;
				ms_context->UpdateSubresource(m_buffer, 0, &box, &m_data[0], 0, 0);
			}
		}

		ID3D11Buffer *getBuffer()
		{
			if (!m_buffer)
				upload();
			return m_buffer;
		}

		int getIndexCount() const
		{
			return m_indexCount;
		}

		Index const *getIndexData() const
		{
			return m_data.empty() ? 0 : &m_data[0];
		}

	private:
		DynamicIndexBuffer const *m_owner;
		int m_indexCount;
		int m_capacity;
		std::vector<Index> m_data;
		ID3D11Buffer *m_buffer;
	};

	class Direct3d11_VertexShaderData : public ShaderImplementationPassVertexShaderGraphicsData
	{
	};

	class Direct3d11_PixelShaderProgramData : public ShaderImplementationPassPixelShaderProgramGraphicsData
	{
	};

	bool verify();
	bool install(Gl_install *gl_install);
	void remove();
	void displayModeChanged();
	int getShaderCapability();
	bool requiresVertexAndPixelShaders();
	void getOtherAdapterRects(stdvector<RECT>::fwd &);
	int getVideoMemoryInMegabytes();
	void flushResources(bool);
	bool isGdiVisible();
	bool wasDeviceReset();
	void addDeviceLostCallback(Gl_api::CallbackFunction);
	void removeDeviceLostCallback(Gl_api::CallbackFunction);
	void addDeviceRestoredCallback(Gl_api::CallbackFunction);
	void removeDeviceRestoredCallback(Gl_api::CallbackFunction);
	bool supportsMipmappedCubeMaps();
	bool supportsScissorRect();
	bool supportsHardwareMouseCursor();
	bool supportsTwoSidedStencil();
	bool supportsStreamOffsets();
	bool supportsDynamicTextures();
	void setBrightnessContrastGamma(float, float, float);
	void resize(int newWidth, int newHeight);
	void setWindowedMode(bool windowed);
	void setFillMode(GlFillMode);
	void setCullMode(GlCullMode);
	void setPointSize(real);
	void setPointSizeMax(real);
	void setPointSizeMin(real);
	void setPointScaleEnable(bool);
	void setPointScaleFactor(real, real, real);
	void setPointSpriteEnable(bool);
	void clearViewport(bool clearColor, uint32 colorValue, bool clearDepth, real depthValue, bool clearStencil, uint32 stencilValue);
	void update(float);
	void beginScene();
	void endScene();
	bool lockBackBuffer(Gl_pixelRect &, const RECT *);
	bool unlockBackBuffer();
	bool present();
	bool presentToWindow(HWND, int, int);
	bool vrIsWorldRenderingEnabled();
	bool vrBeginWorldFrame();
	bool vrGetEyeInfo(int eye, Gl_vrEyeInfo *eyeInfo);
	bool vrBeginEye(int eye);
	void vrEndEye(int eye);
	void vrEndWorldFrame();
	void vrEndWorldFrameWithMenuQuad();
	bool vrBeginHudCapture(int *captureWidth, int *captureHeight);
	void vrEndHudCapture();
	bool vrBeginWristDashboardCapture(int *captureWidth, int *captureHeight);
	void vrEndWristDashboardCapture();
	void vrSubmitHudPanelRect(const char *panelName, int left, int top, int right, int bottom, int sourceWidth, int sourceHeight);
	void vrSubmitHudPanelAnchor(const char *panelName, float x, float y, float z, float radius, bool valid);
	void vrSubmitObjectContextInputRegion(int slot, int textureLeft, int textureTop, int textureRight, int textureBottom, int clientLeft, int clientTop, int clientRight, int clientBottom, bool active);
	void vrSubmitObjectContext(const char *targetName, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool attackable);
	void vrSubmitHoverTargetContext(const char *targetName, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool attackable);
	void vrSubmitWristDashboard(float playerX, float playerZ, float headingRadians, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool valid);
	void bindDefaultRenderTarget();
	void setRenderTarget(Texture *, CubeFace, int);
	bool copyRenderTargetToNonRenderTargetTexture();
	bool screenShot(GlScreenShotFormat, int, const char *);
	ShaderImplementationGraphicsData *createShaderImplementationGraphicsData(const ShaderImplementation &);
	StaticShaderGraphicsData *createStaticShaderGraphicsData(const StaticShader &);
	void setBadVertexShaderStaticShader(const StaticShader *);
	void setStaticShader(const StaticShader &, int);
	bool setMouseCursor(const Texture &, int, int);
	bool showMouseCursor(bool);
	void setViewport(int x, int y, int width, int height, real minZ, real maxZ);
	void setScissorRect(bool, int, int, int, int);
	void setWorldToCameraTransform(const Transform &, const Vector &);
	void setProjectionMatrix(const GlMatrix4x4 &);
	void setFog(bool, real, const PackedArgb &);
	void setObjectToWorldTransformAndScale(const Transform &, const Vector &);
	void setGlobalTexture(Tag, const Texture &);
	void releaseAllGlobalTextures();
	void setTextureTransform(int, bool, int, bool, const real *);
	void setVertexShaderUserConstants(int, float, float, float, float);
	void setPixelShaderUserConstants(VectorRgba const *, int);
	void setAlphaFadeOpacity(bool, float);
	void updateLightsFromCurrentList();
	void setLights(const stdvector<const Light*>::fwd &);
	StaticVertexBufferGraphicsData *createStaticVertexBufferData(const StaticVertexBuffer &);
	DynamicVertexBufferGraphicsData *createDynamicVertexBufferData(const DynamicVertexBuffer &);
	VertexBufferVectorGraphicsData *createVertexBufferVectorData(VertexBufferVector const &);
	void setVertexBuffer(HardwareVertexBuffer const &);
	void setVertexBufferVector(VertexBufferVector const &);
	StaticIndexBufferGraphicsData *createStaticIndexBufferData(const StaticIndexBuffer &);
	DynamicIndexBufferGraphicsData *createDynamicIndexBufferData();
	void setIndexBuffer(const HardwareIndexBuffer &);
	void setDynamicIndexBufferSize(int);
	void getOneToOneUVMapping(int textureWidth, int textureHeight, real &u0, real &v0, real &u1, real &v1);
	TextureGraphicsData *createTextureData(const Texture &, const TextureFormat *, int);
	ShaderImplementationPassVertexShaderGraphicsData *createVertexShaderData(ShaderImplementationPassVertexShader const &);
	ShaderImplementationPassPixelShaderProgramGraphicsData *createPixelShaderProgramData(ShaderImplementationPassPixelShaderProgram const &);
	void drawPointList();
	void drawLineList();
	void drawLineStrip();
	void drawTriangleList();
	void drawTriangleStrip();
	void drawTriangleFan();
	void drawQuadList();
	void drawIndexedPointList();
	void drawIndexedLineList();
	void drawIndexedLineStrip();
	void drawIndexedTriangleList();
	void drawIndexedTriangleStrip();
	void drawIndexedTriangleFan();
	void drawPartialPointList(int, int);
	void drawPartialLineList(int, int);
	void drawPartialLineStrip(int, int);
	void drawPartialTriangleList(int, int);
	void drawPartialTriangleStrip(int, int);
	void drawPartialTriangleFan(int, int);
	void drawPartialIndexedPointList(int, int, int, int, int);
	void drawPartialIndexedLineList(int, int, int, int, int);
	void drawPartialIndexedLineStrip(int, int, int, int, int);
	void drawPartialIndexedTriangleList(int, int, int, int, int);
	void drawPartialIndexedTriangleStrip(int, int, int, int, int);
	void drawPartialIndexedTriangleFan(int, int, int, int, int);
	void optimizeIndexBuffer(WORD *, int);
	int getMaximumVertexBufferStreamCount();
	void setBloomEnabled(bool);
	void pixSetMarker(WCHAR const *);
	void pixBeginEvent(WCHAR const *);
	void pixEndEvent(WCHAR const *);
	bool writeImage(char const *, int const, int const, int const, int const *, bool const, Gl_imageFormat const, Rectangle2d const *);
	bool supportsAntialias();
	void setAntialiasEnabled(bool);

#if PRODUCTION == 0
	bool createVideoBuffers(int, int);
	void fillVideoBuffers();
	bool getVideoBufferData(void *, size_t);
	void releaseVideoBuffers();
#endif

	bool isDiagnosticsEnabled()
	{
		return Direct3d11_Diagnostics::isEnabled();
	}

	void diag(char const *format, ...)
	{
		va_list args;
		va_start(args, format);
		Direct3d11_Diagnostics::writeVa(format, args);
		va_end(args);
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
			diag("show windowed hwnd=0x%p client=%dx%d window=%dx%d pos=%d,%d", ms_window, ms_width, ms_height, windowWidth, windowHeight, ms_windowX, ms_windowY);
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
			diag("show fullscreen hwnd=0x%p size=%dx%d pos=%ld,%ld", ms_window, ms_width, ms_height, r.left, r.top);
		}
	}

	void resetFrameDiagnostics()
	{
		ms_clearColorCount = 0;
		ms_clearDepthCount = 0;
		ms_beginSceneCount = 0;
		ms_endSceneCount = 0;
		ms_shaderSetCount = 0;
		ms_stageReplayShaderSetCount = 0;
		ms_stageReplayMaxStageCount = 0;
		ms_stageReplayLogCount = 0;
		ms_coordinateGenerationShaderSetCount = 0;
		ms_coordinateGenerationLogCount = 0;
		ms_terrainDot3ShaderSetCount = 0;
		ms_terrainDot3LogCount = 0;
		ms_terrainDot3DrawLogCount = 0;
		ms_skyPixelProgramLogCount = 0;
		ms_staticShaderLogCount = 0;
		ms_staticDrawLogCount = 0;
		ms_mobileDrawLogCount = 0;
		ms_shadowBlobDrawLogCount = 0;
		ms_uiShaderLogCount = 0;
		ms_rendererInventoryLogCount = 0;
		ms_rendererVectorInventoryLogCount = 0;
		ms_rendererInventoryEnabled = -1;
		ms_rendererInventoryLimit = -1;
		ms_rendererInventoryStartFrame = -1;
		ms_activeStaticShaderName[0] = '\0';
		ms_activeVertexProgramName[0] = '\0';
		ms_activePixelProgramName[0] = '\0';
		ms_activeTextureTag = 0;
		ms_activeTextureNativeFormat = -1;
		ms_fogSetCount = 0;
		ms_fogEnabledSetCount = 0;
		ms_fogLogCount = 0;
		ms_lightSetCount = 0;
		ms_lightNonEmptySetCount = 0;
		ms_lightLogCount = 0;
		ms_vertexShaderUserConstantSetCount = 0;
		ms_pixelShaderUserConstantSetCount = 0;
		ms_gammaSetCount = 0;
		ms_bloomSetCount = 0;
		ms_vertexBufferSetCount = 0;
		ms_indexBufferSetCount = 0;
		ms_drawCallCount = 0;
		ms_drawIndexedCallCount = 0;
		ms_drawVertexCount = 0;
		ms_drawIndexCount = 0;
		ms_drawSkippedNoContext = 0;
		ms_drawSkippedNoVertexBuffer = 0;
		ms_drawSkippedNoIndexBuffer = 0;
		ms_drawSkippedNoPipeline = 0;
		ms_scissorEnableCount = 0;
		ms_scissorDisableCount = 0;
		ms_scissorLogCount = 0;
		ms_transformedDrawCount = 0;
		ms_transformedDrawLogCount = 0;
	}

	void logFrameDiagnostics(char const *label)
	{
		diag("%s presents=%d clears=%d/%d scene=%d/%d shaders=%d stageReplay=%d maxStage=%d coordGen=%d terrainDot3=%d fog=%d enabled=%d lights=%d nonEmpty=%d userVS/PS=%d/%d gamma=%d bloom=%d vb=%d ib=%d draws=%d indexed=%d verts=%d indices=%d transformed=%d scissor=%d/%d skips ctx/vb/ib/pipe=%d/%d/%d/%d",
			label,
			ms_presentCount,
			ms_clearColorCount,
			ms_clearDepthCount,
			ms_beginSceneCount,
			ms_endSceneCount,
			ms_shaderSetCount,
			ms_stageReplayShaderSetCount,
			ms_stageReplayMaxStageCount,
			ms_coordinateGenerationShaderSetCount,
			ms_terrainDot3ShaderSetCount,
			ms_fogSetCount,
			ms_fogEnabledSetCount,
			ms_lightSetCount,
			ms_lightNonEmptySetCount,
			ms_vertexShaderUserConstantSetCount,
			ms_pixelShaderUserConstantSetCount,
			ms_gammaSetCount,
			ms_bloomSetCount,
			ms_vertexBufferSetCount,
			ms_indexBufferSetCount,
			ms_drawCallCount,
			ms_drawIndexedCallCount,
			ms_drawVertexCount,
			ms_drawIndexCount,
			ms_transformedDrawCount,
			ms_scissorEnableCount,
			ms_scissorDisableCount,
			ms_drawSkippedNoContext,
			ms_drawSkippedNoVertexBuffer,
			ms_drawSkippedNoIndexBuffer,
			ms_drawSkippedNoPipeline);
	}

	bool createBackBufferViews()
	{
		releaseCom(ms_backBufferView);
		releaseCom(ms_depthView);
		releaseCom(ms_depthTexture);

		ID3D11Texture2D *backBuffer = 0;
		HRESULT hr = ms_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backBuffer));
		if (FAILED(hr))
			return false;

		hr = ms_device->CreateRenderTargetView(backBuffer, 0, &ms_backBufferView);
		releaseCom(backBuffer);
		if (FAILED(hr))
			return false;

		D3D11_TEXTURE2D_DESC depthDesc;
		ZeroMemory(&depthDesc, sizeof(depthDesc));
		depthDesc.Width = static_cast<UINT>(ms_width);
		depthDesc.Height = static_cast<UINT>(ms_height);
		depthDesc.MipLevels = 1;
		depthDesc.ArraySize = 1;
		depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthDesc.SampleDesc.Count = 1;
		depthDesc.Usage = D3D11_USAGE_DEFAULT;
		depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		hr = ms_device->CreateTexture2D(&depthDesc, 0, &ms_depthTexture);
		if (FAILED(hr))
			return false;

		hr = ms_device->CreateDepthStencilView(ms_depthTexture, 0, &ms_depthView);
		if (FAILED(hr))
			return false;

		ms_currentRenderTargetView = ms_backBufferView;
		ms_currentDepthView = ms_depthView;
		ms_context->OMSetRenderTargets(1, &ms_currentRenderTargetView, ms_currentDepthView);
		setViewport(0, 0, ms_width, ms_height, CONST_REAL(0), CONST_REAL(1));
		return true;
	}

	void releaseVrEyeTargets()
	{
		releaseCom(ms_vrEyeRenderTargetViews[0]);
		releaseCom(ms_vrEyeRenderTargetViews[1]);
		releaseCom(ms_vrEyeDepthViews[0]);
		releaseCom(ms_vrEyeDepthViews[1]);
		releaseCom(ms_vrEyeDepthTexture);
		ms_vrEyeTexture = 0;
		ms_vrEyeTargetWidth = 0;
		ms_vrEyeTargetHeight = 0;
		ms_activeVrEye = -1;
	}

	bool ensureVrEyeTargets(ID3D11Texture2D *eyeTexture, int width, int height)
	{
		if (!ms_device || !eyeTexture || width <= 0 || height <= 0)
		{
			Direct3d11_VrBridge::logD3dEyeTarget("invalid-input", -1, false, 0, eyeTexture, width, height, 0, 0, 0);
			return false;
		}
		if (ms_vrEyeTexture == eyeTexture && ms_vrEyeRenderTargetViews[0] && ms_vrEyeRenderTargetViews[1] && ms_vrEyeDepthViews[0] && ms_vrEyeDepthViews[1] && ms_vrEyeTargetWidth == width && ms_vrEyeTargetHeight == height)
			return true;

		releaseVrEyeTargets();

		D3D11_TEXTURE2D_DESC eyeDesc;
		eyeTexture->GetDesc(&eyeDesc);
		if (eyeDesc.ArraySize < 2)
		{
			Direct3d11_VrBridge::logD3dEyeTarget("bad-array", -1, false, 0, eyeTexture, width, height, static_cast<unsigned int>(eyeDesc.Format), eyeDesc.ArraySize, eyeDesc.BindFlags);
			return false;
		}

		for (int eye = 0; eye < 2; ++eye)
		{
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
			ZeroMemory(&rtvDesc, sizeof(rtvDesc));
			rtvDesc.Format = eyeDesc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM : eyeDesc.Format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rtvDesc.Texture2DArray.MipSlice = 0;
			rtvDesc.Texture2DArray.FirstArraySlice = static_cast<UINT>(eye);
			rtvDesc.Texture2DArray.ArraySize = 1;
			HRESULT const hr = ms_device->CreateRenderTargetView(eyeTexture, &rtvDesc, &ms_vrEyeRenderTargetViews[eye]);
			if (FAILED(hr))
			{
				Direct3d11_VrBridge::logD3dEyeTarget("create-rtv", eye, false, static_cast<unsigned int>(hr), eyeTexture, width, height, static_cast<unsigned int>(eyeDesc.Format), eyeDesc.ArraySize, eyeDesc.BindFlags);
				releaseVrEyeTargets();
				return false;
			}
		}

		D3D11_TEXTURE2D_DESC depthDesc;
		ZeroMemory(&depthDesc, sizeof(depthDesc));
		depthDesc.Width = static_cast<UINT>(width);
		depthDesc.Height = static_cast<UINT>(height);
		depthDesc.MipLevels = 1;
		depthDesc.ArraySize = 2;
		depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthDesc.SampleDesc.Count = 1;
		depthDesc.Usage = D3D11_USAGE_DEFAULT;
		depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		HRESULT const depthTextureHr = ms_device->CreateTexture2D(&depthDesc, 0, &ms_vrEyeDepthTexture);
		if (FAILED(depthTextureHr))
		{
			Direct3d11_VrBridge::logD3dEyeTarget("create-depth-texture", -1, false, static_cast<unsigned int>(depthTextureHr), eyeTexture, width, height, static_cast<unsigned int>(eyeDesc.Format), eyeDesc.ArraySize, eyeDesc.BindFlags);
			releaseVrEyeTargets();
			return false;
		}

		for (int eye = 0; eye < 2; ++eye)
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
			ZeroMemory(&dsvDesc, sizeof(dsvDesc));
			dsvDesc.Format = depthDesc.Format;
			dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.Texture2DArray.FirstArraySlice = static_cast<UINT>(eye);
			dsvDesc.Texture2DArray.ArraySize = 1;
			HRESULT const hr = ms_device->CreateDepthStencilView(ms_vrEyeDepthTexture, &dsvDesc, &ms_vrEyeDepthViews[eye]);
			if (FAILED(hr))
			{
				Direct3d11_VrBridge::logD3dEyeTarget("create-dsv", eye, false, static_cast<unsigned int>(hr), eyeTexture, width, height, static_cast<unsigned int>(eyeDesc.Format), eyeDesc.ArraySize, eyeDesc.BindFlags);
				releaseVrEyeTargets();
				return false;
			}
		}

		ms_vrEyeTexture = eyeTexture;
		ms_vrEyeTargetWidth = width;
		ms_vrEyeTargetHeight = height;
		Direct3d11_VrBridge::logD3dEyeTarget("ready", -1, true, 0, eyeTexture, width, height, static_cast<unsigned int>(eyeDesc.Format), eyeDesc.ArraySize, eyeDesc.BindFlags);
		return true;
	}

	D3D11_FILL_MODE getD3dFillMode(GlFillMode fillMode)
	{
		switch (fillMode)
		{
			case GFM_wire:
				return D3D11_FILL_WIREFRAME;

			case GFM_solid:
			default:
				return D3D11_FILL_SOLID;
		}
	}

	D3D11_CULL_MODE getD3dCullMode(GlCullMode cullMode)
	{
		switch (cullMode)
		{
			case GCM_none:
				return D3D11_CULL_NONE;

			case GCM_clockwise:
				return D3D11_CULL_FRONT;

			case GCM_counterClockwise:
			default:
				return D3D11_CULL_BACK;
		}
	}

	bool applyRasterizerState()
	{
		if (!ms_device || !ms_context)
			return false;

		D3D11_RASTERIZER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.FillMode = getD3dFillMode(ms_fillMode);
		desc.CullMode = getD3dCullMode(ms_cullMode);
		desc.FrontCounterClockwise = FALSE;
		desc.DepthClipEnable = TRUE;
		desc.ScissorEnable = ms_scissorEnabled ? TRUE : FALSE;

		if (!ms_rasterizerStateMap)
			ms_rasterizerStateMap = new RasterizerStateMap;

		RasterizerStateMap::iterator found = ms_rasterizerStateMap->find(desc);
		if (found != ms_rasterizerStateMap->end())
			ms_rasterizerState = found->second;
		else
		{
			ID3D11RasterizerState *state = 0;
			if (FAILED(ms_device->CreateRasterizerState(&desc, &state)))
				return false;

			(*ms_rasterizerStateMap)[desc] = state;
			ms_rasterizerState = state;
		}

		ms_context->RSSetState(ms_rasterizerState);
		if (ms_scissorEnabled)
			ms_context->RSSetScissorRects(1, &ms_scissorRect);
		return true;
	}

	bool applyDepthStencilState()
	{
		if (!ms_device || !ms_context)
			return false;

		D3D11_DEPTH_STENCIL_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.DepthEnable = ms_depthEnabled ? TRUE : FALSE;
		desc.DepthWriteMask = ms_depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = ms_depthFunc;
		desc.StencilEnable = ms_stencilEnabled ? TRUE : FALSE;
		desc.StencilReadMask = ms_stencilReadMask;
		desc.StencilWriteMask = ms_stencilWriteMask;

		desc.FrontFace.StencilFailOp = ms_stencilFailOp;
		desc.FrontFace.StencilDepthFailOp = ms_stencilDepthFailOp;
		desc.FrontFace.StencilPassOp = ms_stencilPassOp;
		desc.FrontFace.StencilFunc = ms_stencilFunc;

		if (ms_stencilTwoSidedMode)
		{
			desc.BackFace.StencilFailOp = ms_ccwStencilFailOp;
			desc.BackFace.StencilDepthFailOp = ms_ccwStencilDepthFailOp;
			desc.BackFace.StencilPassOp = ms_ccwStencilPassOp;
			desc.BackFace.StencilFunc = ms_ccwStencilFunc;
		}
		else
		{
			desc.BackFace.StencilFailOp = ms_stencilFailOp;
			desc.BackFace.StencilDepthFailOp = ms_stencilDepthFailOp;
			desc.BackFace.StencilPassOp = ms_stencilPassOp;
			desc.BackFace.StencilFunc = ms_stencilFunc;
		}

		if (!ms_depthStencilStateMap)
			ms_depthStencilStateMap = new DepthStencilStateMap;

		DepthStencilStateMap::iterator found = ms_depthStencilStateMap->find(desc);
		if (found != ms_depthStencilStateMap->end())
			ms_depthStencilState = found->second;
		else
		{
			ID3D11DepthStencilState *state = 0;
			if (FAILED(ms_device->CreateDepthStencilState(&desc, &state)))
				return false;

			(*ms_depthStencilStateMap)[desc] = state;
			ms_depthStencilState = state;
		}

		ms_context->OMSetDepthStencilState(ms_depthStencilState, ms_stencilReferenceValue);
		return true;
	}

	bool applyBlendState()
	{
		if (!ms_device || !ms_context)
			return false;

		D3D11_BLEND_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		bool const blendEnabled = ms_alphaBlendEnabled || ms_alphaFadeOpacityEnabled;
		desc.RenderTarget[0].BlendEnable = blendEnabled ? TRUE : FALSE;
		desc.RenderTarget[0].SrcBlend = ms_sourceBlend;
		desc.RenderTarget[0].DestBlend = ms_destinationBlend;
		desc.RenderTarget[0].BlendOp = ms_blendOp;
		desc.RenderTarget[0].SrcBlendAlpha = getAlphaBlend(ms_sourceBlend);
		desc.RenderTarget[0].DestBlendAlpha = getAlphaBlend(ms_destinationBlend);
		desc.RenderTarget[0].BlendOpAlpha = ms_blendOp;
		desc.RenderTarget[0].RenderTargetWriteMask = ms_alphaFadeOpacityEnabled && !ms_alphaBlendEnabled ? (ms_colorWriteMask & ~D3D11_COLOR_WRITE_ENABLE_ALPHA) : ms_colorWriteMask;

		if (!ms_blendStateMap)
			ms_blendStateMap = new BlendStateMap;

		BlendStateMap::iterator found = ms_blendStateMap->find(desc);
		if (found != ms_blendStateMap->end())
			ms_blendState = found->second;
		else
		{
			ID3D11BlendState *state = 0;
			if (FAILED(ms_device->CreateBlendState(&desc, &state)))
				return false;

			(*ms_blendStateMap)[desc] = state;
			ms_blendState = state;
		}

		float const blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		ms_context->OMSetBlendState(ms_blendState, blendFactor, 0xffffffff);
		return true;
	}

	bool createDefaultRenderStates()
	{
		ms_fillMode = GFM_solid;
		ms_cullMode = GCM_counterClockwise;
		ms_alphaBlendEnabled = false;
		ms_depthEnabled = true;
		ms_depthWriteEnabled = true;
		ms_depthFunc = D3D11_COMPARISON_LESS_EQUAL;
		ms_blendOp = D3D11_BLEND_OP_ADD;
		ms_sourceBlend = D3D11_BLEND_ONE;
		ms_destinationBlend = D3D11_BLEND_ZERO;
		ms_colorWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		ms_lightingEnabled = false;
		ms_lightingColorVertex = false;
		ms_lightingSpecularEnabled = false;
		ms_lightingAmbientColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_lightingDiffuseColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_lightingSpecularColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_lightingEmissiveColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
		ms_stencilEnabled = false;
		ms_stencilTwoSidedMode = false;
		ms_stencilFunc = D3D11_COMPARISON_ALWAYS;
		ms_stencilFailOp = D3D11_STENCIL_OP_KEEP;
		ms_stencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		ms_stencilPassOp = D3D11_STENCIL_OP_KEEP;
		ms_ccwStencilFunc = D3D11_COMPARISON_ALWAYS;
		ms_ccwStencilFailOp = D3D11_STENCIL_OP_KEEP;
		ms_ccwStencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		ms_ccwStencilPassOp = D3D11_STENCIL_OP_KEEP;
		ms_stencilReadMask = 0xff;
		ms_stencilWriteMask = 0xff;
		ms_stencilReferenceValue = 0;
		ms_scissorEnabled = false;
		ZeroMemory(&ms_scissorRect, sizeof(ms_scissorRect));
		ms_alphaFadeOpacityEnabled = false;
		ms_alphaFadeOpacity = 1.0f;
		ms_activeFullAmbient = false;
		ms_activeTextureCoordinateSet = 0;
		ms_activeTextureAlphaOnly = false;
		ms_activeTextureBgraSwizzle = false;
		ms_activeTextureCube = false;
		ms_activeTerrainDot3 = false;
		ms_activeTerrainBlendCount = 0;
		ms_activeTerrainProgramMode = 0;
		ms_activeVertexProgramMode = 0;
		ms_activePixelProgramMode = 0;
		ms_activePassFogMode = ShaderImplementation::Pass::FM_Normal;
		ZeroMemory(ms_vertexUserConstants, sizeof(ms_vertexUserConstants));
		ZeroMemory(ms_pixelUserConstants, sizeof(ms_pixelUserConstants));
		resetStageTextures();
		ms_alphaTestEnabled = false;
		ms_alphaTestReference = 0.0f;
		ms_alphaTestCompare = ShaderImplementation::Pass::C_Always;
		setWhiteColor(ms_materialAmbientColor);
		setWhiteColor(ms_materialColor);
		setBlackColor(ms_materialEmissiveColor);
		setBlackColor(ms_materialSpecularColor);
		ms_materialSpecularPower = 32.0f;
		setWhiteColor(ms_textureFactor);
		setWhiteColor(ms_textureFactor2);
		ms_activeTextureStage = 0;
		for (int i = 0; i < 8; ++i)
			resetTextureTransform(i);
		ms_fogEnabled = false;
		ms_fogDensity = 0.0f;
		ms_fogColor[0] = 0.0f;
		ms_fogColor[1] = 0.0f;
		ms_fogColor[2] = 0.0f;
		ms_fogColor[3] = 1.0f;
		copyColor(ms_activeFogColor, ms_fogColor);
		ms_fogColorPacked = 0x00000000;
		ms_activeFogColorPacked = ms_fogColorPacked;
		ms_cameraPosition[0] = 0.0f;
		ms_cameraPosition[1] = 0.0f;
		ms_cameraPosition[2] = 0.0f;
		ms_cameraPosition[3] = 1.0f;
		ms_textureScrollValid = false;
		ms_textureScroll[0] = 0.0f;
		ms_textureScroll[1] = 0.0f;
		ms_textureScroll[2] = 0.0f;
		ms_textureScroll[3] = 0.0f;
		ms_currentTime = 0.0f;
		ms_obeysLightScale = false;
		ms_currentLightList.clear();
		resetLighting();
		return applyRasterizerState() && applyDepthStencilState() && applyBlendState();
	}

	void applyShaderPassState(ShaderPassState const &state)
	{
		ms_depthEnabled = state.depthEnable;
		ms_depthWriteEnabled = state.depthWrite;
		ms_depthFunc = state.depthFunc;
		ms_alphaBlendEnabled = state.alphaBlendEnable;
		ms_blendOp = state.blendOp;
		ms_sourceBlend = state.sourceBlend;
		ms_destinationBlend = state.destinationBlend;
		ms_colorWriteMask = state.colorWriteMask;
		ms_lightingEnabled = state.lightingEnable;
		ms_lightingColorVertex = state.lightingColorVertex;
		ms_lightingSpecularEnabled = state.lightingSpecularEnable;
		ms_lightingAmbientColorSource = state.lightingAmbientColorSource;
		ms_lightingDiffuseColorSource = state.lightingDiffuseColorSource;
		ms_lightingSpecularColorSource = state.lightingSpecularColorSource;
		ms_lightingEmissiveColorSource = state.lightingEmissiveColorSource;
		ms_stencilEnabled = state.stencilEnable;
		ms_stencilTwoSidedMode = state.stencilTwoSidedMode;
		ms_stencilFunc = state.stencilFunc;
		ms_stencilFailOp = state.stencilFailOp;
		ms_stencilDepthFailOp = state.stencilDepthFailOp;
		ms_stencilPassOp = state.stencilPassOp;
		ms_ccwStencilFunc = state.ccwStencilFunc;
		ms_ccwStencilFailOp = state.ccwStencilFailOp;
		ms_ccwStencilDepthFailOp = state.ccwStencilDepthFailOp;
		ms_ccwStencilPassOp = state.ccwStencilPassOp;
		ms_stencilReadMask = state.stencilReadMask;
		ms_stencilWriteMask = state.stencilWriteMask;
		IGNORE_RETURN(applyDepthStencilState());
		IGNORE_RETURN(applyBlendState());
	}

	void installTextureFormatSupport()
	{
		HMODULE const hostModule = GetModuleHandle(NULL);
		if (!hostModule || !GetProcAddress(hostModule, "?getInfo@TextureFormatInfo@@SAABU1@W4TextureFormat@@@Z"))
			return;

		for (int i = 0; i < TF_Count; ++i)
		{
			TextureFormat const format = static_cast<TextureFormat>(i);
			TextureFormatInfo const &info = TextureFormatInfo::getInfo(format);
			bool const supported = info.compressed ? info.blockSize > 0 : info.pixelByteCount > 0;
			TextureFormatInfo::setSupported(format, supported);
		}
	}

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
		ms_glApi.vrEndWorldFrameWithMenuQuad = vrEndWorldFrameWithMenuQuad;
		ms_glApi.vrBeginHudCapture = vrBeginHudCapture;
		ms_glApi.vrEndHudCapture = vrEndHudCapture;
		ms_glApi.vrBeginWristDashboardCapture = vrBeginWristDashboardCapture;
		ms_glApi.vrEndWristDashboardCapture = vrEndWristDashboardCapture;
		ms_glApi.vrSubmitHudPanelRect = vrSubmitHudPanelRect;
		ms_glApi.vrSubmitHudPanelAnchor = vrSubmitHudPanelAnchor;
		ms_glApi.vrSubmitObjectContextInputRegion = vrSubmitObjectContextInputRegion;
		ms_glApi.vrSubmitObjectContext = vrSubmitObjectContext;
		ms_glApi.vrSubmitHoverTargetContext = vrSubmitHoverTargetContext;
		ms_glApi.vrSubmitWristDashboard = vrSubmitWristDashboard;
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

using namespace Direct3d11Namespace;

// ======================================================================

Direct3d11_ShaderImplementationData::DataMap *Direct3d11_ShaderImplementationData::ms_dataMap = 0;

Direct3d11_ShaderImplementationData::Direct3d11_ShaderImplementationData(ShaderImplementation const &implementation)
: ShaderImplementationGraphicsData(),
  m_implementation(&implementation),
  m_pass()
{
	if (!ms_dataMap)
		ms_dataMap = new DataMap;
	(*ms_dataMap)[m_implementation] = this;

	int const passCount = implementation.m_pass ? static_cast<int>(implementation.m_pass->size()) : 0;
	m_pass.reserve(static_cast<size_t>(passCount));
	for (int i = 0; i < passCount; ++i)
	{
		ShaderImplementation::Pass const &pass = *(*implementation.m_pass)[static_cast<ShaderImplementation::Passes::size_type>(i)];
		Direct3d11Namespace::ShaderPassState state;
		state.depthEnable = pass.m_zEnable;
		state.depthWrite = pass.m_zWrite;
		state.depthFunc = Direct3d11Namespace::getD3dComparison(pass.m_zCompare);
		state.alphaBlendEnable = pass.m_alphaBlendEnable;
		state.blendOp = Direct3d11Namespace::getD3dBlendOp(pass.m_alphaBlendOperation);
		state.sourceBlend = Direct3d11Namespace::getD3dBlend(pass.m_alphaBlendSource);
		state.destinationBlend = Direct3d11Namespace::getD3dBlend(pass.m_alphaBlendDestination);
		state.colorWriteMask = Direct3d11Namespace::getD3dColorWriteMask(pass.m_writeEnable);
		state.lightingEnable = pass.m_fixedFunctionPipeline && pass.m_fixedFunctionPipeline->m_lighting;
		state.lightingColorVertex = pass.m_fixedFunctionPipeline && pass.m_fixedFunctionPipeline->m_lightingColorVertex;
		state.lightingSpecularEnable = pass.m_fixedFunctionPipeline && pass.m_fixedFunctionPipeline->m_lightingSpecularEnable;
		state.lightingAmbientColorSource = pass.m_fixedFunctionPipeline ? static_cast<int>(pass.m_fixedFunctionPipeline->m_lightingAmbientColorSource) : static_cast<int>(ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material);
		state.lightingDiffuseColorSource = pass.m_fixedFunctionPipeline ? static_cast<int>(pass.m_fixedFunctionPipeline->m_lightingDiffuseColorSource) : static_cast<int>(ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material);
		state.lightingSpecularColorSource = pass.m_fixedFunctionPipeline ? static_cast<int>(pass.m_fixedFunctionPipeline->m_lightingSpecularColorSource) : static_cast<int>(ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material);
		state.lightingEmissiveColorSource = pass.m_fixedFunctionPipeline ? static_cast<int>(pass.m_fixedFunctionPipeline->m_lightingEmissiveColorSource) : static_cast<int>(ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material);
		state.stencilEnable = pass.m_stencilEnable;
		state.stencilTwoSidedMode = pass.m_stencilTwoSidedMode;
		state.stencilFunc = Direct3d11Namespace::getD3dComparison(pass.m_stencilCompareFunction);
		state.stencilFailOp = Direct3d11Namespace::getD3dStencilOp(pass.m_stencilFailOperation);
		state.stencilDepthFailOp = Direct3d11Namespace::getD3dStencilOp(pass.m_stencilZFailOperation);
		state.stencilPassOp = Direct3d11Namespace::getD3dStencilOp(pass.m_stencilPassOperation);
		state.ccwStencilFunc = Direct3d11Namespace::getD3dComparison(pass.m_stencilCounterClockwiseCompareFunction);
		state.ccwStencilFailOp = Direct3d11Namespace::getD3dStencilOp(pass.m_stencilCounterClockwiseFailOperation);
		state.ccwStencilDepthFailOp = Direct3d11Namespace::getD3dStencilOp(pass.m_stencilCounterClockwiseZFailOperation);
		state.ccwStencilPassOp = Direct3d11Namespace::getD3dStencilOp(pass.m_stencilCounterClockwisePassOperation);
		state.stencilReadMask = static_cast<UINT8>(pass.m_stencilMask);
		state.stencilWriteMask = static_cast<UINT8>(pass.m_stencilWriteMask);
		state.stencilReferenceValueTag = pass.m_stencilReferenceValueTag;
		m_pass.push_back(state);
	}
}

Direct3d11_ShaderImplementationData::~Direct3d11_ShaderImplementationData()
{
	if (ms_dataMap)
	{
		ms_dataMap->erase(m_implementation);
		if (ms_dataMap->empty())
		{
			delete ms_dataMap;
			ms_dataMap = 0;
		}
	}
}

void Direct3d11_ShaderImplementationData::apply(int passNumber) const
{
	if (passNumber < 0 || passNumber >= static_cast<int>(m_pass.size()))
		return;

	Direct3d11Namespace::applyShaderPassState(m_pass[static_cast<size_t>(passNumber)]);
}

Direct3d11_ShaderImplementationData const *Direct3d11_ShaderImplementationData::find(ShaderImplementation const &implementation)
{
	if (!ms_dataMap)
		return 0;

	DataMap::const_iterator const iter = ms_dataMap->find(&implementation);
	return iter != ms_dataMap->end() ? iter->second : 0;
}

Direct3d11_StaticShaderData::Direct3d11_StaticShaderData(StaticShader const &shader)
: StaticShaderGraphicsData(),
  m_implementation(0),
  m_firstTexture()
{
	construct(shader);
}

Direct3d11_StaticShaderData::~Direct3d11_StaticShaderData()
{
}

void Direct3d11_StaticShaderData::update(StaticShader const &shader)
{
	construct(shader);
}

uintptr_t Direct3d11_StaticShaderData::getTextureSortKey() const
{
	return 0;
}

bool Direct3d11_StaticShaderData::getTextureTag(int passNumber, Tag &textureTag) const
{
	if (passNumber < 0 || passNumber >= static_cast<int>(m_firstTexture.size()))
		return false;

	textureTag = m_firstTexture[static_cast<size_t>(passNumber)].textureTag;
	return textureTag != 0;
}

bool Direct3d11_StaticShaderData::getPassTexture(int passNumber, PassTexture &passTexture) const
{
	if (passNumber < 0 || passNumber >= static_cast<int>(m_firstTexture.size()))
		return false;

	passTexture = m_firstTexture[static_cast<size_t>(passNumber)];
	return passTexture.textureTag != 0;
}

ShaderImplementation const *Direct3d11_StaticShaderData::getImplementation(StaticShader const &shader)
{
	StaticShaderTemplate const &shaderTemplate = shader.getStaticShaderTemplate();
	return shaderTemplate.m_effect ? shaderTemplate.m_effect->m_implementation : 0;
}

void Direct3d11_StaticShaderData::construct(StaticShader const &shader)
{
	m_implementation = getImplementation(shader);
	m_firstTexture.clear();
	if (!m_implementation || !m_implementation->m_pass)
		return;

	int const passCount = static_cast<int>(m_implementation->m_pass->size());
	m_firstTexture.resize(static_cast<size_t>(passCount));
	for (int passIndex = 0; passIndex < passCount; ++passIndex)
	{
		PassTexture passTexture;
		IGNORE_RETURN(getPassTexture(shader, passIndex, passTexture));
		m_firstTexture[static_cast<size_t>(passIndex)] = passTexture;
	}
}

bool Direct3d11_StaticShaderData::getPassTexture(StaticShader const &shader, int passNumber, PassTexture &passTexture)
{
	passTexture = PassTexture();

	ShaderImplementation const *implementation = getImplementation(shader);
	if (!implementation || !implementation->m_pass || passNumber < 0 || passNumber >= static_cast<int>(implementation->m_pass->size()))
		return false;

	ShaderImplementation::Pass const &pass = *(*implementation->m_pass)[static_cast<ShaderImplementation::Passes::size_type>(passNumber)];
	passTexture.alphaTestEnable = pass.m_alphaTestEnable;
	passTexture.alphaTestCompare = pass.m_alphaTestFunction;
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

	if (pass.m_textureScrollTag)
	{
		StaticShaderTemplate::TextureScroll textureScroll;
		if (shader.getTextureScroll(pass.m_textureScrollTag, textureScroll))
		{
			passTexture.textureScrollValid = true;
			passTexture.textureScroll[0] = textureScroll.u1;
			passTexture.textureScroll[1] = textureScroll.v1;
			passTexture.textureScroll[2] = textureScroll.u2;
			passTexture.textureScroll[3] = textureScroll.v2;
		}
	}

	if (pass.m_alphaTestEnable)
	{
		Tag const tagA255 = TAG(A,2,5,5);
		Tag const tagA128 = TAG(A,1,2,8);
		Tag const tagA001 = TAG(A,0,0,1);
		Tag const tagA000 = TAG(A,0,0,0);

		if (pass.m_alphaTestReferenceValueTag == tagA255)
			passTexture.alphaTestReference = 255;
		else if (pass.m_alphaTestReferenceValueTag == tagA128)
			passTexture.alphaTestReference = 128;
		else if (pass.m_alphaTestReferenceValueTag == tagA001)
			passTexture.alphaTestReference = 1;
		else if (pass.m_alphaTestReferenceValueTag == tagA000)
			passTexture.alphaTestReference = 0;
		else if (!shader.getAlphaTestReferenceValue(pass.m_alphaTestReferenceValueTag, passTexture.alphaTestReference))
			passTexture.alphaTestReference = 1;
	}

	if (pass.m_vertexShader)
	{
		char const *vertexProgramName = pass.m_vertexShader->getFilename();
		passTexture.vertexProgramName = vertexProgramName;
		if (vertexProgramName && strstr(vertexProgramName, "gradient_sky.vsh"))
			passTexture.vertexProgramMode = 1;
	}

	if (pass.m_pixelShader && pass.m_pixelShader->m_textureSamplers)
	{
		if (pass.m_pixelShader->m_program)
		{
			char const *programName = pass.m_pixelShader->m_program->getFileName();
			passTexture.pixelProgramName = programName;
			char const *terrainDot3 = programName ? strstr(programName, "terrain_dot3_ps20_blend") : 0;
			if (terrainDot3)
			{
				passTexture.terrainDot3 = true;
				char const blendChar = terrainDot3[strlen("terrain_dot3_ps20_blend")];
				passTexture.terrainBlendCount = (blendChar >= '0' && blendChar <= '3') ? (blendChar - '0') : 0;
				if (strstr(programName, "blend0_spec.psh"))
					passTexture.terrainProgramMode = 1;
				else if (strstr(programName, "blend1_pass0_spec.psh"))
					passTexture.terrainProgramMode = 2;
				else if (strstr(programName, "blend1_pass1_spec.psh"))
					passTexture.terrainProgramMode = 3;
				else if (strstr(programName, "blend2_pass0_spec.psh"))
					passTexture.terrainProgramMode = 4;
				else if (strstr(programName, "blend2_pass1_spec.psh"))
					passTexture.terrainProgramMode = 5;
				else if (strstr(programName, "blend2_pass2_spec.psh"))
					passTexture.terrainProgramMode = 6;
				else if (strstr(programName, "blend3_pass0_spec.psh"))
					passTexture.terrainProgramMode = 7;
				else if (strstr(programName, "blend3_pass1_spec.psh"))
					passTexture.terrainProgramMode = 8;
				else if (strstr(programName, "blend3_pass2_spec.psh"))
					passTexture.terrainProgramMode = 9;
				else if (strstr(programName, "blend3_pass3_spec.psh"))
					passTexture.terrainProgramMode = 10;
			}
			else if (strstr(programName, "skybox.psh") || strstr(programName, "skybox_6sided.psh"))
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
			else if (strstr(programName, "h_simple_bump_ps20.psh"))
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
			else if (strstr(programName, "h_color2_specmap_pp_ps20.psh"))
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
			else if (strstr(programName, "a_specmap_pp_ps20.psh"))
				passTexture.pixelProgramMode = 10;
		}

		int bestTextureIndex = 0x7fffffff;
		ShaderImplementation::Pass::PixelShader::TextureSamplers::const_iterator const end = pass.m_pixelShader->m_textureSamplers->end();
		for (ShaderImplementation::Pass::PixelShader::TextureSamplers::const_iterator i = pass.m_pixelShader->m_textureSamplers->begin(); i != end; ++i)
		{
			ShaderImplementation::Pass::PixelShader::TextureSampler const *sampler = *i;
			if (sampler && sampler->m_textureTag)
			{
				PassTexture::StageTexture shaderTexture;
				shaderTexture.textureTag = sampler->m_textureTag;
				shaderTexture.textureStage = sampler->m_textureIndex;
				shaderTexture.addressU = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressU);
				shaderTexture.addressV = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressV);
				shaderTexture.addressW = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressW);
				shaderTexture.mipFilter = static_cast<StaticShaderTemplate::TextureFilter>(sampler->m_textureMipFilter);
				shaderTexture.minificationFilter = static_cast<StaticShaderTemplate::TextureFilter>(sampler->m_textureMinificationFilter);
				shaderTexture.magnificationFilter = static_cast<StaticShaderTemplate::TextureFilter>(sampler->m_textureMagnificationFilter);
				passTexture.shaderTextures.push_back(shaderTexture);
			}
			if (sampler && sampler->m_textureTag && sampler->m_textureIndex < bestTextureIndex)
			{
				bestTextureIndex = sampler->m_textureIndex;
				passTexture.textureTag = sampler->m_textureTag;
				passTexture.textureStage = sampler->m_textureIndex;
				passTexture.addressU = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressU);
				passTexture.addressV = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressV);
				passTexture.addressW = static_cast<StaticShaderTemplate::TextureAddress>(sampler->m_textureAddressW);
				passTexture.mipFilter = static_cast<StaticShaderTemplate::TextureFilter>(sampler->m_textureMipFilter);
				passTexture.minificationFilter = static_cast<StaticShaderTemplate::TextureFilter>(sampler->m_textureMinificationFilter);
				passTexture.magnificationFilter = static_cast<StaticShaderTemplate::TextureFilter>(sampler->m_textureMagnificationFilter);
			}
		}
	}

	if (pass.m_stage)
	{
		int textureStage = 0;
		ShaderImplementation::Pass::Stages::const_iterator const end = pass.m_stage->end();
		for (ShaderImplementation::Pass::Stages::const_iterator i = pass.m_stage->begin(); i != end; ++i)
		{
			ShaderImplementation::Pass::Stage const *stage = *i;
			if (stage && stage->m_textureTag)
			{
				PassTexture::StageTexture stageTexture;
				stageTexture.textureTag = stage->m_textureTag;
				stageTexture.addressU = static_cast<StaticShaderTemplate::TextureAddress>(stage->m_textureAddressU);
				stageTexture.addressV = static_cast<StaticShaderTemplate::TextureAddress>(stage->m_textureAddressV);
				stageTexture.addressW = static_cast<StaticShaderTemplate::TextureAddress>(stage->m_textureAddressW);
				stageTexture.mipFilter = static_cast<StaticShaderTemplate::TextureFilter>(stage->m_textureMipFilter);
				stageTexture.minificationFilter = static_cast<StaticShaderTemplate::TextureFilter>(stage->m_textureMinificationFilter);
				stageTexture.magnificationFilter = static_cast<StaticShaderTemplate::TextureFilter>(stage->m_textureMagnificationFilter);
				IGNORE_RETURN(shader.getTextureCoordinateSet(stage->m_textureCoordinateSetTag, stageTexture.textureCoordinateSet));
				stageTexture.colorOperation = stage->m_colorOperation;
				stageTexture.colorArgument0 = stage->m_colorArgument0;
				stageTexture.colorArgument1 = stage->m_colorArgument1;
				stageTexture.colorArgument2 = stage->m_colorArgument2;
				stageTexture.colorArgumentFlags =
					(stage->m_colorArgument0Complement ? 0x01 : 0) |
					(stage->m_colorArgument0AlphaReplicate ? 0x02 : 0) |
					(stage->m_colorArgument1Complement ? 0x04 : 0) |
					(stage->m_colorArgument1AlphaReplicate ? 0x08 : 0) |
					(stage->m_colorArgument2Complement ? 0x10 : 0) |
					(stage->m_colorArgument2AlphaReplicate ? 0x20 : 0);
				stageTexture.alphaOperation = stage->m_alphaOperation;
				stageTexture.alphaArgument0 = stage->m_alphaArgument0;
				stageTexture.alphaArgument1 = stage->m_alphaArgument1;
				stageTexture.alphaArgument2 = stage->m_alphaArgument2;
				stageTexture.alphaArgumentFlags =
					(stage->m_alphaArgument0Complement ? 0x01 : 0) |
					(stage->m_alphaArgument1Complement ? 0x02 : 0) |
					(stage->m_alphaArgument2Complement ? 0x04 : 0);
				stageTexture.resultArgument = stage->m_resultArgument;
				stageTexture.coordinateGeneration = stage->m_textureCoordinateGeneration;
				if (passTexture.textureScrollValid)
					copyColor(stageTexture.textureScroll, passTexture.textureScroll);

				StaticShaderTemplate::TextureData textureData;
				if (shader.getTextureData(stageTexture.textureTag, textureData))
				{
					if (textureData.addressU != StaticShaderTemplate::TA_invalid)
						stageTexture.addressU = textureData.addressU;
					if (textureData.addressV != StaticShaderTemplate::TA_invalid)
						stageTexture.addressV = textureData.addressV;
					if (textureData.addressW != StaticShaderTemplate::TA_invalid)
						stageTexture.addressW = textureData.addressW;
					if (textureData.mipFilter != StaticShaderTemplate::TF_invalid)
						stageTexture.mipFilter = textureData.mipFilter;
					if (textureData.minificationFilter != StaticShaderTemplate::TF_invalid)
						stageTexture.minificationFilter = textureData.minificationFilter;
					if (textureData.magnificationFilter != StaticShaderTemplate::TF_invalid)
						stageTexture.magnificationFilter = textureData.magnificationFilter;
					stageTexture.maxAnisotropy = textureData.maxAnisotropy;
				}

				passTexture.stageTextures.push_back(stageTexture);
				if (!passTexture.textureTag)
				{
					passTexture.textureTag = stageTexture.textureTag;
					passTexture.textureStage = textureStage;
					passTexture.textureCoordinateSet = stageTexture.textureCoordinateSet;
					passTexture.addressU = stageTexture.addressU;
					passTexture.addressV = stageTexture.addressV;
					passTexture.addressW = stageTexture.addressW;
					passTexture.mipFilter = stageTexture.mipFilter;
					passTexture.minificationFilter = stageTexture.minificationFilter;
					passTexture.magnificationFilter = stageTexture.magnificationFilter;
					passTexture.maxAnisotropy = stageTexture.maxAnisotropy;
				}
			}

			++textureStage;
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
			if (textureData.mipFilter != StaticShaderTemplate::TF_invalid)
				passTexture.mipFilter = textureData.mipFilter;
			if (textureData.minificationFilter != StaticShaderTemplate::TF_invalid)
				passTexture.minificationFilter = textureData.minificationFilter;
			if (textureData.magnificationFilter != StaticShaderTemplate::TF_invalid)
				passTexture.magnificationFilter = textureData.magnificationFilter;
			passTexture.maxAnisotropy = textureData.maxAnisotropy;
		}
	}

	return true;
}

bool Direct3d11_StaticShaderData::getTextureTag(StaticShader const &shader, int passNumber, Tag &textureTag)
{
	PassTexture passTexture;
	if (!getPassTexture(shader, passNumber, passTexture))
	{
		textureTag = 0;
		return false;
	}

	textureTag = passTexture.textureTag;
	return true;
}

Direct3d11_VertexBufferVectorData::Direct3d11_VertexBufferVectorData(VertexBufferVector const &vertexBufferVector)
: VertexBufferVectorGraphicsData(),
  m_vertexBuffers()
{
	if (vertexBufferVector.m_vertexBufferList)
	{
		VertexBufferVector::VertexBufferList::const_iterator const end = vertexBufferVector.m_vertexBufferList->end();
		for (VertexBufferVector::VertexBufferList::const_iterator i = vertexBufferVector.m_vertexBufferList->begin(); i != end; ++i)
			m_vertexBuffers.push_back(*i);
	}
}

Direct3d11_VertexBufferVectorData::~Direct3d11_VertexBufferVectorData()
{
}

std::vector<HardwareVertexBuffer const *> const &Direct3d11_VertexBufferVectorData::getVertexBuffers() const
{
	return m_vertexBuffers;
}

Direct3d11_VertexBufferVectorData const *Direct3d11_VertexBufferVectorData::getData(VertexBufferVector const &vertexBufferVector)
{
	return dynamic_cast<Direct3d11_VertexBufferVectorData const *>(vertexBufferVector.m_graphicsData);
}

// ======================================================================

extern "C" __declspec(dllexport) Gl_api const * GetApi();
extern "C" __declspec(dllexport) void SetVrTvModeEnabled(int enabled);

Gl_api const * GetApi()
{
	fillApi();
	return &ms_glApi;
}

void SetVrTvModeEnabled(int enabled)
{
	Direct3d11_VrBridge::setTvModeEnabled(enabled != 0);
}

// ======================================================================

bool Direct3d11Namespace::verify()
{
	return true;
}

namespace
{
	int selectD3d11ShaderCapability()
	{
		int const overrideCapability = ConfigDirect3d11::getShaderCapabilityOverride();
		if (overrideCapability != 0)
			return overrideCapability;

		int const maxVertexShaderVersion = ConfigDirect3d11::getMaxVertexShaderVersion();
		int const maxPixelShaderVersion = ConfigDirect3d11::getMaxPixelShaderVersion();
		bool const disableVertexAndPixelShaders = ConfigDirect3d11::getDisableVertexAndPixelShaders();

		if (!disableVertexAndPixelShaders && maxVertexShaderVersion >= 0x0200 && maxPixelShaderVersion >= 0x0200)
			return ShaderCapability(2, 0);
		if (!disableVertexAndPixelShaders && maxVertexShaderVersion >= 0x0101 && maxPixelShaderVersion >= 0x0104)
			return ShaderCapability(1, 4);
		if (!disableVertexAndPixelShaders && maxVertexShaderVersion >= 0x0101 && maxPixelShaderVersion >= 0x0101)
			return ShaderCapability(1, 1);

		return ShaderCapability(0, 3);
	}
}

bool Direct3d11Namespace::install(Gl_install *gl_install)
{
	if (!gl_install || ms_installed)
		return false;

	ms_window = gl_install->window;
	ms_width = gl_install->width;
	ms_height = gl_install->height;
	ms_windowed = gl_install->windowed;
	ms_engineOwnsWindow = gl_install->engineOwnsWindow;
	ms_borderlessWindow = gl_install->borderlessWindow;
	ms_windowX = gl_install->windowX;
	ms_windowY = gl_install->windowY;
	ms_presentCount = 0;
	ms_autoCaptureDone = false;
	ms_autoCaptureRequested = false;
	ms_autoCaptureWorldRows = 0;
	ms_autoCaptureWorldRowsThreshold = -1;
	ms_autoCaptureCount = 0;
	ms_autoCaptureLastPresent = -1;
	ConfigDirect3d11::install();
	ms_shaderCapability = selectD3d11ShaderCapability();
	resetFrameDiagnostics();

	diag("install begin hwnd=0x%p size=%dx%d windowed=%d engineOwns=%d borderless=%d pos=%d,%d", ms_window, ms_width, ms_height, ms_windowed ? 1 : 0, ms_engineOwnsWindow ? 1 : 0, ms_borderlessWindow ? 1 : 0, ms_windowX, ms_windowY);
	diag("shader capability config disableVSPS=%d override=0x%04x maxVS=0x%04x maxPS=0x%04x", ConfigDirect3d11::getDisableVertexAndPixelShaders() ? 1 : 0, ConfigDirect3d11::getShaderCapabilityOverride(), ConfigDirect3d11::getMaxVertexShaderVersion(), ConfigDirect3d11::getMaxPixelShaderVersion());
	diag("using graphics shader capability %d.%d", GetShaderCapabilityMajor(ms_shaderCapability), GetShaderCapabilityMinor(ms_shaderCapability));

	DXGI_SWAP_CHAIN_DESC swapDesc;
	ZeroMemory(&swapDesc, sizeof(swapDesc));
	swapDesc.BufferDesc.Width = static_cast<UINT>(ms_width);
	swapDesc.BufferDesc.Height = static_cast<UINT>(ms_height);
	swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapDesc.SampleDesc.Count = 1;
	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapDesc.BufferCount = 2;
	swapDesc.OutputWindow = ms_window;
	swapDesc.Windowed = TRUE;
	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL requested[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};
	D3D_FEATURE_LEVEL created = D3D_FEATURE_LEVEL_10_0;

	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		0,
		D3D_DRIVER_TYPE_HARDWARE,
		0,
		0,
		requested,
		sizeof(requested) / sizeof(requested[0]),
		D3D11_SDK_VERSION,
		&swapDesc,
		&ms_swapChain,
		&ms_device,
		&created,
		&ms_context);

	if (FAILED(hr))
	{
		diag("D3D11CreateDeviceAndSwapChain failed hr=0x%08x", static_cast<unsigned>(hr));
		return false;
	}

	if (!createBackBufferViews())
	{
		diag("createBackBufferViews failed");
		remove();
		return false;
	}

	if (!createDefaultShaders())
	{
		diag("createDefaultShaders failed");
		remove();
		return false;
	}

	if (!createDefaultRenderStates())
	{
		diag("createDefaultRenderStates failed");
		remove();
		return false;
	}

	if (!ensureModernShadowResources())
		diag("modernShadows resources unavailable; D3D11 will render without shadow-map sampling until a modern shadow pass is connected");

	setIdentity(ms_objectToWorldMatrix);
	setIdentity(ms_objectToWorldRotationMatrix);
	ms_objectToWorldTransform = Transform::identity;
	setIdentity(ms_worldToCameraMatrix);
	setIdentity(ms_projectionMatrix);
	ms_transformDirty = true;
	ms_activeVrEye = -1;
	updateTransformConstants();

	installTextureFormatSupport();
	updateWindowSettings();
	Direct3d11_VrBridge::install(ms_device, ms_context, ms_window, gl_install->vrUiInput, gl_install->vrMouseInput, gl_install->vrControllerInput, gl_install->vrMenuInput, gl_install->vrTvMode);

	ms_installed = true;
	gl_install->width = ms_width;
	gl_install->height = ms_height;
	gl_install->windowed = true;
	diag("install complete");
	return true;
}

void Direct3d11Namespace::remove()
{
	Direct3d11_VrBridge::remove();

	releaseBackBufferLock(false);

	if (ms_context)
		ms_context->ClearState();

	ms_currentRenderTargetView = 0;
	ms_currentDepthView = 0;
	ms_activeVrEye = -1;
	ms_copyTargetTextureData = 0;
	ms_copyTargetSubresource = 0;
	ms_backBufferLocked = false;
	releaseCom(ms_copyRenderTargetView);
	releaseCom(ms_copyRenderTargetTexture);
	ms_copyRenderTargetFormat = DXGI_FORMAT_UNKNOWN;
	ms_copyRenderTargetWidth = 0;
	ms_copyRenderTargetHeight = 0;

	releaseCom(ms_depthView);
	releaseCom(ms_depthTexture);
	releaseModernShadowResources();
	releaseCom(ms_backBufferView);
	releaseCom(ms_transformConstantBuffer);
	releaseCom(ms_activeSamplerState);
	resetStageTextures();
	releaseCom(ms_defaultSamplerState);
	releaseCom(ms_defaultPixelShader);
	releaseCom(ms_cubePixelShader);
	releaseCom(ms_defaultVertexShader);
	releaseCom(ms_defaultVertexShaderBytecode);
	releaseCom(ms_terrainDot3PixelShader);
	releaseCom(ms_terrainDot3VertexShader);
	releaseCom(ms_terrainDot3VertexShaderBytecode);
	releaseCom(ms_detailBumpPixelShader);
	releaseCom(ms_detailBumpVertexShader);
	releaseCom(ms_detailBumpVertexShaderBytecode);
	ms_rasterizerState = 0;
	ms_depthStencilState = 0;
	ms_blendState = 0;
	releaseCom(ms_quadListIndexBuffer);
	releaseCom(ms_triangleFanIndexBuffer);
	releaseCom(ms_indexedTriangleFanIndexBuffer);
	releaseCom(ms_swapChain);
	releaseCom(ms_context);
	releaseCom(ms_device);

	Direct3d11_InputLayoutCache::release();

	if (ms_rasterizerStateMap)
	{
		for (RasterizerStateMap::iterator iter = ms_rasterizerStateMap->begin(); iter != ms_rasterizerStateMap->end(); ++iter)
			releaseCom(iter->second);
		delete ms_rasterizerStateMap;
		ms_rasterizerStateMap = 0;
	}
	if (ms_depthStencilStateMap)
	{
		for (DepthStencilStateMap::iterator iter = ms_depthStencilStateMap->begin(); iter != ms_depthStencilStateMap->end(); ++iter)
			releaseCom(iter->second);
		delete ms_depthStencilStateMap;
		ms_depthStencilStateMap = 0;
	}
	if (ms_blendStateMap)
	{
		for (BlendStateMap::iterator iter = ms_blendStateMap->begin(); iter != ms_blendStateMap->end(); ++iter)
			releaseCom(iter->second);
		delete ms_blendStateMap;
		ms_blendStateMap = 0;
	}
	if (ms_globalTextureMap)
	{
		for (GlobalTextureMap::iterator iter = ms_globalTextureMap->begin(); iter != ms_globalTextureMap->end(); ++iter)
			if (iter->second)
				iter->second->release();
		delete ms_globalTextureMap;
		ms_globalTextureMap = 0;
	}
	delete ms_vertexBufferDescriptorMap;
	ms_vertexBufferDescriptorMap = 0;
	delete ms_staticVertexBufferDataMap;
	ms_staticVertexBufferDataMap = 0;
	delete ms_dynamicVertexBufferDataMap;
	ms_dynamicVertexBufferDataMap = 0;
	delete ms_staticIndexBufferDataMap;
	ms_staticIndexBufferDataMap = 0;
	delete ms_dynamicIndexBufferDataMap;
	ms_dynamicIndexBufferDataMap = 0;
	ms_nextSortKey = 0;
	ms_sliceFirstVertex = 0;
	ms_sliceNumberOfVertices = 0;
	ms_sliceFirstIndex = 0;
	ms_sliceNumberOfIndices = 0;
	ms_activeVertexBuffer = 0;
	ms_activeVertexData = 0;
	ms_activeVertexStride = 0;
	ms_activeVertexDescriptor = 0;
	ms_activeVertexFormatFlags = 0;
	ms_activeVertexVector = false;
	ms_activeVertexStreamCount = 0;
	for (int i = 0; i < 2; ++i)
	{
		ms_activeVertexStreamBuffers[i] = 0;
		ms_activeVertexStreamData[i] = 0;
		ms_activeVertexStreamStrides[i] = 0;
		ms_activeVertexStreamDescriptors[i] = 0;
		ms_activeVertexStreamFormatFlags[i] = 0;
	}
	ms_activeIndexBuffer = 0;
	ms_activeIndexData = 0;
	ms_activeTextureView = 0;
	ms_activeTextureCoordinateSet = 0;
	ms_activeTextureTag = 0;
	ms_activeTextureNativeFormat = -1;
	ms_activeTextureCube = false;
	ms_currentRenderTargetView = 0;
	ms_currentDepthView = 0;
	ms_copyTargetTextureData = 0;
	ms_copyTargetSubresource = 0;
	ms_backBufferLockTexture = 0;
	ms_backBufferLockTarget = 0;
	ms_backBufferLocked = false;
	ms_lastDynamicIndexBufferData = 0;
	ms_dynamicIndexBufferCapacity = 0;
	ms_quadListIndexBufferNumberOfQuads = 0;
	ms_triangleFanIndexBufferNumberOfVertices = 0;
	ms_indexedTriangleFanIndexBufferNumberOfIndices = 0;
	setIdentity(ms_objectToWorldMatrix);
	setIdentity(ms_objectToWorldRotationMatrix);
	ms_objectToWorldTransform = Transform::identity;
	setIdentity(ms_worldToCameraMatrix);
	setIdentity(ms_projectionMatrix);
	ms_transformDirty = true;
	ms_fillMode = GFM_solid;
	ms_cullMode = GCM_counterClockwise;
	ms_alphaBlendEnabled = false;
	ms_depthEnabled = true;
	ms_depthWriteEnabled = true;
	ms_depthFunc = D3D11_COMPARISON_LESS_EQUAL;
	ms_blendOp = D3D11_BLEND_OP_ADD;
	ms_sourceBlend = D3D11_BLEND_ONE;
	ms_destinationBlend = D3D11_BLEND_ZERO;
	ms_colorWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	ms_lightingEnabled = false;
	ms_lightingColorVertex = false;
	ms_lightingSpecularEnabled = false;
	ms_lightingAmbientColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
	ms_lightingDiffuseColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
	ms_lightingSpecularColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
	ms_lightingEmissiveColorSource = ShaderImplementation::Pass::FixedFunctionPipeline::MS_Material;
	ms_scissorEnabled = false;
	ZeroMemory(&ms_scissorRect, sizeof(ms_scissorRect));
	ms_alphaFadeOpacityEnabled = false;
	ms_alphaFadeOpacity = 1.0f;
	ms_activeTextureCoordinateSet = 0;
	ms_activeTextureAlphaOnly = false;
	ms_activeTextureBgraSwizzle = false;
	ms_activeTextureCube = false;
	ms_activeTextureTag = 0;
	ms_activeTextureNativeFormat = -1;
	resetStageTextures();
	ms_alphaTestEnabled = false;
	ms_alphaTestReference = 0.0f;
	ms_alphaTestCompare = ShaderImplementation::Pass::C_Always;
	setWhiteColor(ms_materialAmbientColor);
	setWhiteColor(ms_materialColor);
	setBlackColor(ms_materialEmissiveColor);
	setBlackColor(ms_materialSpecularColor);
	ms_materialSpecularPower = 32.0f;
	setWhiteColor(ms_textureFactor);
	setWhiteColor(ms_textureFactor2);
	ms_activeTextureStage = 0;
	for (int i = 0; i < 8; ++i)
		resetTextureTransform(i);
	ms_fogEnabled = false;
	ms_fogDensity = 0.0f;
	ms_fogColor[0] = 0.0f;
	ms_fogColor[1] = 0.0f;
	ms_fogColor[2] = 0.0f;
	ms_fogColor[3] = 1.0f;
	copyColor(ms_activeFogColor, ms_fogColor);
	ms_fogColorPacked = 0x00000000;
	ms_activeFogColorPacked = ms_fogColorPacked;
	ms_cameraPosition[0] = 0.0f;
	ms_cameraPosition[1] = 0.0f;
	ms_cameraPosition[2] = 0.0f;
	ms_cameraPosition[3] = 1.0f;
	ms_textureScrollValid = false;
	ms_textureScroll[0] = 0.0f;
	ms_textureScroll[1] = 0.0f;
	ms_textureScroll[2] = 0.0f;
	ms_textureScroll[3] = 0.0f;
	ms_currentTime = 0.0f;
	ms_obeysLightScale = false;
	ms_currentLightList.clear();
	resetLighting();

	ms_window = 0;
	ms_width = 0;
	ms_height = 0;
	ms_windowed = true;
	ms_shaderCapability = ShaderCapability(2, 0);
	ms_installed = false;
}

void Direct3d11Namespace::displayModeChanged() {}
int Direct3d11Namespace::getShaderCapability() { return ms_shaderCapability; }
bool Direct3d11Namespace::requiresVertexAndPixelShaders() { return true; }
void Direct3d11Namespace::getOtherAdapterRects(stdvector<RECT>::fwd &) {}
int Direct3d11Namespace::getVideoMemoryInMegabytes() { return 2048; }
void Direct3d11Namespace::flushResources(bool) {}
bool Direct3d11Namespace::isGdiVisible() { return false; }
bool Direct3d11Namespace::wasDeviceReset() { return false; }
void Direct3d11Namespace::addDeviceLostCallback(Gl_api::CallbackFunction) {}
void Direct3d11Namespace::removeDeviceLostCallback(Gl_api::CallbackFunction) {}
void Direct3d11Namespace::addDeviceRestoredCallback(Gl_api::CallbackFunction) {}
void Direct3d11Namespace::removeDeviceRestoredCallback(Gl_api::CallbackFunction) {}
bool Direct3d11Namespace::supportsMipmappedCubeMaps() { return true; }
bool Direct3d11Namespace::supportsScissorRect() { return true; }
bool Direct3d11Namespace::supportsHardwareMouseCursor() { return true; }
bool Direct3d11Namespace::supportsTwoSidedStencil() { return true; }
bool Direct3d11Namespace::supportsStreamOffsets() { return true; }
bool Direct3d11Namespace::supportsDynamicTextures() { return true; }
void Direct3d11Namespace::setBrightnessContrastGamma(float brightness, float contrast, float gamma)
{
	if (brightness == ms_brightness && contrast == ms_contrast && gamma == ms_gamma)
		return;

	ms_brightness = brightness;
	ms_contrast = contrast;
	ms_gamma = gamma;

	if (ms_swapChain)
	{
		IDXGIOutput* output = nullptr;
		if (SUCCEEDED(ms_swapChain->GetContainingOutput(&output)))
		{
			DXGI_GAMMA_CONTROL gc;
			gc.Scale.Red = gc.Scale.Green = gc.Scale.Blue = contrast;
			gc.Offset.Red = gc.Offset.Green = gc.Offset.Blue = brightness - 1.0f;
			for (int i = 0; i < 1025; ++i)
			{
				float val = static_cast<float>(i) / 1024.0f;
				float mapped = powf(val, 1.0f / gamma);
				gc.GammaCurve[i].Red = mapped;
				gc.GammaCurve[i].Green = mapped;
				gc.GammaCurve[i].Blue = mapped;
			}
			output->SetGammaControl(&gc);
			output->Release();
		}
	}
}

void Direct3d11Namespace::resize(int newWidth, int newHeight)
{
	if (!ms_swapChain || newWidth <= 0 || newHeight <= 0)
		return;

	ms_width = newWidth;
	ms_height = newHeight;
	ms_currentRenderTargetView = 0;
	ms_currentDepthView = 0;
	releaseBackBufferLock(false);
	releaseCom(ms_backBufferView);
	releaseCom(ms_depthView);
	releaseCom(ms_depthTexture);

	if (SUCCEEDED(ms_swapChain->ResizeBuffers(0, static_cast<UINT>(ms_width), static_cast<UINT>(ms_height), DXGI_FORMAT_UNKNOWN, 0)))
		createBackBufferViews();
}

void Direct3d11Namespace::setWindowedMode(bool windowed) { ms_windowed = windowed; }
void Direct3d11Namespace::setFillMode(GlFillMode fillMode)
{
	if (ms_fillMode == fillMode)
		return;

	ms_fillMode = fillMode;
	IGNORE_RETURN(applyRasterizerState());
}

void Direct3d11Namespace::setCullMode(GlCullMode cullMode)
{
	if (ms_cullMode == cullMode)
		return;

	ms_cullMode = cullMode;
	IGNORE_RETURN(applyRasterizerState());
}
void Direct3d11Namespace::setPointSize(real) {}
void Direct3d11Namespace::setPointSizeMax(real) {}
void Direct3d11Namespace::setPointSizeMin(real) {}
void Direct3d11Namespace::setPointScaleEnable(bool) {}
void Direct3d11Namespace::setPointScaleFactor(real, real, real) {}
void Direct3d11Namespace::setPointSpriteEnable(bool) {}

#ifdef _DEBUG
void Direct3d11Namespace::setTexturesEnabled(bool enabled)
{
	ms_debugTexturesEnabled = enabled;
}

void Direct3d11Namespace::showMipmapLevels(bool enabled)
{
	ms_showMipmapLevelsEnabled = enabled;
}

bool Direct3d11Namespace::getShowMipmapLevels()
{
	return ms_showMipmapLevelsEnabled;
}

void Direct3d11Namespace::setBadVertexBufferVertexShaderCombination(bool *flag, const char *debugAppearanceName)
{
	UNREF(flag);
	UNREF(debugAppearanceName);
}

void Direct3d11Namespace::getRenderedVerticesPointsLinesTrianglesCalls(int &vertices, int &points, int &lines, int &triangles, int &calls)
{
	vertices = 0;
	points = 0;
	lines = 0;
	triangles = 0;
	calls = 0;
}
#endif

void Direct3d11Namespace::clearViewport(bool clearColor, uint32 colorValue, bool clearDepth, real depthValue, bool clearStencil, uint32 stencilValue)
{
	if (clearColor && ms_context && ms_currentRenderTargetView)
	{
		++ms_clearColorCount;
		float color[4];
		color[0] = static_cast<float>((colorValue >> 16) & 0xff) / 255.0f;
		color[1] = static_cast<float>((colorValue >> 8) & 0xff) / 255.0f;
		color[2] = static_cast<float>(colorValue & 0xff) / 255.0f;
		color[3] = static_cast<float>((colorValue >> 24) & 0xff) / 255.0f;
		ms_context->ClearRenderTargetView(ms_currentRenderTargetView, color);
	}

	if ((clearDepth || clearStencil) && ms_context && ms_currentDepthView)
	{
		++ms_clearDepthCount;
		UINT flags = 0;
		if (clearDepth)
			flags |= D3D11_CLEAR_DEPTH;
		if (clearStencil)
			flags |= D3D11_CLEAR_STENCIL;
		ms_context->ClearDepthStencilView(ms_currentDepthView, flags, depthValue, static_cast<UINT8>(stencilValue));
	}
}

void Direct3d11Namespace::update(float elapsedTime)
{
	ms_currentTime += elapsedTime;
}
void Direct3d11Namespace::beginScene() { ++ms_beginSceneCount; }
void Direct3d11Namespace::endScene() { ++ms_endSceneCount; }
bool Direct3d11Namespace::lockBackBuffer(Gl_pixelRect &pixels, const RECT *lockRect)
{
	if (!ms_swapChain || !ms_device || !ms_context || ms_backBufferLocked)
		return false;

	ZeroMemory(&pixels, sizeof(pixels));

	ID3D11Texture2D *backBuffer = 0;
	if (FAILED(ms_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backBuffer))) || !backBuffer)
		return false;

	D3D11_TEXTURE2D_DESC desc;
	backBuffer->GetDesc(&desc);

	switch (desc.Format)
	{
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			pixels.colorBits = 24;
			pixels.alphaBits = 8;
			break;

		case DXGI_FORMAT_B5G6R5_UNORM:
			pixels.colorBits = 16;
			pixels.alphaBits = 0;
			break;

		default:
			releaseCom(backBuffer);
			return false;
	}

	D3D11_TEXTURE2D_DESC stagingDesc = desc;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
	stagingDesc.MiscFlags = 0;

	if (FAILED(ms_device->CreateTexture2D(&stagingDesc, 0, &ms_backBufferLockTexture)))
	{
		releaseCom(backBuffer);
		return false;
	}

	ms_context->CopyResource(ms_backBufferLockTexture, backBuffer);

	D3D11_MAPPED_SUBRESOURCE mapped;
	ZeroMemory(&mapped, sizeof(mapped));
	if (FAILED(ms_context->Map(ms_backBufferLockTexture, 0, D3D11_MAP_READ_WRITE, 0, &mapped)))
	{
		releaseCom(ms_backBufferLockTexture);
		releaseCom(backBuffer);
		return false;
	}

	LONG left = 0;
	LONG top = 0;
	if (lockRect)
	{
		left = std::max<LONG>(0, std::min<LONG>(static_cast<LONG>(desc.Width), lockRect->left));
		top = std::max<LONG>(0, std::min<LONG>(static_cast<LONG>(desc.Height), lockRect->top));
		LONG const right = std::max<LONG>(left, std::min<LONG>(static_cast<LONG>(desc.Width), lockRect->right));
		LONG const bottom = std::max<LONG>(top, std::min<LONG>(static_cast<LONG>(desc.Height), lockRect->bottom));
		if (right <= left || bottom <= top)
		{
			ms_context->Unmap(ms_backBufferLockTexture, 0);
			releaseCom(ms_backBufferLockTexture);
			releaseCom(backBuffer);
			return false;
		}
	}

	int const bytesPerPixel = pixels.alphaBits == 8 ? 4 : 2;
	pixels.pixels = static_cast<byte *>(mapped.pData) + (top * mapped.RowPitch) + (left * bytesPerPixel);
	pixels.pitch = mapped.RowPitch;

	ms_backBufferLockTarget = backBuffer;
	ms_backBufferLocked = true;
	return true;
}

bool Direct3d11Namespace::unlockBackBuffer()
{
	if (!ms_backBufferLocked || !ms_backBufferLockTexture)
		return false;

	releaseBackBufferLock(true);
	return true;
}

bool Direct3d11Namespace::present()
{
	if (!ms_swapChain)
		return false;

	int const nextPresentCount = ms_presentCount + 1;
	if (!ms_autoCaptureDone)
	{
		char captureBase[MAX_PATH];
		DWORD const length = GetEnvironmentVariableA("SWG_D3D11_AUTOCAPTURE", captureBase, sizeof(captureBase));
		if (length > 0 && length < sizeof(captureBase))
		{
			int capturePresent = 30;
			char capturePresentText[64];
			DWORD const presentLength = GetEnvironmentVariableA("SWG_D3D11_AUTOCAPTURE_PRESENT", capturePresentText, sizeof(capturePresentText));
			if (presentLength > 0 && presentLength < sizeof(capturePresentText))
				capturePresent = std::max(1, atoi(capturePresentText));

			char inventoryText[16];
			DWORD const inventoryLength = GetEnvironmentVariableA("SWG_RENDERER_INVENTORY", inventoryText, sizeof(inventoryText));
			bool const inventoryCaptureMode = inventoryLength > 0 && inventoryLength < sizeof(inventoryText) && atoi(inventoryText) != 0;
			bool const captureByPresent = !inventoryCaptureMode && nextPresentCount >= capturePresent;
			char captureMaxText[32];
			DWORD const captureMaxLength = GetEnvironmentVariableA("SWG_RENDERER_AUTOCAPTURE_MAX", captureMaxText, sizeof(captureMaxText));
			int captureMax = (captureMaxLength > 0 && captureMaxLength < sizeof(captureMaxText)) ? atoi(captureMaxText) : 1;
			if (captureMax < 1)
				captureMax = 1;
			char captureIntervalText[32];
			DWORD const captureIntervalLength = GetEnvironmentVariableA("SWG_RENDERER_AUTOCAPTURE_INTERVAL", captureIntervalText, sizeof(captureIntervalText));
			int captureInterval = (captureIntervalLength > 0 && captureIntervalLength < sizeof(captureIntervalText)) ? atoi(captureIntervalText) : 1;
			if (captureInterval < 1)
				captureInterval = 1;
			bool const intervalReady = ms_autoCaptureLastPresent < 0 || (nextPresentCount - ms_autoCaptureLastPresent) >= captureInterval;
			if ((ms_autoCaptureRequested || captureByPresent) && intervalReady)
			{
				char captureSeriesBase[MAX_PATH];
				_snprintf(captureSeriesBase, sizeof(captureSeriesBase) - 1, "%s-present%04d", captureBase, nextPresentCount);
				captureSeriesBase[sizeof(captureSeriesBase) - 1] = '\0';
				bool const captured = screenShot(GSSF_bmp, 100, captureSeriesBase);
				if (captured)
				{
					char sourcePath[MAX_PATH];
					char latestPath[MAX_PATH];
					_snprintf(sourcePath, sizeof(sourcePath) - 1, "%s.bmp", captureSeriesBase);
					sourcePath[sizeof(sourcePath) - 1] = '\0';
					_snprintf(latestPath, sizeof(latestPath) - 1, "%s.bmp", captureBase);
					latestPath[sizeof(latestPath) - 1] = '\0';
					CopyFileA(sourcePath, latestPath, FALSE);
				}

				++ms_autoCaptureCount;
				ms_autoCaptureLastPresent = nextPresentCount;
				if (ms_autoCaptureCount >= captureMax)
					ms_autoCaptureDone = true;
				diag("autocapture %s pre-present=%d capture=%d/%d path=%s.bmp latest=%s.bmp", captured ? "ok" : "failed", nextPresentCount, ms_autoCaptureCount, captureMax, captureSeriesBase, captureBase);
				logFrameDiagnostics("autocapture stats");
			}
		}
	}

	if (Direct3d11_VrBridge::isEnabled())
	{
		Direct3d11_VrBridge::beginFrame();
		ID3D11Texture2D *vrBackBuffer = 0;
		if (SUCCEEDED(ms_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&vrBackBuffer))) && vrBackBuffer)
		{
			Direct3d11_VrBridge::submitBackBuffer(vrBackBuffer);
			releaseCom(vrBackBuffer);
		}
	}

	static int s_presentInterval = -1;
	if (s_presentInterval < 0)
	{
		char text[16];
		DWORD const length = GetEnvironmentVariableA("SWG_D3D11_PRESENT_INTERVAL", text, sizeof(text));
		s_presentInterval = (length > 0 && length < sizeof(text)) ? std::max(0, atoi(text)) : 1;
	}
	HRESULT const hr = ms_swapChain->Present(static_cast<UINT>(s_presentInterval), 0);
	if (SUCCEEDED(hr))
	{
		++ms_presentCount;
		if (ms_presentCount == 1 || ms_presentCount == 30 || ms_presentCount == 300)
		{
			diag("present ok count=%d visible=%d", ms_presentCount, IsWindowVisible(ms_window) ? 1 : 0);
			logFrameDiagnostics("frame stats");
		}
		return true;
	}

	diag("present failed hr=0x%08x count=%d", static_cast<unsigned>(hr), ms_presentCount);
	return false;
}

bool Direct3d11Namespace::presentToWindow(HWND, int, int)
{
	return present();
}

bool Direct3d11Namespace::vrIsWorldRenderingEnabled()
{
	return Direct3d11_VrBridge::isWorldRenderingEnabled();
}

bool Direct3d11Namespace::vrBeginWorldFrame()
{
	return Direct3d11_VrBridge::beginWorldFrame();
}

bool Direct3d11Namespace::vrGetEyeInfo(int eye, Gl_vrEyeInfo *eyeInfo)
{
	if (!eyeInfo)
		return false;

	Direct3d11_VrBridge::EyeInfo bridgeEyeInfo;
	ZeroMemory(&bridgeEyeInfo, sizeof(bridgeEyeInfo));
	if (!Direct3d11_VrBridge::getEyeInfo(eye, bridgeEyeInfo))
		return false;

	eyeInfo->width = bridgeEyeInfo.width;
	eyeInfo->height = bridgeEyeInfo.height;
	for (int i = 0; i < 3; ++i)
		eyeInfo->position[i] = bridgeEyeInfo.position[i];
	for (int i = 0; i < 4; ++i)
	{
		eyeInfo->orientation[i] = bridgeEyeInfo.orientation[i];
		eyeInfo->fov[i] = bridgeEyeInfo.fov[i];
	}
	return true;
}

bool Direct3d11Namespace::vrBeginEye(int eye)
{
	if (eye < 0 || eye >= 2 || !ms_context)
	{
		Direct3d11_VrBridge::logD3dEyeTarget("begin-invalid", eye, false, 0, 0, 0, 0, 0, 0, 0);
		return false;
	}

	Direct3d11_VrBridge::EyeInfo eyeInfo;
	ZeroMemory(&eyeInfo, sizeof(eyeInfo));
	if (!Direct3d11_VrBridge::getEyeInfo(eye, eyeInfo))
	{
		Direct3d11_VrBridge::logD3dEyeTarget("begin-eye-info", eye, false, 0, 0, 0, 0, 0, 0, 0);
		return false;
	}

	ID3D11Texture2D *eyeTexture = Direct3d11_VrBridge::getEyeTexture(eye);
	if (!ensureVrEyeTargets(eyeTexture, eyeInfo.width, eyeInfo.height))
	{
		Direct3d11_VrBridge::logD3dEyeTarget("begin-targets", eye, false, 0, eyeTexture, eyeInfo.width, eyeInfo.height, 0, 0, 0);
		return false;
	}

	ms_currentRenderTargetView = ms_vrEyeRenderTargetViews[eye];
	ms_currentDepthView = ms_vrEyeDepthViews[eye];
	ms_activeVrEye = eye;
	ms_context->OMSetRenderTargets(1, &ms_currentRenderTargetView, ms_currentDepthView);
	setViewport(0, 0, eyeInfo.width, eyeInfo.height, CONST_REAL(0), CONST_REAL(1));
	Direct3d11_VrBridge::logD3dEyeTarget("begin-ready", eye, true, 0, eyeTexture, eyeInfo.width, eyeInfo.height, 0, 0, 0);
	return true;
}

void Direct3d11Namespace::vrEndEye(int eye)
{
	Direct3d11_VrBridge::markEyeRendered(eye);
	ms_activeVrEye = -1;
	bindDefaultRenderTarget();
}

void Direct3d11Namespace::vrEndWorldFrame()
{
	IGNORE_RETURN(Direct3d11_VrBridge::endWorldFrame());
	ms_activeVrEye = -1;
	bindDefaultRenderTarget();
}

void Direct3d11Namespace::vrEndWorldFrameWithMenuQuad()
{
	ID3D11Texture2D *vrBackBuffer = 0;
	if (ms_swapChain && SUCCEEDED(ms_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&vrBackBuffer))) && vrBackBuffer)
	{
		IGNORE_RETURN(Direct3d11_VrBridge::endWorldFrameWithMenuQuad(vrBackBuffer));
		releaseCom(vrBackBuffer);
	}
	else
	{
		IGNORE_RETURN(Direct3d11_VrBridge::endWorldFrame());
	}
	ms_activeVrEye = -1;
	bindDefaultRenderTarget();
}

bool Direct3d11Namespace::vrBeginHudCapture(int *captureWidth, int *captureHeight)
{
	if (!ms_context)
		return false;

	ID3D11RenderTargetView *renderTargetView = 0;
	int width = 0;
	int height = 0;
	if (!Direct3d11_VrBridge::beginHudCapture(&renderTargetView, width, height) || !renderTargetView || width <= 0 || height <= 0)
		return false;

	ms_activeVrEye = -1;
	ms_currentRenderTargetView = renderTargetView;
	ms_currentDepthView = 0;
	ms_context->OMSetRenderTargets(1, &ms_currentRenderTargetView, ms_currentDepthView);
	setViewport(0, 0, width, height, CONST_REAL(0), CONST_REAL(1));
	if (captureWidth)
		*captureWidth = width;
	if (captureHeight)
		*captureHeight = height;
	return true;
}

void Direct3d11Namespace::vrEndHudCapture()
{
	Direct3d11_VrBridge::endHudCapture();
	bindDefaultRenderTarget();
	setViewport(0, 0, ms_width, ms_height, CONST_REAL(0), CONST_REAL(1));
}

bool Direct3d11Namespace::vrBeginWristDashboardCapture(int *captureWidth, int *captureHeight)
{
	if (!ms_context)
		return false;

	ID3D11RenderTargetView *renderTargetView = 0;
	int width = 0;
	int height = 0;
	if (!Direct3d11_VrBridge::beginWristDashboardCapture(&renderTargetView, width, height) || !renderTargetView || width <= 0 || height <= 0)
		return false;

	ms_activeVrEye = -1;
	ms_currentRenderTargetView = renderTargetView;
	ms_currentDepthView = 0;
	ms_context->OMSetRenderTargets(1, &ms_currentRenderTargetView, ms_currentDepthView);
	setViewport(0, 0, width, height, CONST_REAL(0), CONST_REAL(1));
	if (captureWidth)
		*captureWidth = width;
	if (captureHeight)
		*captureHeight = height;
	return true;
}

void Direct3d11Namespace::vrEndWristDashboardCapture()
{
	Direct3d11_VrBridge::endWristDashboardCapture();
	bindDefaultRenderTarget();
	setViewport(0, 0, ms_width, ms_height, CONST_REAL(0), CONST_REAL(1));
}

void Direct3d11Namespace::vrSubmitHudPanelRect(const char *panelName, int left, int top, int right, int bottom, int sourceWidth, int sourceHeight)
{
	Direct3d11_VrBridge::submitHudPanelRect(panelName, left, top, right, bottom, sourceWidth, sourceHeight);
}

void Direct3d11Namespace::vrSubmitHudPanelAnchor(const char *panelName, float x, float y, float z, float radius, bool valid)
{
	Direct3d11_VrBridge::submitHudPanelAnchor(panelName, x, y, z, radius, valid);
}

void Direct3d11Namespace::vrSubmitObjectContextInputRegion(int slot, int textureLeft, int textureTop, int textureRight, int textureBottom, int clientLeft, int clientTop, int clientRight, int clientBottom, bool active)
{
	Direct3d11_VrBridge::submitObjectContextInputRegion(slot, textureLeft, textureTop, textureRight, textureBottom, clientLeft, clientTop, clientRight, clientBottom, active);
}

void Direct3d11Namespace::vrSubmitObjectContext(const char *targetName, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool attackable)
{
	Direct3d11_VrBridge::submitObjectContext(targetName, health, healthMax, action, actionMax, mind, mindMax, attackable);
}

void Direct3d11Namespace::vrSubmitHoverTargetContext(const char *targetName, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool attackable)
{
	Direct3d11_VrBridge::submitHoverTargetContext(targetName, health, healthMax, action, actionMax, mind, mindMax, attackable);
}

void Direct3d11Namespace::vrSubmitWristDashboard(float playerX, float playerZ, float headingRadians, int health, int healthMax, int action, int actionMax, int mind, int mindMax, bool valid)
{
	Direct3d11_VrBridge::submitWristDashboard(playerX, playerZ, headingRadians, health, healthMax, action, actionMax, mind, mindMax, valid);
}

void Direct3d11Namespace::bindDefaultRenderTarget()
{
	if (!ms_context)
		return;

	if (ms_activeVrEye >= 0 && ms_activeVrEye < 2 && ms_vrEyeRenderTargetViews[ms_activeVrEye] && ms_vrEyeDepthViews[ms_activeVrEye])
	{
		ms_currentRenderTargetView = ms_vrEyeRenderTargetViews[ms_activeVrEye];
		ms_currentDepthView = ms_vrEyeDepthViews[ms_activeVrEye];
	}
	else
	{
		ms_currentRenderTargetView = ms_backBufferView;
		ms_currentDepthView = ms_depthView;
	}

	ms_context->OMSetRenderTargets(1, &ms_currentRenderTargetView, ms_currentDepthView);
}

void Direct3d11Namespace::setRenderTarget(Texture *texture, CubeFace cubeFace, int mipmapLevel)
{
	if (!ms_context)
		return;

	if (!texture)
	{
		ms_copyTargetTextureData = 0;
		ms_copyTargetSubresource = 0;
		bindDefaultRenderTarget();
		return;
	}

	if (mipmapLevel < 0 || mipmapLevel >= texture->getMipmapLevelCount())
		return;

	TextureGraphicsData const *graphicsData = texture->getGraphicsData();
	Direct3d11_TextureData const *textureData = dynamic_cast<Direct3d11_TextureData const *>(graphicsData);
	if (!textureData)
		return;

	ID3D11RenderTargetView *renderTargetView = 0;
	if (texture->isRenderTarget())
	{
		Direct3d11_TextureData *modifiableTextureData = const_cast<Direct3d11_TextureData *>(textureData);
		renderTargetView = modifiableTextureData->getRenderTargetView(cubeFace, mipmapLevel);
		modifiableTextureData->markGpuUpdated();
		ms_copyTargetTextureData = 0;
		ms_copyTargetSubresource = 0;
	}
	else
	{
		int const width = getTextureLevelDimension(texture->getWidth(), mipmapLevel);
		int const height = getTextureLevelDimension(texture->getHeight(), mipmapLevel);
		Direct3d11_TextureData *modifiableTextureData = const_cast<Direct3d11_TextureData *>(textureData);
		if (!ensureCopyRenderTarget(modifiableTextureData->getDxgiFormat(), width, height))
			return;

		renderTargetView = ms_copyRenderTargetView;
		ms_copyTargetTextureData = modifiableTextureData;
		ms_copyTargetSubresource = modifiableTextureData->getSubresourceIndex(cubeFace, mipmapLevel);
	}

	if (!renderTargetView)
		return;

	ID3D11ShaderResourceView *nullResource = 0;
	ms_context->PSSetShaderResources(0, 1, &nullResource);
	ms_currentRenderTargetView = renderTargetView;
	ms_currentDepthView = 0;
	ms_context->OMSetRenderTargets(1, &ms_currentRenderTargetView, ms_currentDepthView);
}
bool Direct3d11Namespace::copyRenderTargetToNonRenderTargetTexture()
{
	if (!ms_context || !ms_copyRenderTargetTexture || !ms_copyTargetTextureData)
		return false;

	bindDefaultRenderTarget();

	ID3D11Texture2D *destinationTexture = ms_copyTargetTextureData->getTexture2D();
	if (!destinationTexture)
	{
		ms_copyTargetTextureData = 0;
		ms_copyTargetSubresource = 0;
		return false;
	}

	ms_context->CopySubresourceRegion(destinationTexture, ms_copyTargetSubresource, 0, 0, 0, ms_copyRenderTargetTexture, 0, 0);
	ms_copyTargetTextureData->markGpuUpdated();
	ms_copyTargetTextureData = 0;
	ms_copyTargetSubresource = 0;
	return true;
}
bool Direct3d11Namespace::screenShot(GlScreenShotFormat format, int, const char *fileName)
{
	if (!fileName || ms_width <= 0 || ms_height <= 0 || !ms_swapChain)
		return false;

	ID3D11Texture2D *backBuffer = 0;
	if (FAILED(ms_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backBuffer))) || !backBuffer)
		return false;

	D3D11_TEXTURE2D_DESC desc;
	backBuffer->GetDesc(&desc);
	if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB && desc.Format != DXGI_FORMAT_B5G6R5_UNORM)
	{
		releaseCom(backBuffer);
		return false;
	}

	D3D11_TEXTURE2D_DESC stagingDesc = desc;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;

	ID3D11Texture2D *stagingTexture = 0;
	if (FAILED(ms_device->CreateTexture2D(&stagingDesc, 0, &stagingTexture)))
	{
		releaseCom(backBuffer);
		return false;
	}

	ms_context->CopyResource(stagingTexture, backBuffer);
	releaseCom(backBuffer);

	D3D11_MAPPED_SUBRESOURCE mapped;
	ZeroMemory(&mapped, sizeof(mapped));
	if (FAILED(ms_context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped)))
	{
		releaseCom(stagingTexture);
		return false;
	}

	char outputFile[MAX_PATH];
	bool writeTga = false;
	switch (format)
	{
		case GSSF_tga:
			sprintf(outputFile, "%s.tga", fileName);
			writeTga = true;
			break;

		case GSSF_bmp:
			sprintf(outputFile, "%s.bmp", fileName);
			break;

		case GSSF_jpg:
		default:
			sprintf(outputFile, "%s.bmp", fileName);
			break;
	}

	FILE *file = fopen(outputFile, "wb");
	if (!file)
	{
		ms_context->Unmap(stagingTexture, 0);
		releaseCom(stagingTexture);
		return false;
	}

	if (writeTga)
	{
		unsigned char header[18];
		memset(header, 0, sizeof(header));
		header[2] = 2;
		header[12] = static_cast<unsigned char>(ms_width & 0xff);
		header[13] = static_cast<unsigned char>((ms_width >> 8) & 0xff);
		header[14] = static_cast<unsigned char>(ms_height & 0xff);
		header[15] = static_cast<unsigned char>((ms_height >> 8) & 0xff);
		header[16] = 32;
		header[17] = 0x28;
		fwrite(header, 1, sizeof(header), file);

		for (int y = 0; y < ms_height; ++y)
		{
			byte const *sourceRow = static_cast<byte const *>(mapped.pData) + y * mapped.RowPitch;
			for (int x = 0; x < ms_width; ++x)
				writePixelBgra(file, readDxgiPixelArgb(sourceRow + x * (desc.Format == DXGI_FORMAT_B5G6R5_UNORM ? 2 : 4), desc.Format), false);
		}
	}
	else
	{
		unsigned const rowBytes = static_cast<unsigned>(ms_width * 4);
		unsigned const pixelBytes = rowBytes * static_cast<unsigned>(ms_height);
		unsigned const pixelOffset = 14 + 40;

		fwrite("BM", 1, 2, file);
		writeLittle32(file, pixelOffset + pixelBytes);
		writeLittle16(file, 0);
		writeLittle16(file, 0);
		writeLittle32(file, pixelOffset);
		writeLittle32(file, 40);
		writeLittle32(file, static_cast<unsigned>(ms_width));
		writeLittle32(file, static_cast<unsigned>(ms_height));
		writeLittle16(file, 1);
		writeLittle16(file, 32);
		writeLittle32(file, 0);
		writeLittle32(file, pixelBytes);
		writeLittle32(file, 0);
		writeLittle32(file, 0);
		writeLittle32(file, 0);
		writeLittle32(file, 0);

		for (int y = ms_height - 1; y >= 0; --y)
		{
			byte const *sourceRow = static_cast<byte const *>(mapped.pData) + y * mapped.RowPitch;
			for (int x = 0; x < ms_width; ++x)
				writePixelBgra(file, readDxgiPixelArgb(sourceRow + x * (desc.Format == DXGI_FORMAT_B5G6R5_UNORM ? 2 : 4), desc.Format), false);
		}
	}

	fclose(file);
	ms_context->Unmap(stagingTexture, 0);
	releaseCom(stagingTexture);
	return true;
}
ShaderImplementationGraphicsData *Direct3d11Namespace::createShaderImplementationGraphicsData(const ShaderImplementation &implementation) { return new ::Direct3d11_ShaderImplementationData(implementation); }
StaticShaderGraphicsData *Direct3d11Namespace::createStaticShaderGraphicsData(const StaticShader &shader) { return new ::Direct3d11_StaticShaderData(shader); }
void Direct3d11Namespace::setBadVertexShaderStaticShader(const StaticShader *) {}
namespace
{
	ID3D11ShaderResourceView *getTextureView(Texture const *texture)
	{
		if (!texture)
			return 0;

		TextureGraphicsData const *graphicsData = texture->getGraphicsData();
		Direct3d11_TextureData const *textureData = dynamic_cast<Direct3d11_TextureData const *>(graphicsData);
		return textureData ? const_cast<Direct3d11_TextureData *>(textureData)->getShaderResourceView() : 0;
	}

	Direct3d11_TextureData const *getTextureData(Texture const *texture)
	{
		if (!texture)
			return 0;

		TextureGraphicsData const *graphicsData = texture->getGraphicsData();
		return dynamic_cast<Direct3d11_TextureData const *>(graphicsData);
	}

	ID3D11ShaderResourceView *getGlobalTextureView(Tag tag)
	{
		if (!Direct3d11Namespace::ms_globalTextureMap)
			return 0;

		Direct3d11Namespace::GlobalTextureMap::const_iterator iter = Direct3d11Namespace::ms_globalTextureMap->find(tag);
		return iter != Direct3d11Namespace::ms_globalTextureMap->end() ? getTextureView(iter->second) : 0;
	}

	Direct3d11_TextureData const *getGlobalTextureData(Tag tag)
	{
		if (!Direct3d11Namespace::ms_globalTextureMap)
			return 0;

		Direct3d11Namespace::GlobalTextureMap::const_iterator iter = Direct3d11Namespace::ms_globalTextureMap->find(tag);
		return iter != Direct3d11Namespace::ms_globalTextureMap->end() ? getTextureData(iter->second) : 0;
	}
}

void Direct3d11Namespace::setStaticShader(const StaticShader &shader, int pass)
{
	++ms_shaderSetCount;
	bool const obeysLightScale = shader.obeysLightScale();
	if (ms_obeysLightScale != obeysLightScale)
	{
		ms_obeysLightScale = obeysLightScale;
		updateLightsFromCurrentList();
	}
	char const *activeShaderName = shader.getStaticShaderTemplate().getName().getString();
	strncpy(ms_activeStaticShaderName, activeShaderName ? activeShaderName : "<null>", sizeof(ms_activeStaticShaderName) - 1);
	ms_activeStaticShaderName[sizeof(ms_activeStaticShaderName) - 1] = '\0';
	ms_activeVertexProgramName[0] = '\0';
	ms_activePixelProgramName[0] = '\0';
	ShaderImplementation const *implementation = Direct3d11_StaticShaderData::getImplementation(shader);
	if (implementation)
	{
		Direct3d11_ShaderImplementationData const *data = Direct3d11_ShaderImplementationData::find(*implementation);
		if (data)
		{
			Tag const tag = data->getStencilReferenceValueTag(pass);
			data->apply(pass);
			if (tag)
			{
				uint32 stencilRef = 0;
				if (shader.getStencilReferenceValue(tag, stencilRef))
				{
					ms_stencilReferenceValue = stencilRef;
					if (ms_context && ms_depthStencilState)
						ms_context->OMSetDepthStencilState(ms_depthStencilState, ms_stencilReferenceValue);
				}
			}
		}
	}

	ms_activeTextureView = 0;
	ms_activeTextureCoordinateSet = 0;
	ms_activeTextureAlphaOnly = false;
	ms_activeTextureBgraSwizzle = false;
	ms_activeTextureCube = false;
	ms_activeTextureTag = 0;
	ms_activeTextureNativeFormat = -1;
	ms_activeFullAmbient = false;
	ms_activeTerrainDot3 = false;
	ms_activeTerrainBlendCount = 0;
	ms_activeTerrainProgramMode = 0;
	ms_activeVertexProgramMode = 0;
	ms_activePixelProgramMode = 0;
	ms_activePassFogMode = ShaderImplementation::Pass::FM_Normal;
	ZeroMemory(ms_vertexUserConstants, sizeof(ms_vertexUserConstants));
	ZeroMemory(ms_pixelUserConstants, sizeof(ms_pixelUserConstants));
	ms_activeTextureStage = 0;
	ms_textureScrollValid = false;
	ms_textureScroll[0] = 0.0f;
	ms_textureScroll[1] = 0.0f;
	ms_textureScroll[2] = 0.0f;
	ms_textureScroll[3] = 0.0f;
	releaseCom(ms_activeSamplerState);
	resetStageTextures();

	Direct3d11_StaticShaderData::PassTexture passTexture;
	if (Direct3d11_StaticShaderData::getPassTexture(shader, pass, passTexture))
	{
		ms_alphaTestEnabled = passTexture.alphaTestEnable;
		ms_alphaTestReference = static_cast<float>(passTexture.alphaTestReference) / 255.0f;
		ms_alphaTestCompare = passTexture.alphaTestCompare;
		if (passTexture.materialColorValid)
		{
			copyColor(ms_materialAmbientColor, passTexture.materialAmbientColor);
			copyColor(ms_materialColor, passTexture.materialColor);
			copyColor(ms_materialEmissiveColor, passTexture.materialEmissiveColor);
			copyColor(ms_materialSpecularColor, passTexture.materialSpecularColor);
			ms_materialSpecularPower = passTexture.materialSpecularPower;
		}
		if (passTexture.textureFactorValid)
		{
			copyColor(ms_textureFactor, passTexture.textureFactor);
			copyColor(ms_textureFactor2, passTexture.textureFactor2);
		}
		ms_activeTextureStage = (passTexture.textureStage >= 0 && passTexture.textureStage < 8) ? passTexture.textureStage : 0;
		setActiveFogColor(passTexture.fogMode);
		if (passTexture.textureScrollValid)
		{
			ms_textureScrollValid = true;
			copyColor(ms_textureScroll, passTexture.textureScroll);
		}
		ms_activeFullAmbient = passTexture.fullAmbient;
		ms_activeTerrainDot3 = passTexture.terrainDot3;
		ms_activeTerrainBlendCount = passTexture.terrainBlendCount;
		ms_activeTerrainProgramMode = passTexture.terrainProgramMode;
		ms_activeVertexProgramMode = passTexture.vertexProgramMode;
		ms_activePixelProgramMode = passTexture.pixelProgramMode;
		ms_activePassFogMode = passTexture.fogMode;
		strncpy(ms_activeVertexProgramName, passTexture.vertexProgramName ? passTexture.vertexProgramName : "<null>", sizeof(ms_activeVertexProgramName) - 1);
		ms_activeVertexProgramName[sizeof(ms_activeVertexProgramName) - 1] = '\0';
		strncpy(ms_activePixelProgramName, passTexture.pixelProgramName ? passTexture.pixelProgramName : "<null>", sizeof(ms_activePixelProgramName) - 1);
		ms_activePixelProgramName[sizeof(ms_activePixelProgramName) - 1] = '\0';

		StaticShaderTemplate::TextureData textureData;
		if (passTexture.textureTag && shader.getTextureData(passTexture.textureTag, textureData) && textureData.texture)
		{
			Direct3d11_TextureData const *textureGraphicsData = getTextureData(textureData.texture);
			ms_activeTextureAlphaOnly = textureGraphicsData && textureGraphicsData->getNativeFormat() == TF_A_8;
			ms_activeTextureBgraSwizzle = textureGraphicsData && textureGraphicsData->needsBgraSwizzleForSampling();
			ms_activeTextureCube = textureGraphicsData && textureGraphicsData->isCubeMap();
			ms_activeTextureView = textureGraphicsData ? const_cast<Direct3d11_TextureData *>(textureGraphicsData)->getShaderResourceView() : 0;
			ms_activeTextureTag = passTexture.textureTag;
			ms_activeTextureNativeFormat = textureGraphicsData ? static_cast<int>(textureGraphicsData->getNativeFormat()) : -1;
			ms_activeTextureCoordinateSet = passTexture.textureCoordinateSet < 8 ? passTexture.textureCoordinateSet : 0;
			ms_activeSamplerState = createSamplerState(passTexture);
		}
		else if (passTexture.textureTag && isGlobalTextureTag(passTexture.textureTag))
		{
			Direct3d11_TextureData const *textureGraphicsData = getGlobalTextureData(passTexture.textureTag);
			ms_activeTextureAlphaOnly = textureGraphicsData && textureGraphicsData->getNativeFormat() == TF_A_8;
			ms_activeTextureBgraSwizzle = textureGraphicsData && textureGraphicsData->needsBgraSwizzleForSampling();
			ms_activeTextureCube = textureGraphicsData && textureGraphicsData->isCubeMap();
			ms_activeTextureView = textureGraphicsData ? const_cast<Direct3d11_TextureData *>(textureGraphicsData)->getShaderResourceView() : getGlobalTextureView(passTexture.textureTag);
			ms_activeTextureTag = passTexture.textureTag;
			ms_activeTextureNativeFormat = textureGraphicsData ? static_cast<int>(textureGraphicsData->getNativeFormat()) : -1;
			ms_activeTextureCoordinateSet = passTexture.textureCoordinateSet < 8 ? passTexture.textureCoordinateSet : 0;
			ms_activeSamplerState = createSamplerState(passTexture);
		}

		for (std::vector<Direct3d11_StaticShaderData::PassTexture::StageTexture>::const_iterator shaderTextureIter = passTexture.shaderTextures.begin(); shaderTextureIter != passTexture.shaderTextures.end(); ++shaderTextureIter)
		{
			Direct3d11_StaticShaderData::PassTexture::StageTexture const &shaderTexture = *shaderTextureIter;
			int const slot = shaderTexture.textureStage >= 0 && shaderTexture.textureStage < cms_maxStageTextures ? shaderTexture.textureStage : -1;
			if (slot < 0)
				continue;

			Direct3d11_TextureData const *textureGraphicsData = 0;
			StaticShaderTemplate::TextureData shaderTextureData;
			if (shaderTexture.textureTag && shader.getTextureData(shaderTexture.textureTag, shaderTextureData) && shaderTextureData.texture)
				textureGraphicsData = getTextureData(shaderTextureData.texture);
			else if (shaderTexture.textureTag && isGlobalTextureTag(shaderTexture.textureTag))
				textureGraphicsData = getGlobalTextureData(shaderTexture.textureTag);

			ms_activeStageTextureViews[slot] = textureGraphicsData ? const_cast<Direct3d11_TextureData *>(textureGraphicsData)->getShaderResourceView() : 0;
			ms_activeStageNativeFormat[slot] = textureGraphicsData ? static_cast<int>(textureGraphicsData->getNativeFormat()) : -1;
			ms_activeStageTextureTag[slot] = shaderTexture.textureTag;
			ms_activeStageMeta[slot][2] = textureGraphicsData && textureGraphicsData->getNativeFormat() == TF_A_8 ? 2.0f : (textureGraphicsData && textureGraphicsData->needsBgraSwizzleForSampling() ? 1.0f : 0.0f);

			Direct3d11_StaticShaderData::PassTexture samplerTexture;
			samplerTexture.addressU = shaderTexture.addressU;
			samplerTexture.addressV = shaderTexture.addressV;
			samplerTexture.addressW = shaderTexture.addressW;
			samplerTexture.mipFilter = shaderTexture.mipFilter;
			samplerTexture.minificationFilter = shaderTexture.minificationFilter;
			samplerTexture.magnificationFilter = shaderTexture.magnificationFilter;
			samplerTexture.maxAnisotropy = shaderTexture.maxAnisotropy;
			releaseCom(ms_activeStageSamplerStates[slot]);
			ms_activeStageSamplerStates[slot] = createSamplerState(samplerTexture);
		}

		char const *shaderNameForUi = shader.getStaticShaderTemplate().getName().getString();
		bool const uiShader =
			(shaderNameForUi && (strstr(shaderNameForUi, "uicanvas") || strstr(shaderNameForUi, "ui_shader") || strstr(shaderNameForUi, "font") || strstr(shaderNameForUi, "text"))) ||
			(passTexture.pixelProgramName && strstr(passTexture.pixelProgramName, "ui.psh"));
		if (uiShader && ms_presentCount > 80 && ms_uiShaderLogCount < 120)
		{
			diag("ui shader pass=%d name=%s vprogram=%s pprogram=%s samplers=%u activeTag=0x%08x activeFmt=%d activeTexSet=%d tf=%0.3f,%0.3f,%0.3f,%0.3f alphaBlend=%d src=%d dst=%d op=%d alphaTest=%d cmp=%d ref=%0.3f depth=%d write=%d s0 tag=0x%08x fmt=%d srv=%d s1 tag=0x%08x fmt=%d srv=%d stages=%u",
				pass,
				shaderNameForUi ? shaderNameForUi : "<null>",
				passTexture.vertexProgramName ? passTexture.vertexProgramName : "<null>",
				passTexture.pixelProgramName ? passTexture.pixelProgramName : "<null>",
				static_cast<unsigned>(passTexture.shaderTextures.size()),
				static_cast<unsigned>(ms_activeTextureTag),
				ms_activeTextureNativeFormat,
				ms_activeTextureCoordinateSet,
				ms_textureFactor[0],
				ms_textureFactor[1],
				ms_textureFactor[2],
				ms_textureFactor[3],
				ms_alphaBlendEnabled ? 1 : 0,
				static_cast<int>(ms_sourceBlend),
				static_cast<int>(ms_destinationBlend),
				static_cast<int>(ms_blendOp),
				ms_alphaTestEnabled ? 1 : 0,
				ms_alphaTestCompare,
				ms_alphaTestReference,
				ms_depthEnabled ? 1 : 0,
				ms_depthWriteEnabled ? 1 : 0,
				static_cast<unsigned>(ms_activeStageTextureTag[0]),
				ms_activeStageNativeFormat[0],
				ms_activeStageTextureViews[0] ? 1 : 0,
				static_cast<unsigned>(ms_activeStageTextureTag[1]),
				ms_activeStageNativeFormat[1],
				ms_activeStageTextureViews[1] ? 1 : 0,
				static_cast<unsigned>(passTexture.stageTextures.size()));
			++ms_uiShaderLogCount;
		}

		if (ms_activeTerrainDot3)
		{
			++ms_terrainDot3ShaderSetCount;
			if (ms_terrainDot3LogCount < 40)
			{
				char const *shaderName = shader.getStaticShaderTemplate().getName().getString();
				diag("terrain dot3 shader pass=%d blend=%d mode=%d fullAmbient=%d samplers=%u program=%s name=%s", pass, ms_activeTerrainBlendCount, ms_activeTerrainProgramMode, ms_activeFullAmbient ? 1 : 0, static_cast<unsigned>(passTexture.shaderTextures.size()), passTexture.pixelProgramName ? passTexture.pixelProgramName : "<null>", shaderName ? shaderName : "<null>");
				++ms_terrainDot3LogCount;
			}
		}
		else if (ms_activePixelProgramMode > 0 && ms_skyPixelProgramLogCount < 80)
		{
			char const *shaderName = shader.getStaticShaderTemplate().getName().getString();
			diag("pixel program shader pass=%d vmode=%d pmode=%d fullAmbient=%d samplers=%u vprogram=%s pprogram=%s name=%s material=%d tf=%d tf1=%0.3f,%0.3f,%0.3f,%0.3f tf2=%0.3f,%0.3f,%0.3f,%0.3f fog=%d",
				pass,
				ms_activeVertexProgramMode,
				ms_activePixelProgramMode,
				ms_activeFullAmbient ? 1 : 0,
				static_cast<unsigned>(passTexture.shaderTextures.size()),
				passTexture.vertexProgramName ? passTexture.vertexProgramName : "<null>",
				passTexture.pixelProgramName ? passTexture.pixelProgramName : "<null>",
				shaderName ? shaderName : "<null>",
				passTexture.materialColorValid ? 1 : 0,
				passTexture.textureFactorValid ? 1 : 0,
				ms_textureFactor[0],
				ms_textureFactor[1],
				ms_textureFactor[2],
				ms_textureFactor[3],
				ms_textureFactor2[0],
				ms_textureFactor2[1],
				ms_textureFactor2[2],
				ms_textureFactor2[3],
				static_cast<int>(passTexture.fogMode));
			++ms_skyPixelProgramLogCount;
		}

		int const stageCount = std::min(static_cast<int>(passTexture.stageTextures.size()), cms_maxStageTextures);
		ms_activeStageTextureCount = stageCount;
		if (stageCount > 0)
		{
			++ms_stageReplayShaderSetCount;
			ms_stageReplayMaxStageCount = std::max(ms_stageReplayMaxStageCount, stageCount);
		}
		for (int i = 0; i < stageCount; ++i)
		{
			Direct3d11_StaticShaderData::PassTexture::StageTexture const &stageTexture = passTexture.stageTextures[static_cast<size_t>(i)];
			Direct3d11_TextureData const *textureGraphicsData = 0;
			StaticShaderTemplate::TextureData textureData;
			if (stageTexture.textureTag && shader.getTextureData(stageTexture.textureTag, textureData) && textureData.texture)
				textureGraphicsData = getTextureData(textureData.texture);
			else if (stageTexture.textureTag && isGlobalTextureTag(stageTexture.textureTag))
				textureGraphicsData = getGlobalTextureData(stageTexture.textureTag);

			ms_activeStageTextureViews[i] = textureGraphicsData ? const_cast<Direct3d11_TextureData *>(textureGraphicsData)->getShaderResourceView() : 0;
			ms_activeStageNativeFormat[i] = textureGraphicsData ? static_cast<int>(textureGraphicsData->getNativeFormat()) : -1;
			ms_activeStageTextureTag[i] = stageTexture.textureTag;

			Direct3d11_StaticShaderData::PassTexture samplerTexture;
			samplerTexture.addressU = stageTexture.addressU;
			samplerTexture.addressV = stageTexture.addressV;
			samplerTexture.addressW = stageTexture.addressW;
			samplerTexture.mipFilter = stageTexture.mipFilter;
			samplerTexture.minificationFilter = stageTexture.minificationFilter;
			samplerTexture.magnificationFilter = stageTexture.magnificationFilter;
			samplerTexture.maxAnisotropy = stageTexture.maxAnisotropy;
			ms_activeStageSamplerStates[i] = createSamplerState(samplerTexture);

			ms_activeStageColorOp[i][0] = static_cast<float>(stageTexture.colorOperation);
			ms_activeStageColorOp[i][1] = static_cast<float>(stageTexture.colorArgument0);
			ms_activeStageColorOp[i][2] = static_cast<float>(stageTexture.colorArgument1);
			ms_activeStageColorOp[i][3] = static_cast<float>(stageTexture.colorArgument2);
			ms_activeStageAlphaOp[i][0] = static_cast<float>(stageTexture.alphaOperation);
			ms_activeStageAlphaOp[i][1] = static_cast<float>(stageTexture.alphaArgument0);
			ms_activeStageAlphaOp[i][2] = static_cast<float>(stageTexture.alphaArgument1);
			ms_activeStageAlphaOp[i][3] = static_cast<float>(stageTexture.alphaArgument2);
			ms_activeStageMeta[i][0] = static_cast<float>(stageTexture.colorArgumentFlags);
			ms_activeStageMeta[i][1] = static_cast<float>(stageTexture.alphaArgumentFlags);
			ms_activeStageMeta[i][2] = textureGraphicsData && textureGraphicsData->getNativeFormat() == TF_A_8 ? 2.0f : (textureGraphicsData && textureGraphicsData->needsBgraSwizzleForSampling() ? 1.0f : 0.0f);
			ms_activeStageMeta[i][3] = static_cast<float>(stageTexture.resultArgument);
			ms_activeStageTextureCoordinateSet[i][0] = static_cast<float>(stageTexture.textureCoordinateSet < 4 ? stageTexture.textureCoordinateSet : 0);
			ms_activeStageTextureCoordinateSet[i][1] = static_cast<float>(stageTexture.coordinateGeneration);
			ms_activeStageTextureCoordinateSet[i][2] = static_cast<float>(getD3d9TextureCoordinateIndex(stageTexture.textureCoordinateSet, stageTexture.coordinateGeneration));
			copyColor(ms_activeStageScroll[i], stageTexture.textureScroll);
			if (stageTexture.coordinateGeneration != ShaderImplementation::Pass::Stage::CG_passThru)
			{
				++ms_coordinateGenerationShaderSetCount;
				if (ms_coordinateGenerationLogCount < 80)
				{
					char const *shaderName = shader.getStaticShaderTemplate().getName().getString();
					diag("coordgen shader pass=%d stage=%d gen=%s texTag=0x%08x texSet=%u name=%s",
						pass,
						i,
						getCoordinateGenerationName(stageTexture.coordinateGeneration),
						static_cast<unsigned>(stageTexture.textureTag),
						static_cast<unsigned>(stageTexture.textureCoordinateSet),
						shaderName ? shaderName : "<null>");
					++ms_coordinateGenerationLogCount;
				}
			}
		}
		if (stageCount > 0 && ms_stageReplayLogCount < 40)
		{
			char const *shaderName = shader.getStaticShaderTemplate().getName().getString();
			int firstFormat = -1;
			int secondFormat = -1;
			if (passTexture.stageTextures.size() > 0)
			{
				Direct3d11_StaticShaderData::PassTexture::StageTexture const &stageTexture = passTexture.stageTextures[0];
				Direct3d11_TextureData const *textureGraphicsData = 0;
				StaticShaderTemplate::TextureData textureData;
				if (stageTexture.textureTag && shader.getTextureData(stageTexture.textureTag, textureData) && textureData.texture)
					textureGraphicsData = getTextureData(textureData.texture);
				else if (stageTexture.textureTag && isGlobalTextureTag(stageTexture.textureTag))
					textureGraphicsData = getGlobalTextureData(stageTexture.textureTag);
				firstFormat = textureGraphicsData ? static_cast<int>(textureGraphicsData->getNativeFormat()) : -1;
			}
			if (passTexture.stageTextures.size() > 1)
			{
				Direct3d11_StaticShaderData::PassTexture::StageTexture const &stageTexture = passTexture.stageTextures[1];
				Direct3d11_TextureData const *textureGraphicsData = 0;
				StaticShaderTemplate::TextureData textureData;
				if (stageTexture.textureTag && shader.getTextureData(stageTexture.textureTag, textureData) && textureData.texture)
					textureGraphicsData = getTextureData(textureData.texture);
				else if (stageTexture.textureTag && isGlobalTextureTag(stageTexture.textureTag))
					textureGraphicsData = getGlobalTextureData(stageTexture.textureTag);
				secondFormat = textureGraphicsData ? static_cast<int>(textureGraphicsData->getNativeFormat()) : -1;
			}
			diag("stage replay shader pass=%d stages=%d name=%s s0 op=%d/%d arg=%d,%d,%d fmt=%d s1 op=%d/%d arg=%d,%d,%d fmt=%d",
				pass,
				stageCount,
				shaderName ? shaderName : "<null>",
				stageCount > 0 ? static_cast<int>(passTexture.stageTextures[0].colorOperation) : -1,
				stageCount > 0 ? static_cast<int>(passTexture.stageTextures[0].alphaOperation) : -1,
				stageCount > 0 ? static_cast<int>(passTexture.stageTextures[0].colorArgument0) : -1,
				stageCount > 0 ? static_cast<int>(passTexture.stageTextures[0].colorArgument1) : -1,
				stageCount > 0 ? static_cast<int>(passTexture.stageTextures[0].colorArgument2) : -1,
				firstFormat,
				stageCount > 1 ? static_cast<int>(passTexture.stageTextures[1].colorOperation) : -1,
				stageCount > 1 ? static_cast<int>(passTexture.stageTextures[1].alphaOperation) : -1,
				stageCount > 1 ? static_cast<int>(passTexture.stageTextures[1].colorArgument0) : -1,
				stageCount > 1 ? static_cast<int>(passTexture.stageTextures[1].colorArgument1) : -1,
				stageCount > 1 ? static_cast<int>(passTexture.stageTextures[1].colorArgument2) : -1,
				secondFormat);
			++ms_stageReplayLogCount;
		}
		if (!ms_activeTerrainDot3 && ms_activePixelProgramMode == 0 && ms_staticShaderLogCount < 160 && (ms_activeFullAmbient || passTexture.materialColorValid || ms_lightingEnabled || stageCount > 0))
		{
			char const *shaderName = shader.getStaticShaderTemplate().getName().getString();
			diag("static shader pass=%d lighting=%d colorVertex=%d srcA/D/S/E=%d/%d/%d/%d fullAmbient=%d stages=%d samplers=%u name=%s matA=%0.3f,%0.3f,%0.3f,%0.3f matD=%0.3f,%0.3f,%0.3f,%0.3f matE=%0.3f,%0.3f,%0.3f,%0.3f tf=%0.3f,%0.3f,%0.3f,%0.3f fog=%d alphaBlend=%d",
				pass,
				ms_lightingEnabled ? 1 : 0,
				ms_lightingColorVertex ? 1 : 0,
				ms_lightingAmbientColorSource,
				ms_lightingDiffuseColorSource,
				ms_lightingSpecularColorSource,
				ms_lightingEmissiveColorSource,
				ms_activeFullAmbient ? 1 : 0,
				stageCount,
				static_cast<unsigned>(passTexture.shaderTextures.size()),
				shaderName ? shaderName : "<null>",
				ms_materialAmbientColor[0],
				ms_materialAmbientColor[1],
				ms_materialAmbientColor[2],
				ms_materialAmbientColor[3],
				ms_materialColor[0],
				ms_materialColor[1],
				ms_materialColor[2],
				ms_materialColor[3],
				ms_materialEmissiveColor[0],
				ms_materialEmissiveColor[1],
				ms_materialEmissiveColor[2],
				ms_materialEmissiveColor[3],
				ms_textureFactor[0],
				ms_textureFactor[1],
				ms_textureFactor[2],
				ms_textureFactor[3],
				static_cast<int>(passTexture.fogMode),
				ms_alphaBlendEnabled ? 1 : 0);
			++ms_staticShaderLogCount;
		}
	}
	else
	{
		ms_alphaTestEnabled = false;
		ms_alphaTestReference = 0.0f;
		ms_alphaTestCompare = ShaderImplementation::Pass::C_Always;
	}

	ms_transformDirty = true;
}

bool Direct3d11Namespace::setMouseCursor(const Texture & texture, int hotSpotX, int hotSpotY)
{
	// TODO: Map D3D11 texture data to Win32 HBITMAP and create HCURSOR via CreateIconIndirect
	return false; 
}

bool Direct3d11Namespace::showMouseCursor(bool show) 
{ 
	if (GetForegroundWindow() == ms_window)
	{
		ShowCursor(show ? TRUE : FALSE);
		return true;
	}
	return false; 
}

void Direct3d11Namespace::setViewport(int x, int y, int width, int height, real minZ, real maxZ)
{
	ms_viewportX = x;
	ms_viewportY = y;
	ms_viewportWidth = width;
	ms_viewportHeight = height;
	ms_transformDirty = true;

	if (!ms_context)
		return;

	D3D11_VIEWPORT viewport;
	ZeroMemory(&viewport, sizeof(viewport));
	viewport.TopLeftX = static_cast<float>(x);
	viewport.TopLeftY = static_cast<float>(y);
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = minZ;
	viewport.MaxDepth = maxZ;
	ms_context->RSSetViewports(1, &viewport);
}

void Direct3d11Namespace::setScissorRect(bool enabled, int x, int y, int width, int height)
{
	ms_scissorEnabled = enabled && width > 0 && height > 0;
	if (ms_scissorEnabled)
		++ms_scissorEnableCount;
	else
		++ms_scissorDisableCount;

	if (ms_scissorEnabled)
	{
		ms_scissorRect.left = x;
		ms_scissorRect.top = y;
		ms_scissorRect.right = x + width;
		ms_scissorRect.bottom = y + height;
		if (ms_scissorLogCount < 8)
		{
			++ms_scissorLogCount;
			diag("scissor %d,%d %dx%d -> %ld,%ld,%ld,%ld", x, y, width, height, ms_scissorRect.left, ms_scissorRect.top, ms_scissorRect.right, ms_scissorRect.bottom);
		}
	}

	IGNORE_RETURN(applyRasterizerState());
}
void Direct3d11Namespace::setWorldToCameraTransform(const Transform &transform, const Vector &cameraPosition)
{
	setMatrixFromTransform(ms_worldToCameraMatrix, transform, 0);
	ms_cameraPosition[0] = cameraPosition.x;
	ms_cameraPosition[1] = cameraPosition.y;
	ms_cameraPosition[2] = cameraPosition.z;
	ms_cameraPosition[3] = 1.0f;
	ms_transformDirty = true;
}

void Direct3d11Namespace::setProjectionMatrix(const GlMatrix4x4 &projectionMatrix)
{
	setMatrixFromGl(ms_projectionMatrix, projectionMatrix);
	ms_transformDirty = true;
}

void Direct3d11Namespace::setFog(bool enabled, real density, const PackedArgb &color)
{
	if (density < 0.0f)
		density = 0.0f;

	++ms_fogSetCount;
	if (enabled)
		++ms_fogEnabledSetCount;
	if (ms_fogLogCount < 512)
	{
		diag("fog set d3d11 enabled=%d density=%f color=%u,%u,%u,%u packed=0x%08x count=%d enabledCount=%d",
			enabled ? 1 : 0,
			static_cast<float>(density),
			static_cast<unsigned>(color.getA()),
			static_cast<unsigned>(color.getR()),
			static_cast<unsigned>(color.getG()),
			static_cast<unsigned>(color.getB()),
			static_cast<unsigned>(color.getArgb()),
			ms_fogSetCount,
			ms_fogEnabledSetCount);
		++ms_fogLogCount;
	}
	ms_fogEnabled = enabled;
	ms_fogDensity = density;
	setColorFromPackedArgb(ms_fogColor, color);
	ms_fogColorPacked = color.getArgb();
	copyColor(ms_activeFogColor, ms_fogColor);
	ms_activeFogColorPacked = ms_fogColorPacked;
	ms_transformDirty = true;
}
void Direct3d11Namespace::setObjectToWorldTransformAndScale(const Transform &transform, const Vector &scale)
{
	ms_objectToWorldTransform = transform;
	setMatrixFromTransform(ms_objectToWorldMatrix, transform, &scale);
	setMatrixFromTransform(ms_objectToWorldRotationMatrix, transform, 0);
	ms_transformDirty = true;
}
void Direct3d11Namespace::setGlobalTexture(Tag tag, const Texture &texture)
{
	if (!isGlobalTextureTag(tag))
	{
		Direct3d11_TextureData const *textureGraphicsData = getTextureData(&texture);
		ms_activeTextureAlphaOnly = textureGraphicsData && textureGraphicsData->getNativeFormat() == TF_A_8;
		ms_activeTextureBgraSwizzle = textureGraphicsData && textureGraphicsData->needsBgraSwizzleForSampling();
		ms_activeTextureCube = textureGraphicsData && textureGraphicsData->isCubeMap();
		ms_activeTextureView = textureGraphicsData ? const_cast<Direct3d11_TextureData *>(textureGraphicsData)->getShaderResourceView() : 0;
		ms_activeTextureTag = tag;
		ms_activeTextureNativeFormat = textureGraphicsData ? static_cast<int>(textureGraphicsData->getNativeFormat()) : -1;
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
}

void Direct3d11Namespace::releaseAllGlobalTextures()
{
	ms_activeTextureView = 0;
	ms_activeTextureTag = 0;
	ms_activeTextureNativeFormat = -1;
	ms_activeTextureCube = false;
	if (!ms_globalTextureMap)
		return;

	for (GlobalTextureMap::iterator iter = ms_globalTextureMap->begin(); iter != ms_globalTextureMap->end(); ++iter)
		if (iter->second)
			iter->second->release();
	ms_globalTextureMap->clear();
}
void Direct3d11Namespace::setTextureTransform(int stage, bool enabled, int dimension, bool projected, const real *transform)
{
	if (stage < 0 || stage >= 8)
		return;

	if (!enabled)
	{
		resetTextureTransform(stage);
		ms_transformDirty = true;
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
	ms_transformDirty = true;
}
void Direct3d11Namespace::setVertexShaderUserConstants(int index, float c0, float c1, float c2, float c3)
{
	++ms_vertexShaderUserConstantSetCount;
	if (index >= 0 && index < static_cast<int>(sizeof(ms_vertexUserConstants) / sizeof(ms_vertexUserConstants[0])))
	{
		ms_vertexUserConstants[index][0] = c0;
		ms_vertexUserConstants[index][1] = c1;
		ms_vertexUserConstants[index][2] = c2;
		ms_vertexUserConstants[index][3] = c3;
		ms_transformDirty = true;
	}
	if (ms_vertexShaderUserConstantSetCount <= 32)
		diag("vertex user constants index=%d value=%f,%f,%f,%f", index, c0, c1, c2, c3);
}

void Direct3d11Namespace::setPixelShaderUserConstants(VectorRgba const *constants, int count)
{
	++ms_pixelShaderUserConstantSetCount;
	if (constants && count > 0)
	{
		int const copyCount = std::min(count, static_cast<int>(sizeof(ms_pixelUserConstants) / sizeof(ms_pixelUserConstants[0])));
		for (int i = 0; i < copyCount; ++i)
		{
			ms_pixelUserConstants[i][0] = constants[i].r;
			ms_pixelUserConstants[i][1] = constants[i].g;
			ms_pixelUserConstants[i][2] = constants[i].b;
			ms_pixelUserConstants[i][3] = constants[i].a;
		}
		ms_transformDirty = true;
	}
	if (ms_pixelShaderUserConstantSetCount <= 32)
		diag("pixel user constants count=%d data=0x%p", count, constants);
}
void Direct3d11Namespace::setAlphaFadeOpacity(bool enabled, float opacity)
{
	if (opacity < 0.0f)
		opacity = 0.0f;
	else if (opacity > 1.0f)
		opacity = 1.0f;

	ms_alphaFadeOpacityEnabled = enabled;
	ms_alphaFadeOpacity = opacity;
	IGNORE_RETURN(applyBlendState());
	ms_transformDirty = true;
}
void Direct3d11Namespace::updateLightsFromCurrentList()
{
	++ms_lightSetCount;
	if (!ms_currentLightList.empty())
		++ms_lightNonEmptySetCount;
	if (ms_lightLogCount < 24)
	{
		int ambientCount = 0;
		int parallelCount = 0;
		int otherCount = 0;
		for (stdvector<const Light*>::fwd::const_iterator i = ms_currentLightList.begin(); i != ms_currentLightList.end(); ++i)
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
		diag("lights set total=%u ambient=%d parallel=%d other=%d",
			static_cast<unsigned>(ms_currentLightList.size()),
			ambientCount,
			parallelCount,
			otherCount);
		++ms_lightLogCount;
	}
	ms_lightAmbient[0] = 0.0f;
	ms_lightAmbient[1] = 0.0f;
	ms_lightAmbient[2] = 0.0f;
	ms_lightAmbient[3] = 1.0f;
	ms_lightDiffuse[0] = 0.0f;
	ms_lightDiffuse[1] = 0.0f;
	ms_lightDiffuse[2] = 0.0f;
	ms_lightDiffuse[3] = 1.0f;
	ms_lightDirectionalEnabled = false;
	ms_selectedLightMask = 0;
	for (int lightIndex = 0; lightIndex < 3; ++lightIndex)
	{
		ms_parallelLightDiffuse[lightIndex][0] = 0.0f;
		ms_parallelLightDiffuse[lightIndex][1] = 0.0f;
		ms_parallelLightDiffuse[lightIndex][2] = 0.0f;
		ms_parallelLightDiffuse[lightIndex][3] = 1.0f;
		ms_parallelLightSpecular[lightIndex][0] = 0.0f;
		ms_parallelLightSpecular[lightIndex][1] = 0.0f;
		ms_parallelLightSpecular[lightIndex][2] = 0.0f;
		ms_parallelLightSpecular[lightIndex][3] = 1.0f;
		ms_parallelLightDirection[lightIndex][0] = 0.0f;
		ms_parallelLightDirection[lightIndex][1] = 0.0f;
		ms_parallelLightDirection[lightIndex][2] = -1.0f;
		ms_parallelLightDirection[lightIndex][3] = 0.0f;
	}
	setBlackColor(ms_dot3LightSpecularColor);
	setBlackColor(ms_dot3LightTangentMinusDiffuseColor);
	setBlackColor(ms_dot3LightTangentMinusBackColor);
	bool sawSupportedLight = false;
	Light const *parallelSpecularLight = 0;
	Light const *parallelLights[2] = {0, 0};

	for (stdvector<const Light*>::fwd::const_iterator i = ms_currentLightList.begin(); i != ms_currentLightList.end(); ++i)
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

	Light const *selectedParallelLights[3] = {parallelSpecularLight, parallelLights[0], parallelLights[1]};
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
				VectorArgb const &diffuseColor = getPossiblyScaledDiffuseColor(*light);
				VectorArgb const &specularColor = getPossiblyScaledSpecularColor(*light);
				VectorArgb const &diffuseBackColor = getPossiblyScaledDiffuseBackColor(*light);
				VectorArgb const &diffuseTangentColor = getPossiblyScaledDiffuseTangentColor(*light);
				setColorFromVectorArgb(ms_dot3LightSpecularColor, specularColor);
				ms_dot3LightTangentMinusDiffuseColor[0] = diffuseTangentColor.r - diffuseColor.r;
				ms_dot3LightTangentMinusDiffuseColor[1] = diffuseTangentColor.g - diffuseColor.g;
				ms_dot3LightTangentMinusDiffuseColor[2] = diffuseTangentColor.b - diffuseColor.b;
				ms_dot3LightTangentMinusDiffuseColor[3] = 0.0f;
				ms_dot3LightTangentMinusBackColor[0] = diffuseTangentColor.r - diffuseBackColor.r;
				ms_dot3LightTangentMinusBackColor[1] = diffuseTangentColor.g - diffuseBackColor.g;
				ms_dot3LightTangentMinusBackColor[2] = diffuseTangentColor.b - diffuseBackColor.b;
				ms_dot3LightTangentMinusBackColor[3] = ms_currentTime;

				copyColor(ms_lightDiffuse, ms_parallelLightDiffuse[lightIndex]);
				copyColor(ms_lightDirection, ms_parallelLightDirection[lightIndex]);
				ms_lightDirection[3] = 0.0f;
				ms_lightDirectionalEnabled = true;
			}
		}
	}

	if (!sawSupportedLight)
	{
		resetLighting();
		ms_transformDirty = true;
		return;
	}

	ms_lightAmbient[0] = std::min(ms_lightAmbient[0], 1.0f);
	ms_lightAmbient[1] = std::min(ms_lightAmbient[1], 1.0f);
	ms_lightAmbient[2] = std::min(ms_lightAmbient[2], 1.0f);
	ms_transformDirty = true;
}

void Direct3d11Namespace::setLights(const stdvector<const Light*>::fwd &lightList)
{
	ms_currentLightList = lightList;
	updateLightsFromCurrentList();
}

StaticVertexBufferGraphicsData *Direct3d11Namespace::createStaticVertexBufferData(const StaticVertexBuffer &vertexBuffer)
{
	if (!ms_staticVertexBufferDataMap)
		ms_staticVertexBufferDataMap = new StaticVertexBufferDataMap;
	Direct3d11_StaticVertexBufferData *data = new Direct3d11_StaticVertexBufferData(vertexBuffer);
	(*ms_staticVertexBufferDataMap)[&vertexBuffer] = data;
	return data;
}

DynamicVertexBufferGraphicsData *Direct3d11Namespace::createDynamicVertexBufferData(const DynamicVertexBuffer &vertexBuffer)
{
	if (!ms_dynamicVertexBufferDataMap)
		ms_dynamicVertexBufferDataMap = new DynamicVertexBufferDataMap;
	Direct3d11_DynamicVertexBufferData *data = new Direct3d11_DynamicVertexBufferData(vertexBuffer);
	(*ms_dynamicVertexBufferDataMap)[&vertexBuffer] = data;
	return data;
}

VertexBufferVectorGraphicsData *Direct3d11Namespace::createVertexBufferVectorData(VertexBufferVector const &vertexBufferVector) { return new ::Direct3d11_VertexBufferVectorData(vertexBufferVector); }

void Direct3d11Namespace::setVertexBuffer(HardwareVertexBuffer const &vertexBuffer)
{
	++ms_vertexBufferSetCount;
	ID3D11Buffer *buffer = 0;
	byte const *vertexData = 0;
	UINT stride = 0;
	int vertexCount = 0;
	VertexBufferDescriptor const *descriptor = 0;
	uint32 formatFlags = 0;

	if (vertexBuffer.getType() == HardwareVertexBuffer::T_static)
	{
		StaticVertexBuffer const *staticVertexBuffer = static_cast<StaticVertexBuffer const *>(&vertexBuffer);
		if (ms_staticVertexBufferDataMap)
		{
			StaticVertexBufferDataMap::iterator iter = ms_staticVertexBufferDataMap->find(staticVertexBuffer);
			if (iter != ms_staticVertexBufferDataMap->end())
			{
				buffer = iter->second->getBuffer();
				vertexData = iter->second->getData();
				stride = iter->second->getStride();
				vertexCount = iter->second->getVertexCount();
				descriptor = &iter->second->getDescriptorRef();
				formatFlags = iter->second->getFormatFlags();
			}
		}
	}
	else if (vertexBuffer.getType() == HardwareVertexBuffer::T_dynamic)
	{
		DynamicVertexBuffer const *dynamicVertexBuffer = static_cast<DynamicVertexBuffer const *>(&vertexBuffer);
		if (ms_dynamicVertexBufferDataMap)
		{
			DynamicVertexBufferDataMap::iterator iter = ms_dynamicVertexBufferDataMap->find(dynamicVertexBuffer);
			if (iter != ms_dynamicVertexBufferDataMap->end())
			{
				buffer = iter->second->getBuffer();
				vertexData = iter->second->getData();
				stride = iter->second->getStride();
				vertexCount = iter->second->getVertexCount();
				descriptor = &iter->second->getDescriptorRef();
				formatFlags = iter->second->getFormatFlags();
			}
		}
	}

	ms_activeVertexBuffer = buffer;
	ms_activeVertexData = vertexData;
	ms_activeVertexStride = stride;
	ms_activeVertexDescriptor = descriptor;
	ms_activeVertexFormatFlags = formatFlags;
	ms_activeVertexVector = false;
	ms_activeVertexStreamCount = 0;
	ms_sliceFirstVertex = 0;
	ms_sliceNumberOfVertices = vertexCount;

	if (ms_context)
	{
		UINT const offset = 0;
		ms_context->IASetVertexBuffers(0, 1, &ms_activeVertexBuffer, &ms_activeVertexStride, &offset);
	}
}

void Direct3d11Namespace::setVertexBufferVector(VertexBufferVector const &vertexBufferVector)
{
	++ms_vertexBufferSetCount;
	Direct3d11_VertexBufferVectorData const *data = Direct3d11_VertexBufferVectorData::getData(vertexBufferVector);
	if (!data)
		return;

	std::vector<HardwareVertexBuffer const *> const &vertexBuffers = data->getVertexBuffers();
	int const streamCount = static_cast<int>(std::min<size_t>(vertexBuffers.size(), 2));
	if (streamCount <= 0)
		return;

	ms_activeVertexVector = true;
	ms_activeVertexStreamCount = streamCount;
	ms_sliceFirstVertex = 0;
	ms_sliceNumberOfVertices = 0;

	UINT offsets[2] = { 0, 0 };
	for (int stream = 0; stream < 2; ++stream)
	{
		ms_activeVertexStreamBuffers[stream] = 0;
		ms_activeVertexStreamData[stream] = 0;
		ms_activeVertexStreamStrides[stream] = 0;
		ms_activeVertexStreamDescriptors[stream] = 0;
		ms_activeVertexStreamFormatFlags[stream] = 0;
	}

	for (int stream = 0; stream < streamCount; ++stream)
	{
		HardwareVertexBuffer const *vertexBuffer = vertexBuffers[static_cast<size_t>(stream)];
		if (!vertexBuffer)
			continue;

		ID3D11Buffer *buffer = 0;
		byte const *vertexData = 0;
		UINT stride = 0;
		int vertexCount = 0;
		VertexBufferDescriptor const *descriptor = 0;
		uint32 formatFlags = 0;

		if (vertexBuffer->getType() == HardwareVertexBuffer::T_static)
		{
			StaticVertexBuffer const *staticVertexBuffer = static_cast<StaticVertexBuffer const *>(vertexBuffer);
			if (ms_staticVertexBufferDataMap)
			{
				StaticVertexBufferDataMap::iterator iter = ms_staticVertexBufferDataMap->find(staticVertexBuffer);
				if (iter != ms_staticVertexBufferDataMap->end())
				{
					buffer = iter->second->getBuffer();
					vertexData = iter->second->getData();
					stride = iter->second->getStride();
					vertexCount = iter->second->getVertexCount();
					descriptor = &iter->second->getDescriptorRef();
					formatFlags = iter->second->getFormatFlags();
				}
			}
		}
		else if (vertexBuffer->getType() == HardwareVertexBuffer::T_dynamic)
		{
			DynamicVertexBuffer const *dynamicVertexBuffer = static_cast<DynamicVertexBuffer const *>(vertexBuffer);
			if (ms_dynamicVertexBufferDataMap)
			{
				DynamicVertexBufferDataMap::iterator iter = ms_dynamicVertexBufferDataMap->find(dynamicVertexBuffer);
				if (iter != ms_dynamicVertexBufferDataMap->end())
				{
					buffer = iter->second->getBuffer();
					vertexData = iter->second->getData();
					stride = iter->second->getStride();
					vertexCount = iter->second->getVertexCount();
					descriptor = &iter->second->getDescriptorRef();
					formatFlags = iter->second->getFormatFlags();
				}
			}
		}

		ms_activeVertexStreamBuffers[stream] = buffer;
		ms_activeVertexStreamData[stream] = vertexData;
		ms_activeVertexStreamStrides[stream] = stride;
		ms_activeVertexStreamDescriptors[stream] = descriptor;
		ms_activeVertexStreamFormatFlags[stream] = formatFlags;
		if (stream == 0)
		{
			ms_activeVertexBuffer = buffer;
			ms_activeVertexData = vertexData;
			ms_activeVertexStride = stride;
			ms_activeVertexDescriptor = descriptor;
			ms_activeVertexFormatFlags = formatFlags;
			ms_sliceNumberOfVertices = vertexCount;
		}
	}

	if (ms_context)
		ms_context->IASetVertexBuffers(0, static_cast<UINT>(streamCount), ms_activeVertexStreamBuffers, ms_activeVertexStreamStrides, offsets);
}

StaticIndexBufferGraphicsData *Direct3d11Namespace::createStaticIndexBufferData(const StaticIndexBuffer &indexBuffer)
{
	if (!ms_staticIndexBufferDataMap)
		ms_staticIndexBufferDataMap = new StaticIndexBufferDataMap;
	Direct3d11_StaticIndexBufferData *data = new Direct3d11_StaticIndexBufferData(indexBuffer);
	(*ms_staticIndexBufferDataMap)[&indexBuffer] = data;
	return data;
}

DynamicIndexBufferGraphicsData *Direct3d11Namespace::createDynamicIndexBufferData()
{
	return new Direct3d11_DynamicIndexBufferData(0);
}

void Direct3d11Namespace::setIndexBuffer(const HardwareIndexBuffer &indexBuffer)
{
	++ms_indexBufferSetCount;
	ID3D11Buffer *buffer = 0;
	Index const *indexData = 0;
	int indexCount = 0;

	if (indexBuffer.getType() == HardwareIndexBuffer::T_static)
	{
		StaticIndexBuffer const *staticIndexBuffer = static_cast<StaticIndexBuffer const *>(&indexBuffer);
		if (ms_staticIndexBufferDataMap)
		{
			StaticIndexBufferDataMap::iterator iter = ms_staticIndexBufferDataMap->find(staticIndexBuffer);
			if (iter != ms_staticIndexBufferDataMap->end())
			{
				buffer = iter->second->getBuffer();
				indexData = iter->second->getIndexData();
				indexCount = iter->second->getIndexCount();
			}
		}
	}
	else
	{
		Direct3d11_DynamicIndexBufferData *data = ms_lastDynamicIndexBufferData;
		if (data)
		{
			buffer = data->getBuffer();
			indexData = data->getIndexData();
			indexCount = data->getIndexCount();
		}
	}

	ms_activeIndexBuffer = buffer;
	ms_activeIndexData = indexData;
	ms_sliceFirstIndex = 0;
	ms_sliceNumberOfIndices = indexCount;

	if (ms_context)
		ms_context->IASetIndexBuffer(ms_activeIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
}
void Direct3d11Namespace::setDynamicIndexBufferSize(int numberOfIndices)
{
	ms_dynamicIndexBufferCapacity = numberOfIndices > 0 ? numberOfIndices : 0;
}

void Direct3d11Namespace::getOneToOneUVMapping(int textureWidth, int textureHeight, real &u0, real &v0, real &u1, real &v1)
{
	u0 = (CONST_REAL(0) + CONST_REAL(0.5)) / static_cast<real>(textureWidth);
	v0 = (CONST_REAL(0) + CONST_REAL(0.5)) / static_cast<real>(textureHeight);
	u1 = (static_cast<real>(textureWidth - 1) + CONST_REAL(0.5)) / static_cast<real>(textureWidth);
	v1 = (static_cast<real>(textureHeight - 1) + CONST_REAL(0.5)) / static_cast<real>(textureHeight);
}

TextureGraphicsData *Direct3d11Namespace::createTextureData(const Texture &texture, const TextureFormat *runtimeFormats, int numberOfRuntimeFormats) { return new ::Direct3d11_TextureData(texture, runtimeFormats, numberOfRuntimeFormats); }
ShaderImplementationPassVertexShaderGraphicsData *Direct3d11Namespace::createVertexShaderData(ShaderImplementationPassVertexShader const &) { return new Direct3d11_VertexShaderData; }
ShaderImplementationPassPixelShaderProgramGraphicsData *Direct3d11Namespace::createPixelShaderProgramData(ShaderImplementationPassPixelShaderProgram const &) { return new Direct3d11_PixelShaderProgramData; }

namespace
{
	bool resizeQuadListIndexBuffer(int numberOfQuads)
	{
		if (numberOfQuads <= 0 || !Direct3d11Namespace::ms_device)
			return false;

		if (Direct3d11Namespace::ms_quadListIndexBufferNumberOfQuads == 0)
		{
			Direct3d11Namespace::ms_quadListIndexBufferNumberOfQuads = numberOfQuads;
		}
		else if (numberOfQuads > Direct3d11Namespace::ms_quadListIndexBufferNumberOfQuads)
		{
			while (Direct3d11Namespace::ms_quadListIndexBufferNumberOfQuads < numberOfQuads)
				Direct3d11Namespace::ms_quadListIndexBufferNumberOfQuads *= 2;
		}
		else
		{
			return true;
		}

		Direct3d11Namespace::releaseCom(Direct3d11Namespace::ms_quadListIndexBuffer);

		std::vector<Index> indices(static_cast<size_t>(Direct3d11Namespace::ms_quadListIndexBufferNumberOfQuads) * 6);
		Index *index = indices.empty() ? 0 : &indices[0];
		for (int i = 0, base = 0; i < Direct3d11Namespace::ms_quadListIndexBufferNumberOfQuads; ++i, base += 4)
		{
			*index++ = static_cast<Index>(base + 0);
			*index++ = static_cast<Index>(base + 1);
			*index++ = static_cast<Index>(base + 2);
			*index++ = static_cast<Index>(base + 0);
			*index++ = static_cast<Index>(base + 2);
			*index++ = static_cast<Index>(base + 3);
		}

		Direct3d11Namespace::ms_quadListIndexBuffer = Direct3d11Namespace::createBuffer(D3D11_BIND_INDEX_BUFFER, &indices[0], indices.size() * sizeof(Index));
		return Direct3d11Namespace::ms_quadListIndexBuffer != 0;
	}

	bool resizeTriangleFanIndexBuffer(int numberOfVertices)
	{
		if (numberOfVertices < 3 || !Direct3d11Namespace::ms_device)
			return false;

		if (Direct3d11Namespace::ms_triangleFanIndexBufferNumberOfVertices == 0)
		{
			Direct3d11Namespace::ms_triangleFanIndexBufferNumberOfVertices = numberOfVertices;
		}
		else if (numberOfVertices > Direct3d11Namespace::ms_triangleFanIndexBufferNumberOfVertices)
		{
			while (Direct3d11Namespace::ms_triangleFanIndexBufferNumberOfVertices < numberOfVertices)
				Direct3d11Namespace::ms_triangleFanIndexBufferNumberOfVertices *= 2;
		}
		else
		{
			return true;
		}

		Direct3d11Namespace::releaseCom(Direct3d11Namespace::ms_triangleFanIndexBuffer);

		int const numberOfTriangles = Direct3d11Namespace::ms_triangleFanIndexBufferNumberOfVertices - 2;
		std::vector<Index> indices(static_cast<size_t>(numberOfTriangles) * 3);
		Index *index = indices.empty() ? 0 : &indices[0];
		for (int i = 1; i < Direct3d11Namespace::ms_triangleFanIndexBufferNumberOfVertices - 1; ++i)
		{
			*index++ = 0;
			*index++ = static_cast<Index>(i);
			*index++ = static_cast<Index>(i + 1);
		}

		Direct3d11Namespace::ms_triangleFanIndexBuffer = Direct3d11Namespace::createBuffer(D3D11_BIND_INDEX_BUFFER, &indices[0], indices.size() * sizeof(Index));
		return Direct3d11Namespace::ms_triangleFanIndexBuffer != 0;
	}

	void logTerrainDot3DrawState(char const *drawKind, D3D11_PRIMITIVE_TOPOLOGY topology, int baseIndex, int startIndex, int elementCount)
	{
		if (!Direct3d11Namespace::ms_activeTerrainDot3 || Direct3d11Namespace::ms_terrainDot3DrawLogCount >= 48)
			return;

		++Direct3d11Namespace::ms_terrainDot3DrawLogCount;
		Direct3d11Namespace::diag("terrain dot3 draw kind=%s topo=%d blend=%d mode=%d base=%d start=%d count=%d stageCount=%d texSet=%d alphaOnly=%d bgra=%d alphaBlend=%d src=%d dst=%d op=%d mat=%0.3f,%0.3f,%0.3f,%0.3f tf=%0.3f,%0.3f,%0.3f,%0.3f fog=%d density=%0.6f color=%0.3f,%0.3f,%0.3f activeFog=%0.3f,%0.3f,%0.3f lightAmb=%0.3f,%0.3f,%0.3f lightDiff=%0.3f,%0.3f,%0.3f lightDir=%0.3f,%0.3f,%0.3f dir=%d flags=0x%08x stride=%u streams=%d",
			drawKind,
			static_cast<int>(topology),
			Direct3d11Namespace::ms_activeTerrainBlendCount,
			Direct3d11Namespace::ms_activeTerrainProgramMode,
			baseIndex,
			startIndex,
			elementCount,
			Direct3d11Namespace::ms_activeStageTextureCount,
			Direct3d11Namespace::ms_activeTextureCoordinateSet,
			Direct3d11Namespace::ms_activeTextureAlphaOnly ? 1 : 0,
			Direct3d11Namespace::ms_activeTextureBgraSwizzle ? 1 : 0,
			Direct3d11Namespace::ms_alphaBlendEnabled ? 1 : 0,
			static_cast<int>(Direct3d11Namespace::ms_sourceBlend),
			static_cast<int>(Direct3d11Namespace::ms_destinationBlend),
			static_cast<int>(Direct3d11Namespace::ms_blendOp),
			Direct3d11Namespace::ms_materialColor[0],
			Direct3d11Namespace::ms_materialColor[1],
			Direct3d11Namespace::ms_materialColor[2],
			Direct3d11Namespace::ms_materialColor[3],
			Direct3d11Namespace::ms_textureFactor[0],
			Direct3d11Namespace::ms_textureFactor[1],
			Direct3d11Namespace::ms_textureFactor[2],
			Direct3d11Namespace::ms_textureFactor[3],
			Direct3d11Namespace::ms_fogEnabled ? 1 : 0,
			Direct3d11Namespace::ms_fogDensity,
			Direct3d11Namespace::ms_fogColor[0],
			Direct3d11Namespace::ms_fogColor[1],
			Direct3d11Namespace::ms_fogColor[2],
			Direct3d11Namespace::ms_activeFogColor[0],
			Direct3d11Namespace::ms_activeFogColor[1],
			Direct3d11Namespace::ms_activeFogColor[2],
			Direct3d11Namespace::ms_lightAmbient[0],
			Direct3d11Namespace::ms_lightAmbient[1],
			Direct3d11Namespace::ms_lightAmbient[2],
			Direct3d11Namespace::ms_lightDiffuse[0],
			Direct3d11Namespace::ms_lightDiffuse[1],
			Direct3d11Namespace::ms_lightDiffuse[2],
			Direct3d11Namespace::ms_lightDirection[0],
			Direct3d11Namespace::ms_lightDirection[1],
			Direct3d11Namespace::ms_lightDirection[2],
			Direct3d11Namespace::ms_lightDirectionalEnabled ? 1 : 0,
			Direct3d11Namespace::ms_activeVertexFormatFlags,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStride),
			Direct3d11Namespace::ms_activeVertexStreamCount);

		for (int i = 0; i < Direct3d11Namespace::cms_maxStageTextures; ++i)
		{
			if (!Direct3d11Namespace::ms_activeStageTextureViews[i] && Direct3d11Namespace::ms_activeStageNativeFormat[i] < 0 && !Direct3d11Namespace::ms_activeStageTextureTag[i])
				continue;

			Direct3d11Namespace::diag("terrain dot3 tex slot=%d tag=0x%08x fmt=%d srv=%d meta=%0.1f,%0.1f,%0.1f,%0.1f scroll=%0.3f,%0.3f,%0.3f,%0.3f",
				i,
				static_cast<unsigned>(Direct3d11Namespace::ms_activeStageTextureTag[i]),
				Direct3d11Namespace::ms_activeStageNativeFormat[i],
				Direct3d11Namespace::ms_activeStageTextureViews[i] ? 1 : 0,
				Direct3d11Namespace::ms_activeStageMeta[i][0],
				Direct3d11Namespace::ms_activeStageMeta[i][1],
				Direct3d11Namespace::ms_activeStageMeta[i][2],
				Direct3d11Namespace::ms_activeStageMeta[i][3],
				Direct3d11Namespace::ms_activeStageScroll[i][0],
				Direct3d11Namespace::ms_activeStageScroll[i][1],
				Direct3d11Namespace::ms_activeStageScroll[i][2],
				Direct3d11Namespace::ms_activeStageScroll[i][3]);
		}
	}

	void logStaticShellDrawState(char const *drawKind, D3D11_PRIMITIVE_TOPOLOGY topology, int baseIndex, int startIndex, int elementCount)
	{
		if (Direct3d11Namespace::ms_activeTerrainDot3 || Direct3d11Namespace::ms_activePixelProgramMode != 0 || Direct3d11Namespace::ms_staticDrawLogCount >= 96)
			return;

		char const *shaderName = Direct3d11Namespace::ms_activeStaticShaderName;
		if (!shaderName || (!strstr(shaderName, "stco") && !strstr(shaderName, "tato") && !strstr(shaderName, "tatt") && !strstr(shaderName, "cantina")))
			return;

		bool hasColor0 = false;
		bool hasColor1 = false;
		bool hasNormal = false;
		bool transformed = false;
		if (Direct3d11Namespace::ms_activeVertexVector)
		{
			for (int i = 0; i < Direct3d11Namespace::ms_activeVertexStreamCount; ++i)
			{
				VertexBufferFormat streamFormat;
				streamFormat.setFlags(Direct3d11Namespace::ms_activeVertexStreamFormatFlags[i]);
				hasColor0 = hasColor0 || streamFormat.hasColor0();
				hasColor1 = hasColor1 || streamFormat.hasColor1();
				hasNormal = hasNormal || streamFormat.hasNormal();
				transformed = transformed || streamFormat.isTransformed();
			}
		}
		else
		{
			VertexBufferFormat activeFormat;
			activeFormat.setFlags(Direct3d11Namespace::ms_activeVertexFormatFlags);
			hasColor0 = activeFormat.hasColor0();
			hasColor1 = activeFormat.hasColor1();
			hasNormal = activeFormat.hasNormal();
			transformed = activeFormat.isTransformed();
		}

		++Direct3d11Namespace::ms_staticDrawLogCount;
		Direct3d11Namespace::diag("static shell draw kind=%s topo=%d base=%d start=%d count=%d shader=%s vprogram=%s pprogram=%s lighting=%d colorVertex=%d srcA/D/S/E=%d/%d/%d/%d fullAmbient=%d texTag=0x%08x texFmt=%d texSet=%d alphaOnly=%d bgra=%d stages=%d hasC0/C1/N/T=%d/%d/%d/%d flags=0x%08x stride=%u streams=%d fog=%d passFog=%d density=%0.6f color=%0.3f,%0.3f,%0.3f activeFog=%0.3f,%0.3f,%0.3f alphaBlend=%d src=%d dst=%d op=%d lightAmb=%0.3f,%0.3f,%0.3f lightDiff=%0.3f,%0.3f,%0.3f lightDir=%0.3f,%0.3f,%0.3f matA=%0.3f,%0.3f,%0.3f matD=%0.3f,%0.3f,%0.3f tf=%0.3f,%0.3f,%0.3f,%0.3f",
			drawKind,
			static_cast<int>(topology),
			baseIndex,
			startIndex,
			elementCount,
			shaderName,
			Direct3d11Namespace::ms_activeVertexProgramName,
			Direct3d11Namespace::ms_activePixelProgramName,
			Direct3d11Namespace::ms_lightingEnabled ? 1 : 0,
			Direct3d11Namespace::ms_lightingColorVertex ? 1 : 0,
			Direct3d11Namespace::ms_lightingAmbientColorSource,
			Direct3d11Namespace::ms_lightingDiffuseColorSource,
			Direct3d11Namespace::ms_lightingSpecularColorSource,
			Direct3d11Namespace::ms_lightingEmissiveColorSource,
			Direct3d11Namespace::ms_activeFullAmbient ? 1 : 0,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeTextureTag),
			Direct3d11Namespace::ms_activeTextureNativeFormat,
			Direct3d11Namespace::ms_activeTextureCoordinateSet,
			Direct3d11Namespace::ms_activeTextureAlphaOnly ? 1 : 0,
			Direct3d11Namespace::ms_activeTextureBgraSwizzle ? 1 : 0,
			Direct3d11Namespace::ms_activeStageTextureCount,
			hasColor0 ? 1 : 0,
			hasColor1 ? 1 : 0,
			hasNormal ? 1 : 0,
			transformed ? 1 : 0,
			Direct3d11Namespace::ms_activeVertexFormatFlags,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStride),
			Direct3d11Namespace::ms_activeVertexStreamCount,
			Direct3d11Namespace::ms_fogEnabled ? 1 : 0,
			Direct3d11Namespace::ms_activePassFogMode,
			Direct3d11Namespace::ms_fogDensity,
			Direct3d11Namespace::ms_fogColor[0],
			Direct3d11Namespace::ms_fogColor[1],
			Direct3d11Namespace::ms_fogColor[2],
			Direct3d11Namespace::ms_activeFogColor[0],
			Direct3d11Namespace::ms_activeFogColor[1],
			Direct3d11Namespace::ms_activeFogColor[2],
			Direct3d11Namespace::ms_alphaBlendEnabled ? 1 : 0,
			static_cast<int>(Direct3d11Namespace::ms_sourceBlend),
			static_cast<int>(Direct3d11Namespace::ms_destinationBlend),
			static_cast<int>(Direct3d11Namespace::ms_blendOp),
			Direct3d11Namespace::ms_lightAmbient[0],
			Direct3d11Namespace::ms_lightAmbient[1],
			Direct3d11Namespace::ms_lightAmbient[2],
			Direct3d11Namespace::ms_lightDiffuse[0],
			Direct3d11Namespace::ms_lightDiffuse[1],
			Direct3d11Namespace::ms_lightDiffuse[2],
			Direct3d11Namespace::ms_lightDirection[0],
			Direct3d11Namespace::ms_lightDirection[1],
			Direct3d11Namespace::ms_lightDirection[2],
			Direct3d11Namespace::ms_materialAmbientColor[0],
			Direct3d11Namespace::ms_materialAmbientColor[1],
			Direct3d11Namespace::ms_materialAmbientColor[2],
			Direct3d11Namespace::ms_materialColor[0],
			Direct3d11Namespace::ms_materialColor[1],
			Direct3d11Namespace::ms_materialColor[2],
			Direct3d11Namespace::ms_textureFactor[0],
			Direct3d11Namespace::ms_textureFactor[1],
			Direct3d11Namespace::ms_textureFactor[2],
			Direct3d11Namespace::ms_textureFactor[3]);
		for (int i = 0; i < Direct3d11Namespace::cms_maxStageTextures; ++i)
		{
			if (!Direct3d11Namespace::ms_activeStageTextureViews[i] && Direct3d11Namespace::ms_activeStageNativeFormat[i] < 0 && !Direct3d11Namespace::ms_activeStageTextureTag[i])
				continue;

			Direct3d11Namespace::diag("static shell tex slot=%d tag=0x%08x fmt=%d srv=%d meta=%0.1f,%0.1f,%0.1f,%0.1f",
				i,
				static_cast<unsigned>(Direct3d11Namespace::ms_activeStageTextureTag[i]),
				Direct3d11Namespace::ms_activeStageNativeFormat[i],
				Direct3d11Namespace::ms_activeStageTextureViews[i] ? 1 : 0,
				Direct3d11Namespace::ms_activeStageMeta[i][0],
				Direct3d11Namespace::ms_activeStageMeta[i][1],
				Direct3d11Namespace::ms_activeStageMeta[i][2],
				Direct3d11Namespace::ms_activeStageMeta[i][3]);
		}
	}

	void logShadowBlobDrawState(char const *drawKind, D3D11_PRIMITIVE_TOPOLOGY topology, int baseIndex, int startIndex, int elementCount)
	{
		if (!Direct3d11Namespace::isShadowBlobShaderName(Direct3d11Namespace::ms_activeStaticShaderName) || Direct3d11Namespace::ms_shadowBlobDrawLogCount >= 256)
			return;

		float minX = 0.0f;
		float minY = 0.0f;
		float minZ = 0.0f;
		float maxX = 0.0f;
		float maxY = 0.0f;
		float maxZ = 0.0f;
		float minU = 0.0f;
		float minV = 0.0f;
		float maxU = 0.0f;
		float maxV = 0.0f;
		unsigned firstColor = 0;
		int vertexProbeCount = 0;
		if (Direct3d11Namespace::ms_activeVertexData && Direct3d11Namespace::ms_activeVertexDescriptor && Direct3d11Namespace::ms_activeVertexStride > 0)
		{
			VertexBufferDescriptor const &descriptor = *Direct3d11Namespace::ms_activeVertexDescriptor;
			int const vertexCount = std::min(Direct3d11Namespace::ms_sliceNumberOfVertices, 512);
			for (int i = 0; i < vertexCount; ++i)
			{
				byte const *vertex = Direct3d11Namespace::ms_activeVertexData + static_cast<size_t>(Direct3d11Namespace::ms_sliceFirstVertex + i) * Direct3d11Namespace::ms_activeVertexStride;
				if (descriptor.offsetPosition >= 0)
				{
					float const *position = reinterpret_cast<float const *>(vertex + descriptor.offsetPosition);
					if (vertexProbeCount == 0)
						minX = maxX = position[0], minY = maxY = position[1], minZ = maxZ = position[2];
					else
						minX = std::min(minX, position[0]), maxX = std::max(maxX, position[0]), minY = std::min(minY, position[1]), maxY = std::max(maxY, position[1]), minZ = std::min(minZ, position[2]), maxZ = std::max(maxZ, position[2]);
				}
				if (descriptor.offsetTextureCoordinateSet[0] >= 0)
				{
					float const *uv = reinterpret_cast<float const *>(vertex + descriptor.offsetTextureCoordinateSet[0]);
					if (vertexProbeCount == 0)
						minU = maxU = uv[0], minV = maxV = uv[1];
					else
						minU = std::min(minU, uv[0]), maxU = std::max(maxU, uv[0]), minV = std::min(minV, uv[1]), maxV = std::max(maxV, uv[1]);
				}
				if (vertexProbeCount == 0 && descriptor.offsetColor0 >= 0)
					firstColor = *reinterpret_cast<unsigned const *>(vertex + descriptor.offsetColor0);
				++vertexProbeCount;
			}
		}

		++Direct3d11Namespace::ms_shadowBlobDrawLogCount;
		Direct3d11Namespace::diag("shadowblob draw d3d11 frame=%d draw=%03d kind=%s topo=%d base=%d start=%d count=%d shader=%s vprogram=%s pprogram=%s pmode=%d stages=%d texTag=0x%08x texFmt=%d texSet=%d alphaBlend=%d alphaFade=%d alphaFadeOpacity=%0.3f src=%d dst=%d op=%d alphaTest=%d cmp=%d ref=%0.3f depth=%d write=%d colorWrite=0x%02x fog=%d passFog=%d vprobe=%d bounds=%0.3f,%0.3f,%0.3f-%0.3f,%0.3f,%0.3f uv=%0.3f,%0.3f-%0.3f,%0.3f c0=0x%08x",
			Direct3d11Namespace::ms_presentCount,
			Direct3d11Namespace::ms_shadowBlobDrawLogCount,
			drawKind,
			static_cast<int>(topology),
			baseIndex,
			startIndex,
			elementCount,
			Direct3d11Namespace::ms_activeStaticShaderName,
			Direct3d11Namespace::ms_activeVertexProgramName,
			Direct3d11Namespace::ms_activePixelProgramName,
			Direct3d11Namespace::ms_activePixelProgramMode,
			Direct3d11Namespace::ms_activeStageTextureCount,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeTextureTag),
			Direct3d11Namespace::ms_activeTextureNativeFormat,
			Direct3d11Namespace::ms_activeTextureCoordinateSet,
			Direct3d11Namespace::ms_alphaBlendEnabled ? 1 : 0,
			Direct3d11Namespace::ms_alphaFadeOpacityEnabled ? 1 : 0,
			Direct3d11Namespace::ms_alphaFadeOpacity,
			static_cast<int>(Direct3d11Namespace::ms_sourceBlend),
			static_cast<int>(Direct3d11Namespace::ms_destinationBlend),
			static_cast<int>(Direct3d11Namespace::ms_blendOp),
			Direct3d11Namespace::ms_alphaTestEnabled ? 1 : 0,
			Direct3d11Namespace::ms_alphaTestCompare,
			Direct3d11Namespace::ms_alphaTestReference,
			Direct3d11Namespace::ms_depthEnabled ? 1 : 0,
			Direct3d11Namespace::ms_depthWriteEnabled ? 1 : 0,
			static_cast<unsigned>(Direct3d11Namespace::ms_colorWriteMask),
			Direct3d11Namespace::ms_fogEnabled ? 1 : 0,
			Direct3d11Namespace::ms_activePassFogMode,
			vertexProbeCount,
			minX,
			minY,
			minZ,
			maxX,
			maxY,
			maxZ,
			minU,
			minV,
			maxU,
			maxV,
			firstColor);
	}

	bool isMobileShaderName(char const *shaderName)
	{
		if (!shaderName)
			return false;

		return strstr(shaderName, "hum_") || strstr(shaderName, "human") || strstr(shaderName, "mgn_") || strstr(shaderName, "creature") || strstr(shaderName, "player") || strstr(shaderName, "body") || strstr(shaderName, "face");
	}

	bool computeMobileNormalProbe(int &sampleCount, float &dotNegMin, float &dotNegAvg, float &dotNegMax, float &dotRawMin, float &dotRawAvg, float &dotRawMax, float *firstNormal, float *firstNormalWorld)
	{
		sampleCount = 0;
		dotNegMin = 999.0f;
		dotNegAvg = 0.0f;
		dotNegMax = -999.0f;
		dotRawMin = 999.0f;
		dotRawAvg = 0.0f;
		dotRawMax = -999.0f;
		firstNormal[0] = firstNormal[1] = firstNormal[2] = 0.0f;
		firstNormalWorld[0] = firstNormalWorld[1] = firstNormalWorld[2] = 0.0f;

		byte const *vertexData = Direct3d11Namespace::ms_activeVertexData;
		VertexBufferDescriptor const *descriptor = Direct3d11Namespace::ms_activeVertexDescriptor;
		UINT stride = Direct3d11Namespace::ms_activeVertexStride;
		int vertexCount = Direct3d11Namespace::ms_sliceNumberOfVertices;
		if (Direct3d11Namespace::ms_activeVertexVector && Direct3d11Namespace::ms_activeVertexStreamCount > 0)
		{
			vertexData = Direct3d11Namespace::ms_activeVertexStreamData[0];
			descriptor = Direct3d11Namespace::ms_activeVertexStreamDescriptors[0];
			stride = Direct3d11Namespace::ms_activeVertexStreamStrides[0];
		}
		if (!vertexData || !descriptor || descriptor->offsetNormal < 0 || stride < static_cast<UINT>(descriptor->offsetNormal + static_cast<int>(sizeof(float) * 3)) || vertexCount <= 0)
			return false;

		float lightRaw[3] = { Direct3d11Namespace::ms_parallelLightDirection[0][0], Direct3d11Namespace::ms_parallelLightDirection[0][1], Direct3d11Namespace::ms_parallelLightDirection[0][2] };
		if (Direct3d11Namespace::ms_parallelLightDirection[0][3] <= 0.5f)
		{
			lightRaw[0] = Direct3d11Namespace::ms_lightDirection[0];
			lightRaw[1] = Direct3d11Namespace::ms_lightDirection[1];
			lightRaw[2] = Direct3d11Namespace::ms_lightDirection[2];
		}
		float const lightLength = sqrtf(lightRaw[0] * lightRaw[0] + lightRaw[1] * lightRaw[1] + lightRaw[2] * lightRaw[2]);
		if (lightLength <= 0.00001f)
			return false;
		lightRaw[0] /= lightLength;
		lightRaw[1] /= lightLength;
		lightRaw[2] /= lightLength;

		int const maxSamples = std::min(vertexCount, 64);
		for (int i = 0; i < maxSamples; ++i)
		{
			float const *normal = reinterpret_cast<float const *>(vertexData + static_cast<size_t>(i) * stride + descriptor->offsetNormal);
			float nx = normal[0];
			float ny = normal[1];
			float nz = normal[2];
			float nLength = sqrtf(nx * nx + ny * ny + nz * nz);
			if (nLength <= 0.00001f)
				continue;
			nx /= nLength;
			ny /= nLength;
			nz /= nLength;

			float wx = nx * Direct3d11Namespace::ms_objectToWorldRotationMatrix.m[0][0] + ny * Direct3d11Namespace::ms_objectToWorldRotationMatrix.m[1][0] + nz * Direct3d11Namespace::ms_objectToWorldRotationMatrix.m[2][0];
			float wy = nx * Direct3d11Namespace::ms_objectToWorldRotationMatrix.m[0][1] + ny * Direct3d11Namespace::ms_objectToWorldRotationMatrix.m[1][1] + nz * Direct3d11Namespace::ms_objectToWorldRotationMatrix.m[2][1];
			float wz = nx * Direct3d11Namespace::ms_objectToWorldRotationMatrix.m[0][2] + ny * Direct3d11Namespace::ms_objectToWorldRotationMatrix.m[1][2] + nz * Direct3d11Namespace::ms_objectToWorldRotationMatrix.m[2][2];
			float const wLength = sqrtf(wx * wx + wy * wy + wz * wz);
			if (wLength <= 0.00001f)
				continue;
			wx /= wLength;
			wy /= wLength;
			wz /= wLength;

			float const dotRaw = std::max(0.0f, wx * lightRaw[0] + wy * lightRaw[1] + wz * lightRaw[2]);
			float const dotNeg = std::max(0.0f, wx * -lightRaw[0] + wy * -lightRaw[1] + wz * -lightRaw[2]);
			if (sampleCount == 0)
			{
				firstNormal[0] = nx;
				firstNormal[1] = ny;
				firstNormal[2] = nz;
				firstNormalWorld[0] = wx;
				firstNormalWorld[1] = wy;
				firstNormalWorld[2] = wz;
			}
			dotNegMin = std::min(dotNegMin, dotNeg);
			dotNegMax = std::max(dotNegMax, dotNeg);
			dotNegAvg += dotNeg;
			dotRawMin = std::min(dotRawMin, dotRaw);
			dotRawMax = std::max(dotRawMax, dotRaw);
			dotRawAvg += dotRaw;
			++sampleCount;
		}
		if (sampleCount <= 0)
			return false;

		dotNegAvg /= static_cast<float>(sampleCount);
		dotRawAvg /= static_cast<float>(sampleCount);
		return true;
	}

	void logMobileDrawState(char const *drawKind, D3D11_PRIMITIVE_TOPOLOGY topology, int baseIndex, int startIndex, int elementCount)
	{
		char const *shaderName = Direct3d11Namespace::ms_activeStaticShaderName;
		if ((!Direct3d11Namespace::ms_activeVertexVector && !isMobileShaderName(shaderName)) || Direct3d11Namespace::ms_mobileDrawLogCount >= 512)
			return;

		bool hasColor0 = false;
		bool hasColor1 = false;
		bool hasNormal = false;
		bool transformed = false;
		for (int i = 0; i < Direct3d11Namespace::ms_activeVertexStreamCount && i < 2; ++i)
		{
			VertexBufferFormat streamFormat;
			streamFormat.setFlags(Direct3d11Namespace::ms_activeVertexStreamFormatFlags[i]);
			hasColor0 = hasColor0 || streamFormat.hasColor0();
			hasColor1 = hasColor1 || streamFormat.hasColor1();
			hasNormal = hasNormal || streamFormat.hasNormal();
			transformed = transformed || streamFormat.isTransformed();
		}
		if (!Direct3d11Namespace::ms_activeVertexVector)
		{
			VertexBufferFormat activeFormat;
			activeFormat.setFlags(Direct3d11Namespace::ms_activeVertexFormatFlags);
			hasColor0 = activeFormat.hasColor0();
			hasColor1 = activeFormat.hasColor1();
			hasNormal = activeFormat.hasNormal();
			transformed = activeFormat.isTransformed();
		}
		bool const actorBodyShade = (Direct3d11Namespace::ms_activeVertexVector || Direct3d11Namespace::isActorShaderName(shaderName)) && hasNormal && !hasColor0 && !hasColor1 && !transformed && Direct3d11Namespace::ms_lightingEnabled && Direct3d11Namespace::ms_activeVertexProgramMode == 0 && Direct3d11Namespace::ms_activePixelProgramMode == 0;

		Vector const lightDirection(Direct3d11Namespace::ms_lightDirection[0], Direct3d11Namespace::ms_lightDirection[1], Direct3d11Namespace::ms_lightDirection[2]);
		Vector const localLightDirection = Direct3d11Namespace::ms_objectToWorldTransform.rotate_p2l(lightDirection);
		int normalProbeCount = 0;
		float dotNegMin = 0.0f;
		float dotNegAvg = 0.0f;
		float dotNegMax = 0.0f;
		float dotRawMin = 0.0f;
		float dotRawAvg = 0.0f;
		float dotRawMax = 0.0f;
		float firstNormal[3] = { 0.0f, 0.0f, 0.0f };
		float firstNormalWorld[3] = { 0.0f, 0.0f, 0.0f };
		IGNORE_RETURN(computeMobileNormalProbe(normalProbeCount, dotNegMin, dotNegAvg, dotNegMax, dotRawMin, dotRawAvg, dotRawMax, firstNormal, firstNormalWorld));

		++Direct3d11Namespace::ms_mobileDrawLogCount;
		Direct3d11Namespace::diag("mobile draw d3d11 frame=%d draw=%03d kind=%s topo=%d base=%d start=%d count=%d shader=%s vprogram=%s pprogram=%s vmode=%d pmode=%d stages=%d vector=%d streams=%d flags=0x%08x stride=%u s0flags=0x%08x s0stride=%u s1flags=0x%08x s1stride=%u hasC0/C1/N/T=%d/%d/%d/%d actorShade=%d lighting=%d colorVertex=%d specEnable=%d srcA/D/S/E=%d/%d/%d/%d fullAmbient=%d lightMask=0x%04x worldDir=%0.3f,%0.3f,%0.3f localDir=%0.3f,%0.3f,%0.3f normalProbe=%d dotNeg=%0.3f/%0.3f/%0.3f dotRaw=%0.3f/%0.3f/%0.3f n0=%0.3f,%0.3f,%0.3f nw0=%0.3f,%0.3f,%0.3f amb=%0.3f,%0.3f,%0.3f diff=%0.3f,%0.3f,%0.3f matA=%0.3f,%0.3f,%0.3f matD=%0.3f,%0.3f,%0.3f tf=%0.3f,%0.3f,%0.3f,%0.3f texTag=0x%08x texFmt=%d texSet=%d alphaBlend=%d fog=%d",
			Direct3d11Namespace::ms_presentCount,
			Direct3d11Namespace::ms_mobileDrawLogCount,
			drawKind,
			static_cast<int>(topology),
			baseIndex,
			startIndex,
			elementCount,
			shaderName ? shaderName : "<null>",
			Direct3d11Namespace::ms_activeVertexProgramName,
			Direct3d11Namespace::ms_activePixelProgramName,
			Direct3d11Namespace::ms_activeVertexProgramMode,
			Direct3d11Namespace::ms_activePixelProgramMode,
			Direct3d11Namespace::ms_activeStageTextureCount,
			Direct3d11Namespace::ms_activeVertexVector ? 1 : 0,
			Direct3d11Namespace::ms_activeVertexStreamCount,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexFormatFlags),
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStride),
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStreamFormatFlags[0]),
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStreamStrides[0]),
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStreamFormatFlags[1]),
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStreamStrides[1]),
			hasColor0 ? 1 : 0,
			hasColor1 ? 1 : 0,
			hasNormal ? 1 : 0,
			transformed ? 1 : 0,
			actorBodyShade ? 1 : 0,
			Direct3d11Namespace::ms_lightingEnabled ? 1 : 0,
			Direct3d11Namespace::ms_lightingColorVertex ? 1 : 0,
			Direct3d11Namespace::ms_lightingSpecularEnabled ? 1 : 0,
			Direct3d11Namespace::ms_lightingAmbientColorSource,
			Direct3d11Namespace::ms_lightingDiffuseColorSource,
			Direct3d11Namespace::ms_lightingSpecularColorSource,
			Direct3d11Namespace::ms_lightingEmissiveColorSource,
			Direct3d11Namespace::ms_activeFullAmbient ? 1 : 0,
			static_cast<unsigned>(Direct3d11Namespace::ms_selectedLightMask),
			Direct3d11Namespace::ms_lightDirection[0],
			Direct3d11Namespace::ms_lightDirection[1],
			Direct3d11Namespace::ms_lightDirection[2],
			-localLightDirection.x,
			-localLightDirection.y,
			-localLightDirection.z,
			normalProbeCount,
			dotNegMin,
			dotNegAvg,
			dotNegMax,
			dotRawMin,
			dotRawAvg,
			dotRawMax,
			firstNormal[0],
			firstNormal[1],
			firstNormal[2],
			firstNormalWorld[0],
			firstNormalWorld[1],
			firstNormalWorld[2],
			Direct3d11Namespace::ms_lightAmbient[0],
			Direct3d11Namespace::ms_lightAmbient[1],
			Direct3d11Namespace::ms_lightAmbient[2],
			Direct3d11Namespace::ms_lightDiffuse[0],
			Direct3d11Namespace::ms_lightDiffuse[1],
			Direct3d11Namespace::ms_lightDiffuse[2],
			Direct3d11Namespace::ms_materialAmbientColor[0],
			Direct3d11Namespace::ms_materialAmbientColor[1],
			Direct3d11Namespace::ms_materialAmbientColor[2],
			Direct3d11Namespace::ms_materialColor[0],
			Direct3d11Namespace::ms_materialColor[1],
			Direct3d11Namespace::ms_materialColor[2],
			Direct3d11Namespace::ms_textureFactor[0],
			Direct3d11Namespace::ms_textureFactor[1],
			Direct3d11Namespace::ms_textureFactor[2],
			Direct3d11Namespace::ms_textureFactor[3],
			static_cast<unsigned>(Direct3d11Namespace::ms_activeTextureTag),
			Direct3d11Namespace::ms_activeTextureNativeFormat,
			Direct3d11Namespace::ms_activeTextureCoordinateSet,
			Direct3d11Namespace::ms_alphaBlendEnabled ? 1 : 0,
			Direct3d11Namespace::ms_fogEnabled ? 1 : 0);
	}

	void logTransformedDrawState(char const *drawKind, D3D11_PRIMITIVE_TOPOLOGY topology, int baseIndex, int startIndex, int elementCount)
	{
		bool transformed = false;
		bool hasColor0 = false;
		int texCoordSets = 0;
		if (Direct3d11Namespace::ms_activeVertexVector)
		{
			for (int i = 0; i < Direct3d11Namespace::ms_activeVertexStreamCount; ++i)
			{
				VertexBufferFormat streamFormat;
				streamFormat.setFlags(Direct3d11Namespace::ms_activeVertexStreamFormatFlags[i]);
				transformed = transformed || streamFormat.isTransformed();
				hasColor0 = hasColor0 || streamFormat.hasColor0();
				texCoordSets = std::max(texCoordSets, static_cast<int>(streamFormat.getNumberOfTextureCoordinateSets()));
			}
		}
		else
		{
			VertexBufferFormat activeFormat;
			activeFormat.setFlags(Direct3d11Namespace::ms_activeVertexFormatFlags);
			transformed = activeFormat.isTransformed();
			hasColor0 = activeFormat.hasColor0();
			texCoordSets = activeFormat.getNumberOfTextureCoordinateSets();
		}

		if (!transformed)
			return;

		char const *shaderName = Direct3d11Namespace::ms_activeStaticShaderName;
		char const *pixelProgramName = Direct3d11Namespace::ms_activePixelProgramName;
		bool const inGameplayFrame = Direct3d11Namespace::ms_presentCount > 80;
		bool const interestingShader =
			(shaderName && (strstr(shaderName, "uicanvas") || strstr(shaderName, "ui_shader") || strstr(shaderName, "font") || strstr(shaderName, "text"))) ||
			(pixelProgramName && strstr(pixelProgramName, "ui.psh"));
		bool const interestingGeometry = texCoordSets > 1 || strcmp(drawKind, "triangleFan") == 0 || strcmp(drawKind, "quadList") == 0;
		if ((!inGameplayFrame || (!interestingShader && !interestingGeometry)) && Direct3d11Namespace::ms_transformedDrawLogCount >= 40)
			return;
		if (Direct3d11Namespace::ms_transformedDrawLogCount >= 420)
			return;

		float minX = 0.0f;
		float minY = 0.0f;
		float maxX = 0.0f;
		float maxY = 0.0f;
		float minZ = 0.0f;
		float maxZ = 0.0f;
		unsigned firstColor = 0;
		int vertexProbeCount = 0;
		if (Direct3d11Namespace::ms_activeVertexData && Direct3d11Namespace::ms_activeVertexDescriptor && Direct3d11Namespace::ms_activeVertexStride > 0 && Direct3d11Namespace::ms_activeVertexDescriptor->offsetPosition >= 0)
		{
			VertexBufferDescriptor const &descriptor = *Direct3d11Namespace::ms_activeVertexDescriptor;
			int const vertexCount = std::min(Direct3d11Namespace::ms_sliceNumberOfVertices, 512);
			for (int i = 0; i < vertexCount; ++i)
			{
				byte const *vertex = Direct3d11Namespace::ms_activeVertexData + (Direct3d11Namespace::ms_sliceFirstVertex + i) * Direct3d11Namespace::ms_activeVertexStride;
				float const *position = reinterpret_cast<float const *>(vertex + descriptor.offsetPosition);
				float const x = position[0];
				float const y = position[1];
				float const z = position[2];
				if (vertexProbeCount == 0)
				{
					minX = maxX = x;
					minY = maxY = y;
					minZ = maxZ = z;
					if (descriptor.offsetColor0 >= 0)
						firstColor = *reinterpret_cast<unsigned const *>(vertex + descriptor.offsetColor0);
				}
				else
				{
					minX = std::min(minX, x);
					minY = std::min(minY, y);
					maxX = std::max(maxX, x);
					maxY = std::max(maxY, y);
					minZ = std::min(minZ, z);
					maxZ = std::max(maxZ, z);
				}
				++vertexProbeCount;
			}
		}

		++Direct3d11Namespace::ms_transformedDrawLogCount;
		Direct3d11Namespace::diag("transformed draw kind=%s topo=%d base=%d start=%d count=%d shader=%s vprogram=%s pprogram=%s texSets=%d color=%d activeTexSet=%d texTag=0x%08x texFmt=%d stages=%d alphaBlend=%d src=%d dst=%d op=%d alphaTest=%d cmp=%d ref=%0.3f depth=%d write=%d scissor=%d rect=%ld,%ld,%ld,%ld vprobe=%d bounds=%0.1f,%0.1f-%0.1f,%0.1f z=%0.3f-%0.3f c0=0x%08x a=%u s0 tag=0x%08x fmt=%d tcs=%0.0f meta=%0.1f,%0.1f,%0.1f,%0.1f s1 tag=0x%08x fmt=%d tcs=%0.0f meta=%0.1f,%0.1f,%0.1f,%0.1f",
			drawKind,
			static_cast<int>(topology),
			baseIndex,
			startIndex,
			elementCount,
			Direct3d11Namespace::ms_activeStaticShaderName,
			Direct3d11Namespace::ms_activeVertexProgramName,
			Direct3d11Namespace::ms_activePixelProgramName,
			texCoordSets,
			hasColor0 ? 1 : 0,
			Direct3d11Namespace::ms_activeTextureCoordinateSet,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeTextureTag),
			Direct3d11Namespace::ms_activeTextureNativeFormat,
			Direct3d11Namespace::ms_activeStageTextureCount,
			Direct3d11Namespace::ms_alphaBlendEnabled ? 1 : 0,
			static_cast<int>(Direct3d11Namespace::ms_sourceBlend),
			static_cast<int>(Direct3d11Namespace::ms_destinationBlend),
			static_cast<int>(Direct3d11Namespace::ms_blendOp),
			Direct3d11Namespace::ms_alphaTestEnabled ? 1 : 0,
			Direct3d11Namespace::ms_alphaTestCompare,
			Direct3d11Namespace::ms_alphaTestReference,
			Direct3d11Namespace::ms_depthEnabled ? 1 : 0,
			Direct3d11Namespace::ms_depthWriteEnabled ? 1 : 0,
			Direct3d11Namespace::ms_scissorEnabled ? 1 : 0,
			Direct3d11Namespace::ms_scissorRect.left,
			Direct3d11Namespace::ms_scissorRect.top,
			Direct3d11Namespace::ms_scissorRect.right,
			Direct3d11Namespace::ms_scissorRect.bottom,
			vertexProbeCount,
			minX,
			minY,
			maxX,
			maxY,
			minZ,
			maxZ,
			firstColor,
			(firstColor >> 24) & 0xff,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeStageTextureTag[0]),
			Direct3d11Namespace::ms_activeStageNativeFormat[0],
			Direct3d11Namespace::ms_activeStageTextureCoordinateSet[0][0],
			Direct3d11Namespace::ms_activeStageMeta[0][0],
			Direct3d11Namespace::ms_activeStageMeta[0][1],
			Direct3d11Namespace::ms_activeStageMeta[0][2],
			Direct3d11Namespace::ms_activeStageMeta[0][3],
			static_cast<unsigned>(Direct3d11Namespace::ms_activeStageTextureTag[1]),
			Direct3d11Namespace::ms_activeStageNativeFormat[1],
			Direct3d11Namespace::ms_activeStageTextureCoordinateSet[1][0],
			Direct3d11Namespace::ms_activeStageMeta[1][0],
			Direct3d11Namespace::ms_activeStageMeta[1][1],
			Direct3d11Namespace::ms_activeStageMeta[1][2],
				Direct3d11Namespace::ms_activeStageMeta[1][3]);
	}

	bool isRendererInventoryEnabled()
	{
		if (Direct3d11Namespace::ms_rendererInventoryEnabled < 0)
		{
			char enabled[16];
			DWORD const length = GetEnvironmentVariableA("SWG_RENDERER_INVENTORY", enabled, sizeof(enabled));
			Direct3d11Namespace::ms_rendererInventoryEnabled = (length > 0 && length < sizeof(enabled) && enabled[0] != '0') ? 1 : 0;
		}

		return Direct3d11Namespace::ms_rendererInventoryEnabled != 0;
	}

	int getRendererInventoryLimit()
	{
		if (Direct3d11Namespace::ms_rendererInventoryLimit < 0)
		{
			char limitText[32];
			DWORD const length = GetEnvironmentVariableA("SWG_RENDERER_INVENTORY_LIMIT", limitText, sizeof(limitText));
			Direct3d11Namespace::ms_rendererInventoryLimit = (length > 0 && length < sizeof(limitText)) ? atoi(limitText) : 50000;
			if (Direct3d11Namespace::ms_rendererInventoryLimit <= 0)
				Direct3d11Namespace::ms_rendererInventoryLimit = 0x7fffffff;
		}

		return Direct3d11Namespace::ms_rendererInventoryLimit;
	}

	int getRendererInventoryStartFrame()
	{
		if (Direct3d11Namespace::ms_rendererInventoryStartFrame < 0)
		{
			char startText[32];
			DWORD const length = GetEnvironmentVariableA("SWG_RENDERER_INVENTORY_START_FRAME", startText, sizeof(startText));
			Direct3d11Namespace::ms_rendererInventoryStartFrame = (length > 0 && length < sizeof(startText)) ? atoi(startText) : 0;
			if (Direct3d11Namespace::ms_rendererInventoryStartFrame < 0)
				Direct3d11Namespace::ms_rendererInventoryStartFrame = 0;
		}

		return Direct3d11Namespace::ms_rendererInventoryStartFrame;
	}

	int getAutoCaptureWorldRowsThreshold()
	{
		if (Direct3d11Namespace::ms_autoCaptureWorldRowsThreshold < 0)
		{
			char thresholdText[32];
			DWORD const length = GetEnvironmentVariableA("SWG_RENDERER_AUTOCAPTURE_WORLD_ROWS", thresholdText, sizeof(thresholdText));
			Direct3d11Namespace::ms_autoCaptureWorldRowsThreshold = (length > 0 && length < sizeof(thresholdText)) ? atoi(thresholdText) : 80;
			if (Direct3d11Namespace::ms_autoCaptureWorldRowsThreshold < 1)
				Direct3d11Namespace::ms_autoCaptureWorldRowsThreshold = 1;
		}

		return Direct3d11Namespace::ms_autoCaptureWorldRowsThreshold;
	}

	bool isUiOrLoadingShaderName(char const *shaderName)
	{
		if (!shaderName || !shaderName[0])
			return true;

		return strncmp(shaderName, "shader/uicanvas", 15) == 0
			|| strncmp(shaderName, "shader/ui_", 10) == 0
			|| strncmp(shaderName, "shader/2d_", 10) == 0
			|| strncmp(shaderName, "shader/font", 11) == 0
			|| strncmp(shaderName, "shader/text", 11) == 0;
	}

	void armAutoCaptureFromRendererInventory()
	{
		if (Direct3d11Namespace::ms_autoCaptureDone || Direct3d11Namespace::ms_autoCaptureRequested || isUiOrLoadingShaderName(Direct3d11Namespace::ms_activeStaticShaderName))
			return;

		char captureBase[MAX_PATH];
		DWORD const length = GetEnvironmentVariableA("SWG_D3D11_AUTOCAPTURE", captureBase, sizeof(captureBase));
		if (length == 0 || length >= sizeof(captureBase))
			return;

		++Direct3d11Namespace::ms_autoCaptureWorldRows;
		if (Direct3d11Namespace::ms_autoCaptureWorldRows >= getAutoCaptureWorldRowsThreshold())
		{
			Direct3d11Namespace::ms_autoCaptureRequested = true;
			Direct3d11Namespace::diag("d3d11 autocapture armed frame=%d worldRows=%d shader=%s", Direct3d11Namespace::ms_presentCount, Direct3d11Namespace::ms_autoCaptureWorldRows, Direct3d11Namespace::ms_activeStaticShaderName);
		}
	}

	unsigned getD3d9StageOperation(float operation)
	{
		static unsigned const d3d9TextureOperation[] =
		{
			1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
			14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26
		};

		int const index = static_cast<int>(operation + 0.5f);
		if (index < 0 || index >= static_cast<int>(sizeof(d3d9TextureOperation) / sizeof(d3d9TextureOperation[0])))
			return 1;

		return d3d9TextureOperation[index];
	}

	unsigned getD3d9StageArgument(float argument, bool complement, bool alphaReplicate)
	{
		static unsigned const d3d9TextureArgument[] =
		{
			1, // D3DTA_CURRENT
			0, // D3DTA_DIFFUSE
			4, // D3DTA_SPECULAR
			5, // D3DTA_TEMP
			2, // D3DTA_TEXTURE
			3  // D3DTA_TFACTOR
		};

		int const index = static_cast<int>(argument + 0.5f);
		unsigned value = (index >= 0 && index < static_cast<int>(sizeof(d3d9TextureArgument) / sizeof(d3d9TextureArgument[0]))) ? d3d9TextureArgument[index] : 1;
		if (complement)
			value |= 0x10;
		if (alphaReplicate)
			value |= 0x20;
		return value;
	}

	unsigned getD3d9StageColorArgument(int stage, int argumentIndex)
	{
		int const flags = static_cast<int>(Direct3d11Namespace::ms_activeStageMeta[stage][0] + 0.5f);
		bool const complement = (flags & (1 << (argumentIndex * 2))) != 0;
		bool const alphaReplicate = (flags & (1 << (argumentIndex * 2 + 1))) != 0;
		return getD3d9StageArgument(Direct3d11Namespace::ms_activeStageColorOp[stage][argumentIndex + 1], complement, alphaReplicate);
	}

	unsigned getD3d9StageAlphaArgument(int stage, int argumentIndex)
	{
		int const flags = static_cast<int>(Direct3d11Namespace::ms_activeStageMeta[stage][1] + 0.5f);
		bool const complement = (flags & (1 << argumentIndex)) != 0;
		return getD3d9StageArgument(Direct3d11Namespace::ms_activeStageAlphaOp[stage][argumentIndex + 1], complement, false);
	}

	unsigned getD3d9StageResultArgument(int stage)
	{
		return getD3d9StageArgument(Direct3d11Namespace::ms_activeStageMeta[stage][3], false, false);
	}

	void logRendererVectorInventoryDraw(char const *drawKind, D3D11_PRIMITIVE_TOPOLOGY topology, int baseIndex, int startIndex, int elementCount)
	{
		if (!Direct3d11Namespace::ms_activeVertexVector || Direct3d11Namespace::ms_rendererVectorInventoryLogCount >= 2048)
			return;

		bool streamHasColor0[2] = { false, false };
		bool streamHasColor1[2] = { false, false };
		bool streamHasNormal[2] = { false, false };
		bool streamTransformed[2] = { false, false };
		for (int i = 0; i < Direct3d11Namespace::ms_activeVertexStreamCount && i < 2; ++i)
		{
			VertexBufferFormat format;
			format.setFlags(Direct3d11Namespace::ms_activeVertexStreamFormatFlags[i]);
			streamHasColor0[i] = format.hasColor0();
			streamHasColor1[i] = format.hasColor1();
			streamHasNormal[i] = format.hasNormal();
			streamTransformed[i] = format.isTransformed();
		}

		++Direct3d11Namespace::ms_rendererVectorInventoryLogCount;
		Direct3d11Namespace::diag("renderer vector d3d11 frame=%d draw=%04d kind=%s topo=%d base=%d start=%d count=%d shader=%s vprogram=%s pprogram=%s vmode=%d pmode=%d stages=%d streams=%d s0flags=0x%08x s0stride=%u s0c0=%d s0c1=%d s0n=%d s0t=%d s1flags=0x%08x s1stride=%u s1c0=%d s1c1=%d s1n=%d s1t=%d lighting=%d colorVertex=%d srcA/D/S/E=%d/%d/%d/%d fullAmbient=%d lightMask=0x%04x amb=%0.3f,%0.3f,%0.3f diff=%0.3f,%0.3f,%0.3f dir=%0.3f,%0.3f,%0.3f p0diff=%0.3f,%0.3f,%0.3f p0dir=%0.3f,%0.3f,%0.3f matA=%0.3f,%0.3f,%0.3f matD=%0.3f,%0.3f,%0.3f matE=%0.3f,%0.3f,%0.3f obj0=%0.3f,%0.3f,%0.3f obj1=%0.3f,%0.3f,%0.3f obj2=%0.3f,%0.3f,%0.3f",
			Direct3d11Namespace::ms_presentCount,
			Direct3d11Namespace::ms_rendererVectorInventoryLogCount,
			drawKind,
			static_cast<int>(topology),
			baseIndex,
			startIndex,
			elementCount,
			Direct3d11Namespace::ms_activeStaticShaderName,
			Direct3d11Namespace::ms_activeVertexProgramName,
			Direct3d11Namespace::ms_activePixelProgramName,
			Direct3d11Namespace::ms_activeVertexProgramMode,
			Direct3d11Namespace::ms_activePixelProgramMode,
			Direct3d11Namespace::ms_activeStageTextureCount,
			Direct3d11Namespace::ms_activeVertexStreamCount,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStreamFormatFlags[0]),
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStreamStrides[0]),
			streamHasColor0[0] ? 1 : 0,
			streamHasColor1[0] ? 1 : 0,
			streamHasNormal[0] ? 1 : 0,
			streamTransformed[0] ? 1 : 0,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStreamFormatFlags[1]),
			static_cast<unsigned>(Direct3d11Namespace::ms_activeVertexStreamStrides[1]),
			streamHasColor0[1] ? 1 : 0,
			streamHasColor1[1] ? 1 : 0,
			streamHasNormal[1] ? 1 : 0,
			streamTransformed[1] ? 1 : 0,
			Direct3d11Namespace::ms_lightingEnabled ? 1 : 0,
			Direct3d11Namespace::ms_lightingColorVertex ? 1 : 0,
			static_cast<int>(Direct3d11Namespace::ms_lightingAmbientColorSource),
			static_cast<int>(Direct3d11Namespace::ms_lightingDiffuseColorSource),
			static_cast<int>(Direct3d11Namespace::ms_lightingSpecularColorSource),
			static_cast<int>(Direct3d11Namespace::ms_lightingEmissiveColorSource),
			Direct3d11Namespace::ms_activeFullAmbient ? 1 : 0,
			static_cast<unsigned>(Direct3d11Namespace::ms_selectedLightMask),
			Direct3d11Namespace::ms_lightAmbient[0],
			Direct3d11Namespace::ms_lightAmbient[1],
			Direct3d11Namespace::ms_lightAmbient[2],
			Direct3d11Namespace::ms_lightDiffuse[0],
			Direct3d11Namespace::ms_lightDiffuse[1],
			Direct3d11Namespace::ms_lightDiffuse[2],
			Direct3d11Namespace::ms_lightDirection[0],
			Direct3d11Namespace::ms_lightDirection[1],
			Direct3d11Namespace::ms_lightDirection[2],
			Direct3d11Namespace::ms_parallelLightDiffuse[0][0],
			Direct3d11Namespace::ms_parallelLightDiffuse[0][1],
			Direct3d11Namespace::ms_parallelLightDiffuse[0][2],
			Direct3d11Namespace::ms_parallelLightDirection[0][0],
			Direct3d11Namespace::ms_parallelLightDirection[0][1],
			Direct3d11Namespace::ms_parallelLightDirection[0][2],
			Direct3d11Namespace::ms_materialAmbientColor[0],
			Direct3d11Namespace::ms_materialAmbientColor[1],
			Direct3d11Namespace::ms_materialAmbientColor[2],
			Direct3d11Namespace::ms_materialColor[0],
			Direct3d11Namespace::ms_materialColor[1],
			Direct3d11Namespace::ms_materialColor[2],
			Direct3d11Namespace::ms_materialEmissiveColor[0],
			Direct3d11Namespace::ms_materialEmissiveColor[1],
			Direct3d11Namespace::ms_materialEmissiveColor[2],
			Direct3d11Namespace::ms_objectToWorldMatrix.m[0][0],
			Direct3d11Namespace::ms_objectToWorldMatrix.m[0][1],
			Direct3d11Namespace::ms_objectToWorldMatrix.m[0][2],
			Direct3d11Namespace::ms_objectToWorldMatrix.m[1][0],
			Direct3d11Namespace::ms_objectToWorldMatrix.m[1][1],
			Direct3d11Namespace::ms_objectToWorldMatrix.m[1][2],
			Direct3d11Namespace::ms_objectToWorldMatrix.m[2][0],
			Direct3d11Namespace::ms_objectToWorldMatrix.m[2][1],
			Direct3d11Namespace::ms_objectToWorldMatrix.m[2][2]);
	}

	void logRendererInventoryDraw(char const *drawKind, D3D11_PRIMITIVE_TOPOLOGY topology, int baseIndex, int startIndex, int elementCount)
	{
		if (!isRendererInventoryEnabled() || Direct3d11Namespace::ms_presentCount < getRendererInventoryStartFrame() || Direct3d11Namespace::ms_rendererInventoryLogCount >= getRendererInventoryLimit())
			return;

		logRendererVectorInventoryDraw(drawKind, topology, baseIndex, startIndex, elementCount);

		++Direct3d11Namespace::ms_rendererInventoryLogCount;
		Direct3d11Namespace::diag("renderer inventory d3d11 frame=%d draw=%04d kind=%s topo=%d base=%d start=%d count=%d shader=%s vprogram=%s pprogram=%s stages=%d texTag=0x%08x texFmt=%d texSet=%d alphaBlend=%d alphaFade=%d alphaFadeOpacity=%0.3f src=%d dst=%d op=%d alphaTest=%d cmp=%d ref=%0.3f depth=%d write=%d colorWrite=0x%02x fog=%d passFog=%d fogColor=%0.3f,%0.3f,%0.3f fogColorPacked=0x%08x lightAmb=%0.3f,%0.3f,%0.3f lightDiff=%0.3f,%0.3f,%0.3f lightDir=%0.3f,%0.3f,%0.3f enabledLights=0x%04x matA=%0.3f,%0.3f,%0.3f matD=%0.3f,%0.3f,%0.3f matE=%0.3f,%0.3f,%0.3f matS=%0.3f,%0.3f,%0.3f specPower=%0.3f tf=%0.3f,%0.3f,%0.3f,%0.3f tf2=%0.3f,%0.3f,%0.3f,%0.3f s0=0x%08x/%d/%0.0f s1=0x%08x/%d/%0.0f s2=0x%08x/%d/%0.0f s0fmt=%d s0tci=%0.0f s1fmt=%d s1tci=%0.0f s2fmt=%d s2tci=%0.0f s0cop=%u s0c0=%u s0c1=%u s0c2=%u s0aop=%u s0a0=%u s0a1=%u s0a2=%u s0res=%u s1cop=%u s1c0=%u s1c1=%u s1c2=%u s1aop=%u s1a0=%u s1a1=%u s1a2=%u s1res=%u s2cop=%u s2c0=%u s2c1=%u s2c2=%u s2aop=%u s2a0=%u s2a1=%u s2a2=%u s2res=%u",
			Direct3d11Namespace::ms_presentCount,
			Direct3d11Namespace::ms_rendererInventoryLogCount,
			drawKind,
			static_cast<int>(topology),
			baseIndex,
			startIndex,
			elementCount,
			Direct3d11Namespace::ms_activeStaticShaderName,
			Direct3d11Namespace::ms_activeVertexProgramName,
			Direct3d11Namespace::ms_activePixelProgramName,
			Direct3d11Namespace::ms_activeStageTextureCount,
			static_cast<unsigned>(Direct3d11Namespace::ms_activeTextureTag),
			Direct3d11Namespace::ms_activeTextureNativeFormat,
			Direct3d11Namespace::ms_activeTextureCoordinateSet,
			Direct3d11Namespace::ms_alphaBlendEnabled ? 1 : 0,
			Direct3d11Namespace::ms_alphaFadeOpacityEnabled ? 1 : 0,
			Direct3d11Namespace::ms_alphaFadeOpacity,
			static_cast<int>(Direct3d11Namespace::ms_sourceBlend),
			static_cast<int>(Direct3d11Namespace::ms_destinationBlend),
			static_cast<int>(Direct3d11Namespace::ms_blendOp),
			Direct3d11Namespace::ms_alphaTestEnabled ? 1 : 0,
			Direct3d11Namespace::ms_alphaTestCompare,
			Direct3d11Namespace::ms_alphaTestReference,
			Direct3d11Namespace::ms_depthEnabled ? 1 : 0,
			Direct3d11Namespace::ms_depthWriteEnabled ? 1 : 0,
			static_cast<unsigned>(Direct3d11Namespace::ms_colorWriteMask),
			Direct3d11Namespace::ms_fogEnabled ? 1 : 0,
			Direct3d11Namespace::ms_activePassFogMode,
			Direct3d11Namespace::ms_activeFogColor[0],
			Direct3d11Namespace::ms_activeFogColor[1],
			Direct3d11Namespace::ms_activeFogColor[2],
			static_cast<unsigned>(Direct3d11Namespace::ms_activeFogColorPacked),
			Direct3d11Namespace::ms_lightAmbient[0],
			Direct3d11Namespace::ms_lightAmbient[1],
			Direct3d11Namespace::ms_lightAmbient[2],
			Direct3d11Namespace::ms_lightDiffuse[0],
			Direct3d11Namespace::ms_lightDiffuse[1],
			Direct3d11Namespace::ms_lightDiffuse[2],
			Direct3d11Namespace::ms_lightDirection[0],
			Direct3d11Namespace::ms_lightDirection[1],
			Direct3d11Namespace::ms_lightDirection[2],
			static_cast<unsigned>(Direct3d11Namespace::ms_selectedLightMask),
			Direct3d11Namespace::ms_materialAmbientColor[0],
			Direct3d11Namespace::ms_materialAmbientColor[1],
			Direct3d11Namespace::ms_materialAmbientColor[2],
			Direct3d11Namespace::ms_materialColor[0],
			Direct3d11Namespace::ms_materialColor[1],
			Direct3d11Namespace::ms_materialColor[2],
			Direct3d11Namespace::ms_materialEmissiveColor[0],
			Direct3d11Namespace::ms_materialEmissiveColor[1],
			Direct3d11Namespace::ms_materialEmissiveColor[2],
			Direct3d11Namespace::ms_materialSpecularColor[0],
			Direct3d11Namespace::ms_materialSpecularColor[1],
			Direct3d11Namespace::ms_materialSpecularColor[2],
			Direct3d11Namespace::ms_materialSpecularPower,
			Direct3d11Namespace::ms_textureFactor[0],
			Direct3d11Namespace::ms_textureFactor[1],
			Direct3d11Namespace::ms_textureFactor[2],
			Direct3d11Namespace::ms_textureFactor[3],
			Direct3d11Namespace::ms_textureFactor2[0],
			Direct3d11Namespace::ms_textureFactor2[1],
			Direct3d11Namespace::ms_textureFactor2[2],
			Direct3d11Namespace::ms_textureFactor2[3],
			static_cast<unsigned>(Direct3d11Namespace::ms_activeStageTextureTag[0]),
			Direct3d11Namespace::ms_activeStageNativeFormat[0],
			Direct3d11Namespace::ms_activeStageTextureCoordinateSet[0][2],
			static_cast<unsigned>(Direct3d11Namespace::ms_activeStageTextureTag[1]),
			Direct3d11Namespace::ms_activeStageNativeFormat[1],
			Direct3d11Namespace::ms_activeStageTextureCoordinateSet[1][2],
			static_cast<unsigned>(Direct3d11Namespace::ms_activeStageTextureTag[2]),
			Direct3d11Namespace::ms_activeStageNativeFormat[2],
			Direct3d11Namespace::ms_activeStageTextureCoordinateSet[2][2],
			Direct3d11Namespace::ms_activeStageNativeFormat[0],
			Direct3d11Namespace::ms_activeStageTextureCoordinateSet[0][2],
			Direct3d11Namespace::ms_activeStageNativeFormat[1],
			Direct3d11Namespace::ms_activeStageTextureCoordinateSet[1][2],
			Direct3d11Namespace::ms_activeStageNativeFormat[2],
			Direct3d11Namespace::ms_activeStageTextureCoordinateSet[2][2],
			getD3d9StageOperation(Direct3d11Namespace::ms_activeStageColorOp[0][0]),
			getD3d9StageColorArgument(0, 0),
			getD3d9StageColorArgument(0, 1),
			getD3d9StageColorArgument(0, 2),
			getD3d9StageOperation(Direct3d11Namespace::ms_activeStageAlphaOp[0][0]),
			getD3d9StageAlphaArgument(0, 0),
			getD3d9StageAlphaArgument(0, 1),
			getD3d9StageAlphaArgument(0, 2),
			getD3d9StageResultArgument(0),
			getD3d9StageOperation(Direct3d11Namespace::ms_activeStageColorOp[1][0]),
			getD3d9StageColorArgument(1, 0),
			getD3d9StageColorArgument(1, 1),
			getD3d9StageColorArgument(1, 2),
			getD3d9StageOperation(Direct3d11Namespace::ms_activeStageAlphaOp[1][0]),
			getD3d9StageAlphaArgument(1, 0),
			getD3d9StageAlphaArgument(1, 1),
			getD3d9StageAlphaArgument(1, 2),
			getD3d9StageResultArgument(1),
			getD3d9StageOperation(Direct3d11Namespace::ms_activeStageColorOp[2][0]),
			getD3d9StageColorArgument(2, 0),
			getD3d9StageColorArgument(2, 1),
			getD3d9StageColorArgument(2, 2),
			getD3d9StageOperation(Direct3d11Namespace::ms_activeStageAlphaOp[2][0]),
			getD3d9StageAlphaArgument(2, 0),
			getD3d9StageAlphaArgument(2, 1),
			getD3d9StageAlphaArgument(2, 2),
			getD3d9StageResultArgument(2));

		armAutoCaptureFromRendererInventory();
	}

	void drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY topology, int startVertex, int vertexCount)
	{
		if (vertexCount <= 0)
			return;
		if (!Direct3d11Namespace::ms_context)
		{
			++Direct3d11Namespace::ms_drawSkippedNoContext;
			return;
		}
		if (!Direct3d11Namespace::ms_activeVertexBuffer)
		{
			++Direct3d11Namespace::ms_drawSkippedNoVertexBuffer;
			return;
		}
		if (!Direct3d11Namespace::bindDefaultPipeline())
		{
			++Direct3d11Namespace::ms_drawSkippedNoPipeline;
			return;
		}

		Direct3d11Namespace::ms_context->IASetPrimitiveTopology(topology);
		logTerrainDot3DrawState("draw", topology, 0, startVertex, vertexCount);
		logStaticShellDrawState("draw", topology, 0, startVertex, vertexCount);
		logShadowBlobDrawState("draw", topology, 0, startVertex, vertexCount);
		logMobileDrawState("draw", topology, 0, startVertex, vertexCount);
		logTransformedDrawState("draw", topology, 0, startVertex, vertexCount);
		logRendererInventoryDraw("draw", topology, 0, startVertex, vertexCount);
		Direct3d11Namespace::ms_context->Draw(static_cast<UINT>(vertexCount), static_cast<UINT>(Direct3d11Namespace::ms_sliceFirstVertex + startVertex));
		++Direct3d11Namespace::ms_drawCallCount;
		Direct3d11Namespace::ms_drawVertexCount += vertexCount;
		VertexBufferFormat activeFormat;
		activeFormat.setFlags(Direct3d11Namespace::ms_activeVertexFormatFlags);
		if (activeFormat.isTransformed())
			++Direct3d11Namespace::ms_transformedDrawCount;
	}

	void drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY topology, int baseIndex, int startIndex, int indexCount)
	{
		if (indexCount <= 0)
			return;
		if (!Direct3d11Namespace::ms_context)
		{
			++Direct3d11Namespace::ms_drawSkippedNoContext;
			return;
		}
		if (!Direct3d11Namespace::ms_activeVertexBuffer)
		{
			++Direct3d11Namespace::ms_drawSkippedNoVertexBuffer;
			return;
		}
		if (!Direct3d11Namespace::ms_activeIndexBuffer)
		{
			++Direct3d11Namespace::ms_drawSkippedNoIndexBuffer;
			return;
		}
		if (!Direct3d11Namespace::bindDefaultPipeline())
		{
			++Direct3d11Namespace::ms_drawSkippedNoPipeline;
			return;
		}

		Direct3d11Namespace::ms_context->IASetPrimitiveTopology(topology);
		logTerrainDot3DrawState("drawIndexed", topology, baseIndex, startIndex, indexCount);
		logStaticShellDrawState("drawIndexed", topology, baseIndex, startIndex, indexCount);
		logShadowBlobDrawState("drawIndexed", topology, baseIndex, startIndex, indexCount);
		logMobileDrawState("drawIndexed", topology, baseIndex, startIndex, indexCount);
		logTransformedDrawState("drawIndexed", topology, baseIndex, startIndex, indexCount);
		logRendererInventoryDraw("drawIndexed", topology, baseIndex, startIndex, indexCount);
		Direct3d11Namespace::ms_context->DrawIndexed(static_cast<UINT>(indexCount), static_cast<UINT>(Direct3d11Namespace::ms_sliceFirstIndex + startIndex), Direct3d11Namespace::ms_sliceFirstVertex + baseIndex);
		++Direct3d11Namespace::ms_drawCallCount;
		++Direct3d11Namespace::ms_drawIndexedCallCount;
		Direct3d11Namespace::ms_drawIndexCount += indexCount;
		VertexBufferFormat activeFormat;
		activeFormat.setFlags(Direct3d11Namespace::ms_activeVertexFormatFlags);
		if (activeFormat.isTransformed())
			++Direct3d11Namespace::ms_transformedDrawCount;
	}

	void drawTriangleFanD3d11(int startVertex, int vertexCount)
	{
		int const numberOfTriangles = vertexCount - 2;
		if (numberOfTriangles <= 0)
			return;
		if (!resizeTriangleFanIndexBuffer(vertexCount))
			return;
		if (!Direct3d11Namespace::ms_context)
		{
			++Direct3d11Namespace::ms_drawSkippedNoContext;
			return;
		}
		if (!Direct3d11Namespace::ms_activeVertexBuffer)
		{
			++Direct3d11Namespace::ms_drawSkippedNoVertexBuffer;
			return;
		}
		if (!Direct3d11Namespace::bindDefaultPipeline())
		{
			++Direct3d11Namespace::ms_drawSkippedNoPipeline;
			return;
		}

		Direct3d11Namespace::ms_context->IASetIndexBuffer(Direct3d11Namespace::ms_triangleFanIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
		Direct3d11Namespace::ms_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		logShadowBlobDrawState("triangleFan", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, startVertex, vertexCount);
		logMobileDrawState("triangleFan", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, startVertex, vertexCount);
		logTransformedDrawState("triangleFan", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, startVertex, vertexCount);
		logRendererInventoryDraw("triangleFan", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, startVertex, vertexCount);
		Direct3d11Namespace::ms_context->DrawIndexed(static_cast<UINT>(numberOfTriangles * 3), 0, Direct3d11Namespace::ms_sliceFirstVertex + startVertex);
		Direct3d11Namespace::ms_context->IASetIndexBuffer(Direct3d11Namespace::ms_activeIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
		++Direct3d11Namespace::ms_drawCallCount;
		++Direct3d11Namespace::ms_drawIndexedCallCount;
		Direct3d11Namespace::ms_drawIndexCount += numberOfTriangles * 3;
		VertexBufferFormat activeFormat;
		activeFormat.setFlags(Direct3d11Namespace::ms_activeVertexFormatFlags);
		if (activeFormat.isTransformed())
			++Direct3d11Namespace::ms_transformedDrawCount;
	}

	bool updateIndexedTriangleFanBuffer(int startIndex, int primitiveCount)
	{
		if (!Direct3d11Namespace::ms_device || !Direct3d11Namespace::ms_activeIndexData || primitiveCount <= 0)
			return false;

		int const sourceIndex = Direct3d11Namespace::ms_sliceFirstIndex + startIndex;
		int const sourceIndexCount = primitiveCount + 2;
		if (sourceIndex < 0 || sourceIndex + sourceIndexCount > Direct3d11Namespace::ms_sliceNumberOfIndices)
			return false;

		int const outputIndexCount = primitiveCount * 3;
		std::vector<Index> indices(static_cast<size_t>(outputIndexCount));
		Index const *source = Direct3d11Namespace::ms_activeIndexData + sourceIndex;
		Index *target = indices.empty() ? 0 : &indices[0];
		for (int i = 1; i < sourceIndexCount - 1; ++i)
		{
			*target++ = source[0];
			*target++ = source[i];
			*target++ = source[i + 1];
		}

		if (!Direct3d11Namespace::ms_indexedTriangleFanIndexBuffer || outputIndexCount > Direct3d11Namespace::ms_indexedTriangleFanIndexBufferNumberOfIndices)
		{
			Direct3d11Namespace::releaseCom(Direct3d11Namespace::ms_indexedTriangleFanIndexBuffer);
			Direct3d11Namespace::ms_indexedTriangleFanIndexBufferNumberOfIndices = outputIndexCount;
			Direct3d11Namespace::ms_indexedTriangleFanIndexBuffer = Direct3d11Namespace::createBuffer(D3D11_BIND_INDEX_BUFFER, &indices[0], indices.size() * sizeof(Index));
			return Direct3d11Namespace::ms_indexedTriangleFanIndexBuffer != 0;
		}

		if (Direct3d11Namespace::ms_context)
		{
			D3D11_BOX box;
			ZeroMemory(&box, sizeof(box));
			box.right = static_cast<UINT>(outputIndexCount * sizeof(Index));
			box.bottom = 1;
			box.back = 1;
			Direct3d11Namespace::ms_context->UpdateSubresource(Direct3d11Namespace::ms_indexedTriangleFanIndexBuffer, 0, &box, &indices[0], 0, 0);
		}
		return true;
	}

	void drawIndexedTriangleFanD3d11(int baseIndex, int startIndex, int primitiveCount)
	{
		if (!updateIndexedTriangleFanBuffer(startIndex, primitiveCount))
			return;
		if (!Direct3d11Namespace::ms_context)
		{
			++Direct3d11Namespace::ms_drawSkippedNoContext;
			return;
		}
		if (!Direct3d11Namespace::ms_activeVertexBuffer)
		{
			++Direct3d11Namespace::ms_drawSkippedNoVertexBuffer;
			return;
		}
		if (!Direct3d11Namespace::bindDefaultPipeline())
		{
			++Direct3d11Namespace::ms_drawSkippedNoPipeline;
			return;
		}

		Direct3d11Namespace::ms_context->IASetIndexBuffer(Direct3d11Namespace::ms_indexedTriangleFanIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
		Direct3d11Namespace::ms_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		logShadowBlobDrawState("indexedTriangleFan", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, baseIndex, startIndex, primitiveCount * 3);
		logMobileDrawState("indexedTriangleFan", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, baseIndex, startIndex, primitiveCount * 3);
		logTransformedDrawState("indexedTriangleFan", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, baseIndex, startIndex, primitiveCount * 3);
		logRendererInventoryDraw("indexedTriangleFan", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, baseIndex, startIndex, primitiveCount * 3);
		Direct3d11Namespace::ms_context->DrawIndexed(static_cast<UINT>(primitiveCount * 3), 0, Direct3d11Namespace::ms_sliceFirstVertex + baseIndex);
		Direct3d11Namespace::ms_context->IASetIndexBuffer(Direct3d11Namespace::ms_activeIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
		++Direct3d11Namespace::ms_drawCallCount;
		++Direct3d11Namespace::ms_drawIndexedCallCount;
		Direct3d11Namespace::ms_drawIndexCount += primitiveCount * 3;
		VertexBufferFormat activeFormat;
		activeFormat.setFlags(Direct3d11Namespace::ms_activeVertexFormatFlags);
		if (activeFormat.isTransformed())
			++Direct3d11Namespace::ms_transformedDrawCount;
	}
}

void Direct3d11Namespace::drawPointList() { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, 0, ms_sliceNumberOfVertices); }
void Direct3d11Namespace::drawLineList() { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_LINELIST, 0, (ms_sliceNumberOfVertices / 2) * 2); }
void Direct3d11Namespace::drawLineStrip() { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, 0, ms_sliceNumberOfVertices); }
void Direct3d11Namespace::drawTriangleList() { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, (ms_sliceNumberOfVertices / 3) * 3); }
void Direct3d11Namespace::drawTriangleStrip() { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, 0, ms_sliceNumberOfVertices); }
void Direct3d11Namespace::drawTriangleFan() { drawTriangleFanD3d11(0, ms_sliceNumberOfVertices); }
void Direct3d11Namespace::drawQuadList()
{
	int const numberOfQuads = ms_sliceNumberOfVertices / 4;
	int const numberOfTriangles = numberOfQuads * 2;
	if (numberOfQuads <= 0)
		return;
	if (!resizeQuadListIndexBuffer(numberOfQuads))
		return;
	if (!ms_context)
	{
		++ms_drawSkippedNoContext;
		return;
	}
	if (!ms_activeVertexBuffer)
	{
		++ms_drawSkippedNoVertexBuffer;
		return;
	}
	if (!bindDefaultPipeline())
	{
		++ms_drawSkippedNoPipeline;
		return;
	}

	ms_context->IASetIndexBuffer(ms_quadListIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
	ms_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	logShadowBlobDrawState("quadList", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, 0, numberOfTriangles * 3);
	logMobileDrawState("quadList", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, 0, numberOfTriangles * 3);
	logTransformedDrawState("quadList", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, 0, numberOfTriangles * 3);
	logRendererInventoryDraw("quadList", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, 0, numberOfTriangles * 3);
	ms_context->DrawIndexed(static_cast<UINT>(numberOfTriangles * 3), 0, ms_sliceFirstVertex);
	ms_context->IASetIndexBuffer(ms_activeIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
	++ms_drawCallCount;
	++ms_drawIndexedCallCount;
	ms_drawIndexCount += numberOfTriangles * 3;
	VertexBufferFormat activeFormat;
	activeFormat.setFlags(ms_activeVertexFormatFlags);
	if (activeFormat.isTransformed())
		++ms_transformedDrawCount;
}
void Direct3d11Namespace::drawIndexedPointList() { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, 0, 0, ms_sliceNumberOfIndices); }
void Direct3d11Namespace::drawIndexedLineList() { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_LINELIST, 0, 0, (ms_sliceNumberOfIndices / 2) * 2); }
void Direct3d11Namespace::drawIndexedLineStrip() { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, 0, 0, ms_sliceNumberOfIndices); }
void Direct3d11Namespace::drawIndexedTriangleList() { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 0, 0, (ms_sliceNumberOfIndices / 3) * 3); }
void Direct3d11Namespace::drawIndexedTriangleStrip() { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, 0, 0, ms_sliceNumberOfIndices); }
void Direct3d11Namespace::drawIndexedTriangleFan() { drawIndexedTriangleFanD3d11(0, 0, ms_sliceNumberOfIndices - 2); }
void Direct3d11Namespace::drawPartialPointList(int startVertex, int primitiveCount) { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, startVertex, primitiveCount); }
void Direct3d11Namespace::drawPartialLineList(int startVertex, int primitiveCount) { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_LINELIST, startVertex, primitiveCount * 2); }
void Direct3d11Namespace::drawPartialLineStrip(int startVertex, int primitiveCount) { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, startVertex, primitiveCount + 1); }
void Direct3d11Namespace::drawPartialTriangleList(int startVertex, int primitiveCount) { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, startVertex, primitiveCount * 3); }
void Direct3d11Namespace::drawPartialTriangleStrip(int startVertex, int primitiveCount) { drawPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, startVertex, primitiveCount + 2); }
void Direct3d11Namespace::drawPartialTriangleFan(int startVertex, int primitiveCount) { drawTriangleFanD3d11(startVertex, primitiveCount + 2); }
void Direct3d11Namespace::drawPartialIndexedPointList(int baseIndex, int, int, int startIndex, int primitiveCount) { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, baseIndex, startIndex, primitiveCount); }
void Direct3d11Namespace::drawPartialIndexedLineList(int baseIndex, int, int, int startIndex, int primitiveCount) { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_LINELIST, baseIndex, startIndex, primitiveCount * 2); }
void Direct3d11Namespace::drawPartialIndexedLineStrip(int baseIndex, int, int, int startIndex, int primitiveCount) { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, baseIndex, startIndex, primitiveCount + 1); }
void Direct3d11Namespace::drawPartialIndexedTriangleList(int baseIndex, int, int, int startIndex, int primitiveCount) { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, baseIndex, startIndex, primitiveCount * 3); }
void Direct3d11Namespace::drawPartialIndexedTriangleStrip(int baseIndex, int, int, int startIndex, int primitiveCount) { drawIndexedPrimitiveD3d11(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, baseIndex, startIndex, primitiveCount + 2); }
void Direct3d11Namespace::drawPartialIndexedTriangleFan(int baseIndex, int, int, int startIndex, int primitiveCount) { drawIndexedTriangleFanD3d11(baseIndex, startIndex, primitiveCount); }
void Direct3d11Namespace::optimizeIndexBuffer(WORD *, int) {}
int Direct3d11Namespace::getMaximumVertexBufferStreamCount() { return 16; }
void Direct3d11Namespace::setBloomEnabled(bool enabled)
{
	ms_alphaFadeOpacityEnabled = enabled;
	ms_alphaFadeOpacity = enabled ? 1.0f : 0.0f;
	ms_transformDirty = true;
}
void Direct3d11Namespace::pixSetMarker(WCHAR const *) {}
void Direct3d11Namespace::pixBeginEvent(WCHAR const *) {}
void Direct3d11Namespace::pixEndEvent(WCHAR const *) {}
bool Direct3d11Namespace::writeImage(char const *file, int const width, int const height, int const pitch, int const *pixelsARGB, bool const alphaExtend, Gl_imageFormat const imageFormat, Rectangle2d const *subRect)
{
	return writeImageFileArgb(file, width, height, pitch, pixelsARGB, alphaExtend, imageFormat, subRect);
}
bool Direct3d11Namespace::supportsAntialias() { return false; }
void Direct3d11Namespace::setAntialiasEnabled(bool) {}

#if PRODUCTION == 0
bool Direct3d11Namespace::createVideoBuffers(int, int) { return false; }
void Direct3d11Namespace::fillVideoBuffers() {}
bool Direct3d11Namespace::getVideoBufferData(void *, size_t) { return false; }
void Direct3d11Namespace::releaseVideoBuffers() {}
#endif

// ======================================================================
