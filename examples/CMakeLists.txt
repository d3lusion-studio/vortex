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

# A loading screen + main menu + play state on the App loop, AssetManager and UI.
# Run with VORTEX_GAMEMENU_CHECK=1 for a headless, self-verifying CI pass.
add_executable(game_menu game_menu/main.cpp)
target_link_libraries(game_menu PRIVATE Vortex::vortex)

add_executable(physics physics/main.cpp)
target_link_libraries(physics PRIVATE Vortex::vortex)

# Breakout: the full 2D stack (App + physics + rendering) with the ECS command
# buffer and observer/event facilities driving destruction and scoring. Run with
# VORTEX_BREAKOUT_CHECK=1 for a headless, self-verifying CI pass.
add_executable(breakout breakout/main.cpp)
target_link_libraries(breakout PRIVATE Vortex::vortex)

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

# Headless: exercises the ECS command buffer, observers/events, entity disabling,
# iter-combinations, delayed commands and callbacks with no window or GPU.
add_executable(ecs_features ecs_features/main.cpp)
target_link_libraries(ecs_features PRIVATE Vortex::vortex)

# Headless: 2D bounding volumes, intersection/raycast tests, primitive measures,
# uniform sampling and a custom primitive — pure math, no window or GPU.
add_executable(math_primitives math_primitives/main.cpp)
target_link_libraries(math_primitives PRIVATE Vortex::vortex)

# Headless: pointer picking — hover/click/layering/drag/drop and a custom hit-test
# backend, driven by synthetic pointer input through the ECS event system.
add_executable(picking picking/main.cpp)
target_link_libraries(picking PRIVATE Vortex::vortex)

# Headless: 3D mesh picking — ray/AABB/sphere/triangle math, occlusion, transforms,
# precise triangle proxies, and the camera-ray backend feeding the PickingSystem.
add_executable(mesh_picking mesh_picking/main.cpp)
target_link_libraries(mesh_picking PRIVATE Vortex::vortex)

# A small 3D grid game on the mesh/shadow/HDR path. The rules are GPU-free, so
# VORTEX_ALIENCAKE_CHECK=1 self-checks the gameplay with no window.
add_executable(alien_cake_addict alien_cake_addict/main.cpp)
target_link_libraries(alien_cake_addict PRIVATE Vortex::vortex)

# World-space 3D debug gizmos (grid/axes/box/sphere). Run VORTEX_GIZMOS3D_CHECK=1
# for a headless CI pass that reads the rendered axes back and checks the colours.
add_executable(gizmos3d gizmos3d/main.cpp)
target_link_libraries(gizmos3d PRIVATE Vortex::vortex)

# Headless: the interactive translate gizmo's hit-testing and axis-locked dragging.
add_executable(transform_gizmo transform_gizmo/main.cpp)
target_link_libraries(transform_gizmo PRIVATE Vortex::vortex)

# The picking debug overlay (pick ray, hit marker, hovered bounds). Run
# VORTEX_PICKINGDEBUG_CHECK=1 for a headless CI pass that also reads it back.
add_executable(picking_debug picking_debug/main.cpp)
target_link_libraries(picking_debug PRIVATE Vortex::vortex)

# Shader defs + specialized pipelines. effect.frag is compiled once per feature
# combination (WAVE/TINT) at build time; the runtime selects and caches variants via
# renderer::PipelineCache. Run with VORTEX_SHADERDEFS_CHECK=1 for a headless CI pass.
add_executable(shader_defs shader_defs/main.cpp)
target_link_libraries(shader_defs PRIVATE Vortex::vortex)

set(_sd_src ${CMAKE_CURRENT_SOURCE_DIR}/shader_defs/shaders)
set(_sd_out ${CMAKE_CURRENT_BINARY_DIR}/shader_defs_shaders)
file(MAKE_DIRECTORY ${_sd_out})
set(_sd_spv "")

