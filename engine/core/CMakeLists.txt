configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/vortex/core/version.hpp
        @ONLY)

vortex_add_module(core
        SOURCES
        src/log.cpp)

target_include_directories(vortex_core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# Enable asserts in non-optimised builds automatically.
target_compile_definitions(vortex_core PUBLIC
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:VORTEX_ENABLE_ASSERTS>)

if(VORTEX_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/vortex/core/version.hpp
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vortex/core)
endif()
