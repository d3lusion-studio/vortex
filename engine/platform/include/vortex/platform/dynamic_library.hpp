#pragma once
#include "vortex/core/types.hpp"

#include <memory>

namespace vortex::pf {

class DynamicLibrary {
public:
    [[nodiscard]] static std::unique_ptr<DynamicLibrary> load(const char* path);

    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&)            = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    [[nodiscard]] void* symbol(const char* name) const;

    template <typename Fn>
    [[nodiscard]] Fn symbolAs(const char* name) const {
        return reinterpret_cast<Fn>(symbol(name));
    }

private:
    explicit DynamicLibrary(void* handle) : m_handle(handle) {}
    void* m_handle = nullptr;
};

// Last-modification time of a file in implementation-defined ticks (0 if the
// file is missing). Suitable for change detection by comparing successive reads.
[[nodiscard]] i64 fileModifiedTime(const char* path);

}
