#include "vortex/asset/asset_manager.hpp"

#include "vortex/asset/cooked_texture.hpp"
#include "vortex/asset/image.hpp"
#include "vortex/core/log.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace vortex::assets {

namespace {
i64 fileMTime(const char* path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<i64>(t.time_since_epoch().count());
}

bool isCookedTexture(const char* path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".vtex";
}

}

AssetManager::AssetManager(rhi::IGraphicsDevice& device, pf::IFileSystem& fs)
    : m_device(device), m_fs(fs) {}

AssetManager::~AssetManager() {
    // The caller is expected to have made the GPU idle before teardown.
    for (PendingDestroy& p : m_pending) m_device.destroyTexture(p.gpu);
    for (Slot& s : m_slots)
        if (s.alive) m_device.destroyTexture(s.asset.gpu);
}

bool AssetManager::importTexture(const char* path, TextureAsset& outAsset, i64& outMtime) const {
    const std::vector<std::byte> bytes = m_fs.readFile(path);
    if (bytes.empty()) {
        VORTEX_ERROR("Asset", "Failed to read '%s'", path);
        return false;
    }

    const Image image = isCookedTexture(path)
                            ? decodeCookedTexture(bytes.data(), bytes.size())
                            : decodeImage(bytes.data(), bytes.size());
    if (!image.valid()) {
        VORTEX_ERROR("Asset", "Failed to decode '%s'", path);
        return false;
    }

    outAsset.gpu = m_device.createTexture(
        {.width = image.width, .height = image.height, .debugName = "texture_asset"},
        image.pixels.data());
    outAsset.width  = image.width;
    outAsset.height = image.height;
    outMtime        = fileMTime(path);
    return true;
}

void AssetManager::scheduleDestroy(rhi::TextureHandle gpu) {
    if (!gpu.valid()) return;

    m_pending.push_back({gpu, m_frame + rhi::kMaxFramesInFlight + 1});
}

TextureHandle AssetManager::loadTexture(const char* path) {
    const StringId key = stringId(path);
    if (auto it = m_cache.find(key.value); it != m_cache.end()) {
        const Slot& slot = m_slots[it->second];
        return TextureHandle{it->second, slot.generation};
    }

    TextureAsset asset;
    i64          mtime = 0;
    if (!importTexture(path, asset, mtime)) return TextureHandle{};

    u32 index;
    if (!m_free.empty()) {
        index = m_free.back();
        m_free.pop_back();
    } else {
        index = static_cast<u32>(m_slots.size());
        m_slots.push_back({});
    }

    Slot& slot  = m_slots[index];
    slot.asset  = asset;
    slot.alive  = true;
    slot.key    = key;
    slot.path   = path;
    slot.mtime  = mtime;
    m_cache.emplace(key.value, index);

    VORTEX_INFO("Asset", "Loaded '%s' (%ux%u)", path, asset.width, asset.height);
    return TextureHandle{index, slot.generation};
}

const TextureAsset* AssetManager::get(TextureHandle handle) const {
    if (handle.index >= m_slots.size()) return nullptr;
    const Slot& slot = m_slots[handle.index];
    if (!slot.alive || slot.generation != handle.generation) return nullptr;
    return &slot.asset;
}

rhi::TextureHandle AssetManager::gpuTexture(TextureHandle handle) const {
    const TextureAsset* asset = get(handle);
    return asset ? asset->gpu : rhi::TextureHandle{};
}

std::string AssetManager::pathOf(rhi::TextureHandle gpu) const {
    if (!gpu.valid()) return {};
    // A linear scan over the loaded textures. This runs when a scene is saved, not
    // when one is drawn, and a project has hundreds of textures rather than millions.
    for (const Slot& slot : m_slots)
        if (slot.alive && slot.asset.gpu == gpu) return slot.path;
    return {};
}

void AssetManager::unload(TextureHandle handle) {
    if (handle.index >= m_slots.size()) return;
    Slot& slot = m_slots[handle.index];
    if (!slot.alive || slot.generation != handle.generation) return;

    scheduleDestroy(slot.asset.gpu);
    m_cache.erase(slot.key.value);
    slot.alive = false;
    slot.path.clear();
    ++slot.generation;                 // invalidate every outstanding handle
    m_free.push_back(handle.index);
}

void AssetManager::beginFrame() {
    ++m_frame;
    usize keep = 0;
    for (PendingDestroy& p : m_pending) {
        if (p.reclaimFrame <= m_frame) {
            m_device.destroyTexture(p.gpu);
        } else {
            m_pending[keep++] = p;
        }
    }
    m_pending.resize(keep);
}

u32 AssetManager::pollHotReload() {
    u32 reloaded = 0;
    for (Slot& slot : m_slots) {
        if (!slot.alive || slot.path.empty()) continue;

        const i64 mtime = fileMTime(slot.path.c_str());
        if (mtime == 0 || mtime == slot.mtime) continue;

        TextureAsset fresh;
        i64          freshMtime = 0;
        if (!importTexture(slot.path.c_str(), fresh, freshMtime)) {
            slot.mtime = mtime;        // avoid hammering a broken file every frame
            continue;
        }

        scheduleDestroy(slot.asset.gpu);   // retire the old GPU texture safely
        slot.asset = fresh;                // handle stays the same; data swapped
        slot.mtime = freshMtime;
        ++reloaded;
        VORTEX_INFO("Asset", "Hot-reloaded '%s' (%ux%u)",
                    slot.path.c_str(), fresh.width, fresh.height);
    }
    return reloaded;
}

}
