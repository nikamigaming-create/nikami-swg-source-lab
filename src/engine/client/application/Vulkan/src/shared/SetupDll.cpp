// ======================================================================
//
// SetupDll.cpp
//
// ======================================================================

#include "FirstVulkan.h"

#include "sharedMath/Transform.h"
#include "sharedMath/VectorArgb.h"

#include <DelayImp.h>

// ======================================================================

const Transform Transform::identity;

const VectorArgb VectorArgb::solidBlack(CONST_REAL(1), CONST_REAL(0), CONST_REAL(0), CONST_REAL(0));
const VectorArgb VectorArgb::solidBlue(CONST_REAL(1), CONST_REAL(0), CONST_REAL(0), CONST_REAL(1));
const VectorArgb VectorArgb::solidCyan(CONST_REAL(1), CONST_REAL(0), CONST_REAL(1), CONST_REAL(1));
const VectorArgb VectorArgb::solidGreen(CONST_REAL(1), CONST_REAL(0), CONST_REAL(1), CONST_REAL(0));
const VectorArgb VectorArgb::solidRed(CONST_REAL(1), CONST_REAL(1), CONST_REAL(0), CONST_REAL(0));
const VectorArgb VectorArgb::solidMagenta(CONST_REAL(1), CONST_REAL(1), CONST_REAL(0), CONST_REAL(1));
const VectorArgb VectorArgb::solidYellow(CONST_REAL(1), CONST_REAL(1), CONST_REAL(1), CONST_REAL(0));
const VectorArgb VectorArgb::solidWhite(CONST_REAL(1), CONST_REAL(1), CONST_REAL(1), CONST_REAL(1));
const VectorArgb VectorArgb::solidGray(CONST_REAL(1), CONST_REAL(0.5), CONST_REAL(0.5), CONST_REAL(0.5));

// ======================================================================

static FARPROC WINAPI DliHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
	if (dliNotify == dliNotePreLoadLibrary && _stricmp(pdli->szDll, "dllexport.dll") == 0)
	{
		HMODULE const hostModule = GetModuleHandle(NULL);
		if (hostModule && GetProcAddress(hostModule, "?getInfo@TextureFormatInfo@@SAABU1@W4TextureFormat@@@Z"))
			return reinterpret_cast<FARPROC>(hostModule);
	}

	return 0;
}

// ======================================================================

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
#if _MSC_VER < 1300
	__pfnDliNotifyHook = DliHook;
#else
	__pfnDliNotifyHook2 = DliHook;
#endif
	return TRUE;
}

// ======================================================================
