// ======================================================================
//
// SetupSharedFoundation.cpp
// copyright 1998 Bootprint Entertainment
// copyright 2001 Sony Online Entertainment
//
// ======================================================================

#include "sharedFoundation/FirstSharedFoundation.h"
#include "sharedFoundation/SetupSharedFoundation.h"

#include "sharedDebug/DebugMonitor.h"
#include "sharedDebug/InstallTimer.h"
#include "sharedDebug/Profiler.h"

#include "sharedFoundation/ApplicationVersion.h"
#include "sharedFoundation/Clock.h"
#include "sharedFoundation/CommandLine.h"
#include "sharedFoundation/ConfigFile.h"
#include "sharedFoundation/ConfigSharedFoundation.h"
#include "sharedFoundation/CrashReportInformation.h"
#include "sharedFoundation/CrcLowerString.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedFoundation/Os.h"
#include "sharedFoundation/Production.h"
#include "sharedFoundation/RegistryKey.h"
#include "sharedFoundation/StaticCallbackEntry.h"
#include "sharedFoundation/MemoryBlockManager.h"
#include "sharedFoundation/Watcher.h"
#include "sharedLog/TailFileLogObserver.h"

#include <eh.h>
#include <cstdio>

// ======================================================================

namespace FatalNamespace
{
	extern char ms_buffer[32 * 1024];
}

namespace SetupSharedFoundationNamespace
{
	LONG __stdcall MyUnhandledExceptionFilter(LPEXCEPTION_POINTERS exceptionPointers);

	bool  ms_writeMiniDumps;

	void writeStartupTrace(char const *stage)
	{
		char path[MAX_PATH];
		DWORD const result = GetEnvironmentVariable("SWG_STARTUP_TRACE_FILE", path, sizeof(path));
		if (result == 0 || result >= sizeof(path))
			return;

		HANDLE const file = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
			return;

		char line[256];
		int const count = _snprintf_s(line, sizeof(line), _TRUNCATE, "SetupSharedFoundation:%s\r\n", stage);
		if (count > 0)
		{
			DWORD bytesWritten = 0;
			IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
		}

		CloseHandle(file);
	}

	void writeExceptionStartupTrace(LPEXCEPTION_POINTERS exceptionPointers)
	{
		char path[MAX_PATH];
		DWORD const result = GetEnvironmentVariable("SWG_STARTUP_TRACE_FILE", path, sizeof(path));
		if (result == 0 || result >= sizeof(path))
			return;

		HANDLE const file = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
			return;

		PEXCEPTION_RECORD const record = exceptionPointers ? exceptionPointers->ExceptionRecord : 0;
		if (record)
		{
			char line[512];
			int const count = _snprintf_s(line, sizeof(line), _TRUNCATE, "UnhandledException: code=0x%08lx flags=0x%08lx address=%p parameters=%lu\r\n",
				static_cast<unsigned long>(record->ExceptionCode),
				static_cast<unsigned long>(record->ExceptionFlags),
				record->ExceptionAddress,
				static_cast<unsigned long>(record->NumberParameters));
			if (count > 0)
			{
				DWORD bytesWritten = 0;
				IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
			}

			for (DWORD i = 0; i < record->NumberParameters; ++i)
			{
				int const parameterCount = _snprintf_s(line, sizeof(line), _TRUNCATE, "UnhandledExceptionParam[%lu]=0x%p\r\n",
					static_cast<unsigned long>(i),
					reinterpret_cast<void const *>(record->ExceptionInformation[i]));
				if (parameterCount > 0)
				{
					DWORD bytesWritten = 0;
					IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
				}
			}

#if defined(_M_X64)
			CONTEXT const * const context = exceptionPointers ? exceptionPointers->ContextRecord : 0;
			if (context)
			{
				HMODULE const moduleBase = GetModuleHandle(NULL);
				int const contextCount = _snprintf_s(line, sizeof(line), _TRUNCATE, "UnhandledContext: moduleBase=0x%p rip=0x%p rsp=0x%p rbp=0x%p\r\n",
					reinterpret_cast<void const *>(moduleBase),
					reinterpret_cast<void const *>(context->Rip),
					reinterpret_cast<void const *>(context->Rsp),
					reinterpret_cast<void const *>(context->Rbp));
				if (contextCount > 0)
				{
					DWORD bytesWritten = 0;
					IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
				}

				DWORD64 const * const stack = reinterpret_cast<DWORD64 const *>(context->Rsp);
				for (DWORD i = 0; i < 32; ++i)
				{
					int const stackCount = _snprintf_s(line, sizeof(line), _TRUNCATE, "UnhandledStack[%lu]=0x%p\r\n",
						static_cast<unsigned long>(i),
						reinterpret_cast<void const *>(stack[i]));
					if (stackCount > 0)
					{
						DWORD bytesWritten = 0;
						IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
					}
				}
			}
#endif
		}
		else
		{
			char const line[] = "UnhandledException: missing exception record\r\n";
			DWORD bytesWritten = 0;
			IGNORE_RETURN(WriteFile(file, line, static_cast<DWORD>(strlen(line)), &bytesWritten, NULL));
		}

		CloseHandle(file);
	}
}

