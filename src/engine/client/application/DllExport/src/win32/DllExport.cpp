// ======================================================================
//
// DllExport.cpp
// copyright (c) 2001 Sony Online Entertainment
//
// ======================================================================

// make windows.h more strict in the types of handles
#ifndef STRICT
#define STRICT 1
#endif

// trim down the amount of stuff windows.h includes
#define NOGDICAPMASKS
#define NOVIRTUALKEYCODE
#define NOKEYSTATES
#define NORASTEROPS
#define NOATOM
#define NOCOLOR
#define NODRAWTEXT
#define NOMEMMGR
#define NOMETAFILE
#define NOMINMAX
#define NOOPENFILE
#define NOSERVICE
#define NOSOUND
#define NOCOMM
#define NOHELP
#define NOPROFILER
#define NODEFERWINDOWPOS
#define NOMCX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <wtypes.h>

#ifndef _STLP_WINDOWS_H_INCLUDED
#define _STLP_WINDOWS_H_INCLUDED
#endif

#include <stdio.h>
#include <stdlib.h>
#include <map>

// ======================================================================

#include "sharedFoundation/FirstSharedFoundation.h"

#include "clientGraphics/DynamicIndexBuffer.h"
#include "clientGraphics/DynamicVertexBuffer.h"
#include "clientGraphics/Graphics.h"
#include "clientGraphics/Material.h"
#include "clientGraphics/ShaderImplementation.h"
#include "clientGraphics/StaticIndexBuffer.h"
#include "clientGraphics/StaticShader.h"
#include "clientGraphics/StaticVertexBuffer.h"
#include "clientGraphics/Texture.h"
#include "clientGraphics/TextureFormatInfo.h"
#include "clientGraphics/VertexBufferVector.h"
#include "sharedDebug/DataLint.h"
#include "sharedDebug/DebugFlags.h"
#include "sharedDebug/DebugKey.h"
#include "sharedDebug/PerformanceTimer.h"
#include "sharedDebug/Profiler.h"
#include "sharedFile/TreeFile.h"
#include "sharedFoundation/Clock.h"
#include "sharedFoundation/ConfigFile.h"
#include "sharedFoundation/ConfigSharedFoundation.h"
#include "sharedFoundation/CrashReportInformation.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedFoundation/Fatal.h"
#include "sharedFoundation/MemoryBlockManager.h"
#include "sharedFoundation/Os.h"
#include "sharedFoundation/PersistentCrcString.h"
#include "sharedFoundation/TemporaryCrcString.h"
#include "sharedMath/Transform.h"
#include "sharedMath/VectorArgb.h"
#include "sharedObject/Object.h"
#include "sharedSynchronization/Mutex.h"

// ======================================================================

#if defined(_M_IX86)
#define DLL_EXPORT_STUB_BREAK() __asm int 3
#else
namespace
{
	void dllExportStubBreak(char const *functionName)
	{
		char buffer[256];
		_snprintf(buffer, sizeof(buffer), "DllExport stub break: %s\n", functionName ? functionName : "<unknown>");
		buffer[sizeof(buffer) - 1] = '\0';
		OutputDebugStringA(buffer);
		DebugBreak();
	}
}
#define DLL_EXPORT_STUB_BREAK() dllExportStubBreak(__FUNCTION__)
#endif

// ======================================================================

void Fatal(const char *, ...)
{
	DLL_EXPORT_STUB_BREAK();
}

void DebugFatal(const char *, ...)
{
	DLL_EXPORT_STUB_BREAK();
}

void Warning(const char *, ...)
{
#if !defined(_WIN64)
	DLL_EXPORT_STUB_BREAK();
#endif
}

// ======================================================================

void Report::setFlags(int)
{
#if !defined(_WIN64)
	DLL_EXPORT_STUB_BREAK();
#endif
}

void Report::vprintf(const char *, va_list)
{
#if !defined(_WIN64)
	DLL_EXPORT_STUB_BREAK();
#endif
}

void Report::printf(const char *, ...)
{
#if !defined(_WIN64)
	DLL_EXPORT_STUB_BREAK();
#endif
}

// ======================================================================

bool ExitChain::isFataling()
{
	DLL_EXPORT_STUB_BREAK();
	return false;
}

// ======================================================================

bool ConfigSharedFoundation::getVerboseHardwareLogging()
{
	DLL_EXPORT_STUB_BREAK();
	return false;
}

// ======================================================================

Mutex::Mutex()
{
	DLL_EXPORT_STUB_BREAK();
}

Mutex::~Mutex()
{
	DLL_EXPORT_STUB_BREAK();
}

