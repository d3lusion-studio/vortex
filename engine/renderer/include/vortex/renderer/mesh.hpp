#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <vector>

namespace vortex::rhi {
class IGraphicsDevice;
class ICommandList;
}

namespace vortex::renderer {

struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

// A single directional light. Also drives the shadow map: the scene is rendered
// depth-only from an orthographic camera aimed along `direction`.
struct DirectionalLight {
    Vec3 direction{-0.4f, -1.0f, -0.5f};   // direction the light travels (world)
    Vec3 color{1.0f, 1.0f, 1.0f};
    f32  intensity = 1.0f;
    Vec3 ambient{1.0f, 1.0f, 1.0f};        // IBL intensity/tint (ambient from cubemaps)

    // Shadow frustum: an ortho box of half-extent `shadowExtent` centred on
    // `shadowTarget`, with the light placed `shadowDistance` back along -dir.
    Vec3 shadowTarget{0.0f, 0.0f, 0.0f};
    f32  shadowExtent   = 16.0f;
    f32  shadowDistance = 40.0f;
};

using MeshHandle = Handle<struct MeshTag>;

struct MeshInstance {
    MeshHandle mesh;
    Mat4       model = Mat4::identity();
    Vec4       color{1.0f, 1.0f, 1.0f, 1.0f};
    f32        metallic  = 0.0f;
    f32        roughness = 0.5f;
};

// Raw CPU mesh data produced by the primitive generators below.
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<u32>        indices;
};

[[nodiscard]] MeshData makeCube(f32 size = 1.0f);
[[nodiscard]] MeshData makePlane(f32 size = 1.0f);
[[nodiscard]] MeshData makeSphere(u32 rings = 16, u32 sectors = 24, f32 radius = 0.5f);

class MeshRenderer {
public:
    MeshRenderer(rhi::IGraphicsDevice& device, rhi::Format colorFormat, rhi::Format depthFormat);
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&)            = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    [[nodiscard]] MeshHandle createMesh(const MeshVertex* vertices, usize vertexCount,
                                        const u32* indices, usize indexCount);
    [[nodiscard]] MeshHandle createMesh(const MeshData& data) {
        return createMesh(data.vertices.data(), data.vertices.size(),
                          data.indices.data(), data.indices.size());
    }
    void destroyMesh(MeshHandle);

    void begin(const Camera& camera, const DirectionalLight& light);
    void drawMesh(MeshHandle, const Mat4& model, Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});
    void submit(const MeshInstance&);
    void submit(const MeshInstance* items, usize count);

    // Depth-only pass from the light's point of view; fills the shadow map.
    void renderShadow(rhi::ICommandList& cmd);
    // Main lit pass. `shadowMap` is the sampling bind group for the depth target
    // written by renderShadow(); pass an invalid handle to render unshadowed.
    void end(rhi::ICommandList& cmd, rhi::BindGroupHandle shadowMap = {});

    // World -> light clip space, valid after begin(); use it to size the shadow pass.
    [[nodiscard]] const Mat4& lightViewProj() const { return m_frameData.lightViewProj; }

    [[nodiscard]] u32 drawCallCount() const { return m_drawCalls; }

private:
    struct GpuMesh {
        rhi::BufferHandle vbo;
        rhi::BufferHandle ibo;
        u32               indexCount = 0;
        bool              alive      = false;
    };

    struct FrameUBO {
        Mat4 viewProj;
        Mat4 lightViewProj;
        Vec4 lightDir;
        Vec4 lightColor;
        Vec4 ambient;
        Vec4 cameraPos;
    };

    struct Push {
        Mat4 model;
        Vec4 color;
        Vec4 material;   // x: metallic, y: roughness
    };

    rhi::IGraphicsDevice& m_device;
    rhi::PipelineHandle   m_pipeline;
    rhi::PipelineHandle   m_shadowPipeline;
    rhi::BufferHandle     m_uniformBuffers[rhi::kMaxFramesInFlight];
    rhi::BindGroupHandle  m_uniformBindGroups[rhi::kMaxFramesInFlight];

    // Image-based lighting: a procedural environment + its diffuse irradiance,
    // built once on the CPU and uploaded as cubemaps.
    rhi::TextureHandle    m_envMap;
    rhi::TextureHandle    m_irradiance;
    rhi::SamplerHandle    m_iblSampler;
    rhi::BindGroupHandle  m_iblBindGroup;

    u32                   m_frame = 0;

    std::vector<GpuMesh>      m_meshes;
    std::vector<MeshInstance> m_instances;
    FrameUBO                  m_frameData{};
    u32                       m_drawCalls = 0;
};

}