using namespace SetupSharedFoundationNamespace;

// ======================================================================

LONG __stdcall SetupSharedFoundationNamespace::MyUnhandledExceptionFilter(LPEXCEPTION_POINTERS exceptionPointers)
{
	// make the routine somewhat safe from re-entrance
	static bool entered = false;
	if (entered)
		return EXCEPTION_CONTINUE_SEARCH;
	entered = true;

	// log some important information
	static char buffer[256];
	sprintf(buffer, "Exception %08x(%d) addr=%p\n", exceptionPointers->ExceptionRecord->ExceptionCode, exceptionPointers->ExceptionRecord->ExceptionCode, exceptionPointers->ExceptionRecord->ExceptionAddress);
	OutputDebugString(buffer);
	writeExceptionStartupTrace(exceptionPointers);

	// write the minidump if we're in here for the first time
	static bool ms_alreadyWroteMiniDump = false;
	if (ms_writeMiniDumps && !ms_alreadyWroteMiniDump)
	{
		ms_alreadyWroteMiniDump = true;

		uint64 timestamp;
		time_t now;
		tm t;

		IGNORE_RETURN(time(&now));
		IGNORE_RETURN(gmtime_r(&now, &t));
		timestamp = t.tm_year+1900; //lint !e732 !e737 !e776
		timestamp *= 100;
		timestamp += t.tm_mon+1; //lint !e737 !e776
		timestamp *= 100;
		timestamp += static_cast<unsigned int>(t.tm_mday);
		timestamp *= 100;
		timestamp += static_cast<unsigned int>(t.tm_hour);
		timestamp *= 100;
		timestamp += static_cast<unsigned int>(t.tm_min);
		timestamp *= 100;
		timestamp += static_cast<unsigned int>(t.tm_sec);

		static char fileName[512];

		sprintf(fileName, "%s-%s-%I64d.txt", Os::getShortProgramName(), ApplicationVersion::getInternalVersion(), timestamp);
		HANDLE const file = CreateFile(fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_ARCHIVE, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			char text1[] = "automated crash dump from ";
			DWORD bytesWritten;
			WriteFile(file, text1, static_cast<DWORD>(strlen(text1)), &bytesWritten, NULL);

			char const * text2 = Os::getShortProgramName();
			WriteFile(file, text2, static_cast<DWORD>(strlen(text2)), &bytesWritten, NULL);

			char text3[] = " ";
			WriteFile(file, text3, static_cast<DWORD>(strlen(text3)), &bytesWritten, NULL);

			char const * text4 = ApplicationVersion::getInternalVersion();
			WriteFile(file, text4, static_cast<DWORD>(strlen(text4)), &bytesWritten, NULL);

			char text5[] = "\n\n\n";
			WriteFile(file, text5, static_cast<DWORD>(strlen(text5)), &bytesWritten, NULL);

			if (exceptionPointers->ExceptionRecord->ExceptionCode == 0x80000003)
			{
				// write out the fatal buffer
				char const * text6 = FatalNamespace::ms_buffer;
				WriteFile(file, text6, static_cast<DWORD>(strlen(text6)), &bytesWritten, NULL);
			}
			else
			{
				char const * text6 = buffer;
				WriteFile(file, text6, static_cast<DWORD>(strlen(text6)), &bytesWritten, NULL);
			}

			char text7[] = "\n\n";
			WriteFile(file, text7, static_cast<DWORD>(strlen(text7)), &bytesWritten, NULL);

			char const * text8 = "";
			for (int i = 0; text8; ++i)
			{
				text8 = CrashReportInformation::getEntry(i);
				if (text8)
				{
					int const text8Length = static_cast<int>(strlen(text8));
					if (text8Length)
						WriteFile(file, text8, text8Length, &bytesWritten, NULL);
				}
			}

			CloseHandle(file);
		}

		sprintf(fileName, "%s-%s-%I64d.mdmp", Os::getShortProgramName(), ApplicationVersion::getInternalVersion(), timestamp);
		OutputDebugString("Generating minidump ");
		OutputDebugString(fileName);
		OutputDebugString("\n");
		DebugHelp::writeMiniDump(fileName, exceptionPointers);

		sprintf(fileName, "%s-%s-%I64d.log", Os::getShortProgramName(), ApplicationVersion::getInternalVersion(), timestamp);
		TailFileLogObserver::flushAllTailFileLogObservers(fileName);
	}

	// tell the Os not to abort so we can rethrow the exception
	Os::returnFromAbort();

	// Let the ExitChain do its job
	Fatal("ExceptionHandler invoked");

	// rethrow the exception so that the debugger can catch it
	entered = false;
	return EXCEPTION_CONTINUE_SEARCH;  //lint !e527 // Unreachable
}

