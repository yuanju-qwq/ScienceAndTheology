#pragma once

#include <cstdint>

#include "rune_def.hpp"

namespace science_and_theology::magic {

using RuneId = uint8_t;
inline constexpr RuneId kInvalidRuneId = 0xFF;

class RuneRegistry {
public:
    static void initialize();
    // 完全清空 registry（用于热重载），不预留 ID 0。
    static void reset();

    // Register a rune from GDScript. Stores the name string persistently.
    // If explicit_id != kInvalidRuneId, stores at that ID (enables deterministic
    // ID assignment from GD, e.g. element*8+tier+1). Otherwise auto-assigns.
    // Returns the assigned RuneId, or kInvalidRuneId on failure.
    static RuneId register_rune(const RuneDef& def, RuneId explicit_id);

    static const RuneDef* get_by_id(RuneId id);
    static const RuneDef* get_by_name(const char* name);
    static const RuneDef* get(RuneElement element, RuneTier tier);

    static RuneId get_id(RuneElement element, RuneTier tier);
    static size_t count();

private:
    static void register_builtin_runes();
};

} // namespace science_and_theology::magic
