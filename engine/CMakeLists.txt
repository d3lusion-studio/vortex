add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(rhi)
add_subdirectory(jobs)
add_subdirectory(renderer)
add_subdirectory(ecs)
add_subdirectory(asset)
add_subdirectory(text)
add_subdirectory(ui)
add_subdirectory(audio)
add_subdirectory(physics)
add_subdirectory(debug)

add_library(vortex_all INTERFACE)
add_library(Vortex::vortex ALIAS vortex_all)
set_target_properties(vortex_all PROPERTIES EXPORT_NAME vortex)

target_link_libraries(vortex_all INTERFACE
    Vortex::core
    Vortex::platform
    Vortex::rhi
    Vortex::jobs
    Vortex::renderer
    Vortex::ecs
    Vortex::asset
    Vortex::text
    Vortex::ui
    Vortex::audio
    Vortex::physics
    Vortex::debug
)

if (VORTEX_INSTALL)
    install(TARGETS vortex_all EXPORT VortexTargets)
endif ()