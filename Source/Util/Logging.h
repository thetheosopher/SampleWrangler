#pragma once

/// Lightweight logging macros.
///
/// In Debug builds these write to std::cerr.
/// In Release builds they compile to nothing.
/// NEVER use these from the audio callback — RT-safe constraints apply.

#include <iostream>
#include <string>

namespace sw
{

#ifdef NDEBUG
#define SW_LOG(msg) ((void)0)
#define SW_LOG_WARN(msg) ((void)0)
#define SW_LOG_ERR(msg) ((void)0)
#else
#define SW_LOG(msg)                          \
    do                                       \
    {                                        \
        std::cerr << "[SW] " << msg << "\n"; \
    } while (false)
#define SW_LOG_WARN(msg)                          \
    do                                            \
    {                                             \
        std::cerr << "[SW WARN] " << msg << "\n"; \
    } while (false)
#define SW_LOG_ERR(msg)                            \
    do                                             \
    {                                              \
        std::cerr << "[SW ERROR] " << msg << "\n"; \
    } while (false)
#endif

} // namespace sw
