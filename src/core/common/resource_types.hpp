#pragma once

#include <cstdint>

namespace science_and_theology::gt {

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

} // namespace science_and_theology::gt