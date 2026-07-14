#include "vortex/asset/asset_manager.hpp"

#include "http.hpp"
#include "vortex/asset/cooked_texture.hpp"
#include "vortex/asset/image.hpp"
#include "vortex/core/log.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>

namespace vortex::assets {

namespace detail {
u32 nextAssetTypeId() {
    static u32 counter = 0;
    return counter++;
}
}

namespace {

// The mtime of the thing an asset was actually read FROM.
//
// Two traps, both silent. A subasset's name is `cato.gltf#mesh1`, and stat() of that string fails —
// so the mesh would never hot-reload and nobody would ever guess why. And an embedded or web asset
// has no mtime at all: 0 means "cannot go stale", which for bytes baked into the executable is
// simply the truth.
i64 sourceMTime(std::string_view name) {
    if (isEmbedded(name) || isWeb(name)) return 0;

    struct stat st{};
    if (::stat(std::string(basePath(name)).c_str(), &st) != 0) return 0;
    return static_cast<i64>(st.st_mtime);
}

bool hasExtension(std::string_view path, std::string_view ext) {
    return path.size() > ext.size() && path.substr(path.size() - ext.size()) == ext;
}

bool isCookedTexture(std::string_view path) { return hasExtension(basePath(path), ".vtex"); }

// The engine's own texture loader — registered exactly the way a game registers one of its own. It
// is not privileged; it is simply the one that ships.
class TextureLoader final : public IAssetLoader {
public:
    const char* name() const override { return "texture"; }

    // The BASE path: a name may carry a scheme and a subaset — `embedded://ui.png#icon` — and an
    // extension test run against the whole string would simply never match.
    bool canLoad(std::string_view full) const override {
        const std::string_view path = basePath(full);
        return hasExtension(path, ".png")  || hasExtension(path, ".jpg") ||
               hasExtension(path, ".jpeg") || hasExtension(path, ".bmp") ||
               hasExtension(path, ".tga")  || hasExtension(path, ".vtex");
    }

    // WORKER THREAD. Decode only — there is no device here, and there must not be.
    std::shared_ptr<void> decode(std::string_view path, const std::vector<std::byte>& bytes,
                                 const LoadOptions&) override {
        Image image = isCookedTexture(path) ? decodeCookedTexture(bytes.data(), bytes.size())
                                            : decodeImage(bytes.data(), bytes.size());
        if (!image.valid()) return nullptr;

        auto asset = std::make_shared<Pending>();
        asset->texture.width  = image.width;
        asset->texture.height = image.height;
        asset->pixels         = std::move(image.pixels);
        return asset;
    }

    // MAIN THREAD. The one and only place a texture load touches the device.
    bool finalize(std::shared_ptr<void>& asset, rhi::IGraphicsDevice& device,
                  const LoadOptions& options) override {
        auto pending = std::static_pointer_cast<Pending>(asset);

        pending->texture.gpu = device.createTexture(
            {.width  = pending->texture.width,
             .height = pending->texture.height,
             // sRGB for a PICTURE, UNORM for DATA. Get this wrong and nothing fails — the lighting
             // is just quietly incorrect, forever.
             .format = options.texture.srgb ? rhi::Format::R8G8B8A8_SRGB
                                            : rhi::Format::R8G8B8A8_UNORM,
             .debugName = "texture_asset"},
            pending->pixels.data());

        pending->texture.sampler = device.createSampler({.minFilter = options.texture.filter,
                                                         .magFilter = options.texture.filter,
                                                         .addressU  = options.texture.address,
                                                         .addressV  = options.texture.address});
        if (!pending->texture.gpu.valid()) return false;

        // The CPU pixels have done their job. Replace the carrier with the asset itself, so nothing
        // holds a second copy of every texture in RAM for the rest of the run.
        asset = std::make_shared<TextureAsset>(pending->texture);
        return true;
    }

