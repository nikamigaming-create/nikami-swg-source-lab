// ======================================================================
//
// Direct3d11_InputLayoutCache.cpp
//
// ======================================================================

#include "FirstDirect3d11.h"
#include "Direct3d11_InputLayoutCache.h"

#include "clientGraphics/VertexBufferDescriptor.h"
#include "clientGraphics/VertexBufferFormat.h"

#include <d3dcompiler.h>

#include <map>
#include <string.h>
#include <vector>

// ======================================================================

namespace Direct3d11InputLayoutCacheNamespace
{
	typedef std::map<uint32, ID3D11InputLayout *> InputLayoutMap;
	typedef std::map<std::vector<uint32>, ID3D11InputLayout *> InputLayoutVectorMap;

	InputLayoutMap *ms_inputLayoutMap;
	InputLayoutMap *ms_terrainDot3InputLayoutMap;
	InputLayoutMap *ms_detailBumpInputLayoutMap;
	InputLayoutVectorMap *ms_inputLayoutVectorMap;

	template <typename T>
	void releaseCom(T *&object)
	{
		if (object)
		{
			object->Release();
			object = 0;
		}
	}

	DXGI_FORMAT getTexCoordFormat(int dimension)
	{
		switch (dimension)
		{
			case 1:
				return DXGI_FORMAT_R32_FLOAT;
			case 2:
				return DXGI_FORMAT_R32G32_FLOAT;
			case 3:
				return DXGI_FORMAT_R32G32B32_FLOAT;
			default:
				return DXGI_FORMAT_R32G32B32A32_FLOAT;
		}
	}

	void addInputElement(std::vector<D3D11_INPUT_ELEMENT_DESC> &elements, UINT slot, char const *semantic, UINT semanticIndex, DXGI_FORMAT format, UINT offset)
	{
		D3D11_INPUT_ELEMENT_DESC element;
		ZeroMemory(&element, sizeof(element));
		element.SemanticName = semantic;
		element.SemanticIndex = semanticIndex;
		element.Format = format;
		element.InputSlot = slot;
		element.AlignedByteOffset = offset;
		element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		element.InstanceDataStepRate = 0;
		elements.push_back(element);
	}

