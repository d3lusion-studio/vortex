#include "vortex/platform/dynamic_library.hpp"

#include "vortex/core/log.hpp"

#include <filesystem>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace vortex::pf {

std::unique_ptr<DynamicLibrary> DynamicLibrary::load(const char* path) {
#if defined(_WIN32)
    void* handle = static_cast<void*>(::LoadLibraryA(path));
    if (!handle) {
        VORTEX_ERROR("DynLib", "LoadLibrary failed for '%s' (err %lu)", path, ::GetLastError());
        return nullptr;
    }
#else
    // RTLD_NOW surfaces unresolved symbols at load time rather than first call.
    void* handle = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        VORTEX_ERROR("DynLib", "dlopen failed for '%s': %s", path, ::dlerror());
        return nullptr;
    }
#endif
    return std::unique_ptr<DynamicLibrary>(new DynamicLibrary(handle));
}

DynamicLibrary::~DynamicLibrary() {
    if (!m_handle) return;
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(m_handle));
#else
    ::dlclose(m_handle);
#endif
}

void* DynamicLibrary::symbol(const char* name) const {
    if (!m_handle) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(m_handle), name));
#else
    return ::dlsym(m_handle, name);
#endif
}

i64 fileModifiedTime(const char* path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<i64>(t.time_since_epoch().count());
}

}