    // MAIN THREAD. The pixels are on the GPU — finalize() dropped the CPU copy on purpose — so
    // saving means reading them back. That is the price of not keeping a second copy of every
    // texture in RAM for the whole run, and it is the right trade: a save happens once, a texture
    // sits in memory forever.
    bool save(const std::shared_ptr<void>& asset, rhi::IGraphicsDevice& device,
              const char* path) override {
        const auto* tex = static_cast<const TextureAsset*>(asset.get());
        if (tex == nullptr || !tex->gpu.valid()) return false;

        std::vector<u8> pixels(static_cast<usize>(tex->width) * tex->height * 4);
        device.readTexture(tex->gpu, pixels.data());
        return writePng(path, tex->width, tex->height, pixels.data());
    }

private:
    // What crosses the thread boundary: the asset, plus the pixels it still needs uploading. Held
    // in the shared_ptr rather than in the loader, because two IO threads decode at once and a
    // member would be a data race.
    struct Pending {
        TextureAsset    texture;
        std::vector<u8> pixels;
    };
};

} // namespace

// ---------------------------------------------------------------------------

struct AssetManager::Impl {
    rhi::IGraphicsDevice& device;
    pf::IFileSystem&      fs;

    struct Slot {
        std::shared_ptr<void> data;
        LoadState             state      = LoadState::Pending;
        u32                   generation = 0;
        bool                  alive      = false;
        std::string           name;      // the path, or the name a generated asset was given
        LoadOptions           options;
        IAssetLoader*         loader = nullptr;
        i64                   mtime  = 0;
    };

    struct TypeStore {
        std::vector<std::unique_ptr<IAssetLoader>> loaders;
        std::vector<Slot>                          slots;
        std::vector<u32>                           free;
        std::unordered_map<std::string, u32>       byName;
    };

    // One unit of work, handed to an IO thread and handed back.
    struct Job {
        u32                   type       = 0;
        u32                   index      = 0;
        u32                   generation = 0;
        std::string           path;
        LoadOptions           options;
        IAssetLoader*         loader = nullptr;
        std::shared_ptr<void> result;
        bool                  ok = false;
    };

    std::vector<TypeStore> types;

    // `inbox` is what the IO threads take from; `outbox` is what update() drains on the main
    // thread. Two queues, because the two halves of a load run on different threads by design.
    mutable std::mutex      mutex;
    std::condition_variable wake;
    std::deque<Job>         inbox;
    std::deque<Job>         outbox;
    std::atomic<usize>      inFlight{0};
    bool                    stopping = false;

    std::vector<std::thread> workers;

    // Bytes compiled into the binary. A span, not a copy: the data is already in the executable's
    // read-only pages, and copying it into the heap at startup would double the cost of the very
    // thing that was embedded to be cheap.
    std::unordered_map<std::string, std::span<const std::byte>> embedded;

    // Deferred destruction, so a texture is never freed while a frame in flight still samples it.
    struct PendingDestroy { rhi::TextureHandle gpu; rhi::SamplerHandle sampler; u64 frame; };
    std::vector<PendingDestroy> pendingDestroy;
    u64                         frame = 0;

    Impl(rhi::IGraphicsDevice& d, pf::IFileSystem& f) : device(d), fs(f) {}

    TypeStore& store(u32 type) {
        if (type >= types.size()) types.resize(type + 1);
        return types[type];
    }
    const TypeStore* storeIf(u32 type) const {
        return type < types.size() ? &types[type] : nullptr;
    }

    // WORKER THREAD. Where an asset's bytes come from, and the only place that answer is decided.
    //
    // Three sources, one signature. That is what lets the rest of the engine — the cache, the
    // barrier, hot-reload, the loaders — stay completely ignorant of whether an asset came off the
    // disk, out of the executable, or over the network. A texture from a URL is loaded by the same
    // TextureLoader, on the same thread, into the same cache slot.
    std::vector<std::byte> fetchBytes(const std::string& path, const LoadOptions& options) const {
        const std::string_view base = basePath(path);

        if (isEmbedded(path)) {
            // No lock: embed() is documented as setup-time, and the spans point at static storage.
            const auto it = embedded.find(std::string(base));
            if (it == embedded.end()) {
                VORTEX_ERROR("Asset", "nothing embedded under '%.*s'",
                             static_cast<int>(base.size()), base.data());
                return {};
            }
            return {it->second.begin(), it->second.end()};
        }

        if (isWeb(path)) {
            std::vector<std::byte> bytes;
            std::string            error;
            if (!httpGet(std::string(base), options.timeoutSeconds, bytes, error)) {
                // Name the URL and the reason. "failed to load" alone, for a network asset, is the
                // difference between a five-second fix and an afternoon.
                VORTEX_ERROR("Asset", "GET %.*s: %s", static_cast<int>(base.size()), base.data(),
                             error.c_str());
                return {};
            }
            return bytes;
        }

        // Blocking IO — off the main thread, and off the job system's pool. This is the whole
        // reason the manager owns threads of its own.
        return fs.readFile(std::string(base).c_str());
    }

