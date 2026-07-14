#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <string>
#include <string_view>

namespace vortex::assets {

// A handle is typed by the asset it points at: AssetHandle<TextureAsset> cannot be passed where
// an AssetHandle<GltfModel> is expected. The tag IS the asset type, so there is no separate tag
// struct to keep in sync with anything.
template <typename T>
using AssetHandle = Handle<T>;

// Where an asset is in its life.
//
// `Pending` exists because loading is asynchronous: a handle is returned immediately and is valid,
// but get() will hand back nullptr until the bytes have been read, decoded and uploaded. Code that
// forgets this reads a null and blames the file.
enum class LoadState : u8 { Pending, Loaded, Failed };

// How a texture should be created, decided AT LOAD TIME.
//
// The sampler belongs to the asset, not to the code that draws with it: whether a texture tiles is
// a property of the texture (a brick wall tiles; a UI icon does not), and threading that decision
// through every draw call is how half the call sites end up with the wrong one.
struct TextureOptions {
    rhi::Filter      filter  = rhi::Filter::Linear;
    rhi::AddressMode address = rhi::AddressMode::ClampToEdge;

    // sRGB is right for a PICTURE and wrong for DATA. A normal map, a roughness map, a height
    // field — those hold numbers, and an sRGB decode bends every one of them. Getting this wrong
    // does not fail; it just makes the lighting subtly incorrect forever.
    bool srgb = true;
};

// Everything a load might want to be told. One struct, because a load request that grows a
// parameter should not grow an overload.
struct LoadOptions {
    TextureOptions texture{};

    // How long to wait on a web asset before giving up. A load that hangs forever is worse than a
    // load that fails: the game never starts and nothing says why.
    u32 timeoutSeconds = 30;
};

// --- Asset paths -------------------------------------------------------------
//
// An asset is named by a small URI, and both halves are optional:
//
//     assets/hero.png                  a file
//     embedded://icon.png              bytes compiled INTO the binary
//     https://example.com/tex.png      bytes fetched over the network
//     assets/cato.gltf#mesh1           a SUBASSET — one piece of a file that holds many
//
// The scheme decides where the bytes come from; the fragment decides which piece of them you
// wanted. They are independent, and they compose: `embedded://cato.gltf#mesh1` is a perfectly good
// name for the second mesh of a model baked into the executable.
//
// The CACHE KEY is the whole string. That matters: `cato.gltf#mesh0` and `cato.gltf#mesh1` are two
// different assets that happen to be read out of one file, and a cache that keyed on the file alone
// would hand you the wrong mesh — and would look like it worked.

// Everything before the '#'. This is the thing that gets read: the file, the blob, the URL.
[[nodiscard]] inline std::string_view basePath(std::string_view path) {
    const usize hash = path.find('#');
    return hash == std::string_view::npos ? path : path.substr(0, hash);
}

// Everything after the '#', empty if there is none. This is the piece of the file that was asked
// for — a loader that does not support subassets can simply ignore it.
[[nodiscard]] inline std::string_view subassetOf(std::string_view path) {
    const usize hash = path.find('#');
    return hash == std::string_view::npos ? std::string_view{} : path.substr(hash + 1);
}

[[nodiscard]] inline bool isEmbedded(std::string_view path) {
    return basePath(path).starts_with("embedded://");
}

[[nodiscard]] inline bool isWeb(std::string_view path) {
    const std::string_view base = basePath(path);
    return base.starts_with("http://") || base.starts_with("https://");
}

struct TextureAsset {
    rhi::TextureHandle gpu;
    rhi::SamplerHandle sampler;   // created from TextureOptions; owned by the manager
    u32                width  = 0;
    u32                height = 0;
};

using TextureHandle = AssetHandle<TextureAsset>;

}
