#include "vortex/platform/filesystem.hpp"
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

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


std::string executableDir() {
    std::error_code ec;

#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return {};
    const std::filesystem::path exe(buf, buf + n);
#elif defined(__APPLE__)
    u32 size = 0;
    _NSGetExecutablePath(nullptr, &size);   // asks for the length
    std::string raw(size, '\0');
    if (_NSGetExecutablePath(raw.data(), &size) != 0) return {};
    const std::filesystem::path exe = std::filesystem::canonical(raw, ec);
    if (ec) return {};
#else
    // /proc/self/exe is a symlink to the binary, and reading it works even when argv[0] is
    // a bare name found on PATH — which is exactly when a game most needs the answer.
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
#endif

    return exe.parent_path().string();
}

}