// ======================================================================

const TextureFormatInfo &TextureFormatInfo::getInfo(TextureFormat format)
{
#if defined(_WIN64)
	static TextureFormatInfo info[TF_Count] =
	{
		{ false, false, 4, 8, 8, 8, 8, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff, 0, 0, 0, "TF_ARGB_8888" },
		{ false, false, 2, 4, 4, 4, 4, 0x0000f000, 0x00000f00, 0x000000f0, 0x0000000f, 0, 0, 0, "TF_ARGB_4444" },
		{ false, false, 2, 1, 5, 5, 5, 0x00008000, 0x00007c00, 0x000003e0, 0x0000001f, 0, 0, 0, "TF_ARGB_1555" },
		{ false, false, 4, 0, 8, 8, 8, 0x00000000, 0x00ff0000, 0x0000ff00, 0x000000ff, 0, 0, 0, "TF_XRGB_8888" },
		{ false, false, 3, 0, 8, 8, 8, 0x00000000, 0x00ff0000, 0x0000ff00, 0x000000ff, 0, 0, 0, "TF_RGB_888" },
		{ false, false, 2, 0, 5, 6, 5, 0x00000000, 0x0000f800, 0x000007e0, 0x0000001f, 0, 0, 0, "TF_RGB_565" },
		{ false, false, 2, 0, 5, 5, 5, 0x00000000, 0x00007c00, 0x000003e0, 0x0000001f, 0, 0, 0, "TF_RGB_555" },
		{ false, true, 0, 0, 0, 0, 0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 4, 4, 8, "TF_DXT1" },
		{ false, true, 0, 0, 0, 0, 0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 4, 4, 16, "TF_DXT2" },
		{ false, true, 0, 0, 0, 0, 0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 4, 4, 16, "TF_DXT3" },
		{ false, true, 0, 0, 0, 0, 0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 4, 4, 16, "TF_DXT4" },
		{ false, true, 0, 0, 0, 0, 0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 4, 4, 16, "TF_DXT5" },
		{ false, false, 1, 8, 0, 0, 0, 0x000000ff, 0x00000000, 0x00000000, 0x00000000, 0, 0, 0, "TF_A_8" },
		{ false, false, 1, 0, 8, 0, 0, 0x00000000, 0x000000ff, 0x00000000, 0x00000000, 0, 0, 0, "TF_L_8" },
		{ false, false, 1, 0, 8, 8, 8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 1, 1, 1, "TF_P_8" },
		{ false, false, 8, 16, 16, 16, 16, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0, 0, 0, "TF_ABGR_16F" },
		{ false, false, 16, 32, 32, 32, 32, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0, 0, 0, "TF_ABGR_32F" },
	};
	return info[format];
#else
	DLL_EXPORT_STUB_BREAK();
	static TextureFormatInfo dummy;
	return dummy;
#endif
}

