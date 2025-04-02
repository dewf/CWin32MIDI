#include "header.h"
#include "CWin32MIDI.h"

#include <mmsystem.h>
#include <atomic>
#include <algorithm>

#include "unicodestuff.h"

#include "../deps/readerwriterqueue/readerwriterqueue.h"
using namespace moodycamel;

// Ensure 'min' and 'max' macros don't interfere with std::min/max
#ifdef min
    #undef min
#endif
#ifdef max
    #undef max
#endif

struct _CWin32Midi_DeviceID {
    bool isInput;
    int deviceIndex;
    std::string name;
    _CWin32Midi_DeviceID(bool isInput, int deviceIndex, const std::string &name) {
        this->isInput = isInput;
        this->deviceIndex = deviceIndex;
        this->name = name;
    }
};

struct MidiMsgInternal {
    DWORD absTime;          // from start
    unsigned int data;
};

struct _CWin32Midi_Device {
    CWin32Midi_DeviceID id;
    HMIDIIN hMidiIn;
    void *userData;
    CWin32Midi_InputMode inputMode;
    bool started = false;
    //
    ReaderWriterQueue<MidiMsgInternal> queue;

    // for time offset calculation
    DWORD startTime;
    std::atomic<DWORD> lastReadTime; // referenced by both threads

    _CWin32Midi_Device() : queue(1024) {}
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
            MidiMsgInternal msg {};
            msg.absTime = static_cast<DWORD>(dev->startTime + dwParam2);
            msg.data = static_cast<unsigned int>(dwParam1); // should this be longer? DWORD_PTR is 64-bit, no?
            dev->queue.try_enqueue(msg);
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
        handle->startTime = timeGetTime();
        handle->lastReadTime = handle->startTime;
        //
        midiInStart(handle->hMidiIn);
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
    MidiMsgInternal queueMsg {};
    int read = 0;
    while (destSize > 0 && handle->queue.try_dequeue(queueMsg)) {
        CWin32Midi_MidiMsg outMsg;
        outMsg.relTime = std::max(0L, static_cast<long>(queueMsg.absTime) - static_cast<long>(handle->lastReadTime));
        outMsg.data.uint32 = queueMsg.data;
        *dest++ = outMsg;
        destSize--;
        read++;
    }
    // race condition here: a new MIDI event could be added before we change lastRefTime
    // but it should be OK - its relative time would be a small negative number, which will be clamped during reading (see .relTime assignment above)
    if (destSize > 0) {
        // if there's still remaning buffer, no more need to be read,
        // and it's safe to reset the ref time
        // (otherwise the client needs to call this function again to be sure it didn't miss anything because of a full buffer)
        handle->lastReadTime = timeGetTime();
    }
    *numRead = read;
    return 0;
}