	void addInputElementsForDescriptor(std::vector<D3D11_INPUT_ELEMENT_DESC> &elements, UINT slot, VertexBufferDescriptor const &descriptor, VertexBufferFormat const &format)
	{
		if (descriptor.offsetPosition >= 0)
			addInputElement(elements, slot, "POSITION", 0, descriptor.offsetOoz >= 0 ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT, static_cast<UINT>(descriptor.offsetPosition));
		if (descriptor.offsetNormal >= 0)
			addInputElement(elements, slot, "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, static_cast<UINT>(descriptor.offsetNormal));
		if (descriptor.offsetColor0 >= 0)
			addInputElement(elements, slot, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(descriptor.offsetColor0));
		if (descriptor.offsetColor1 >= 0)
			addInputElement(elements, slot, "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(descriptor.offsetColor1));
		for (int i = 0; i < 4; ++i)
		{
			if (i < format.getNumberOfTextureCoordinateSets() && descriptor.offsetTextureCoordinateSet[i] >= 0)
				addInputElement(elements, slot, "TEXCOORD", static_cast<UINT>(i), getTexCoordFormat(format.getTextureCoordinateSetDimension(i)), static_cast<UINT>(descriptor.offsetTextureCoordinateSet[i]));
			else if (format.getNumberOfTextureCoordinateSets() > 0 && descriptor.offsetTextureCoordinateSet[0] >= 0)
				addInputElement(elements, slot, "TEXCOORD", static_cast<UINT>(i), getTexCoordFormat(format.getTextureCoordinateSetDimension(0)), static_cast<UINT>(descriptor.offsetTextureCoordinateSet[0]));
		}
	}

	void addTerrainInputElementsForDescriptor(std::vector<D3D11_INPUT_ELEMENT_DESC> &elements, UINT slot, VertexBufferDescriptor const &descriptor, VertexBufferFormat const &format)
	{
		if (descriptor.offsetPosition >= 0)
			addInputElement(elements, slot, "POSITION", 0, descriptor.offsetOoz >= 0 ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT, static_cast<UINT>(descriptor.offsetPosition));
		if (descriptor.offsetNormal >= 0)
			addInputElement(elements, slot, "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, static_cast<UINT>(descriptor.offsetNormal));
		if (descriptor.offsetColor0 >= 0)
			addInputElement(elements, slot, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(descriptor.offsetColor0));
		for (int i = 0; i < 4; ++i)
		{
			if (i < format.getNumberOfTextureCoordinateSets() && descriptor.offsetTextureCoordinateSet[i] >= 0)
				addInputElement(elements, slot, "TEXCOORD", static_cast<UINT>(i), getTexCoordFormat(format.getTextureCoordinateSetDimension(i)), static_cast<UINT>(descriptor.offsetTextureCoordinateSet[i]));
			else if (format.getNumberOfTextureCoordinateSets() > 0 && descriptor.offsetTextureCoordinateSet[0] >= 0)
				addInputElement(elements, slot, "TEXCOORD", static_cast<UINT>(i), getTexCoordFormat(format.getTextureCoordinateSetDimension(0)), static_cast<UINT>(descriptor.offsetTextureCoordinateSet[0]));
		}
	}

	void satisfyInputSignature(std::vector<D3D11_INPUT_ELEMENT_DESC> &elements)
	{
		bool hasPosition = false;
		bool hasNormal = false;
		bool hasColor0 = false;
		bool hasColor1 = false;
		bool hasTexCoord[4] = { false, false, false, false };

		for (size_t i = 0; i < elements.size(); ++i)
		{
			if (strcmp(elements[i].SemanticName, "POSITION") == 0 && elements[i].SemanticIndex == 0)
				hasPosition = true;
			if (strcmp(elements[i].SemanticName, "NORMAL") == 0 && elements[i].SemanticIndex == 0)
				hasNormal = true;
			if (strcmp(elements[i].SemanticName, "COLOR") == 0 && elements[i].SemanticIndex == 0)
				hasColor0 = true;
			if (strcmp(elements[i].SemanticName, "COLOR") == 0 && elements[i].SemanticIndex == 1)
				hasColor1 = true;
			if (strcmp(elements[i].SemanticName, "TEXCOORD") == 0 && elements[i].SemanticIndex < 4)
				hasTexCoord[elements[i].SemanticIndex] = true;
		}

		if (!hasPosition)
			addInputElement(elements, 0, "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
		if (!hasNormal)
			addInputElement(elements, 0, "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0);
		if (!hasColor0)
			addInputElement(elements, 0, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
		if (!hasColor1)
			addInputElement(elements, 0, "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
		for (int i = 0; i < 4; ++i)
			if (!hasTexCoord[i])
				addInputElement(elements, 0, "TEXCOORD", static_cast<UINT>(i), DXGI_FORMAT_R32G32_FLOAT, 0);
	}

	ID3D11InputLayout *createLayout(ID3D11Device *device, ID3DBlob *vertexShaderBytecode, std::vector<D3D11_INPUT_ELEMENT_DESC> const &elements)
	{
		if (!device || !vertexShaderBytecode || elements.empty())
			return 0;

		ID3D11InputLayout *layout = 0;
		HRESULT const hr = device->CreateInputLayout(&elements[0], static_cast<UINT>(elements.size()), vertexShaderBytecode->GetBufferPointer(), vertexShaderBytecode->GetBufferSize(), &layout);
		return FAILED(hr) ? 0 : layout;
	}
}
using namespace Direct3d11InputLayoutCacheNamespace;

// ======================================================================

ID3D11InputLayout *Direct3d11_InputLayoutCache::getDefault(
	ID3D11Device *device,
	ID3DBlob *vertexShaderBytecode,
	VertexBufferDescriptor const &descriptor,
	VertexBufferFormat const &format,
	int activeTextureCoordinateSet,
	LogFunction logFunction)
{
	if (!ms_inputLayoutMap)
		ms_inputLayoutMap = new InputLayoutMap;

	uint32 const layoutKey = format.getFlags() ^ (static_cast<uint32>(activeTextureCoordinateSet & 0xf) << 28);
	InputLayoutMap::iterator found = ms_inputLayoutMap->find(layoutKey);
	if (found != ms_inputLayoutMap->end())
		return found->second;

	std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
	addInputElementsForDescriptor(elements, 0, descriptor, format);
	satisfyInputSignature(elements);

	ID3D11InputLayout *layout = createLayout(device, vertexShaderBytecode, elements);
	if (!layout)
	{
		if (logFunction)
			logFunction("CreateInputLayout failed flags=0x%08x texSet=%d elements=%u pos=%d color=%d tex0=%d", format.getFlags(), activeTextureCoordinateSet, static_cast<unsigned>(elements.size()), descriptor.offsetPosition, descriptor.offsetColor0, descriptor.offsetTextureCoordinateSet[0]);
		return 0;
	}

	(*ms_inputLayoutMap)[layoutKey] = layout;
	return layout;
}

// ----------------------------------------------------------------------

ID3D11InputLayout *Direct3d11_InputLayoutCache::getTerrainDot3(
	ID3D11Device *device,
	ID3DBlob *vertexShaderBytecode,
	VertexBufferDescriptor const &descriptor,
	VertexBufferFormat const &format,
	LogFunction logFunction)
{
	if (!ms_terrainDot3InputLayoutMap)
		ms_terrainDot3InputLayoutMap = new InputLayoutMap;

	uint32 const layoutKey = format.getFlags();
	InputLayoutMap::iterator found = ms_terrainDot3InputLayoutMap->find(layoutKey);
	if (found != ms_terrainDot3InputLayoutMap->end())
		return found->second;

	std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
	addTerrainInputElementsForDescriptor(elements, 0, descriptor, format);
	satisfyInputSignature(elements);

	ID3D11InputLayout *layout = createLayout(device, vertexShaderBytecode, elements);
	if (!layout)
	{
		if (logFunction)
			logFunction("CreateTerrainInputLayout failed flags=0x%08x elements=%u tex0=%d tex1=%d tex2=%d tex3=%d", format.getFlags(), static_cast<unsigned>(elements.size()), descriptor.offsetTextureCoordinateSet[0], descriptor.offsetTextureCoordinateSet[1], descriptor.offsetTextureCoordinateSet[2], descriptor.offsetTextureCoordinateSet[3]);
		return 0;
	}

	(*ms_terrainDot3InputLayoutMap)[layoutKey] = layout;
	return layout;
}

// ----------------------------------------------------------------------

ID3D11InputLayout *Direct3d11_InputLayoutCache::getDetailBump(
	ID3D11Device *device,
	ID3DBlob *vertexShaderBytecode,
	VertexBufferDescriptor const &descriptor,
	VertexBufferFormat const &format,
	LogFunction logFunction)
{
	if (!ms_detailBumpInputLayoutMap)
		ms_detailBumpInputLayoutMap = new InputLayoutMap;

	uint32 const layoutKey = format.getFlags();
	InputLayoutMap::iterator found = ms_detailBumpInputLayoutMap->find(layoutKey);
	if (found != ms_detailBumpInputLayoutMap->end())
		return found->second;

	std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
	addTerrainInputElementsForDescriptor(elements, 0, descriptor, format);
	satisfyInputSignature(elements);

	ID3D11InputLayout *layout = createLayout(device, vertexShaderBytecode, elements);
	if (!layout)
	{
		if (logFunction)
			logFunction("CreateDetailBumpInputLayout failed flags=0x%08x elements=%u tex0=%d tex1=%d tex2=%d tex3=%d", format.getFlags(), static_cast<unsigned>(elements.size()), descriptor.offsetTextureCoordinateSet[0], descriptor.offsetTextureCoordinateSet[1], descriptor.offsetTextureCoordinateSet[2], descriptor.offsetTextureCoordinateSet[3]);
		return 0;
	}

	(*ms_detailBumpInputLayoutMap)[layoutKey] = layout;
	return layout;
}

// ----------------------------------------------------------------------

ID3D11InputLayout *Direct3d11_InputLayoutCache::getVertexVector(
	ID3D11Device *device,
	ID3DBlob *vertexShaderBytecode,
	int activeTextureCoordinateSet,
	int activeVertexStreamCount,
	VertexBufferDescriptor const * const *activeVertexStreamDescriptors,
	ID3D11Buffer * const *activeVertexStreamBuffers,
	uint32 const *activeVertexStreamFormatFlags,
	LogFunction logFunction)
{
	if (!ms_inputLayoutVectorMap)
		ms_inputLayoutVectorMap = new InputLayoutVectorMap;

	std::vector<uint32> key;
	key.push_back(static_cast<uint32>(activeTextureCoordinateSet & 0xf));
	for (int i = 0; i < activeVertexStreamCount; ++i)
		key.push_back(activeVertexStreamFormatFlags[i]);

	InputLayoutVectorMap::iterator found = ms_inputLayoutVectorMap->find(key);
	if (found != ms_inputLayoutVectorMap->end())
		return found->second;

	std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
	for (int i = 0; i < activeVertexStreamCount; ++i)
	{
		if (!activeVertexStreamDescriptors[i] || !activeVertexStreamBuffers[i])
			return 0;

		VertexBufferFormat streamFormat;
		streamFormat.setFlags(activeVertexStreamFormatFlags[i]);
		addInputElementsForDescriptor(elements, static_cast<UINT>(i), *activeVertexStreamDescriptors[i], streamFormat);
	}
	satisfyInputSignature(elements);

	ID3D11InputLayout *layout = createLayout(device, vertexShaderBytecode, elements);
	if (!layout)
	{
		if (logFunction)
			logFunction("CreateInputLayout vector failed streams=%d elements=%u", activeVertexStreamCount, static_cast<unsigned>(elements.size()));
		return 0;
	}

	(*ms_inputLayoutVectorMap)[key] = layout;
	return layout;
}

// ----------------------------------------------------------------------

void Direct3d11_InputLayoutCache::release()
{
	if (ms_inputLayoutMap)
	{
		for (InputLayoutMap::iterator iter = ms_inputLayoutMap->begin(); iter != ms_inputLayoutMap->end(); ++iter)
			releaseCom(iter->second);
		delete ms_inputLayoutMap;
		ms_inputLayoutMap = 0;
	}
	if (ms_terrainDot3InputLayoutMap)
	{
		for (InputLayoutMap::iterator iter = ms_terrainDot3InputLayoutMap->begin(); iter != ms_terrainDot3InputLayoutMap->end(); ++iter)
			releaseCom(iter->second);
		delete ms_terrainDot3InputLayoutMap;
		ms_terrainDot3InputLayoutMap = 0;
	}
	if (ms_detailBumpInputLayoutMap)
	{
		for (InputLayoutMap::iterator iter = ms_detailBumpInputLayoutMap->begin(); iter != ms_detailBumpInputLayoutMap->end(); ++iter)
			releaseCom(iter->second);
		delete ms_detailBumpInputLayoutMap;
		ms_detailBumpInputLayoutMap = 0;
	}
	if (ms_inputLayoutVectorMap)
	{
		for (InputLayoutVectorMap::iterator iter = ms_inputLayoutVectorMap->begin(); iter != ms_inputLayoutVectorMap->end(); ++iter)
			releaseCom(iter->second);
		delete ms_inputLayoutVectorMap;
		ms_inputLayoutVectorMap = 0;
	}
}

// ======================================================================
