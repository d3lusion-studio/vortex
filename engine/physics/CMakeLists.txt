vortex_add_module(physics
    SOURCES
        src/box2d/physics_world.cpp
    PUBLIC_DEPS
        Vortex::core
        Vortex::ecs
)

target_link_libraries(vortex_physics PRIVATE box2d)
