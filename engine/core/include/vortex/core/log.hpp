#pragma once

namespace vortex {
    enum class LogLevel { Trace, Info, Warn, Error };

#if defined(__GNUC__) || defined(__clang__)
    #define VORTEX_PRINTF_FORMAT(fmt_idx, args_idx) \
        __attribute__((format(printf, fmt_idx, args_idx)))
#else
    #define VORTEX_PRINTF_FORMAT(fmt_idx, args_idx)
#endif

    void log(LogLevel level, const char* category, const char* fmt, ...)
        VORTEX_PRINTF_FORMAT(3, 4);

    #define VORTEX_INFO(cat, ...)  ::vortex::log(::vortex::LogLevel::Info,  cat, __VA_ARGS__)
    #define VORTEX_WARN(cat, ...)  ::vortex::log(::vortex::LogLevel::Warn,  cat, __VA_ARGS__)
    #define VORTEX_ERROR(cat, ...) ::vortex::log(::vortex::LogLevel::Error, cat, __VA_ARGS__)
}