add_executable(hello hello/main.cpp)
target_link_libraries(hello PRIVATE Vortex::vortex)

add_executable(window window/main.cpp)
target_link_libraries(window PRIVATE Vortex::vortex)

add_executable(triangle triangle/main.cpp)
target_link_libraries(triangle PRIVATE Vortex::vortex)

find_program(VORTEX_GLSLC glslc REQUIRED)
set(_shader_out_dir ${CMAKE_CURRENT_BINARY_DIR}/triangle_shaders)
file(MAKE_DIRECTORY ${_shader_out_dir})

set(_triangle_shaders
    ${CMAKE_CURRENT_SOURCE_DIR}/triangle/shaders/triangle.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/triangle/shaders/triangle.frag)

set(_spv_outputs "")
foreach (_shader ${_triangle_shaders})
    get_filename_component(_name ${_shader} NAME)
    set(_out ${_shader_out_dir}/${_name}.spv)
    add_custom_command(
        OUTPUT ${_out}
        COMMAND ${VORTEX_GLSLC} ${_shader} -o ${_out}
        DEPENDS ${_shader}
        COMMENT "Compiling shader ${_name}"
        VERBATIM)
    list(APPEND _spv_outputs ${_out})
endforeach ()

add_custom_target(triangle_shaders DEPENDS ${_spv_outputs})
add_dependencies(triangle triangle_shaders)
target_compile_definitions(triangle PRIVATE VORTEX_TRIANGLE_SHADER_DIR="${_shader_out_dir}")

add_executable(sprites sprites/main.cpp)
target_link_libraries(sprites PRIVATE Vortex::vortex)

add_executable(sprite2d sprite2d/main.cpp)
target_link_libraries(sprite2d PRIVATE Vortex::vortex)

add_executable(atlas atlas/main.cpp)
target_link_libraries(atlas PRIVATE Vortex::vortex)

add_executable(mesh2d mesh2d/main.cpp)
target_link_libraries(mesh2d PRIVATE Vortex::vortex)

add_executable(ecs ecs/main.cpp)
target_link_libraries(ecs PRIVATE Vortex::vortex)

# Target name is not "game": examples/hotreload already claims that for its
# hot-swappable module library.
add_executable(game_example game/main.cpp)
target_link_libraries(game_example PRIVATE Vortex::vortex)

add_executable(mesh3d mesh3d/main.cpp)
target_link_libraries(mesh3d PRIVATE Vortex::vortex)

