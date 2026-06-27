vortex_add_module(ecs
    SOURCES
        src/systems.cpp
    PUBLIC_DEPS
        Vortex::core
        Vortex::rhi
        Vortex::renderer
)
