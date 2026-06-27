#pragma once
#include "vortex/asset/texture_asset.hpp"
#include "vortex/core/string_id.hpp"
#include "vortex/core/types.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace vortex::rhi { class IGraphicsDevice; }
namespace vortex::pf  { class IFileSystem; }

namespace vortex::assets {

class AssetManager {
public:
    AssetManager(rhi::IGraphicsDevice& device, pf::IFileSystem& fs);
    ~AssetManager();

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    [[nodiscard]] TextureHandle loadTexture(const char* path);

    // Resolve a handle to its data, or nullptr if it is stale/invalid.
    [[nodiscard]] const TextureAsset* get(TextureHandle handle) const;

    // Convenience: the current GPU texture for a handle (invalid if stale).
    [[nodiscard]] rhi::TextureHandle gpuTexture(TextureHandle handle) const;

    void unload(TextureHandle handle);

    void beginFrame();

    u32 pollHotReload();

    [[nodiscard]] usize liveTextureCount() const { return m_cache.size(); }

private:
    struct Slot {
        TextureAsset asset;
        u32          generation = 0;
        bool         alive      = false;
        StringId     key;            // path hash, for cache eviction on unload
        std::string  path;           // watched source path
        i64          mtime = 0;      // last observed modification time
    };

    struct PendingDestroy {
        rhi::TextureHandle gpu;
        u64                reclaimFrame;
    };

    [[nodiscard]] bool importTexture(const char* path, TextureAsset& outAsset,
                                     i64& outMtime) const;
    void scheduleDestroy(rhi::TextureHandle gpu);

    rhi::IGraphicsDevice& m_device;
    pf::IFileSystem&      m_fs;

    std::vector<Slot>            m_slots;
    std::vector<u32>             m_free;
    std::unordered_map<u64, u32> m_cache;   // path hash -> slot index
    std::vector<PendingDestroy>  m_pending;
    u64                          m_frame = 0;
};

}
