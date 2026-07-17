#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace vortex::pf {

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    [[nodiscard]] virtual std::vector<std::byte> readFile(const char* path)  const = 0;
    [[nodiscard]] virtual bool writeFile(const char* path, const void* data,
                                         std::size_t size) const = 0;
    [[nodiscard]] virtual bool exists(const char* path) const = 0;
};

[[nodiscard]] std::unique_ptr<IFileSystem> createFileSystem();

// The directory the running executable is in.
//
// This is how a shipped game finds its own data. A build-time path baked in by CMake works
// on the machine that built it and nowhere else — copy the binary to a second machine and
// its assets are still on the first one. Empty if the platform cannot answer.
//
// Not the working directory: that is wherever the user happened to launch from, which is
// the desktop as often as not.
[[nodiscard]] std::string executableDir();

}
