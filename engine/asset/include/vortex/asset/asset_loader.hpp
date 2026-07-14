#pragma once
#include "vortex/asset/asset_types.hpp"
#include "vortex/core/types.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vortex::rhi { class IGraphicsDevice; }

namespace vortex::assets {

// A loader turns bytes into an asset. Registering one is how a game teaches the engine a file
// format the engine has never heard of.
//
// The load is split in two, and the split is not a style choice — it is the whole reason async
// asset loading is possible at all:
//
//   decode()   runs on an IO/worker thread. It gets bytes and must produce CPU data. It may NOT
//              touch the graphics device: the RHI is not thread-safe, and a texture uploaded from
//              a worker thread is a race that will crash on someone else's machine, next month.
//
//   finalize() runs on the MAIN thread, inside AssetManager::update(). This is where the GPU
//              upload happens. It is short by construction, because the expensive part — reading
//              and decoding — already happened somewhere else.
//
// Get this backwards and you have either a stall (decode on the main thread) or a race (upload on
// a worker). The interface makes it hard to write either.
class IAssetLoader {
public:
    virtual ~IAssetLoader() = default;

    IAssetLoader(const IAssetLoader&)            = delete;
    IAssetLoader& operator=(const IAssetLoader&) = delete;

    // For logs and diagnostics.
    [[nodiscard]] virtual const char* name() const = 0;

    // Does this loader handle that file? Usually an extension test. The first registered loader
    // for a type that says yes gets it.
    [[nodiscard]] virtual bool canLoad(std::string_view path) const = 0;

    // WORKER THREAD. Bytes in, CPU-side asset out. Return null to fail the load; the manager logs
    // it and marks the handle Failed rather than pretending.
    [[nodiscard]] virtual std::shared_ptr<void> decode(std::string_view path,
                                                       const std::vector<std::byte>& bytes,
                                                       const LoadOptions&) = 0;

    // MAIN THREAD. Upload to the GPU, resolve anything that needed a device. The default does
    // nothing, which is right for an asset that lives entirely on the CPU.
    virtual bool finalize(std::shared_ptr<void>& /*asset*/, rhi::IGraphicsDevice& /*device*/,
                          const LoadOptions&) {
        return true;
    }

    // MAIN THREAD. Write the asset back out. Saving lives on the LOADER because the code that knows
    // how to read a format is the only code that knows how to write it — a saver bolted on
    // somewhere else drifts out of sync with the parser and starts producing files the engine can
    // no longer load.
    //
    // The device is passed because some assets do not keep a CPU copy of themselves: a texture's
    // pixels live on the GPU, and saving one means reading them back. Returning false is the honest
    // answer for a format that has no writer, and is the default.
    virtual bool save(const std::shared_ptr<void>& /*asset*/, rhi::IGraphicsDevice& /*device*/,
                      const char* /*path*/) {
        return false;
    }

protected:
    IAssetLoader() = default;
};

}