    void workerLoop() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !inbox.empty(); });
                if (stopping && inbox.empty()) return;
                job = std::move(inbox.front());
                inbox.pop_front();
            }

            const std::vector<std::byte> bytes = fetchBytes(job.path, job.options);

            // decode() gets the FULL name, fragment and all: the bytes are the whole file, and the
            // subasset is how the loader knows which piece of them was asked for.
            if (!bytes.empty() && job.loader != nullptr)
                job.result = job.loader->decode(job.path, bytes, job.options);

            job.ok = job.result != nullptr;
            if (!job.ok) VORTEX_ERROR("Asset", "failed to load '%s'", job.path.c_str());

            {
                const std::lock_guard lock(mutex);
                outbox.push_back(std::move(job));
            }
        }
    }

    // Textures are the only assets that own GPU objects today. A loader that owns more would have
    // to say so — which is a gap, and an honest one.
    static bool ownsGpu(const IAssetLoader* loader) {
        return loader != nullptr && std::string_view(loader->name()) == "texture";
    }
};

// ---------------------------------------------------------------------------

AssetManager::AssetManager(rhi::IGraphicsDevice& device, pf::IFileSystem& fs, u32 ioThreads)
    : m_impl(std::make_unique<Impl>(device, fs)) {

    addLoader<TextureAsset>(std::make_unique<TextureLoader>());

    const u32 count = std::max(ioThreads, 1u);
    for (u32 i = 0; i < count; ++i)
        m_impl->workers.emplace_back([this] { m_impl->workerLoop(); });

    VORTEX_TRACE("Asset", "%u IO thread(s)", count);
}

AssetManager::~AssetManager() {
    {
        const std::lock_guard lock(m_impl->mutex);
        m_impl->stopping = true;
    }
    m_impl->wake.notify_all();
    for (std::thread& t : m_impl->workers) t.join();

    m_impl->device.waitIdle();

    for (Impl::TypeStore& store : m_impl->types)
        for (Impl::Slot& slot : store.slots) {
            if (!slot.alive || !slot.data || !Impl::ownsGpu(slot.loader)) continue;
            auto* tex = static_cast<TextureAsset*>(slot.data.get());
            if (tex->gpu.valid())     m_impl->device.destroyTexture(tex->gpu);
            if (tex->sampler.valid()) m_impl->device.destroySampler(tex->sampler);
        }

    for (const Impl::PendingDestroy& p : m_impl->pendingDestroy) {
        if (p.gpu.valid())     m_impl->device.destroyTexture(p.gpu);
        if (p.sampler.valid()) m_impl->device.destroySampler(p.sampler);
    }
}

void AssetManager::registerLoader(u32 type, std::unique_ptr<IAssetLoader> loader) {
    if (!loader) return;
    VORTEX_TRACE("Asset", "loader registered: %s", loader->name());
    m_impl->store(type).loaders.push_back(std::move(loader));
}

AssetManager::Id AssetManager::loadUntyped(u32 type, const char* path,
                                           const LoadOptions& options) {
    if (path == nullptr || *path == '\0') return {};

    Impl::TypeStore& store = m_impl->store(type);

    // The same path twice is the same asset. Reading the file again would not merely be slow: it
    // would give two handles to two identical GPU textures, and nothing would ever notice.
    {
        const std::lock_guard lock(m_impl->mutex);
        const auto it = store.byName.find(path);
        if (it != store.byName.end())
            return {it->second, store.slots[it->second].generation, type};
    }

    IAssetLoader* loader = nullptr;
    for (const auto& candidate : store.loaders)
        if (candidate->canLoad(path)) { loader = candidate.get(); break; }

    if (loader == nullptr) {
        VORTEX_ERROR("Asset", "no loader claims '%s'", path);
        return {};
    }

    u32 index = 0, generation = 0;
    {
        const std::lock_guard lock(m_impl->mutex);
        if (!store.free.empty()) {
            index = store.free.back();
            store.free.pop_back();
        } else {
            index = static_cast<u32>(store.slots.size());
            store.slots.emplace_back();
        }

        Impl::Slot& slot = store.slots[index];
        slot.data    = nullptr;
        slot.state   = LoadState::Pending;
        slot.alive   = true;
        slot.name    = path;
        slot.options = options;
        slot.loader  = loader;
        slot.mtime   = sourceMTime(path);
        generation   = slot.generation;

        store.byName[path] = index;

        m_impl->inbox.push_back({type, index, generation, path, options, loader, nullptr, false});
        m_impl->inFlight.fetch_add(1);
    }
    m_impl->wake.notify_one();

    return {index, generation, type};
}

