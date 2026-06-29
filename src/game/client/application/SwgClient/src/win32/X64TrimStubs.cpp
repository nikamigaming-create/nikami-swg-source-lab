#include "FirstSwgClient.h"

#if defined(_WIN64)

#include <cstddef>
#include <cstdlib>
#include <string>
#include <windows.h>

namespace
{
	extern "C" unsigned __int64 __cdecl trimReturnZero()
	{
		return 0;
	}

	extern "C" char const * __cdecl trimReturnEmptyString()
	{
		return "";
	}

	HMODULE loadMilesLibrary()
	{
		char overridePath[MAX_PATH];
		DWORD const overrideLength = GetEnvironmentVariableA("SWG_MSS64_PATH", overridePath, sizeof(overridePath));
		if (overrideLength > 0 && overrideLength < sizeof(overridePath))
		{
			HMODULE const module = LoadLibraryA(overridePath);
			if (module)
				return module;
		}

		return LoadLibraryA("mss64.dll");
	}

	void (*resolveMilesFunction(char const *name, void (*fallback)()))()
	{
		static HMODULE const milesModule = loadMilesLibrary();
		if (!milesModule)
			return fallback;

		FARPROC const proc = GetProcAddress(milesModule, name);
		return proc ? reinterpret_cast<void (*)()>(proc) : fallback;
	}

	void trimDestroyResponse(void *)
	{
	}

	void trimDestroyEvent(void *)
	{
	}

	int trimGetMessage(void *)
	{
		return -1;
	}

	int trimIssueRequest(void *)
	{
		return -1;
	}

	char * trimStringDuplicate(char const *)
	{
		return 0;
	}

	void trimCreateRequest(void *)
	{
	}

	int trimAllocSdkHandle(char const *, unsigned short, unsigned int *)
	{
		return -1;
	}

	int trimFreeSdkHandle(unsigned int)
	{
		return 0;
	}

	void trimXmlFree(void *)
	{
	}
}

#define MILES_AIL_IMPORT(name) extern "C" void (*__imp_##name)() = resolveMilesFunction(#name, reinterpret_cast<void (*)()>(&trimReturnZero))

MILES_AIL_IMPORT(AIL_get_timer_highest_delay);
MILES_AIL_IMPORT(AIL_serve);
MILES_AIL_IMPORT(AIL_startup);
MILES_AIL_IMPORT(AIL_get_preference);
MILES_AIL_IMPORT(AIL_shutdown);
MILES_AIL_IMPORT(AIL_set_preference);
MILES_AIL_IMPORT(AIL_lock);
MILES_AIL_IMPORT(AIL_unlock);
MILES_AIL_IMPORT(AIL_open_digital_driver);
extern "C" void (*__imp_AIL_set_redist_directory)() = resolveMilesFunction("AIL_set_redist_directory", reinterpret_cast<void (*)()>(&trimReturnEmptyString));
MILES_AIL_IMPORT(AIL_digital_CPU_percent);
MILES_AIL_IMPORT(AIL_digital_latency);
MILES_AIL_IMPORT(AIL_allocate_sample_handle);
MILES_AIL_IMPORT(AIL_speaker_configuration);
MILES_AIL_IMPORT(AIL_release_sample_handle);
MILES_AIL_IMPORT(AIL_set_sample_file);
MILES_AIL_IMPORT(AIL_set_named_sample_file);
MILES_AIL_IMPORT(AIL_start_sample);
MILES_AIL_IMPORT(AIL_stop_sample);
MILES_AIL_IMPORT(AIL_end_sample);
MILES_AIL_IMPORT(AIL_set_sample_playback_rate);
MILES_AIL_IMPORT(AIL_set_sample_volume_levels);
MILES_AIL_IMPORT(AIL_set_sample_reverb_levels);
MILES_AIL_IMPORT(AIL_set_sample_loop_count);
MILES_AIL_IMPORT(AIL_set_sample_loop_block);
MILES_AIL_IMPORT(AIL_sample_status);
MILES_AIL_IMPORT(AIL_sample_playback_rate);
MILES_AIL_IMPORT(AIL_sample_volume_levels);
MILES_AIL_IMPORT(AIL_sample_reverb_levels);
MILES_AIL_IMPORT(AIL_set_sample_position);
MILES_AIL_IMPORT(AIL_sample_position);
MILES_AIL_IMPORT(AIL_register_EOS_callback);
MILES_AIL_IMPORT(AIL_set_sample_ms_position);
MILES_AIL_IMPORT(AIL_sample_ms_position);
MILES_AIL_IMPORT(AIL_open_stream);
MILES_AIL_IMPORT(AIL_close_stream);
MILES_AIL_IMPORT(AIL_stream_sample_handle);
MILES_AIL_IMPORT(AIL_start_stream);
MILES_AIL_IMPORT(AIL_set_stream_loop_count);
MILES_AIL_IMPORT(AIL_set_stream_loop_block);
MILES_AIL_IMPORT(AIL_stream_status);
MILES_AIL_IMPORT(AIL_register_stream_callback);
MILES_AIL_IMPORT(AIL_set_stream_ms_position);
MILES_AIL_IMPORT(AIL_stream_ms_position);
MILES_AIL_IMPORT(AIL_set_file_callbacks);
MILES_AIL_IMPORT(AIL_WAV_info);
MILES_AIL_IMPORT(AIL_file_type);
MILES_AIL_IMPORT(AIL_room_type);
MILES_AIL_IMPORT(AIL_set_room_type);
MILES_AIL_IMPORT(AIL_set_3D_rolloff_factor);
MILES_AIL_IMPORT(AIL_set_sample_obstruction);
MILES_AIL_IMPORT(AIL_set_sample_occlusion);
MILES_AIL_IMPORT(AIL_set_sample_3D_distances);
MILES_AIL_IMPORT(AIL_set_sample_3D_position);
MILES_AIL_IMPORT(AIL_set_sample_3D_velocity_vector);
MILES_AIL_IMPORT(AIL_set_listener_3D_position);
MILES_AIL_IMPORT(AIL_set_listener_3D_velocity_vector);
MILES_AIL_IMPORT(AIL_set_listener_3D_orientation);