// ----------------------------------------------------------------------

static void setFatalVersionString()
{
	char buffer[256];
	sprintf(buffer, "%s: %s\n", Os::getShortProgramName(), ApplicationVersion::getInternalVersion());
	FatalSetVersionString(buffer);
}

// ======================================================================
// Install the engine
//
// Remarks:
//
//   The settings in the Data structure will determine which subsystems
//   get initialized.

void SetupSharedFoundation::install(const Data &data)
{
	InstallTimer const installTimer("SetupSharedFoundation::install");
	writeStartupTrace("entry");

	DEBUG_REPORT_LOG(true, ("SetupSharedFoundation::install: version %s\n", ApplicationVersion::getInternalVersion()));
	writeStartupTrace("after-version-log");

	ms_writeMiniDumps = data.writeMiniDumps;
	SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);
	writeStartupTrace("after-exception-filter");

	// and get the command line stuff in quick so we can make decisions based on the command line settings
	CommandLine::install();
	writeStartupTrace("after-command-line-install");

	// feed CommandLine with appropriate strings
	if (data.commandLine)
		CommandLine::absorbString(data.commandLine);
	if (data.argc)
		CommandLine::absorbStrings(const_cast<const char**>(data.argv+1), data.argc-1);
	writeStartupTrace("after-command-line-absorb");

	// load the config file
	ConfigFile::install();
	writeStartupTrace("after-config-install");
	if (data.configFile)
		IGNORE_RETURN(ConfigFile::loadFile(data.configFile));
	writeStartupTrace("after-config-load");

	// get the post command-line text for the ConfigFile (key-value pairs)
	const char *configString = CommandLine::getPostCommandLineString();
	if (configString)
	{
		IGNORE_RETURN(ConfigFile::loadFromCommandLine(configString));
	}
	writeStartupTrace("after-post-command-line-config");

	// @todo codereorg should this be here?
	MemoryManager::registerDebugFlags();
#if PRODUCTION == 0
	Profiler::registerDebugFlags();
#endif
	FatalInstall();
	writeStartupTrace("after-fatal-install");

	// @todo move these, it's part of sharedDebug.  However, sharedDebug is installed before sharedFoundation, but these need the ConfigFile.  ugh.
#if PRODUCTION == 0
	DebugMonitor::install();
#endif
	SetWarningStrictFatal(ConfigFile::getKeyBool("SharedDebug", "strict", false));
	writeStartupTrace("after-debug-monitor");

	{
		ConfigSharedFoundation::Defaults defaults;
		defaults.frameRateLimit = data.frameRateLimit;
		defaults.minFrameRate = data.minFrameRate;
		defaults.demoMode       = data.demoMode;
		defaults.verboseWarnings = data.verboseWarnings;
		ConfigSharedFoundation::install(defaults);

		if (ConfigSharedFoundation::getCauseAccessViolation())
			static_cast<int*>(0)[0] = 0;
	}
	writeStartupTrace("after-config-shared-foundation");

	// @todo codereorg should this be here?
#ifdef _DEBUG
	MemoryManager::setReportAllocations (ConfigSharedFoundation::getMemoryManagerReportAllocations ());
#endif

	MemoryBlockManager::install (ConfigSharedFoundation::getMemoryBlockManagerDebugDumpOnRemove ());
	writeStartupTrace("after-memory-block-manager");

	ExitChain::install();
	writeStartupTrace("after-exit-chain");
	Report::install();
	writeStartupTrace("after-report");
	Clock::install(data.clockUsesSleep, data.clockUsesRecalibrationThread);
	writeStartupTrace("after-clock");
	CrashReportInformation::install();
	writeStartupTrace("after-crash-report-information");
	RegistryKey::install(data.productRegistryKey);
	writeStartupTrace("after-registry-key");

	PersistentCrcString::install();
	writeStartupTrace("after-persistent-crc-string");
	CrcLowerString::install();
	writeStartupTrace("after-crc-lower-string");

	WatchedByList::install();
	writeStartupTrace("after-watched-by-list");

	if (data.createWindow)
	{
		DEBUG_FATAL(data.useWindowHandle, ("exactly one of createWindow and useWindowHandle must be true"));
		writeStartupTrace("before-os-install-create-window");
		Os::install(data.hInstance, data.windowName, data.windowNormalIcon, data.windowSmallIcon);
		writeStartupTrace("after-os-install-create-window");
	}
	else
	{
		if (data.useWindowHandle)
		{
			writeStartupTrace("before-os-install-window-handle");
			Os::install(data.windowHandle, data.processMessagePump);
			writeStartupTrace("after-os-install-window-handle");
		}
		else
		{
			writeStartupTrace("before-os-install-headless");
			Os::install();
			writeStartupTrace("after-os-install-headless");
		}
	}

	StaticCallbackEntry::install();
	writeStartupTrace("after-static-callback-entry");

	setFatalVersionString();
	writeStartupTrace("after-fatal-version-string");
}

