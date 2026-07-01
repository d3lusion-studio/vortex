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

// A single directional light, the minimal lighting model for Phase 11.
struct DirectionalLight {
    Vec3 direction{-0.4f, -1.0f, -0.5f};   // direction the light travels (world)
    Vec3 color{1.0f, 1.0f, 1.0f};
    f32  intensity = 1.0f;
    Vec3 ambient{0.08f, 0.09f, 0.11f};
};

using MeshHandle = Handle<struct MeshTag>;

struct MeshInstance {
    MeshHandle mesh;
    Mat4       model = Mat4::identity();
    Vec4       color{1.0f, 1.0f, 1.0f, 1.0f};
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
    void end(rhi::ICommandList& cmd);

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
        Vec4 lightDir;
        Vec4 lightColor;
        Vec4 ambient;
        Vec4 cameraPos;
    };

    struct Push {
        Mat4 model;
        Vec4 color;
    };

    rhi::IGraphicsDevice& m_device;
    rhi::PipelineHandle   m_pipeline;
    rhi::BufferHandle     m_uniformBuffers[rhi::kMaxFramesInFlight];
    rhi::BindGroupHandle  m_uniformBindGroups[rhi::kMaxFramesInFlight];
    u32                   m_frame = 0;

    std::vector<GpuMesh>      m_meshes;
    std::vector<MeshInstance> m_instances;
    FrameUBO                  m_frameData{};
    u32                       m_drawCalls = 0;
};

}