AssetManager::Id AssetManager::insertUntyped(u32 type, std::string_view name,
                                             std::shared_ptr<void> asset) {
    if (!asset) return {};

    Impl::TypeStore& store = m_impl->store(type);
    const std::lock_guard lock(m_impl->mutex);

    u32 index = 0;
    if (!store.free.empty()) {
        index = store.free.back();
        store.free.pop_back();
    } else {
        index = static_cast<u32>(store.slots.size());
        store.slots.emplace_back();
    }

    Impl::Slot& slot = store.slots[index];
    slot.data = std::move(asset);
    // A generated asset is Loaded the moment it exists. Making callers poll for it would be a lie
    // about where it came from.
    slot.state  = LoadState::Loaded;
    slot.alive  = true;
    slot.name   = std::string(name);
    slot.loader = nullptr;
    slot.mtime  = 0;   // nothing on disk to watch

    if (!name.empty()) store.byName[std::string(name)] = index;
    return {index, slot.generation, type};
}

AssetManager::Id AssetManager::findUntyped(u32 type, std::string_view name) const {
    const Impl::TypeStore* store = m_impl->storeIf(type);
    if (store == nullptr) return {};

    const std::lock_guard lock(m_impl->mutex);
    const auto it = store->byName.find(std::string(name));
    if (it == store->byName.end()) return {};
    return {it->second, store->slots[it->second].generation, type};
}

const void* AssetManager::getUntyped(u32 type, u32 index, u32 generation) const {
    const Impl::TypeStore* store = m_impl->storeIf(type);
    if (store == nullptr) return nullptr;

    const std::lock_guard lock(m_impl->mutex);
    if (index >= store->slots.size()) return nullptr;

    const Impl::Slot& slot = store->slots[index];
    if (!slot.alive || slot.generation != generation) return nullptr;
    if (slot.state != LoadState::Loaded) return nullptr;   // still in flight, or failed
    return slot.data.get();
}

LoadState AssetManager::stateUntyped(u32 type, u32 index, u32 generation) const {
    const Impl::TypeStore* store = m_impl->storeIf(type);
    if (store == nullptr) return LoadState::Failed;

    const std::lock_guard lock(m_impl->mutex);
    if (index >= store->slots.size()) return LoadState::Failed;

    const Impl::Slot& slot = store->slots[index];
    if (!slot.alive || slot.generation != generation) return LoadState::Failed;
    return slot.state;
}

LoadState AssetManager::stateOf(const Id& id) const {
    if (!id.valid()) return LoadState::Failed;
    return stateUntyped(id.type, id.index, id.generation);
}

void AssetManager::update() {
    ++m_impl->frame;

    auto& pending = m_impl->pendingDestroy;
    for (usize i = 0; i < pending.size();) {
        if (m_impl->frame >= pending[i].frame) {
            if (pending[i].gpu.valid())     m_impl->device.destroyTexture(pending[i].gpu);
            if (pending[i].sampler.valid()) m_impl->device.destroySampler(pending[i].sampler);
            pending[i] = pending.back();
            pending.pop_back();
        } else {
            ++i;
        }
    }

    for (;;) {
        Impl::Job job;
        {
            const std::lock_guard lock(m_impl->mutex);
            if (m_impl->outbox.empty()) return;
            job = std::move(m_impl->outbox.front());
            m_impl->outbox.pop_front();
        }

        // MAIN THREAD ONLY. The one place a load touches the graphics device.
        bool ok = job.ok;
        if (ok && job.loader != nullptr)
            ok = job.loader->finalize(job.result, m_impl->device, job.options);

        {
            const std::lock_guard lock(m_impl->mutex);
            Impl::Slot& slot = m_impl->types[job.type].slots[job.index];
            if (slot.alive && slot.generation == job.generation) {
                slot.data  = ok ? std::move(job.result) : nullptr;
                slot.state = ok ? LoadState::Loaded : LoadState::Failed;
            }
        }
        m_impl->inFlight.fetch_sub(1);
    }
}

