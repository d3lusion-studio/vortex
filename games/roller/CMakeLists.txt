# Roller: a small 3D game on the App loop's render3D path. Run with VORTEX_ROLLER_CHECK=1
# for a scripted self-check that plays a round hands-free and fails if it was not won.
add_executable(roller
    src/main.cpp
    src/game.cpp)

target_link_libraries(roller PRIVATE Vortex::vortex)
