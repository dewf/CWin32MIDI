cmake_minimum_required(VERSION 3.30)
project(CWin32MIDI)

set(CMAKE_CXX_STANDARD 20)

add_library(CWin32MIDI SHARED
        source/dllmain.cpp
        source/CWin32MIDI.cpp
        source/unicodestuff.cpp
)

target_link_libraries(CWin32MIDI PRIVATE Winmm.lib)

add_compile_definitions(CWIN32MIDI_EXPORTS)
