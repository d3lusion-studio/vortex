#include "vortex/renderer/skinning.hpp"

#include "vortex/core/log.hpp"
#include "vortex/core/profiler.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace vortex::renderer {

namespace {

// --- GPU ------------------------------------------------------------------
//
// The mesh is uploaded once and never touched again. Each frame only the bone matrices move,
// and the vertex shader does the blend — so the per-frame cost is the size of the skeleton
// (tens of matrices), not the size of the mesh (thousands of vertices).
class GpuSkinner final : public ISkinner {
public:
    explicit GpuSkinner(MeshRenderer& renderer) : m_renderer(renderer) {}

    SkinHandle addMesh(const MeshData& data) override {
        Entry e;
        e.mesh = m_renderer.createMesh(data);
        m_entries.push_back(e);
        return {.index = static_cast<u32>(m_entries.size() - 1), .generation = 0};
    }

    void setPose(SkinHandle h, const Mat4* bones, u32 boneCount) override {
        if (!h.valid() || h.index >= m_entries.size()) return;
        Entry& e = m_entries[h.index];

        // Copied, not aliased: the caller's pose vector is theirs to overwrite the moment this
        // returns, and the renderer does not read the bones until submit.
        e.bones.assign(bones, bones + boneCount);
    }

    void apply(SkinHandle h, MeshInstance& inst) const override {
        if (!h.valid() || h.index >= m_entries.size()) return;
        const Entry& e = m_entries[h.index];
        inst.mesh      = e.mesh;
        inst.bones     = e.bones.data();
        inst.boneCount = static_cast<u32>(e.bones.size());
    }

    [[nodiscard]] const char* name() const override { return "gpu"; }

private:
    struct Entry {
        MeshHandle        mesh;
        std::vector<Mat4> bones;
    };

    MeshRenderer&      m_renderer;
    std::vector<Entry> m_entries;
};

// --- CPU ------------------------------------------------------------------
//
// The same arithmetic, done here, and the whole posed mesh re-uploaded every frame. Slow by
// construction — but it is the one implementation that can be read and believed, and a
// backend with no storage buffers has no other way to skin at all.
class CpuSkinner final : public ISkinner {
public:
    explicit CpuSkinner(MeshRenderer& renderer) : m_renderer(renderer) {}

    SkinHandle addMesh(const MeshData& data) override {
        Entry e;
        e.rest = data;
        e.posed = data.vertices;
        // Dynamic: the vertex buffer is host-visible, because it is rewritten every frame.
        e.mesh = m_renderer.createMesh(data, /*dynamic=*/true);
        m_entries.push_back(std::move(e));
        return {.index = static_cast<u32>(m_entries.size() - 1), .generation = 0};
    }

    void setPose(SkinHandle h, const Mat4* bones, u32 boneCount) override {
        if (!h.valid() || h.index >= m_entries.size() || boneCount == 0) return;
        VORTEX_PROFILE_ZONE("skinning.cpu");

        Entry& e = m_entries[h.index];

        for (usize v = 0; v < e.rest.vertices.size(); ++v) {
            const MeshVertex& src = e.rest.vertices[v];
            MeshVertex&       dst = e.posed[v];
            dst = src;

            const f32 total = src.weights.x + src.weights.y + src.weights.z + src.weights.w;
            if (total < 0.0001f) continue;   // not skinned: leave it exactly where it was

            // The weighted sum of the four bone matrices — the same matrix the vertex shader
            // builds, built the same way, so the two paths cannot disagree by construction.
            Mat4 skin{};
            std::memset(skin.m, 0, sizeof(skin.m));
            for (int j = 0; j < 4; ++j) {
                const f32 w = (&src.weights.x)[j];
                if (w == 0.0f) continue;
                const u32 joint = src.joints[j];
                if (joint >= boneCount) continue;

                const Mat4& b = bones[joint];
                for (int k = 0; k < 16; ++k) skin.m[k] += b.m[k] * w;
            }

            const Vec4 p = skin * Vec4{src.position.x, src.position.y, src.position.z, 1.0f};
            dst.position = {p.x, p.y, p.z};

            // Directions carry a w of 0, so the matrix's translation column does not move them.
            const Vec4 n = skin * Vec4{src.normal.x, src.normal.y, src.normal.z, 0.0f};
            dst.normal = normalize(Vec3{n.x, n.y, n.z});

            const Vec4 t = skin * Vec4{src.tangent.x, src.tangent.y, src.tangent.z, 0.0f};
            const Vec3 tn = normalize(Vec3{t.x, t.y, t.z});
            dst.tangent = {tn.x, tn.y, tn.z, src.tangent.w};

            // The vertex shader must NOT skin these a second time. Zeroing the weights is what
            // tells it so — and it is also why the posed copy is a copy: the rest vertices keep
            // their weights, for the next frame.
            dst.weights = {0.0f, 0.0f, 0.0f, 0.0f};
        }

        m_renderer.updateMesh(e.mesh, e.posed.data(), e.posed.size());
    }

    void apply(SkinHandle h, MeshInstance& inst) const override {
        if (!h.valid() || h.index >= m_entries.size()) return;
        inst.mesh = m_entries[h.index].mesh;
        // No bones: the vertices arrive already posed, and the vertex shader leaves them alone.
        inst.bones     = nullptr;
        inst.boneCount = 0;
    }

    [[nodiscard]] const char* name() const override { return "cpu"; }

private:
    struct Entry {
        MeshHandle              mesh;
        MeshData                rest;    // the bind pose, never modified
        std::vector<MeshVertex> posed;   // this frame's, rewritten in place
    };

    MeshRenderer&      m_renderer;
    std::vector<Entry> m_entries;
};

} // namespace

const char* skinningBackendName(SkinningBackend backend) {
    return backend == SkinningBackend::Cpu ? "cpu" : "gpu";
}

SkinningBackend defaultSkinningBackend() {
    if (const char* env = std::getenv("VORTEX_SKINNING")) {
        if (std::strcmp(env, "cpu") == 0) return SkinningBackend::Cpu;
        if (std::strcmp(env, "gpu") == 0) return SkinningBackend::Gpu;
        VORTEX_WARN("Skinning", "VORTEX_SKINNING='%s' is not 'cpu' or 'gpu'; using gpu", env);
    }
    return SkinningBackend::Gpu;
}

std::unique_ptr<ISkinner> createSkinner(SkinningBackend backend, MeshRenderer& renderer) {
    VORTEX_INFO("Skinning", "Selected skinning backend: %s", skinningBackendName(backend));
    if (backend == SkinningBackend::Cpu) return std::make_unique<CpuSkinner>(renderer);
    return std::make_unique<GpuSkinner>(renderer);
}

std::unique_ptr<ISkinner> createSkinner(MeshRenderer& renderer) {
    return createSkinner(defaultSkinningBackend(), renderer);
}

}
