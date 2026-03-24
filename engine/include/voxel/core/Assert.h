#pragma once

#include "voxel/core/Log.h"

#include <cstdio>
#include <cstdlib>

// VX_ASSERT: Debug-only assertion. Routes failure through spdlog for log file capture. No-op in Release.
// Falls back to stderr if logger is not yet initialized.
#ifdef NDEBUG
#define VX_ASSERT(cond, msg) ((void)0)
#else
#define VX_ASSERT(cond, msg)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            if (::voxel::core::Log::getLogger())                                                                       \
                VX_LOG_CRITICAL("ASSERT FAILED: {}", msg);                                                             \
            else                                                                                                       \
                std::fprintf(stderr, "ASSERT FAILED [%s:%d]: %s\n", __FILE__, __LINE__, (msg));                        \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
#endif

// VX_FATAL: Always active. Logs critical then aborts.
// Falls back to stderr if logger is not yet initialized.
#define VX_FATAL(msg)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::voxel::core::Log::getLogger())                                                                           \
            VX_LOG_CRITICAL("{}", msg);                                                                                \
        else                                                                                                           \
            std::fprintf(stderr, "FATAL [%s:%d]: %s\n", __FILE__, __LINE__, (msg));                                    \
        std::abort();                                                                                                  \
    } while (0)
