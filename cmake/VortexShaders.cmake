# Shader compilation, shared by every module that ships shaders.
#
# One GLSL source produces two binaries, because the two backends cannot eat the same thing:
#
#   Vulkan  <- glslc               -> SPIR-V
#   WebGPU  <- glslc + naga        -> WGSL   (browsers accept no other shader language)
#
# The WebGPU variant is compiled with VORTEX_NO_PUSH_CONSTANTS, which swaps every
# `layout(push_constant)` block for a uniform buffer in set 3 — WebGPU has no push constants,
# and the RHI emulates them with a ring buffer bound at that set.
#
# Both end up in one generated header, so call sites never branch on the backend.

include_guard(GLOBAL)

find_program(VORTEX_GLSLC glslc REQUIRED)

if (VORTEX_RHI_WITH_WEBGPU)
    find_program(VORTEX_NAGA naga HINTS $ENV{HOME}/.cargo/bin)
    if (NOT VORTEX_NAGA)
        message(FATAL_ERROR
            "naga not found, but the WebGPU backend needs WGSL shaders.\n"
            "  Install it:  cargo install naga-cli\n"
            "  Or build without WebGPU:  -DVORTEX_RHI_WITH_WEBGPU=OFF")
    endif ()
endif ()

function(vortex_embed_shader stage symbol out_header)
    set(_src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${stage})
    set(_gen ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${_gen})

    set(_spv ${_gen}/${stage}.spv)
    set(_hdr ${_gen}/${symbol}.h)

    add_custom_command(
        OUTPUT ${_spv}
        COMMAND ${VORTEX_GLSLC} ${_src} -o ${_spv}
        DEPENDS ${_src}
        COMMENT "Compiling shader ${stage} (SPIR-V)"
        VERBATIM)

    set(_wgsl "")
    set(_embed_deps ${_spv})

    if (VORTEX_NAGA)
        # A second SPIR-V, compiled without push constants, purely as naga's input.
        set(_web_spv ${_gen}/${stage}.web.spv)
        set(_wgsl    ${_gen}/${stage}.wgsl)

        add_custom_command(
            OUTPUT ${_web_spv}
            COMMAND ${VORTEX_GLSLC} -DVORTEX_NO_PUSH_CONSTANTS ${_src} -o ${_web_spv}
            DEPENDS ${_src}
            COMMENT "Compiling shader ${stage} (SPIR-V, no push constants)"
            VERBATIM)

        add_custom_command(
            OUTPUT ${_wgsl}
            COMMAND ${VORTEX_NAGA} ${_web_spv} ${_wgsl}
            DEPENDS ${_web_spv}
            COMMENT "Transpiling shader ${stage} (WGSL)"
            VERBATIM)

        list(APPEND _embed_deps ${_wgsl})
    endif ()

    add_custom_command(
        OUTPUT ${_hdr}
        COMMAND ${CMAKE_COMMAND}
                -DSPV_INPUT=${_spv} -DWGSL_INPUT=${_wgsl}
                -DOUTPUT=${_hdr} -DSYMBOL=${symbol}
                -P ${CMAKE_SOURCE_DIR}/cmake/EmbedShader.cmake
        DEPENDS ${_embed_deps} ${CMAKE_SOURCE_DIR}/cmake/EmbedShader.cmake
        COMMENT "Embedding shader ${symbol}"
        VERBATIM)

    set(${out_header} ${_hdr} PARENT_SCOPE)
endfunction()
