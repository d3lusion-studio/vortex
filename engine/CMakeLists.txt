add_subdirectory(core)
add_subdirectory(platform)

add_library(vortex_all INTERFACE)
add_library(Vortex::vortex ALIAS vortex_all)
set_target_properties(vortex_all PROPERTIES EXPORT_NAME vortex)

target_link_libraries(vortex_all INTERFACE
    Vortex::core
    Vortex::platform
)

if (VORTEX_INSTALL)
    install(TARGETS vortex_all EXPORT VortexTargets)
endif ()