void TextureFormatInfo::setSupported(TextureFormat format, bool supported)
{
#if defined(_WIN64)
	const_cast<TextureFormatInfo &>(getInfo(format)).supported = supported;
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

// ======================================================================

void *MemoryManager::allocate(size_t size, uint32, bool, bool)
{
#if defined(_WIN64)
	return ::malloc(size);
#else
	DLL_EXPORT_STUB_BREAK();
	return NULL;
#endif
}

void  MemoryManager::free(void * pointer, bool)
{
#if defined(_WIN64)
	::free(pointer);
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

void  MemoryManager::own(void * pointer)
{
#if defined(_WIN64)
	UNREF(pointer);
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

// ======================================================================

bool DataLint::isEnabled()
{
	DLL_EXPORT_STUB_BREAK();
	return false;
}

// ======================================================================

void DebugFlags::registerFlag(bool &, const char *, const char *)
{
	DLL_EXPORT_STUB_BREAK();
}

void DebugFlags::registerFlag(bool &, const char *, const char *, ReportRoutine1, int)
{
	DLL_EXPORT_STUB_BREAK();
}

void DebugFlags::unregisterFlag(bool &)
{
	DLL_EXPORT_STUB_BREAK();
}

// ======================================================================

void DebugKey::registerFlag(bool &, const char *)
{
	DLL_EXPORT_STUB_BREAK();
}

// ----------------------------------------------------------------------

bool DebugKey::isPressed(int)
{
	DLL_EXPORT_STUB_BREAK();
	return false;
}

// ----------------------------------------------------------------------

bool DebugKey::isDown(int)
{
	DLL_EXPORT_STUB_BREAK();
	return false;
}

// ======================================================================

Material::Material()
{
#if defined(_WIN64)
	ambientColor = VectorArgb(CONST_REAL(1), CONST_REAL(1), CONST_REAL(1), CONST_REAL(1));
	diffuseColor = VectorArgb(CONST_REAL(1), CONST_REAL(1), CONST_REAL(1), CONST_REAL(1));
	emissiveColor = VectorArgb(CONST_REAL(1), CONST_REAL(0), CONST_REAL(0), CONST_REAL(0));
	specularColor = VectorArgb(CONST_REAL(1), CONST_REAL(0), CONST_REAL(0), CONST_REAL(0));
	specularPower = CONST_REAL(0);
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

Material::~Material()
{
#if !defined(_WIN64)
	DLL_EXPORT_STUB_BREAK();
#endif
}

// ======================================================================

MemoryBlockManager::MemoryBlockManager(const char *, bool, int, int, int, int)
{
	DLL_EXPORT_STUB_BREAK();
}

MemoryBlockManager::~MemoryBlockManager()
{
	DLL_EXPORT_STUB_BREAK();
}

int MemoryBlockManager::getElementSize() const
{
	DLL_EXPORT_STUB_BREAK();
	return 0;
}

void *MemoryBlockManager::allocate(bool)
{
	DLL_EXPORT_STUB_BREAK();
	return 0;
}

void MemoryBlockManager::free(void *)
{
	DLL_EXPORT_STUB_BREAK();
}

// ======================================================================

bool Os::isMainThread(void)
{
	DLL_EXPORT_STUB_BREAK();
	return false;
}

Os::ThreadId Os::getThreadId()
{
	DLL_EXPORT_STUB_BREAK();
	return 0;
}

// ======================================================================

Transform const Transform::identity;

void Transform::multiply(const Transform &lhs, const Transform &rhs)
{
#if defined(_WIN64)
	const Transform::matrix_t &lhsMatrix = lhs.getMatrix();
	const Transform::matrix_t &rhsMatrix = rhs.getMatrix();
	Transform::matrix_t result;

	for (int row = 0; row < 3; ++row)
	{
		for (int column = 0; column < 3; ++column)
		{
			result[row][column] =
				lhsMatrix[row][0] * rhsMatrix[0][column] +
				lhsMatrix[row][1] * rhsMatrix[1][column] +
				lhsMatrix[row][2] * rhsMatrix[2][column];
		}

		result[row][3] =
			lhsMatrix[row][0] * rhsMatrix[0][3] +
			lhsMatrix[row][1] * rhsMatrix[1][3] +
			lhsMatrix[row][2] * rhsMatrix[2][3] +
			lhsMatrix[row][3];
	}

	for (int row = 0; row < 3; ++row)
	{
		for (int column = 0; column < 4; ++column)
			matrix[row][column] = result[row][column];
	}
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

// ======================================================================

const char *Shader::getName() const
{
	DLL_EXPORT_STUB_BREAK();
	return NULL;
}

bool StaticShader::getMaterial(Tag tag, Material &material) const
{
#if defined(_WIN64)
	if (!m_materialMap)
		return false;

	StaticShaderTemplate::MaterialMap::const_iterator const i = m_materialMap->find(tag);
	if (i == m_materialMap->end())
		return false;

	material = i->second;
	return true;
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

bool StaticShader::getTextureData(Tag tag, StaticShaderTemplate::TextureData &textureData) const
{
#if defined(_WIN64)
	if (!m_textureDataMap)
		return false;

	StaticShaderTemplate::TextureDataMap::const_iterator const i = m_textureDataMap->find(tag);
	if (i == m_textureDataMap->end())
		return false;

	textureData = i->second;
	return true;
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

bool StaticShader::getTexture(Tag tag, const Texture *&texture) const
{
#if defined(_WIN64)
	if (!m_textureDataMap)
		return false;

	StaticShaderTemplate::TextureDataMap::const_iterator const i = m_textureDataMap->find(tag);
	if (i == m_textureDataMap->end())
		return false;

	texture = i->second.texture;
	return true;
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

bool StaticShader::getTextureCoordinateSet(Tag tag, uint8 &textureCoordinateSet) const
{
#if defined(_WIN64)
	if (!m_textureCoordinateSetMap)
		return false;

	StaticShaderTemplate::TextureCoordinateSetMap::const_iterator const i = m_textureCoordinateSetMap->find(tag);
	if (i == m_textureCoordinateSetMap->end())
		return false;

	textureCoordinateSet = i->second;
	return true;
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

bool StaticShader::getTextureFactor(Tag tag, uint32 &textureFactor) const
{
#if defined(_WIN64)
	if (!m_textureFactorMap)
		return false;

	StaticShaderTemplate::TextureFactorMap::const_iterator const i = m_textureFactorMap->find(tag);
	if (i == m_textureFactorMap->end())
		return false;

	textureFactor = i->second;
	return true;
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

bool StaticShader::getTextureScroll(Tag tag, StaticShaderTemplate::TextureScroll &textureScroll) const
{
#if defined(_WIN64)
	if (!m_textureScrollMap)
		return false;

	StaticShaderTemplate::TextureScrollMap::const_iterator const i = m_textureScrollMap->find(tag);
	if (i == m_textureScrollMap->end())
		return false;

	textureScroll = i->second;
	return true;
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

bool StaticShader::getAlphaTestReferenceValue(Tag tag, uint8 &alphaTestReferenceValue) const
{
#if defined(_WIN64)
	if (!m_alphaTestReferenceValueMap)
		return false;

	StaticShaderTemplate::AlphaTestReferenceValueMap::const_iterator const i = m_alphaTestReferenceValueMap->find(tag);
	if (i == m_alphaTestReferenceValueMap->end())
		return false;

	alphaTestReferenceValue = i->second;
	return true;
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

bool StaticShader::getStencilReferenceValue(Tag tag, uint32 &stencilReferenceValue) const
{
#if defined(_WIN64)
	if (!m_stencilReferenceValueMap)
		return false;

	StaticShaderTemplate::StencilReferenceValueMap::const_iterator const i = m_stencilReferenceValueMap->find(tag);
	if (i == m_stencilReferenceValueMap->end())
		return false;

	stencilReferenceValue = i->second;
	return true;
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

bool StaticShader::containsPrecalculatedVertexLighting() const
{
#if defined(_WIN64)
	return getStaticShaderTemplate().containsPrecalculatedVertexLighting();
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

// ======================================================================

void Texture::fetch() const
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

void Texture::release() const
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

// ======================================================================

DynamicIndexBufferGraphicsData::~DynamicIndexBufferGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

DynamicVertexBufferGraphicsData::~DynamicVertexBufferGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

HardwareIndexBuffer::~HardwareIndexBuffer()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

StaticIndexBuffer::StaticIndexBuffer(int)
: HardwareIndexBuffer(T_static)
{
	DLL_EXPORT_STUB_BREAK();
}

StaticIndexBuffer::~StaticIndexBuffer()
{
	DLL_EXPORT_STUB_BREAK();
}

StaticIndexBufferGraphicsData::~StaticIndexBufferGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

StaticShaderGraphicsData::~StaticShaderGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

ShaderImplementationGraphicsData::~ShaderImplementationGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

StaticVertexBufferGraphicsData::~StaticVertexBufferGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

TextureGraphicsData::~TextureGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

VertexBufferVectorGraphicsData::~VertexBufferVectorGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

ShaderImplementationPassVertexShaderGraphicsData::~ShaderImplementationPassVertexShaderGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

ShaderImplementationPassPixelShaderProgramGraphicsData::~ShaderImplementationPassPixelShaderProgramGraphicsData()
{
#if defined(_WIN64)
#else
	DLL_EXPORT_STUB_BREAK();
#endif
}

char const * ShaderImplementationPassPixelShaderProgram::getFileName() const
{
#if defined(_WIN64)
	return m_fileName.getString();
#else
	DLL_EXPORT_STUB_BREAK();
	return 0;
#endif
}

int ShaderImplementationPassPixelShaderProgram::getVersionMajor() const
{
#if defined(_WIN64)
	return m_exe ? ((m_exe[0] >> 8) & 0xff) : 0;
#else
	DLL_EXPORT_STUB_BREAK();
	return 0;
#endif
}

int ShaderImplementationPassPixelShaderProgram::getVersionMinor() const
{
#if defined(_WIN64)
	return m_exe ? ((m_exe[0] >> 0) & 0xff) : 0;
#else
	DLL_EXPORT_STUB_BREAK();
	return 0;
#endif
}

// ======================================================================

int ConfigFile::getKeyInt(const char *, const char *, int defaultValue, bool)
{
#if defined(_WIN64)
	return defaultValue;
#else
	DLL_EXPORT_STUB_BREAK();
	return 0;
#endif
}

bool  ConfigFile::getKeyBool  (const char *, const char *, bool defaultValue, bool)
{
#if defined(_WIN64)
	return defaultValue;
#else
	DLL_EXPORT_STUB_BREAK();
	return false;
#endif
}

float ConfigFile::getKeyFloat(const char *, const char *, int, float defaultValue)
{
#if defined(_WIN64)
	return defaultValue;
#else
	DLL_EXPORT_STUB_BREAK();
	return 0.0f;
#endif
}

float ConfigFile::getKeyFloat(const char *, const char *, float defaultValue, bool)
{
#if defined(_WIN64)
	return defaultValue;
#else
	DLL_EXPORT_STUB_BREAK();
	return 0.0f;
#endif
}

// ======================================================================

real Clock::frameTime()
{
	DLL_EXPORT_STUB_BREAK();
	return 0.0f;
}

// ======================================================================

void Profiler::enter(char const *)
{
	DLL_EXPORT_STUB_BREAK();
}

void Profiler::leave(char const *)
{
	DLL_EXPORT_STUB_BREAK();
}

void Profiler::transfer(char const *, char const *)
{
	DLL_EXPORT_STUB_BREAK();
}

// ======================================================================

AbstractFile *TreeFile::open(const char *, AbstractFile::PriorityType, bool)
{
	DLL_EXPORT_STUB_BREAK();
	return NULL;
}

// ======================================================================

CrcString::CrcString()
{
	DLL_EXPORT_STUB_BREAK();
}

CrcString::~CrcString()
{
	DLL_EXPORT_STUB_BREAK();
}

bool CrcString::operator < (CrcString const &) const
{
	return false;
}

// ======================================================================

PersistentCrcString::PersistentCrcString(CrcString const &)
{
	DLL_EXPORT_STUB_BREAK();
}

PersistentCrcString::~PersistentCrcString()
{
	DLL_EXPORT_STUB_BREAK();
}


char const * PersistentCrcString::getString() const
{
	DLL_EXPORT_STUB_BREAK();
	return NULL;
}

void PersistentCrcString::clear()
{
	DLL_EXPORT_STUB_BREAK();
}

void PersistentCrcString::set(char const *, bool)
{
	DLL_EXPORT_STUB_BREAK();
}

void PersistentCrcString::set(char const *, uint32)
{
	DLL_EXPORT_STUB_BREAK();
}

// ======================================================================

TemporaryCrcString::TemporaryCrcString(char const *, bool)
{
	DLL_EXPORT_STUB_BREAK();
}

TemporaryCrcString::~TemporaryCrcString()
{
	DLL_EXPORT_STUB_BREAK();
}

char const * TemporaryCrcString::getString() const
{
	DLL_EXPORT_STUB_BREAK();
	return NULL;
}

void TemporaryCrcString::clear()
{
	DLL_EXPORT_STUB_BREAK();
}

void TemporaryCrcString::set(char const *, bool)
{
	DLL_EXPORT_STUB_BREAK();
}

void TemporaryCrcString::set(char const *, uint32)
{
	DLL_EXPORT_STUB_BREAK();
}

// ======================================================================

void Graphics::setLastError(char const *, char const *)
{
	DLL_EXPORT_STUB_BREAK();
}

// ======================================================================

bool Graphics::writeImage(char const *, int const, int const, int const, int const *, bool const, Gl_imageFormat const, Rectangle2d const *)
{
	DLL_EXPORT_STUB_BREAK();
	return true;
}

// ======================================================================

void CrashReportInformation::addStaticText(char const *, ...)
{
	DLL_EXPORT_STUB_BREAK();
}


// ======================================================================

PerformanceTimer::PerformanceTimer()
{
	DLL_EXPORT_STUB_BREAK();
}

PerformanceTimer::~PerformanceTimer()
{
	DLL_EXPORT_STUB_BREAK();
}

void PerformanceTimer::start()
{
	DLL_EXPORT_STUB_BREAK();
}

void PerformanceTimer::resume()
{
	DLL_EXPORT_STUB_BREAK();
}

void PerformanceTimer::stop()
{
	DLL_EXPORT_STUB_BREAK();
}

float PerformanceTimer::getElapsedTime() const
{
	DLL_EXPORT_STUB_BREAK();
	return 0.0f;
}

// ======================================================================

Transform const & Object::getTransform_o2w() const
{
#if defined(_WIN64)
	Object const * const attachedToObject = getAttachedTo();
	if (attachedToObject)
	{
		static Transform objectToWorld;
		Transform const attachedToWorld(attachedToObject->getTransform_o2w());
		objectToWorld.multiply(attachedToWorld, getTransform_o2p());
		return objectToWorld;
	}

	return getTransform_o2p();
#else
	DLL_EXPORT_STUB_BREAK();
	return Transform::identity;
#endif
}

// ======================================================================

BOOL APIENTRY DllMain(HANDLE, DWORD, LPVOID)
{
	return TRUE;
}
