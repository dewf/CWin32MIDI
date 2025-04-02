#include "header.h"
#include "CWin32MIDI.h"

#include <mmsystem.h>

#include "unicodestuff.h"

#include "..\deps\readerwriterqueue\readerwriterqueue.h"

using namespace moodycamel;

struct _CWin32Midi_DeviceID {
    bool isInput;
    int deviceIndex;
    std::string name;
    _CWin32Midi_DeviceID(bool isInput, int deviceIndex, std::string name) {
        this->isInput = isInput;
        this->deviceIndex = deviceIndex;
        this->name = name;
    }
};

struct _CWin32Midi_Device {
    CWin32Midi_DeviceID id;
    HMIDIIN hMidiIn;
    void *userData;
    CWin32Midi_InputMode inputMode;
    bool started = false;
    //
    ReaderWriterQueue<CWin32Midi_MidiMsg> q;
    DWORD startTime, lastRefTime; // for time offset calculation
    
    _CWin32Midi_Device() : q(1024) {}
};

static CWin32Midi_EventCallback apiClientCallback;

//============ logging stuff =================================================

void logMessage(const char *message) {
    CWin32Midi_Event event;
    event.eventType = CWin32Midi_EventType_Log;
    // .handled doesn't matter
    event.logEvent.message = message;
    apiClientCallback(&event, nullptr, nullptr);
}

#define VSPRINTF_BUFFER_LEN 10*1024
char formatBuffer[VSPRINTF_BUFFER_LEN];
void logFormat(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsprintf_s<VSPRINTF_BUFFER_LEN>(formatBuffer, format, args);
    va_end(args);
    //
    logMessage(formatBuffer);
}

#define DEV_FORMAT_BUFFER_LEN 1024
char devFormat[DEV_FORMAT_BUFFER_LEN];
void logFormatDev(CWin32Midi_Device d, const char *format, ...) {
    sprintf_s<DEV_FORMAT_BUFFER_LEN>(devFormat, "[%d:%s] %s", d->id->deviceIndex, d->id->name.c_str(), format);
    //
    va_list args;
    va_start(args, format);
    vsprintf_s<VSPRINTF_BUFFER_LEN>(formatBuffer, devFormat, args);
    va_end(args);
    //
    logMessage(formatBuffer);
}

//============================================================================

CWIN32MIDI_API int CDECL CWin32Midi_Init(CWin32Midi_EventCallback callback)
{
    apiClientCallback = callback;
    logMessage("hello from CWin32Midi_Init");
    return 0;
}

CWIN32MIDI_API int CDECL CWin32Midi_Shutdown()
{
    logMessage("goodbye from CWin32Midi_Shutdown");
    apiClientCallback = nullptr;
    return 0;
}

CWIN32MIDI_API int CDECL CWin32Midi_EnumerateInputs(CWin32Midi_DeviceInfo **infos, int *count)
{
    auto numInputs = midiInGetNumDevs();
    auto ret = new CWin32Midi_DeviceInfo[numInputs];
    for (unsigned i = 0; i < numInputs; i++) {
        MIDIINCAPSW incaps;
        midiInGetDevCapsW(i, &incaps, sizeof(MIDIINCAPSW));
        auto utf8 = wstring_to_utf8(incaps.szPname);
        //
        ret[i].id = new _CWin32Midi_DeviceID(true, i, utf8);
        ret[i].isInput = true;
        ret[i].name = _strdup(utf8.c_str());
    }
    *infos = ret;
    *count = numInputs;
    return 0;
}


void CALLBACK myMidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) 
{
    auto dev = (CWin32Midi_Device)dwInstance;
    switch (wMsg) {
    case MIM_OPEN:
        logFormatDev(dev, "MIM_OPEN event");
        break;

    case MIM_CLOSE:
        logFormatDev(dev, "MIM_CLOSE event");
        break;

    case MIM_DATA:
    {
        if (dev->inputMode == CWin32Midi_InputMode_Callback) {
            CWin32Midi_Event event;
            event.eventType = CWin32Midi_EventType_Data;
            event.handled = false;
            event.dataEvent.uint32 = (UINT32)dwParam1;
            apiClientCallback(&event, dev, dev->userData);
        }
        else { // Queue
            CWin32Midi_MidiMsg msg;
            // refTime is the absolute time from which the dwParam2s are measured
            // lastRefTime is the absolute last-read time
            auto absTime = (DWORD)(dev->startTime + dwParam2);
            msg.relTime = absTime - dev->lastRefTime;
            msg.data.uint32 = (UINT32)dwParam1;
            dev->q.try_enqueue(msg);
        }
        break;
    }
    case MIM_LONGDATA:
        logFormatDev(dev, "(sysex data)");
        break;

    default:
        logFormatDev(dev, "unhandled message: %d", wMsg);
    }
}

CWIN32MIDI_API int CDECL CWin32Midi_OpenInput(CWin32Midi_DeviceID id, void *userData, CWin32Midi_InputMode inputMode, CWin32Midi_Device *outHandle)
{
    if (!id->isInput) {
        logFormat("device %d:%s is not an input device!", id->deviceIndex, id->name.c_str());
        return -1;
    }

    auto ret = new _CWin32Midi_Device();
    // set fields BEFORE the open, since it will give us a 'MIM_OPEN' callback immediately
    ret->userData = userData;
    ret->id = id;
    ret->inputMode = inputMode;
    ret->started = false;
    //
    if (midiInOpen(&ret->hMidiIn, id->deviceIndex, (DWORD_PTR)&myMidiInProc, (DWORD_PTR)ret, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
        *outHandle = ret;
        logFormatDev(ret, "input opened");
        return 0;
    }
    // else
    delete ret;
    logFormat("failed to open input device %d [%s]", id->deviceIndex, id->name.c_str());
    *outHandle = nullptr;
    return -1;
}

CWIN32MIDI_API int CDECL CWin32Midi_CloseInput(CWin32Midi_Device handle)
{
    if (handle) {
        midiInReset(handle->hMidiIn);
        logFormatDev(handle, "input closed");
        delete handle;
        return 0;
    }
    return -1;
}

CWIN32MIDI_API int CDECL CWin32Midi_Start(CWin32Midi_Device handle)
{
    if (handle && !handle->started) {
        midiInStart(handle->hMidiIn);
        //
        handle->startTime = timeGetTime();
        handle->lastRefTime = handle->startTime;
        //
        logFormatDev(handle, "midi in started");
        handle->started = true;
        return 0;
    }
    return -1;
}

CWIN32MIDI_API int CDECL CWin32Midi_Stop(CWin32Midi_Device handle)
{
    if (handle && handle->started) {
        midiInStop(handle->hMidiIn);
        logFormatDev(handle, "midi in stopped");
        handle->started = false;
        return 0;
    }
    return -1;
}

CWIN32MIDI_API int CDECL CWin32Midi_ReadInput(CWin32Midi_Device handle, CWin32Midi_MidiMsg *dest, int destSize, int *numRead)
{
    CWin32Midi_MidiMsg msg;
    int read = 0;
    while (destSize > 0 && handle->q.try_dequeue(msg)) {
        *dest++ = msg;
        destSize--;
        read++;
    }
    if (destSize > 0) {
        // if there's still remaning buffer, no more need to be read,
        // and it's safe to reset the ref time
        // (otherwise the client needs to call this function again to be sure it didn't miss anything because of a full buffer)
        handle->lastRefTime = timeGetTime();
    }
    *numRead = read;
    return 0;
}
