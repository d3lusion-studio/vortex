include(VortexShaders)

vortex_embed_shader(sprite.vert     sprite_vert_spv     _vert_header)
vortex_embed_shader(sprite.frag     sprite_frag_spv     _frag_header)
vortex_embed_shader(mesh2d.vert     mesh2d_vert_spv     _mesh2d_vert_header)
vortex_embed_shader(mesh2d.frag     mesh2d_frag_spv     _mesh2d_frag_header)
vortex_embed_shader(mesh.vert       mesh_vert_spv       _mesh_vert_header)
vortex_embed_shader(mesh.frag       mesh_frag_spv       _mesh_frag_header)
vortex_embed_shader(shadow.vert     shadow_vert_spv     _shadow_vert_header)
vortex_embed_shader(shadow.frag     shadow_frag_spv     _shadow_frag_header)
vortex_embed_shader(skybox.vert     skybox_vert_spv     _skybox_vert_header)
vortex_embed_shader(skybox.frag     skybox_frag_spv     _skybox_frag_header)
vortex_embed_shader(gbuffer.frag    gbuffer_frag_spv    _gbuffer_frag_header)
vortex_embed_shader(deferred.frag   deferred_frag_spv   _deferred_frag_header)
vortex_embed_shader(ssao.frag       ssao_frag_spv       _ssao_frag_header)
vortex_embed_shader(ssao_blur.frag  ssao_blur_frag_spv  _ssao_blur_frag_header)
vortex_embed_shader(motion_blur.frag motion_blur_frag_spv _motion_blur_frag_header)
vortex_embed_shader(volumetric.frag volumetric_frag_spv _volumetric_frag_header)
vortex_embed_shader(blit.frag       blit_frag_spv       _blit_frag_header)
vortex_embed_shader(decal.vert      decal_vert_spv      _decal_vert_header)
vortex_embed_shader(decal.frag      decal_frag_spv      _decal_frag_header)
vortex_embed_shader(fullscreen.vert fullscreen_vert_spv _fs_vert_header)
vortex_embed_shader(bright.frag     bright_frag_spv     _bright_header)
vortex_embed_shader(blur.frag       blur_frag_spv       _blur_header)
vortex_embed_shader(composite.frag  composite_frag_spv  _composite_header)
vortex_embed_shader(tonemap.frag    tonemap_frag_spv    _tonemap_header)
vortex_embed_shader(fxaa.frag       fxaa_frag_spv       _fxaa_header)
vortex_embed_shader(line3d.vert     line3d_vert_spv     _line3d_vert_header)
vortex_embed_shader(line3d.frag     line3d_frag_spv     _line3d_frag_header)

vortex_add_module(renderer
    SOURCES
        src/sprite_batch.cpp
        src/sprite_animation.cpp
        src/camera_controller.cpp
        src/atlas.cpp
        src/mesh2d.cpp
        src/particles.cpp
        src/tilemap.cpp
        src/render_graph.cpp
        src/mesh.cpp
        src/skinning.cpp
        src/post_process.cpp
        src/gizmos3d.cpp
        ${_vert_header}
        ${_frag_header}
        ${_mesh2d_vert_header}
        ${_mesh2d_frag_header}
        ${_mesh_vert_header}
        ${_mesh_frag_header}
        ${_shadow_vert_header}
        ${_shadow_frag_header}
        ${_skybox_vert_header}
        ${_skybox_frag_header}
        ${_gbuffer_frag_header}
        ${_deferred_frag_header}
        ${_ssao_frag_header}
        ${_ssao_blur_frag_header}
        ${_motion_blur_frag_header}
        ${_volumetric_frag_header}
        ${_blit_frag_header}
        ${_decal_vert_header}
        ${_decal_frag_header}
        ${_fs_vert_header}
        ${_bright_header}
        ${_blur_header}
        ${_composite_header}
        ${_tonemap_header}
        ${_fxaa_header}
        ${_line3d_vert_header}
        ${_line3d_frag_header}
    PUBLIC_DEPS
        Vortex::core
        Vortex::rhi
        Vortex::jobs
)

target_include_directories(vortex_renderer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