extern "C" void (*__imp_AIL_file_error)() = resolveMilesFunction("AIL_file_error", reinterpret_cast<void (*)()>(&trimReturnZero));
extern "C" void (*__imp_AIL_last_error)() = resolveMilesFunction("AIL_last_error", reinterpret_cast<void (*)()>(&trimReturnEmptyString));

struct HINSTANCE__;
struct vx_resp_base;
struct vx_evt_base;
struct vx_message_base;
struct vx_req_base;
struct vx_req_connector_initiate_shutdown;
struct vx_req_account_logout;
struct vx_req_connector_create;
struct vx_req_account_login;
struct vx_req_sessiongroup_add_session;
struct vx_req_sessiongroup_remove_session;
struct vx_req_session_create;
struct vx_req_session_send_message;
struct vx_req_connector_mute_local_mic;
struct vx_req_connector_mute_local_speaker;
struct vx_req_connector_set_local_mic_volume;
struct vx_req_connector_set_local_speaker_volume;
struct vx_req_session_set_participant_mute_for_me;
struct vx_req_session_set_participant_volume_for_me;
struct vx_req_aux_get_render_devices;
struct vx_req_aux_set_render_device;
struct vx_req_aux_get_capture_devices;
struct vx_req_aux_set_capture_device;
struct vx_req_aux_start_buffer_capture;
struct vx_req_aux_play_audio_buffer;
struct vx_req_aux_render_audio_stop;
struct vx_req_session_set_3d_position;
struct vx_req_sessiongroup_set_tx_session;
struct vx_req_session_send_notification;
struct vx_req_aux_capture_audio_start;
struct vx_req_aux_capture_audio_stop;
struct vx_req_aux_set_mic_level;
struct vx_req_aux_set_speaker_level;
struct vx_req_aux_diagnostic_state_dump;

enum VivoxCheckMic
{
	VCM_OK = 0,
	VCM_ERROR = (1 << 4)
};

