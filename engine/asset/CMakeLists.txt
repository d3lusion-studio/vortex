vortex_add_module(asset
    SOURCES
        src/asset_manager.cpp
        src/image.cpp
        src/cooked_texture.cpp
        src/http.cpp
        src/tiled.cpp
        src/gltf.cpp
    PUBLIC_DEPS
        Vortex::core
        Vortex::rhi
        # An importer produces renderer types — a Tiled map *is* a renderer::Tilemap, a
        # glTF *is* a renderer::MeshData plus an anim::Skeleton — so these headers travel
        # with the module.
        Vortex::renderer
        Vortex::anim
    PRIVATE_DEPS
        Vortex::platform
)

target_link_libraries(vortex_asset PRIVATE $<BUILD_INTERFACE:vortex_stb>)

# Optional. Without zlib the Tiled importer still reads CSV and uncompressed base64
# layers, and a compressed map fails with a message naming the dropdown to change.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(vortex_asset PRIVATE ZLIB::ZLIB)
    target_compile_definitions(vortex_asset PRIVATE VORTEX_TILED_ZLIB)
else()
    message(STATUS "zlib not found: Tiled maps must use CSV or uncompressed base64 layers")
endif()

# Optional, and PRIVATE: libcurl stops at src/http.cpp. Nothing outside this module includes a
# curl header, so replacing it later touches one file.
#
# Without it, `http://` and `https://` assets fail with a message that says so — rather than
# failing the way a dead server would, which is the confusion worth paying a find_package to avoid.
find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(vortex_asset PRIVATE CURL::libcurl)
    target_compile_definitions(vortex_asset PRIVATE VORTEX_ASSET_HTTP)
    message(STATUS "libcurl ${CURL_VERSION_STRING}: web assets enabled")
else()
    message(STATUS "libcurl not found: http(s):// assets will fail to load")
endif()

set_source_files_properties(src/image.cpp PROPERTIES COMPILE_OPTIONS "-w")
