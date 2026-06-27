vortex_add_module(text
    SOURCES
        src/font.cpp
        src/text_renderer.cpp
    PUBLIC_DEPS
        Vortex::core
        Vortex::rhi
        Vortex::renderer
    PRIVATE_DEPS
        Vortex::platform
)

target_link_libraries(vortex_text PRIVATE $<BUILD_INTERFACE:vortex_stb>)

set_source_files_properties(src/font.cpp PROPERTIES COMPILE_OPTIONS "-w")
