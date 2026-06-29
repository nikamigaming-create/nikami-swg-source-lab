// ======================================================================
//
// Direct3d11_Diagnostics.cpp
//
// ======================================================================

#include "FirstDirect3d11.h"
#include "Direct3d11_Diagnostics.h"

#include <stdio.h>
#include <windows.h>

// ======================================================================

bool Direct3d11_Diagnostics::isEnabled()
{
	static bool initialized = false;
	static bool enabled = false;
	if (!initialized)
	{
		char value[16];
		DWORD const length = GetEnvironmentVariableA("SWG_D3D11_DIAGNOSTICS", value, sizeof(value));
		enabled = length > 0 && value[0] != '\0' && value[0] != '0';
		initialized = true;
	}
	return enabled;
}

// ----------------------------------------------------------------------

void Direct3d11_Diagnostics::write(char const *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	writeVa(format, arguments);
	va_end(arguments);
}

// ----------------------------------------------------------------------

void Direct3d11_Diagnostics::writeVa(char const *format, va_list arguments)
{
	if (!isEnabled())
		return;

	char buffer[4096];
	_vsnprintf(buffer, sizeof(buffer) - 1, format, arguments);
	buffer[sizeof(buffer) - 1] = '\0';
	OutputDebugStringA("Direct3d11: ");
	OutputDebugStringA(buffer);
	OutputDebugStringA("\n");
}

// ======================================================================
