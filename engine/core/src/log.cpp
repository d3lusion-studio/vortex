#include "vortex/core/log.hpp"
#include <cstdarg>
#include <cstdio>

namespace vortex {
    static const char* levelTag(LogLevel l) {
        switch (l) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
        }
        return "?????";
    }

    void log(LogLevel level, const char* category, const char* fmt, ...) {
        std::fprintf(stderr, "[%s][%s] ", levelTag(level), category);
        va_list args;
        va_start(args, fmt);
        std::vfprintf(stderr, fmt, args);
        va_end(args);
        std::fputc('\n', stderr);
    }
}
