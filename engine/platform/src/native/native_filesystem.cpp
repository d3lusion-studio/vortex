#include "vortex/platform/filesystem.hpp"
#include <filesystem>
#include <fstream>

namespace vortex::pf {

class NativeFileSystem final : public IFileSystem {
public:
    std::vector<std::byte> readFile(const char* path) const override {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return {};
        auto size = f.tellg();
        f.seekg(0);
        std::vector<std::byte> buf(static_cast<std::size_t>(size));
        f.read(reinterpret_cast<char*>(buf.data()), size);
        return buf;
    }

    bool writeFile(const char* path, const void* data, std::size_t size) const override {
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        return f.good();
    }

    bool exists(const char* path) const override {
        return std::filesystem::exists(path);
    }
};

std::unique_ptr<IFileSystem> createFileSystem() {
    return std::make_unique<NativeFileSystem>();
}

}
