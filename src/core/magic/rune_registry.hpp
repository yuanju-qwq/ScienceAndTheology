#pragma once

#include <cstdint>

#include "rune_def.hpp"

namespace science_and_theology::magic {

using RuneId = uint8_t;
inline constexpr RuneId kInvalidRuneId = 0xFF;

class RuneRegistry {
public:
    static void initialize();

    // Register a rune from GDScript. Stores the name string persistently.
    // Returns the assigned RuneId, or kInvalidRuneId on failure.
    static RuneId register_rune(const RuneDef& def);

    static const RuneDef* get_by_id(RuneId id);
    static const RuneDef* get_by_name(const char* name);
    static const RuneDef* get(RuneElement element, RuneTier tier);

    static RuneId get_id(RuneElement element, RuneTier tier);
    static size_t count();

private:
    static void register_builtin_runes();
};

} // namespace science_and_theology::magic
