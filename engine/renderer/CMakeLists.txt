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

vortex_embed_shader(sprite.vert sprite_vert_spv _vert_header)
vortex_embed_shader(sprite.frag sprite_frag_spv _frag_header)

vortex_add_module(renderer
    SOURCES
        src/sprite_batch.cpp
        src/render_graph.cpp
        ${_vert_header}
        ${_frag_header}
    PUBLIC_DEPS
        Vortex::core
        Vortex::rhi
)

target_include_directories(vortex_renderer PRIVATE ${_gen_dir})
