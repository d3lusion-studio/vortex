# Farm RPG: a farming-sim game built on the engine, rather than an example of one
# corner of it. Run with VORTEX_FARM_CHECK=1 for a scripted self-check of the loop.
add_executable(farm_rpg
    src/main.cpp
    src/farm.cpp
    src/assets.cpp
    src/world.cpp
    src/player.cpp
    src/hud.cpp
    src/save.cpp)

target_link_libraries(farm_rpg PRIVATE Vortex::vortex)

# The art is read straight from the source tree: it is 5,000 files and the game only
# ever touches a few dozen, so copying it into the build directory would cost more
# than it buys.
target_compile_definitions(farm_rpg PRIVATE
    VORTEX_FARM_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/2d/FarmRPG")
