// ======================================================================
//
// Direct3d11_InputLayoutCache.h
//
// ======================================================================

#ifndef INCLUDED_Direct3d11_InputLayoutCache_H
#define INCLUDED_Direct3d11_InputLayoutCache_H

// ======================================================================

#include "FirstDirect3d11.h"

#include <d3d11.h>
#include <d3dcompiler.h>

// ======================================================================

struct VertexBufferDescriptor;
class VertexBufferFormat;

// ======================================================================

class Direct3d11_InputLayoutCache
{
public:
	typedef void (*LogFunction)(char const *format, ...);

	static ID3D11InputLayout *getDefault(
		ID3D11Device *device,
		ID3DBlob *vertexShaderBytecode,
		VertexBufferDescriptor const &descriptor,
		VertexBufferFormat const &format,
		int activeTextureCoordinateSet,
		LogFunction logFunction);

	static ID3D11InputLayout *getTerrainDot3(
		ID3D11Device *device,
		ID3DBlob *vertexShaderBytecode,
		VertexBufferDescriptor const &descriptor,
		VertexBufferFormat const &format,
		LogFunction logFunction);

	static ID3D11InputLayout *getDetailBump(
		ID3D11Device *device,
		ID3DBlob *vertexShaderBytecode,
		VertexBufferDescriptor const &descriptor,
		VertexBufferFormat const &format,
		LogFunction logFunction);

	static ID3D11InputLayout *getVertexVector(
		ID3D11Device *device,
		ID3DBlob *vertexShaderBytecode,
		int activeTextureCoordinateSet,
		int activeVertexStreamCount,
		VertexBufferDescriptor const * const *activeVertexStreamDescriptors,
		ID3D11Buffer * const *activeVertexStreamBuffers,
		uint32 const *activeVertexStreamFormatFlags,
		LogFunction logFunction);

	static void release();

private:
	Direct3d11_InputLayoutCache();
	Direct3d11_InputLayoutCache(Direct3d11_InputLayoutCache const &);
	Direct3d11_InputLayoutCache &operator =(Direct3d11_InputLayoutCache const &);
};

// ======================================================================

#endif
