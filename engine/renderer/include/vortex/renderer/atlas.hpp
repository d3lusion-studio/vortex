#pragma once
#include "vortex/core/types.hpp"
#include "vortex/renderer/sprite_atlas.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace vortex::rhi { class IGraphicsDevice; }

namespace vortex::renderer {

// Packs many small images into a few large texture pages.
//
// This is the draw-call fix. SpriteBatch starts a new draw call every time the
// texture changes, so a scene built from a hundred loose PNGs costs a hundred draw
// calls no matter how well it is sorted. Pack those hundred images onto one page and
// they become one draw call, because they are now literally the same texture — no
// shader, no bindless, no per-backend feature to negotiate.
//
//   AtlasBuilder b;
//   const u32 hero = b.add("hero", heroPixels, 32, 32);
//   TextureAtlas atlas = b.build(device);
//   sprite.setRegion(atlas.region(hero));
//
// Images are RGBA8, tightly packed. Anything wider or taller than a page is dropped
// (it cannot be packed by definition) and its region comes back invalid.
class TextureAtlas {
public:
    [[nodiscard]] TextureRegion region(u32 id) const;

    // Regions are also addressable by the name they were added under. Null if no
    // entry carries that name.
    [[nodiscard]] const TextureRegion* find(std::string_view name) const;

    [[nodiscard]] const std::vector<rhi::TextureHandle>& pages() const noexcept { return m_pages; }
    [[nodiscard]] usize entryCount() const noexcept { return m_regions.size(); }

    // Pages are plain device textures; the atlas does not own them past this call.
    void destroy(rhi::IGraphicsDevice&);

private:
    friend class AtlasBuilder;

    std::vector<rhi::TextureHandle> m_pages;
    std::vector<TextureRegion>      m_regions;
    std::vector<std::string>        m_names;
};

class AtlasBuilder {
public:
    // `padding` is the gutter left around each image, in pixels. The gutter is filled
    // by replicating the image's own edge pixels outwards, so a linear sampler that
    // reaches past the edge of a region reads that region's colour rather than its
    // neighbour's — which is what atlas "bleeding" is. One pixel is enough unless the
    // atlas is drawn minified.
    explicit AtlasBuilder(u32 pageSize = 2048, u32 padding = 1);

    // Copies the pixels; the caller's buffer need not outlive the call. Returns the
    // id to resolve the region with after build().
    u32 add(std::string name, const u8* rgba, u32 width, u32 height);

    [[nodiscard]] TextureAtlas build(rhi::IGraphicsDevice&);

    [[nodiscard]] usize pendingCount() const noexcept { return m_entries.size(); }

private:
    struct Entry {
        std::string     name;
        std::vector<u8> pixels;   // RGBA8, width * height * 4
        u32             width  = 0;
        u32             height = 0;
        u32             id     = 0;
    };

    u32                m_pageSize;
    u32                m_padding;
    std::vector<Entry> m_entries;
};

}
