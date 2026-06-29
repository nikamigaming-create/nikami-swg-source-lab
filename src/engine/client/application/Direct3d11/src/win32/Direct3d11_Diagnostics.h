// ======================================================================
//
// Direct3d11_Diagnostics.h
//
// ======================================================================

#ifndef INCLUDED_Direct3d11_Diagnostics_H
#define INCLUDED_Direct3d11_Diagnostics_H

// ======================================================================

#include "FirstDirect3d11.h"

#include <stdarg.h>

// ======================================================================

class Direct3d11_Diagnostics
{
public:
	static bool isEnabled();
	static void write(char const *format, ...);
	static void writeVa(char const *format, va_list arguments);

private:
	Direct3d11_Diagnostics();
	Direct3d11_Diagnostics(Direct3d11_Diagnostics const &);
	Direct3d11_Diagnostics &operator =(Direct3d11_Diagnostics const &);
};

// ======================================================================

#endif
