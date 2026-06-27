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
