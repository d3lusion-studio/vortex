include(VortexShaders)

vortex_embed_shader(imgui.vert imgui_vert_spv _imgui_vert_header)
vortex_embed_shader(imgui.frag imgui_frag_spv _imgui_frag_header)

vortex_add_module(debug
    SOURCES
        src/debug_draw.cpp
        src/imgui_layer.cpp
        src/inspector.cpp
        src/perf_overlay.cpp
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

target_include_directories(vortex_debug PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
