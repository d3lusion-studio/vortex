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

add_executable(ecs ecs/main.cpp)
target_link_libraries(ecs PRIVATE Vortex::vortex)

add_executable(assets assets/main.cpp)
target_link_libraries(assets PRIVATE Vortex::vortex)
target_compile_definitions(assets PRIVATE VORTEX_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

add_executable(ui ui/main.cpp)
target_link_libraries(ui PRIVATE Vortex::vortex)

add_executable(physics physics/main.cpp)
target_link_libraries(physics PRIVATE Vortex::vortex)

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