void AssetManager::waitFor(std::span<const Id> ids) {
    // Pump, do not sleep. An asset only becomes Loaded when the main thread finalises it — and the
    // main thread is the one waiting here. A wait that merely slept would deadlock against the very
    // step it is waiting for.
    for (;;) {
        update();

        bool allDone = true;
        for (const Id& id : ids) {
            if (!id.valid()) continue;
            if (stateOf(id) == LoadState::Pending) { allDone = false; break; }
        }
        if (allDone) return;

        std::this_thread::yield();
    }
}

void AssetManager::waitForAll() {
    while (m_impl->inFlight.load() > 0) {
        update();
        std::this_thread::yield();
    }
    update();
}

u32 AssetManager::pollHotReload() {
    std::vector<std::pair<u32, u32>> stale;   // (type, index)
    {
        const std::lock_guard lock(m_impl->mutex);
        for (u32 type = 0; type < m_impl->types.size(); ++type) {
            Impl::TypeStore& store = m_impl->types[type];
            for (u32 i = 0; i < store.slots.size(); ++i) {
                Impl::Slot& slot = store.slots[i];
                // A generated asset has no file, so it has no mtime and cannot go stale.
                if (!slot.alive || slot.loader == nullptr || slot.mtime == 0) continue;
                if (slot.state == LoadState::Pending) continue;

                const i64 mtime = sourceMTime(slot.name);
                if (mtime == 0 || mtime == slot.mtime) continue;

                slot.mtime = mtime;
                stale.emplace_back(type, i);
            }
        }
    }

    for (const auto& [type, index] : stale) {
        {
            const std::lock_guard lock(m_impl->mutex);
            Impl::Slot& slot = m_impl->types[type].slots[index];

            // Retire the current GPU objects on a delay. Freeing them now would leave a frame in
            // flight drawing with a destroyed texture.
            if (slot.data && Impl::ownsGpu(slot.loader)) {
                auto* tex = static_cast<TextureAsset*>(slot.data.get());
                m_impl->pendingDestroy.push_back({tex->gpu, tex->sampler, m_impl->frame + 3});
            }

            m_impl->inbox.push_back({type, index, slot.generation, slot.name, slot.options,
                                     slot.loader, nullptr, false});
            m_impl->inFlight.fetch_add(1);
        }
        m_impl->wake.notify_one();
    }

    const u32 reloaded = static_cast<u32>(stale.size());
    if (reloaded > 0) VORTEX_INFO("Asset", "hot reload: %u file(s) changed", reloaded);
    return reloaded;
}

void AssetManager::embed(std::string_view name, std::span<const std::byte> bytes) {
    if (name.empty() || bytes.empty()) return;

    std::string key(name);
    if (!key.starts_with("embedded://")) key = "embedded://" + key;

    const std::lock_guard lock(m_impl->mutex);
    m_impl->embedded[key] = bytes;
    VORTEX_TRACE("Asset", "embedded %zu bytes as '%s'", bytes.size(), key.c_str());
}

bool AssetManager::hasEmbedded(std::string_view name) const {
    std::string key(name);
    if (!key.starts_with("embedded://")) key = "embedded://" + key;

    const std::lock_guard lock(m_impl->mutex);
    return m_impl->embedded.contains(key);
}

// --- Saving ------------------------------------------------------------------

bool AssetManager::saveUntyped(u32 type, u32 index, u32 generation, const char* path) {
    if (path == nullptr || *path == '\0') return false;

    std::shared_ptr<void> data;
    IAssetLoader*         loader = nullptr;
    {
        const std::lock_guard lock(m_impl->mutex);
        const Impl::TypeStore* store = m_impl->storeIf(type);
        if (store == nullptr || index >= store->slots.size()) return false;

        const Impl::Slot& slot = store->slots[index];
        if (!slot.alive || slot.generation != generation) return false;
        if (slot.state != LoadState::Loaded || !slot.data) {
            VORTEX_ERROR("Asset", "cannot save '%s': it is not loaded", slot.name.c_str());
            return false;
        }
        data = slot.data;

        // The writer is chosen by the DESTINATION, not by where the asset came from. Two reasons,
        // and both are cases the other way round gets wrong:
        //
        //   - A generated asset was never read, so it has no loader of its own — and it is exactly
        //     the asset you most want to save. Keying on its origin would make the one case that
        //     matters the one case that fails.
        //   - Saving a .png as a .jpg is a change of destination, not of source.
        for (const auto& candidate : store->loaders)
            if (candidate->canLoad(path)) { loader = candidate.get(); break; }

        // Nothing claims the extension: fall back to whatever read it in the first place, which is
        // right for a format whose writer does not advertise via canLoad().
        if (loader == nullptr) loader = slot.loader;
    }

    if (loader == nullptr) {
        VORTEX_ERROR("Asset", "cannot save to '%s': no loader of that type claims it", path);
        return false;
    }

    // The lock is released: save() reads pixels back off the GPU, which can take milliseconds, and
    // holding the manager's mutex across that would stall every IO thread trying to hand work back.
    if (!loader->save(data, m_impl->device, path)) {
        VORTEX_ERROR("Asset", "loader '%s' could not write '%s'", loader->name(), path);
        return false;
    }

    VORTEX_TRACE("Asset", "saved '%s' via loader '%s'", path, loader->name());
    return true;
}