HINSTANCE__ * shVivoxDll = 0;
void (*destroy_resp)(vx_resp_base *) = reinterpret_cast<void (*)(vx_resp_base *)>(&trimDestroyResponse);
void (*destroy_evt)(vx_evt_base *) = reinterpret_cast<void (*)(vx_evt_base *)>(&trimDestroyEvent);
int (*vx_get_message)(vx_message_base **) = reinterpret_cast<int (*)(vx_message_base **)>(&trimGetMessage);
int (*vx_issue_request)(vx_req_base *) = reinterpret_cast<int (*)(vx_req_base *)>(&trimIssueRequest);
char * (*vx_strdup)(char const *) = trimStringDuplicate;
void (*vx_req_connector_initiate_shutdown_create)(vx_req_connector_initiate_shutdown **) = reinterpret_cast<void (*)(vx_req_connector_initiate_shutdown **)>(&trimCreateRequest);
void (*vx_req_account_logout_create)(vx_req_account_logout **) = reinterpret_cast<void (*)(vx_req_account_logout **)>(&trimCreateRequest);
void (*vx_req_connector_create_create)(vx_req_connector_create **) = reinterpret_cast<void (*)(vx_req_connector_create **)>(&trimCreateRequest);
void (*vx_req_account_login_create)(vx_req_account_login **) = reinterpret_cast<void (*)(vx_req_account_login **)>(&trimCreateRequest);
void (*vx_req_sessiongroup_add_session_create)(vx_req_sessiongroup_add_session **) = reinterpret_cast<void (*)(vx_req_sessiongroup_add_session **)>(&trimCreateRequest);
void (*vx_req_sessiongroup_remove_session_create)(vx_req_sessiongroup_remove_session **) = reinterpret_cast<void (*)(vx_req_sessiongroup_remove_session **)>(&trimCreateRequest);
void (*vx_req_session_create_create)(vx_req_session_create **) = reinterpret_cast<void (*)(vx_req_session_create **)>(&trimCreateRequest);
void (*vx_req_session_send_message_create)(vx_req_session_send_message **) = reinterpret_cast<void (*)(vx_req_session_send_message **)>(&trimCreateRequest);
void (*vx_req_connector_mute_local_mic_create)(vx_req_connector_mute_local_mic **) = reinterpret_cast<void (*)(vx_req_connector_mute_local_mic **)>(&trimCreateRequest);
void (*vx_req_connector_mute_local_speaker_create)(vx_req_connector_mute_local_speaker **) = reinterpret_cast<void (*)(vx_req_connector_mute_local_speaker **)>(&trimCreateRequest);
void (*vx_req_connector_set_local_mic_volume_create)(vx_req_connector_set_local_mic_volume **) = reinterpret_cast<void (*)(vx_req_connector_set_local_mic_volume **)>(&trimCreateRequest);
void (*vx_req_connector_set_local_speaker_volume_create)(vx_req_connector_set_local_speaker_volume **) = reinterpret_cast<void (*)(vx_req_connector_set_local_speaker_volume **)>(&trimCreateRequest);
void (*vx_req_session_set_participant_mute_for_me_create)(vx_req_session_set_participant_mute_for_me **) = reinterpret_cast<void (*)(vx_req_session_set_participant_mute_for_me **)>(&trimCreateRequest);
void (*vx_req_session_set_participant_volume_for_me_create)(vx_req_session_set_participant_volume_for_me **) = reinterpret_cast<void (*)(vx_req_session_set_participant_volume_for_me **)>(&trimCreateRequest);
void (*vx_req_aux_get_render_devices_create)(vx_req_aux_get_render_devices **) = reinterpret_cast<void (*)(vx_req_aux_get_render_devices **)>(&trimCreateRequest);
void (*vx_req_aux_set_render_device_create)(vx_req_aux_set_render_device **) = reinterpret_cast<void (*)(vx_req_aux_set_render_device **)>(&trimCreateRequest);
void (*vx_req_aux_get_capture_devices_create)(vx_req_aux_get_capture_devices **) = reinterpret_cast<void (*)(vx_req_aux_get_capture_devices **)>(&trimCreateRequest);
void (*vx_req_aux_set_capture_device_create)(vx_req_aux_set_capture_device **) = reinterpret_cast<void (*)(vx_req_aux_set_capture_device **)>(&trimCreateRequest);
void (*vx_req_aux_start_buffer_capture_create)(vx_req_aux_start_buffer_capture **) = reinterpret_cast<void (*)(vx_req_aux_start_buffer_capture **)>(&trimCreateRequest);
void (*vx_req_aux_play_audio_buffer_create)(vx_req_aux_play_audio_buffer **) = reinterpret_cast<void (*)(vx_req_aux_play_audio_buffer **)>(&trimCreateRequest);
void (*vx_req_aux_render_audio_stop_create)(vx_req_aux_render_audio_stop **) = reinterpret_cast<void (*)(vx_req_aux_render_audio_stop **)>(&trimCreateRequest);
void (*vx_req_session_set_3d_position_create)(vx_req_session_set_3d_position **) = reinterpret_cast<void (*)(vx_req_session_set_3d_position **)>(&trimCreateRequest);
void (*vx_req_sessiongroup_set_tx_session_create)(vx_req_sessiongroup_set_tx_session **) = reinterpret_cast<void (*)(vx_req_sessiongroup_set_tx_session **)>(&trimCreateRequest);
void (*vx_req_session_send_notification_create)(vx_req_session_send_notification **) = reinterpret_cast<void (*)(vx_req_session_send_notification **)>(&trimCreateRequest);
void (*vx_req_aux_capture_audio_start_create)(vx_req_aux_capture_audio_start **) = reinterpret_cast<void (*)(vx_req_aux_capture_audio_start **)>(&trimCreateRequest);
void (*vx_req_aux_capture_audio_stop_create)(vx_req_aux_capture_audio_stop **) = reinterpret_cast<void (*)(vx_req_aux_capture_audio_stop **)>(&trimCreateRequest);
void (*vx_req_aux_set_mic_level_create)(vx_req_aux_set_mic_level **) = reinterpret_cast<void (*)(vx_req_aux_set_mic_level **)>(&trimCreateRequest);
void (*vx_req_aux_set_speaker_level_create)(vx_req_aux_set_speaker_level **) = reinterpret_cast<void (*)(vx_req_aux_set_speaker_level **)>(&trimCreateRequest);
void (*vx_req_aux_diagnostic_state_dump_create)(vx_req_aux_diagnostic_state_dump **) = reinterpret_cast<void (*)(vx_req_aux_diagnostic_state_dump **)>(&trimCreateRequest);
int (*vx_alloc_sdk_handle)(char const *, unsigned short, unsigned int *) = trimAllocSdkHandle;
int (*vx_free_sdk_handle)(unsigned int) = trimFreeSdkHandle;

