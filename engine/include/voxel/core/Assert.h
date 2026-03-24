#pragma once

#include <cstdio>
#include <cstdlib>

// VX_ASSERT: Debug-only assertion. No-op in Release.
#ifdef NDEBUG
#define VX_ASSERT(cond, msg) ((void)0)
#else
#define VX_ASSERT(cond, msg)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "ASSERT FAILED [%s:%d]: %s\n", __FILE__, __LINE__, (msg));                            \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
#endif

// VX_FATAL: Always active. Logs to stderr and aborts.
#define VX_FATAL(msg)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        std::fprintf(stderr, "FATAL [%s:%d]: %s\n", __FILE__, __LINE__, (msg));                                        \
        std::abort();                                                                                                  \
    } while (0)
