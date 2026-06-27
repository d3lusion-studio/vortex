find_package(Threads REQUIRED)

vortex_add_module(audio
    SOURCES
        src/miniaudio/ma_audio.cpp
    PUBLIC_DEPS
        Vortex::core
)

target_link_libraries(vortex_audio PRIVATE $<BUILD_INTERFACE:vortex_miniaudio> Threads::Threads ${CMAKE_DL_LIBS})
set_source_files_properties(src/miniaudio/ma_audio.cpp PROPERTIES COMPILE_OPTIONS "-w")
