#include <stdio.h>

#include "../../../source/CWin32MIDI.h"

extern "C" int midiEventHandler(CWin32Midi_Event *event, CWin32Midi_Device device, void *userData)
{
    event->handled = true;
    switch (event->eventType) {
    case CWin32Midi_EventType_Log:
        printf("CWin32Midi>> %s\n", event->logEvent.message);
        break;
    case CWin32Midi_EventType_Data:
        printf("data: %08X\n", event->dataEvent.uint32);
        break;
    default:
        event->handled = false;
    }
    return 0;
}

#define MSG_BUFFER_SIZE 2048
CWin32Midi_MidiMsg msgBuffer[MSG_BUFFER_SIZE];

int main()
{
    CWin32Midi_Init(&midiEventHandler);

    CWin32Midi_DeviceInfo *inputInfos;
    int numInputs;

    if (CWin32Midi_EnumerateInputs(&inputInfos, &numInputs) != 0) {
        printf("error enumerating inputs\n");
        return -1;
    }

    for (int i = 0; i < numInputs; i++) {
        auto info = inputInfos[i];
        printf("%d: [%s]\n", i, info.name);
    }

    CWin32Midi_Device input;
    if (CWin32Midi_OpenInput(inputInfos[0].id, nullptr, CWin32Midi_InputMode_Queue, &input) == 0) {

        if (CWin32Midi_Start(input) == 0) {
            printf("sleeping 10 seconds...\n");

            int interval = 100;
            for (int i = 0; i < 10000/interval; i++) {
                printf("== outer interval\n");
                Sleep(interval);
                int numRead;
                do {
                    printf("--\n"); // inner read loop
                    CWin32Midi_ReadInput(input, msgBuffer, MSG_BUFFER_SIZE, &numRead);
                    for (int j = 0; j < numRead; j++) {
                        bool out_of_bounds = (msgBuffer[j].relTime < 0 || msgBuffer[j].relTime >= interval);
                        printf("[%d] data: %08X %s\n", 
                            msgBuffer[j].relTime, 
                            msgBuffer[j].data.uint32, 
                            out_of_bounds ? "<<OOB>>" : "");
                    }
                } while (numRead >= MSG_BUFFER_SIZE); // we might have missed something, loop again to be sure (reference time won't reset until we do a read with some buffer remaining)
            }

            CWin32Midi_Stop(input);
        }

        CWin32Midi_CloseInput(input);
    }
    CWin32Midi_Shutdown();
    return 0;
}

