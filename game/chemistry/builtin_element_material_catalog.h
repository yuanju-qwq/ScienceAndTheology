// Immutable periodic-table material registration.
//
// The element facts themselves live in ElementCatalog. This boundary supplies
// the corresponding base gameplay materials before AngelScript content loads,
// so scripts can build compounds and alloys from them but cannot redefine
// their canonical material records.

#pragma once

#include <string_view>

#include "core/expected.h"

namespace snt::game {
class GameContentRegistry;
}

namespace snt::game::chemistry {

[[nodiscard]] snt::core::Expected<void> register_builtin_element_materials(
    GameContentRegistry& registry);

// Returns true only for the stable gameplay material IDs owned by the
// immutable standard periodic table.
[[nodiscard]] bool is_builtin_element_material_id(std::string_view id) noexcept;

}  // namespace snt::game::chemistry
