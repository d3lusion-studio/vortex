vortex_add_module(asset
    SOURCES
        src/asset_manager.cpp
        src/image.cpp
        src/cooked_texture.cpp
    PUBLIC_DEPS
        Vortex::core
        Vortex::rhi
    PRIVATE_DEPS
        Vortex::platform
)

target_link_libraries(vortex_asset PRIVATE $<BUILD_INTERFACE:vortex_stb>)

set_source_files_properties(src/image.cpp PROPERTIES COMPILE_OPTIONS "-w")
