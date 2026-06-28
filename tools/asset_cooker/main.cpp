#include "vortex/asset/cooked_texture.hpp"
#include "vortex/asset/image.hpp"
#include "vortex/core/log.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace vortex;

namespace {

std::vector<std::byte> readFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto size = f.tellg();
    std::vector<std::byte> buf(static_cast<usize>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

bool writeFile(const fs::path& path, const std::vector<std::byte>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return f.good();
}

bool cookTexture(const fs::path& input, const fs::path& out) {
    const std::vector<std::byte> raw = readFile(input);
    if (raw.empty()) {
        VORTEX_ERROR("Cooker", "cannot read '%s'", input.string().c_str());
        return false;
    }

    const assets::Image image = assets::decodeImage(raw.data(), raw.size());
    if (!image.valid()) {
        VORTEX_ERROR("Cooker", "failed to decode image '%s'", input.string().c_str());
        return false;
    }

    const std::vector<std::byte> cooked = assets::encodeCookedTexture(image);
    if (cooked.empty()) {
        VORTEX_ERROR("Cooker", "failed to encode '%s'", input.string().c_str());
        return false;
    }

    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    if (!writeFile(out, cooked)) {
        VORTEX_ERROR("Cooker", "cannot write '%s'", out.string().c_str());
        return false;
    }

    VORTEX_INFO("Cooker", "%s -> %s (%ux%u, %zu bytes)", input.filename().string().c_str(),
                out.filename().string().c_str(), image.width, image.height, cooked.size());
    return true;
}

bool isPng(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".png";
}

}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <input.png|input_dir> <output_dir>\n", argv[0]);
        return 2;
    }

    const fs::path input{argv[1]};
    const fs::path outDir{argv[2]};

    std::error_code ec;
    if (!fs::exists(input, ec)) {
        VORTEX_ERROR("Cooker", "input does not exist: %s", input.string().c_str());
        return 2;
    }

    u32 ok = 0, failed = 0;
    if (fs::is_directory(input, ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(input)) {
            if (!entry.is_regular_file() || !isPng(entry.path())) continue;
            // Mirror the source tree under outDir so files sharing a stem in
            // different subdirectories don't overwrite each other.
            fs::path rel = fs::relative(entry.path(), input, ec);
            rel.replace_extension(".vtex");
            (cookTexture(entry.path(), outDir / rel) ? ok : failed)++;
        }
    } else {
        if (!isPng(input)) {
            VORTEX_ERROR("Cooker", "unsupported input type: %s", input.string().c_str());
            return 2;
        }
        (cookTexture(input, outDir / (input.stem().string() + ".vtex")) ? ok : failed)++;
    }

    VORTEX_INFO("Cooker", "done: %u cooked, %u failed", ok, failed);
    return failed == 0 ? 0 : 1;
}
