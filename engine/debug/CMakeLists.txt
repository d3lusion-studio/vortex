find_program(VORTEX_GLSLC glslc REQUIRED)

set(_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${_gen_dir})

function(vortex_embed_shader stage symbol out_header)
    set(_src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${stage})
    set(_spv ${_gen_dir}/${stage}.spv)
    set(_hdr ${_gen_dir}/${symbol}.h)
    add_custom_command(
        OUTPUT ${_spv}
        COMMAND ${VORTEX_GLSLC} ${_src} -o ${_spv}
        DEPENDS ${_src}
        COMMENT "Compiling shader ${stage}"
        VERBATIM)
    add_custom_command(
        OUTPUT ${_hdr}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${_spv} -DOUTPUT=${_hdr} -DSYMBOL=${symbol}
                -P ${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake
        DEPENDS ${_spv} ${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake
        COMMENT "Embedding shader ${symbol}"
        VERBATIM)
    set(${out_header} ${_hdr} PARENT_SCOPE)
endfunction()

vortex_embed_shader(imgui.vert imgui_vert_spv _imgui_vert_header)
vortex_embed_shader(imgui.frag imgui_frag_spv _imgui_frag_header)

vortex_add_module(debug
    SOURCES
        src/debug_draw.cpp
        src/imgui_layer.cpp
        src/inspector.cpp
        ${_imgui_vert_header}
        ${_imgui_frag_header}
    PUBLIC_DEPS
        Vortex::core
        Vortex::rhi
        Vortex::renderer
        Vortex::text
        Vortex::ecs
    PRIVATE_DEPS
        $<BUILD_INTERFACE:vortex_imgui>
)

target_include_directories(vortex_debug PRIVATE ${_gen_dir})
