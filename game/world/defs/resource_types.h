// Resource type aliases — universal identifiers for items and fluids.
//
// Ported from src/core/common/resource_types.hpp.
// Namespace: science_and_theology::gt -> snt::game (gt sub-namespace merged
// into snt::game per P2 decision).

#pragma once

#include <cstdint>

namespace snt::game {

// ============================================================
// ItemId — universal item identifier
// ============================================================

using ItemId = uint32_t;
inline constexpr ItemId kInvalidItemId = 0;

// ============================================================
// FluidId — universal fluid identifier
// ============================================================

using FluidId = uint16_t;
inline constexpr FluidId kInvalidFluidId = 0xFFFF;

} // namespace snt::game
