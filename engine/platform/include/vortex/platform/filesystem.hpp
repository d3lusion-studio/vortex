#pragma once
#include <cstddef>
#include <memory>
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

}