add_executable(assets assets/main.cpp)
target_link_libraries(assets PRIVATE Vortex::vortex)
target_compile_definitions(assets PRIVATE VORTEX_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

add_executable(ui ui/main.cpp)
target_link_libraries(ui PRIVATE Vortex::vortex)

add_executable(physics physics/main.cpp)
target_link_libraries(physics PRIVATE Vortex::vortex)

add_executable(physics_app physics_app/main.cpp)
target_link_libraries(physics_app PRIVATE Vortex::vortex)

add_executable(particles particles/main.cpp)
target_link_libraries(particles PRIVATE Vortex::vortex)

add_executable(tilemap tilemap/main.cpp)
target_link_libraries(tilemap PRIVATE Vortex::vortex)

# Reads its level straight out of the source tree, so editing level.tmj in Tiled and
# re-running needs no build step at all.
add_executable(tiled tiled/main.cpp)
target_link_libraries(tiled PRIVATE Vortex::vortex)
target_compile_definitions(tiled PRIVATE VORTEX_TILED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tiled")

add_executable(input input/main.cpp)
target_link_libraries(input PRIVATE Vortex::vortex)

add_executable(scenes scenes/main.cpp)
target_link_libraries(scenes PRIVATE Vortex::vortex)

add_executable(benchmark benchmark/main.cpp)
target_link_libraries(benchmark PRIVATE Vortex::vortex)

set(_mt_shader_out ${CMAKE_CURRENT_BINARY_DIR}/mt_shaders)
file(MAKE_DIRECTORY ${_mt_shader_out})
set(_mt_spv "")
foreach (_sh mt.vert mt.frag)
    add_custom_command(
        OUTPUT ${_mt_shader_out}/${_sh}.spv
        COMMAND ${VORTEX_GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/mt_record/shaders/${_sh}
                -o ${_mt_shader_out}/${_sh}.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mt_record/shaders/${_sh}
        COMMENT "Compiling shader ${_sh}"
        VERBATIM)
    list(APPEND _mt_spv ${_mt_shader_out}/${_sh}.spv)
endforeach ()
add_custom_target(mt_shaders DEPENDS ${_mt_spv})

add_executable(mt_record mt_record/main.cpp)
add_dependencies(mt_record mt_shaders)
target_link_libraries(mt_record PRIVATE Vortex::vortex)
target_compile_definitions(mt_record PRIVATE VORTEX_MT_SHADER_DIR="${_mt_shader_out}")

set(_rg_shader_out ${CMAKE_CURRENT_BINARY_DIR}/rendergraph_shaders)
file(MAKE_DIRECTORY ${_rg_shader_out})
set(_rg_spv "")
foreach (_sh post.vert post.frag)
    add_custom_command(
        OUTPUT ${_rg_shader_out}/${_sh}.spv
        COMMAND ${VORTEX_GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/rendergraph/shaders/${_sh}
                -o ${_rg_shader_out}/${_sh}.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rendergraph/shaders/${_sh}
        COMMENT "Compiling shader ${_sh}"
        VERBATIM)
    list(APPEND _rg_spv ${_rg_shader_out}/${_sh}.spv)
endforeach ()
add_custom_target(rendergraph_shaders DEPENDS ${_rg_spv})

add_executable(rendergraph rendergraph/main.cpp)
add_dependencies(rendergraph rendergraph_shaders)
target_link_libraries(rendergraph PRIVATE Vortex::vortex)
target_compile_definitions(rendergraph PRIVATE VORTEX_RG_SHADER_DIR="${_rg_shader_out}")

add_executable(debug_draw debug/main.cpp)
target_link_libraries(debug_draw PRIVATE Vortex::vortex)

add_executable(inspector inspector/main.cpp)
target_link_libraries(inspector PRIVATE Vortex::vortex)

add_library(game MODULE hotreload/game.cpp)
set_target_properties(game PROPERTIES PREFIX "lib")

add_executable(hotreload hotreload/host.cpp)
target_link_libraries(hotreload PRIVATE Vortex::vortex)
target_compile_definitions(hotreload PRIVATE
    VORTEX_GAME_LIB="$<TARGET_FILE:game>")
add_dependencies(hotreload game)

add_executable(bloom2d bloom2d/main.cpp)
target_link_libraries(bloom2d PRIVATE Vortex::vortex)

add_executable(skinned skinned/main.cpp)
target_link_libraries(skinned PRIVATE Vortex::vortex)

# Headless: exercises the animation state machine + root motion with no window or GPU,
# so it doubles as a CI check for the anim module.
add_executable(anim_state anim_state/main.cpp)
target_link_libraries(anim_state PRIVATE Vortex::vortex)

# Headless self-checking examples — same idea as anim_state: they demonstrate the API and
# fail loudly in CI if the promise breaks.
add_executable(async_tasks async_tasks/main.cpp)
target_link_libraries(async_tasks PRIVATE Vortex::vortex)

add_executable(camera_rig camera_rig/main.cpp)
target_link_libraries(camera_rig PRIVATE Vortex::vortex)

add_executable(diagnostics diagnostics/main.cpp)
target_link_libraries(diagnostics PRIVATE Vortex::vortex)

# A scripted audio tour: needs speakers, not a window; exits cleanly when there is no
# audio device, so it is CI-safe.
add_executable(audio_demo audio_demo/main.cpp)
target_link_libraries(audio_demo PRIVATE Vortex::vortex)

# The pipelined game loop (AppConfig::threadedSimulation). Prints a simulation checksum and
# the sim/render overlap; run it with VORTEX_THREADED=0 and =1 and compare.
add_executable(threaded threaded/main.cpp)
target_link_libraries(threaded PRIVATE Vortex::vortex)

add_executable(ui_anim ui_anim/main.cpp)
target_link_libraries(ui_anim PRIVATE Vortex::vortex)

add_executable(camera3d camera3d/main.cpp)
target_link_libraries(camera3d PRIVATE Vortex::vortex)

add_executable(app_empty app_empty/main.cpp)
target_link_libraries(app_empty PRIVATE Vortex::vortex)

add_executable(app_plugin app_plugin/main.cpp)
target_link_libraries(app_plugin PRIVATE Vortex::vortex)

add_executable(app_settings app_settings/main.cpp)
target_link_libraries(app_settings PRIVATE Vortex::vortex)

add_executable(assets_demo assets_demo/main.cpp)
target_link_libraries(assets_demo PRIVATE Vortex::vortex)

# Bake two files into the executable. From here on they are loadable as `embedded://meme1.png` and
# `embedded://fallback.pal` — note the second is embedded under a name that is not its filename.
vortex_embed_asset(assets_demo
    NAMESPACE demo
    FILES     ${CMAKE_SOURCE_DIR}/assets/meme1.png
              assets_demo/fallback.pal
    NAMES     meme1.png
              fallback.pal)
