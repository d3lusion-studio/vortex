#include "vortex/renderer/atlas.hpp"

#include "vortex/core/log.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>

namespace vortex::renderer {

namespace {

// Shelf packing: images are placed left to right in rows ("shelves") whose height is
// set by the first, and therefore tallest, image on them. Feeding the images in
// descending height order is what keeps the wasted strip under each shelf small.
//
// It is not the tightest packer there is — skyline beats it — but it is a dozen lines
// and it reaches a high fill ratio on the input that matters here, a pile of sprites
// of similar size. Swap it out if the fill ratio ever shows up in a profile.
struct Shelf {
    u32 cursorX = 0;
    u32 cursorY = 0;
    u32 height  = 0;
};

} // namespace

TextureRegion TextureAtlas::region(u32 id) const {
    if (id >= m_regions.size()) return {};
    return m_regions[id];
}

const TextureRegion* TextureAtlas::find(std::string_view name) const {
    for (usize i = 0; i < m_names.size(); ++i)
        if (m_names[i] == name) return &m_regions[i];
    return nullptr;
}

void TextureAtlas::destroy(rhi::IGraphicsDevice& device) {
    for (rhi::TextureHandle page : m_pages) device.destroyTexture(page);
    m_pages.clear();
    m_regions.clear();
    m_names.clear();
}

AtlasBuilder::AtlasBuilder(u32 pageSize, u32 padding)
    : m_pageSize(pageSize), m_padding(padding) {}

u32 AtlasBuilder::add(std::string name, const u8* rgba, u32 width, u32 height) {
    const auto id = static_cast<u32>(m_entries.size());

    Entry entry;
    entry.name   = std::move(name);
    entry.width  = width;
    entry.height = height;
    entry.id     = id;
    if (rgba != nullptr && width > 0u && height > 0u) {
        entry.pixels.resize(static_cast<usize>(width) * height * 4u);
        std::memcpy(entry.pixels.data(), rgba, entry.pixels.size());
    }

    m_entries.push_back(std::move(entry));
    return id;
}

TextureAtlas AtlasBuilder::build(rhi::IGraphicsDevice& device) {
    TextureAtlas atlas;
    atlas.m_regions.resize(m_entries.size());
    atlas.m_names.resize(m_entries.size());
    for (const Entry& e : m_entries) atlas.m_names[e.id] = e.name;

    if (m_entries.empty()) return atlas;

    // Tallest first. Sorting indices rather than the entries keeps `id` meaning
    // "the order you added them in", which is what the caller holds.
    std::vector<u32> order(m_entries.size());
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&](u32 a, u32 b) {
        return m_entries[a].height > m_entries[b].height;
    });

    // Which page each entry landed on. The page textures do not exist until their
    // pixels are complete, so the region's handle can only be filled in at the end;
    // until then this is where the association lives.
    constexpr u32 kUnpacked = ~0u;
    std::vector<u32> pageOf(m_entries.size(), kUnpacked);

    const usize pageBytes = static_cast<usize>(m_pageSize) * m_pageSize * 4u;
    std::vector<u8> page;
    Shelf shelf;
    bool  pageStarted = false;

    // Upload whatever is in `page` as a new atlas page, and start a blank one.
    const auto flushPage = [&] {
        if (!pageStarted) return;
        atlas.m_pages.push_back(device.createTexture(
            {.width = m_pageSize, .height = m_pageSize, .debugName = "atlas_page"},
            page.data()));
        pageStarted = false;
    };

    const auto beginPage = [&] {
        page.assign(pageBytes, 0u);
        shelf       = {};
        pageStarted = true;
    };

    for (u32 index : order) {
        const Entry& e = m_entries[index];
        if (e.pixels.empty()) continue;   // nothing to pack; region stays invalid

        const u32 boxW = e.width  + m_padding * 2u;
        const u32 boxH = e.height + m_padding * 2u;
        if (boxW > m_pageSize || boxH > m_pageSize) {
            VORTEX_WARN("Atlas", "'%s' is %ux%u, larger than a %u page — dropped",
                        e.name.c_str(), e.width, e.height, m_pageSize);
            continue;
        }

        if (!pageStarted) beginPage();

        // Next shelf when this one is full across, next page when the shelves are.
        if (shelf.cursorX + boxW > m_pageSize) {
            shelf.cursorY += shelf.height;
            shelf.cursorX  = 0;
            shelf.height   = 0;
        }
        if (shelf.cursorY + boxH > m_pageSize) {
            flushPage();
            beginPage();
        }
        shelf.height = std::max(shelf.height, boxH);

        // Blit the padded box. Source coordinates are clamped into the image, so the
        // gutter fills with a copy of the nearest edge pixel — that is the extrusion,
        // and it is what stops a linear sampler from bleeding a neighbour's colour in.
        const u32 boxX = shelf.cursorX;
        const u32 boxY = shelf.cursorY;
        for (u32 dy = 0; dy < boxH; ++dy) {
            const u32 sy = std::clamp(static_cast<i32>(dy) - static_cast<i32>(m_padding),
                                      0, static_cast<i32>(e.height) - 1);
            const u8* srcRow = &e.pixels[static_cast<usize>(sy) * e.width * 4u];
            u8* dstRow = &page[(static_cast<usize>(boxY + dy) * m_pageSize + boxX) * 4u];
            for (u32 dx = 0; dx < boxW; ++dx) {
                const u32 sx = std::clamp(static_cast<i32>(dx) - static_cast<i32>(m_padding),
                                          0, static_cast<i32>(e.width) - 1);
                std::memcpy(dstRow + static_cast<usize>(dx) * 4u,
                            srcRow + static_cast<usize>(sx) * 4u, 4u);
            }
        }

        // The region names the image itself, not its gutter.
        pageOf[e.id] = static_cast<u32>(atlas.m_pages.size());
        atlas.m_regions[e.id] = TextureRegion{
            .texture = {},   // filled in below, once the page it sits on exists
            .uv = pixelsToUV({static_cast<f32>(boxX + m_padding),
                              static_cast<f32>(boxY + m_padding),
                              static_cast<f32>(e.width), static_cast<f32>(e.height)},
                             m_pageSize, m_pageSize),
            .sizePixels = {static_cast<f32>(e.width), static_cast<f32>(e.height)},
        };

        shelf.cursorX = boxX + boxW;
    }

    flushPage();

    for (usize i = 0; i < atlas.m_regions.size(); ++i) {
        const u32 pageIndex = pageOf[i];
        if (pageIndex < atlas.m_pages.size())
            atlas.m_regions[i].texture = atlas.m_pages[pageIndex];
    }

    VORTEX_INFO("Atlas", "packed %zu images into %zu page(s) of %ux%u",
                m_entries.size(), atlas.m_pages.size(), m_pageSize, m_pageSize);
    return atlas;
}

}
