// FirstSharedNetwork.h
// Copyright 2000-02, Sony Online Entertainment Inc., all rights reserved. 

//-----------------------------------------------------------------------

#ifndef	INCLUDED_FirstSharedNetwork_H
#define	INCLUDED_FirstSharedNetwork_H

//-----------------------------------------------------------------------

// On x64, include winsock2 before any windows headers to prevent SOCKET type mismatch
#if defined(_WIN64) && !defined(_WINSOCK2API_)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "../../../../../../engine/shared/library/sharedFoundation/include/public/sharedFoundation/FirstSharedFoundation.h"
#include "sharedDebug/FirstSharedDebug.h"
#include "sharedMemoryManager/FirstSharedMemoryManager.h"

#include <string>

//-----------------------------------------------------------------------

#endif	// INCLUDED_FirstSharedNetwork_H
