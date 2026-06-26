#pragma once
#include "vortex/core/log.hpp"
#include <cstdlib>

#if defined(VORTEX_ENABLE_ASSERTS)
    #define VORTEX_ASSERT(cond, msg)                                          \
        do {                                                                  \
            if (!(cond)) {                                                    \
                ::vortex::log(::vortex::LogLevel::Error, "Assert",            \
                    "%s:%d  (%s)  %s", __FILE__, __LINE__, #cond, msg);       \
                std::abort();                                                 \
            }                                                                 \
        } while (0)
#else
    #define VORTEX_ASSERT(cond, msg) ((void)0)
#endif