VivoxCheckMic sCheckMic(std::string const &, bool)
{
	return VCM_ERROR;
}

bool sStartService(char const *, char const *, int)
{
	return false;
}

bool sLoadVivoxDLL(void (*)(), void (*)(char const *, int, char const *, ...))
{
	return false;
}

void sUnloadVivoxDLL()
{
}

bool sGetKeyState(int)
{
	return false;
}

bool sGrabVivoxSystemMutex()
{
	return true;
}

bool sReleaseVivoxSystemMutex()
{
	return true;
}

extern "C" void * (*pcre_malloc)(size_t) = &std::malloc;
extern "C" void (*pcre_free)(void *) = &std::free;

extern "C" int xmlMemSetup(void (*)(void *), void * (*)(size_t), void * (*)(void *, size_t), char * (*)(char const *))
{
	return 0;
}

extern "C" void xmlCleanupParser()
{
}

extern "C" void * xmlNewDoc(char const *)
{
	return 0;
}

extern "C" void xmlFreeDoc(void *)
{
}

extern "C" void * xmlNewNode(void *, char const *)
{
	return 0;
}

extern "C" void * xmlDocGetRootElement(void *)
{
	return 0;
}

extern "C" void * xmlDocSetRootElement(void *, void *)
{
	return 0;
}

extern "C" void xmlDocDumpFormatMemory(void *, unsigned char **, int *, int)
{
}

extern "C" void * xmlParseMemory(char const *, int)
{
	return 0;
}

extern "C" void (*__imp_xmlFree)(void *) = &trimXmlFree;

extern "C" void * xmlNewProp(void *, char const *, char const *)
{
	return 0;
}

extern "C" void * xmlNewText(char const *)
{
	return 0;
}

extern "C" void * xmlAddChild(void *, void *)
{
	return 0;
}

unsigned long htonl(unsigned long value)
{
	return ((value & 0x000000ffUL) << 24) | ((value & 0x0000ff00UL) << 8) | ((value & 0x00ff0000UL) >> 8) | ((value & 0xff000000UL) >> 24);
}

unsigned long ntohl(unsigned long value)
{
	return htonl(value);
}

extern "C" unsigned long __stdcall lgLcdInit()
{
	return 0;
}

extern "C" unsigned long __stdcall lgLcdDeInit()
{
	return 0;
}

extern "C" unsigned long __stdcall lgLcdConnectA(void *)
{
	return 1;
}

extern "C" unsigned long __stdcall lgLcdDisconnect(int)
{
	return 0;
}

extern "C" unsigned long __stdcall lgLcdEnumerate(int, int, void *)
{
	return 1;
}

extern "C" unsigned long __stdcall lgLcdOpen(void *)
{
	return 1;
}

extern "C" unsigned long __stdcall lgLcdClose(int)
{
	return 0;
}

extern "C" unsigned long __stdcall lgLcdReadSoftButtons(int, unsigned long *)
{
	return 0;
}

extern "C" unsigned long __stdcall lgLcdUpdateBitmap(int, void const *, unsigned long)
{
	return 0;
}

extern "C" unsigned long __stdcall lgLcdSetAsLCDForegroundApp(int, int)
{
	return 0;
}

#endif
