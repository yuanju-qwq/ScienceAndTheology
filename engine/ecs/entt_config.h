// EnTT configuration wrapper.
//
// EnTT's config/config.h checks if ENTT_ASSERT is user-defined. If not,
// it falls back to `#include <cassert>` + `assert(condition)`, which
// triggers MSVC C3861 ("assert": identifier not found) in certain
// template-instantiation contexts (mixed C/C++ headers, volk.h SDL3
// interaction). By defining ENTT_ASSERT *before* any EnTT header is
// parsed, we route EnTT assertions through our own SNT_LOG_FATAL +
// SNT_DEBUGBREAK path.
//
// Rule: every file that needs EnTT MUST include this wrapper instead of
// <entt/entt.hpp> directly, so the assertion hook is always installed
// before EnTT's config.h runs.

#pragma once

// GLM and other third-party headers use `assert(...)` directly; keep
// <cassert> included here so any TU that pulls EnTT (which most do via
// World / RenderSystem) also sees the assert macro.
#include <cassert>

#include "core/assert.h"  // SNT_DEBUGBREAK
#include "core/log.h"     // SNT_LOG_FATAL

#include <cstdlib>  // std::abort

#ifndef ENTT_ASSERT
#  define ENTT_ASSERT(condition, msg)                                        \
      do {                                                                  \
          if (!(condition)) {                                              \
              SNT_LOG_FATAL("EnTT assertion failed: %s", #condition);       \
              SNT_LOG_FATAL("  %s", msg);                                  \
              SNT_DEBUGBREAK();                                             \
              std::abort();                                                 \
          }                                                                 \
      } while (0)
#endif

#include <entt/entt.hpp>