bool AssetManager::saveTexture(TextureHandle handle, const char* path) {
    return saveAsset<TextureAsset>(handle, path);
}

// --- The old texture-only API ------------------------------------------------

TextureHandle AssetManager::loadTexture(const char* path, const LoadOptions& options) {
    // The old API was synchronous and its callers were written against that. Keeping it blocking is
    // what lets every existing example keep working while new code uses load<T>() and a barrier.
    return loadBlocking<TextureAsset>(path, options);
}

rhi::TextureHandle AssetManager::gpuTexture(TextureHandle handle) const {
    const TextureAsset* asset = get<TextureAsset>(handle);
    return asset != nullptr ? asset->gpu : rhi::TextureHandle{};
}

std::string AssetManager::pathOf(rhi::TextureHandle gpu) const {
    if (!gpu.valid()) return {};

    const Impl::TypeStore* store = m_impl->storeIf(detail::assetTypeId<TextureAsset>());
    if (store == nullptr) return {};

    const std::lock_guard lock(m_impl->mutex);
    for (const Impl::Slot& slot : store->slots) {
        if (!slot.alive || !slot.data || !Impl::ownsGpu(slot.loader)) continue;
        if (static_cast<const TextureAsset*>(slot.data.get())->gpu == gpu) return slot.name;
    }
    return {};
}

void AssetManager::unload(TextureHandle handle) {
    const std::lock_guard lock(m_impl->mutex);

    Impl::TypeStore& store = m_impl->store(detail::assetTypeId<TextureAsset>());
    if (handle.index >= store.slots.size()) return;

    Impl::Slot& slot = store.slots[handle.index];
    if (!slot.alive || slot.generation != handle.generation) return;

    if (slot.data) {
        auto* tex = static_cast<TextureAsset*>(slot.data.get());
        m_impl->pendingDestroy.push_back({tex->gpu, tex->sampler, m_impl->frame + 3});
    }

    store.byName.erase(slot.name);
    slot.data  = nullptr;
    slot.alive = false;
    ++slot.generation;   // every handle to the old asset is now stale, and says so
    store.free.push_back(handle.index);
}

usize AssetManager::liveTextureCount() const {
    const Impl::TypeStore* store = m_impl->storeIf(detail::assetTypeId<TextureAsset>());
    if (store == nullptr) return 0;

    const std::lock_guard lock(m_impl->mutex);
    usize count = 0;
    for (const Impl::Slot& slot : store->slots)
        if (slot.alive) ++count;
    return count;
}

// --- Barrier -----------------------------------------------------------------

bool AssetManager::Barrier::done(const AssetManager& mgr) const { return pending(mgr) == 0; }

usize AssetManager::Barrier::pending(const AssetManager& mgr) const {
    usize count = 0;
    for (const Id& id : m_ids)
        if (mgr.stateOf(id) == LoadState::Pending) ++count;
    return count;
}

usize AssetManager::Barrier::failed(const AssetManager& mgr) const {
    usize count = 0;
    for (const Id& id : m_ids)
        if (mgr.stateOf(id) == LoadState::Failed) ++count;
    return count;
}

f32 AssetManager::Barrier::progress(const AssetManager& mgr) const {
    if (m_ids.empty()) return 1.0f;
    return 1.0f - static_cast<f32>(pending(mgr)) / static_cast<f32>(m_ids.size());
}

}
