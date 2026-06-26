#pragma once

namespace vortex {
    enum class LogLevel { Trace, Info, Warn, Error };

    void log(LogLevel level, const char* category, const char* fmt, ...);

    #define VORTEX_INFO(cat, ...)  ::vortex::log(::vortex::LogLevel::Info,  cat, __VA_ARGS__)
    #define VORTEX_WARN(cat, ...)  ::vortex::log(::vortex::LogLevel::Warn,  cat, __VA_ARGS__)
    #define VORTEX_ERROR(cat, ...) ::vortex::log(::vortex::LogLevel::Error, cat, __VA_ARGS__)
}