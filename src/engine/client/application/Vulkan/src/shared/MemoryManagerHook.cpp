// ======================================================================
//
// MemoryManagerHook.cpp
//
// ======================================================================

#include "FirstVulkan.h"

// ======================================================================

#pragma warning(disable: 4100)

// ======================================================================

static void * __cdecl localAllocate(size_t size, uint32 owner, bool array, bool leakTest)
{
	return MemoryManager::allocate(size, owner, array, leakTest);
}

// ======================================================================

void *operator new(size_t size, MemoryManagerNotALeak)
{
#if defined(_M_IX86)
	_asm
	{
		push    ebp
		mov     ebp, esp

		push    0
		push    0
		mov     eax, dword ptr [ebp+4]
		push    eax
		mov     eax, dword ptr [ebp+8]
		push    eax
		call    localAllocate
		add     esp, 12

		mov     esp, ebp
		pop     ebp
		ret
	}
#else
	return localAllocate(size, 0, false, false);
#endif
}

// ----------------------------------------------------------------------

void *operator new(size_t size)
{
#if defined(_M_IX86)
	_asm
	{
		push    ebp
		mov     ebp, esp

		push    1
		push    0
		mov     eax, dword ptr [ebp+4]
		push    eax
		mov     eax, dword ptr [ebp+8]
		push    eax
		call    localAllocate
		add     esp, 12

		mov     esp, ebp
		pop     ebp
		ret
	}
#else
	return localAllocate(size, 0, false, true);
#endif
}

// ----------------------------------------------------------------------

void *operator new[](size_t size)
{
#if defined(_M_IX86)
	_asm
	{
		push    ebp
		mov     ebp, esp

		push    1
		push    1
		mov     eax, dword ptr [ebp+4]
		push    eax
		mov     eax, dword ptr [ebp+8]
		push    eax
		call    localAllocate
		add     esp, 12

		mov     esp, ebp
		pop     ebp
		ret
	}
#else
	return localAllocate(size, 0, true, true);
#endif
}

// ----------------------------------------------------------------------

void *operator new(size_t size, const char *file, int line)
{
	UNREF(file);
	UNREF(line);
#if defined(_M_IX86)
	_asm
	{
		push    ebp
		mov     ebp, esp

		push    1
		push    0
		mov     eax, dword ptr [ebp+4]
		push    eax
		mov     eax, dword ptr [ebp+8]
		push    eax
		call    localAllocate
		add     esp, 12

		mov     esp, ebp
		pop     ebp
		ret
	}
#else
	return localAllocate(size, 0, false, true);
#endif
}

// ----------------------------------------------------------------------

void *operator new[](size_t size, const char *file, int line)
{
	UNREF(file);
	UNREF(line);
#if defined(_M_IX86)
	_asm
	{
		push    ebp
		mov     ebp, esp

		push    1
		push    1
		mov     eax, dword ptr [ebp+4]
		push    eax
		mov     eax, dword ptr [ebp+8]
		push    eax
		call    localAllocate
		add     esp, 12

		mov     esp, ebp
		pop     ebp
		ret
	}
#else
	return localAllocate(size, 0, true, true);
#endif
}

// ----------------------------------------------------------------------

void operator delete(void *pointer)
{
	if (pointer)
		MemoryManager::free(pointer, false);
}

// ----------------------------------------------------------------------

void operator delete[](void *pointer)
{
	if (pointer)
		MemoryManager::free(pointer, true);
}

// ----------------------------------------------------------------------

void operator delete(void *pointer, const char *, int)
{
	if (pointer)
		MemoryManager::free(pointer, true);
}

// ----------------------------------------------------------------------

void operator delete[](void *pointer, const char *, int)
{
	if (pointer)
		MemoryManager::free(pointer, true);
}

// ======================================================================
