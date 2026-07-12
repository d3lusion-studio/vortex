vortex_add_module(app
    SOURCES
        src/app.cpp
    PUBLIC_DEPS
        # All of these are reachable through App's accessors, so a game that links
        # only Vortex::app still gets their headers and symbols.
        Vortex::core
        Vortex::ecs
        Vortex::renderer
        Vortex::rhi
        Vortex::platform
        Vortex::asset
        Vortex::jobs
)