// ----------------------------------------------------------------------
// Call a function with appropriate exception handling
//
// Remarks:
//
//   If exception handling has been disabled in the config file, this routine
//   will call the callback without exception handling.

void SetupSharedFoundation::callbackWithExceptionHandling(
	void (*callback)(void)   // Routine to call with exception handling
	)
{
	callback();
}

// ----------------------------------------------------------------------
// Uninstall the engine
//
// Remarks:
//
//   This routine will properly uninstall the engine componenets that were
//   installed by SetupSharedFoundation::install().

void SetupSharedFoundation::remove(void)
{
	ExitChain::quit();

	if (!ConfigSharedFoundation::getDemoMode() && GetNumberOfWarnings())
		REPORT(true, Report::RF_print | Report::RF_log | Report::RF_dialog, ("%d warnings logged", GetNumberOfWarnings()));
}

// ======================================================================

SetupSharedFoundation::Data::Data(Defaults defaults)
{
	Zero(*this);

	switch (defaults)
	{
		case D_console: setupConsoleDefaults(); break;
		case D_game:    setupGameDefaults();    break;
		case D_mfc:     setupMfcDefaults();     break;
		default: DEBUG_FATAL(true, ("invalid enum value"));
	}
}

// ----------------------------------------------------------------------

void SetupSharedFoundation::Data::setupGameDefaults()
{
	createWindow                             = true;
	windowName                               = NULL;
	windowNormalIcon                         = NULL;
	windowSmallIcon                          = NULL;
	hInstance                                = NULL;
	useWindowHandle                          = false;
	processMessagePump                       = true;
	windowHandle                             = NULL;
	clockUsesSleep                           = false;
	clockUsesRecalibrationThread             = true;
	writeMiniDumps	                         = false;

	commandLine                              = NULL;
	argc                                     = 0;
	argv                                     = NULL;

	configFile                               = NULL;

	productRegistryKey                       = NULL;

	frameRateLimit                           = CONST_REAL(0);
	minFrameRate							 = CONST_REAL(0);

	lostFocusCallback                        = NULL;

	demoMode                                 = false;
	verboseWarnings                          = false;
}

// ----------------------------------------------------------------------

void SetupSharedFoundation::Data::setupConsoleDefaults()
{
	createWindow                             = false;
	windowName                               = NULL;
	windowNormalIcon                         = NULL;
	windowSmallIcon                          = NULL;
	hInstance                                = NULL;
	useWindowHandle                          = false;
	processMessagePump                       = true;
	windowHandle                             = NULL;
	clockUsesSleep                           = false;
	clockUsesRecalibrationThread             = false;
	writeMiniDumps	                         = false;

	commandLine                              = NULL;
	argc                                     = NULL;
	argv                                     = NULL;

	configFile                               = NULL;

	productRegistryKey                       = NULL;

	frameRateLimit                           = CONST_REAL(0);
	minFrameRate							 = CONST_REAL(0);

	lostFocusCallback                        = NULL;

	demoMode                                 = false;
	verboseWarnings                          = false;
}

// ----------------------------------------------------------------------

void SetupSharedFoundation::Data::setupMfcDefaults()
{
	createWindow                             = false;
	windowName                               = NULL;
	windowNormalIcon                         = NULL;
	windowSmallIcon                          = NULL;
	hInstance                                = NULL;
	useWindowHandle                          = false;
	processMessagePump                       = true;
	windowHandle                             = NULL;
	clockUsesSleep                           = false;
	clockUsesRecalibrationThread             = false;
	writeMiniDumps	                         = false;

	commandLine                              = NULL;
	argc                                     = 0;
	argv                                     = NULL;

	configFile                               = NULL;

	productRegistryKey                       = NULL;

	frameRateLimit                           = CONST_REAL(0);
	minFrameRate							 = CONST_REAL(0);

	demoMode                                 = false;
	verboseWarnings                          = false;
}

// ======================================================================
