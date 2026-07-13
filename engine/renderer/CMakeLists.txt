include(VortexShaders)

vortex_embed_shader(sprite.vert     sprite_vert_spv     _vert_header)
vortex_embed_shader(sprite.frag     sprite_frag_spv     _frag_header)
vortex_embed_shader(mesh.vert       mesh_vert_spv       _mesh_vert_header)
vortex_embed_shader(mesh.frag       mesh_frag_spv       _mesh_frag_header)
vortex_embed_shader(shadow.vert     shadow_vert_spv     _shadow_vert_header)
vortex_embed_shader(shadow.frag     shadow_frag_spv     _shadow_frag_header)
vortex_embed_shader(fullscreen.vert fullscreen_vert_spv _fs_vert_header)
vortex_embed_shader(bright.frag     bright_frag_spv     _bright_header)
vortex_embed_shader(blur.frag       blur_frag_spv       _blur_header)
vortex_embed_shader(composite.frag  composite_frag_spv  _composite_header)
vortex_embed_shader(tonemap.frag    tonemap_frag_spv    _tonemap_header)
vortex_embed_shader(fxaa.frag       fxaa_frag_spv       _fxaa_header)

vortex_add_module(renderer
    SOURCES
        src/sprite_batch.cpp
        src/sprite_animation.cpp
        src/particles.cpp
        src/tilemap.cpp
        src/render_graph.cpp
        src/mesh.cpp
        src/post_process.cpp
        ${_vert_header}
        ${_frag_header}
        ${_mesh_vert_header}
        ${_mesh_frag_header}
        ${_shadow_vert_header}
        ${_shadow_frag_header}
        ${_fs_vert_header}
        ${_bright_header}
        ${_blur_header}
        ${_composite_header}
        ${_tonemap_header}
        ${_fxaa_header}
    PUBLIC_DEPS
        Vortex::core
        Vortex::rhi
        Vortex::jobs
)

target_include_directories(vortex_renderer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
