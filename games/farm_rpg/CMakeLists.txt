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

# A DEVELOPMENT fallback, not the way the game finds its art.
#
# This bakes an absolute path into this machine's source tree, which is right while you are
# working and wrong everywhere else — the binary would look for art on the box that built
# it. assets.cpp searches next to the executable first and only lands here last, so a dev
# build runs straight from the build directory and a copied one still works. See
# assetRoot().
target_compile_definitions(farm_rpg PRIVATE
    VORTEX_FARM_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/2d/FarmRPG")

# --- A folder you can zip and send -------------------------------------------------
#
#   cmake --build <build> --target farm_rpg_dist
#
# Produces <build>/dist/farm_rpg/ holding the executable and the art beside it, which is
# the layout assetRoot() looks for. Nothing in it points back at this source tree.
#
# The whole pack is copied rather than the few dozen files the game actually opens: a hand
# written list of those would be wrong the first time someone adds a crop, and wrong
# silently — the game would ship missing one texture. Cooking only what is reachable is the
# asset pipeline's job (tools/), not this target's.
set(_farm_dist ${CMAKE_BINARY_DIR}/dist/farm_rpg)

add_custom_target(farm_rpg_dist
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_farm_dist}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_farm_dist}/assets/2d
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:farm_rpg> ${_farm_dist}/
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets/2d/FarmRPG ${_farm_dist}/assets/2d/FarmRPG
    COMMAND ${CMAKE_COMMAND} -E echo "farm_rpg packaged into ${_farm_dist}"
    DEPENDS farm_rpg
    VERBATIM)
