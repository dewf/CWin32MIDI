// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the CWIN32MIDI_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// CWIN32MIDI_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef CWIN32MIDI_EXPORTS
#define CWIN32MIDI_API __declspec(dllexport)
#else
#define CWIN32MIDI_API __declspec(dllimport)
#endif

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#define APIHANDLE(x) struct _##x; typedef struct _##x* x

#ifdef __cplusplus
extern "C" {
#endif

    APIHANDLE(CWin32Midi_DeviceID);
    APIHANDLE(CWin32Midi_Device);

    typedef enum {
        CWin32Midi_EventType_Log,
        CWin32Midi_EventType_Data
    } CWin32Midi_EventType;

    typedef struct {
        CWin32Midi_EventType eventType;
        bool handled;
        struct {
            const char *message;
        } logEvent;
        struct {
            union {
                unsigned char bytes[4];
                unsigned int uint32;
            };
        } dataEvent;
    } CWin32Midi_Event;

    typedef int(CDECL *CWin32Midi_EventCallback)(CWin32Midi_Event *event, CWin32Midi_Device device, void *userData);

    CWIN32MIDI_API int CDECL CWin32Midi_Init(CWin32Midi_EventCallback callback);
    CWIN32MIDI_API int CDECL CWin32Midi_Shutdown();

    typedef struct {
        CWin32Midi_DeviceID id;
        bool isInput; // else output
        const char *name;
    } CWin32Midi_DeviceInfo;
    CWIN32MIDI_API int CDECL CWin32Midi_EnumerateInputs(CWin32Midi_DeviceInfo **infos, int *count);

    typedef enum {
        CWin32Midi_InputMode_Callback,
        CWin32Midi_InputMode_Queue
    } CWin32Midi_InputMode;
    CWIN32MIDI_API int CDECL CWin32Midi_OpenInput(CWin32Midi_DeviceID id, void *userData, CWin32Midi_InputMode inputMode, CWin32Midi_Device *outHandle);
    CWIN32MIDI_API int CDECL CWin32Midi_CloseInput(CWin32Midi_Device handle);

    CWIN32MIDI_API int CDECL CWin32Midi_Start(CWin32Midi_Device handle);
    CWIN32MIDI_API int CDECL CWin32Midi_Stop(CWin32Midi_Device handle);

    typedef struct {
        long relTime; // from last read time (or start)
        union {
            unsigned char bytes[4];
            unsigned int uint32;
        } data;
    } CWin32Midi_MidiMsg;
    CWIN32MIDI_API int CDECL CWin32Midi_ReadInput(CWin32Midi_Device handle, CWin32Midi_MidiMsg *dest, int destSize, int *numRead);

#ifdef __cplusplus
}
#endif