add_custom_command(
    OUTPUT ${_sd_out}/fullscreen.vert.spv
    COMMAND ${VORTEX_GLSLC} ${_sd_src}/fullscreen.vert -o ${_sd_out}/fullscreen.vert.spv
    DEPENDS ${_sd_src}/fullscreen.vert
    COMMENT "Compiling shader_defs fullscreen.vert"
    VERBATIM)
list(APPEND _sd_spv ${_sd_out}/fullscreen.vert.spv)

# effect.frag, once per def combination: mask bit 0 = WAVE, bit 1 = TINT.
set(_sd_defs_0 "")
set(_sd_defs_1 -DWAVE)
set(_sd_defs_2 -DTINT)
set(_sd_defs_3 -DWAVE -DTINT)
foreach (_mask 0 1 2 3)
    add_custom_command(
        OUTPUT ${_sd_out}/effect.${_mask}.spv
        COMMAND ${VORTEX_GLSLC} ${_sd_defs_${_mask}} ${_sd_src}/effect.frag -o ${_sd_out}/effect.${_mask}.spv
        DEPENDS ${_sd_src}/effect.frag
        COMMENT "Compiling shader_defs effect.frag variant ${_mask}"
        VERBATIM)
    list(APPEND _sd_spv ${_sd_out}/effect.${_mask}.spv)
endforeach ()

add_custom_target(shader_defs_shaders DEPENDS ${_sd_spv})
add_dependencies(shader_defs shader_defs_shaders)
target_compile_definitions(shader_defs PRIVATE VORTEX_SHADERDEFS_DIR="${_sd_out}")

# A time-driven full-screen shader. Run VORTEX_ANIMATED_CHECK=1 for a headless CI
# pass that renders two times to an offscreen target and reads them back.
add_executable(animated animated/main.cpp)
target_link_libraries(animated PRIVATE Vortex::vortex)

set(_anim_src ${CMAKE_CURRENT_SOURCE_DIR}/animated/shaders)
set(_anim_out ${CMAKE_CURRENT_BINARY_DIR}/animated_shaders)
file(MAKE_DIRECTORY ${_anim_out})
set(_anim_spv "")
foreach (_stage animated.vert animated.frag)
    add_custom_command(
        OUTPUT ${_anim_out}/${_stage}.spv
        COMMAND ${VORTEX_GLSLC} ${_anim_src}/${_stage} -o ${_anim_out}/${_stage}.spv
        DEPENDS ${_anim_src}/${_stage}
        COMMENT "Compiling animated ${_stage}"
        VERBATIM)
    list(APPEND _anim_spv ${_anim_out}/${_stage}.spv)
endforeach ()
add_custom_target(animated_shaders DEPENDS ${_anim_spv})
add_dependencies(animated animated_shaders)
target_compile_definitions(animated PRIVATE VORTEX_ANIMATED_SHADER_DIR="${_anim_out}")

# Many quads in one draw call via a per-instance vertex buffer. Run
# VORTEX_INSTANCING_CHECK=1 for a headless CI pass that counts the instances by
# reading the rendered image back.
add_executable(instancing instancing/main.cpp)
target_link_libraries(instancing PRIVATE Vortex::vortex)

set(_inst_src ${CMAKE_CURRENT_SOURCE_DIR}/instancing/shaders)
set(_inst_out ${CMAKE_CURRENT_BINARY_DIR}/instancing_shaders)
file(MAKE_DIRECTORY ${_inst_out})
set(_inst_spv "")
foreach (_stage instancing.vert instancing.frag)
    add_custom_command(
        OUTPUT ${_inst_out}/${_stage}.spv
        COMMAND ${VORTEX_GLSLC} ${_inst_src}/${_stage} -o ${_inst_out}/${_stage}.spv
        DEPENDS ${_inst_src}/${_stage}
        COMMENT "Compiling instancing ${_stage}"
        VERBATIM)
    list(APPEND _inst_spv ${_inst_out}/${_stage}.spv)
endforeach ()
add_custom_target(instancing_shaders DEPENDS ${_inst_spv})
add_dependencies(instancing instancing_shaders)
target_compile_definitions(instancing PRIVATE VORTEX_INSTANCING_SHADER_DIR="${_inst_out}")

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
