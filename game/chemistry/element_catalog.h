// Immutable canonical periodic-table catalog.
//
// This module owns scientific facts that must not vary with save data,
// scripts, or Mods. `ElementId` is deliberately the atomic number: 0 is
// invalid and 1 through 118 name the IUPAC-recognized elements.

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace snt::game::chemistry {

inline constexpr uint8_t kElementCount = 118;

// A compact strong identifier for a canonical chemical element. It never
// represents a fictional element, isotope, ion, or free nuclear particle.
struct ElementId {
    uint8_t atomic_number = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return atomic_number >= 1 && atomic_number <= kElementCount;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_valid();
    }

    constexpr auto operator<=>(const ElementId&) const = default;
};

inline constexpr ElementId kInvalidElementId{};

enum class ElementCategory : uint8_t {
    kUnknown = 0,
    kAlkaliMetal,
    kAlkalineEarthMetal,
    kTransitionMetal,
    kPostTransitionMetal,
    kMetalloid,
    kNonmetal,
    kHalogen,
    kNobleGas,
    kLanthanide,
    kActinide,
};

struct ElementDefinition {
    ElementId id;
    std::string_view symbol;
    std::string_view canonical_name;
    uint8_t period = 0;
    // Group 0 denotes the detached f-block placement used by elements 58-71
    // and 90-103. Lanthanum and actinium retain group 3.
    uint8_t group = 0;
    ElementCategory category = ElementCategory::kUnknown;
    double relative_atomic_mass = 0.0;
};

// Provides allocation-free lookup over the fixed canonical table. Symbol
// matching is exact and case-sensitive so script content must use `Fe`, not
// `fe` or `FE`.
class ElementCatalog final {
public:
    [[nodiscard]] static const ElementDefinition* find(ElementId id) noexcept;
    [[nodiscard]] static const ElementDefinition* find_by_symbol(
        std::string_view symbol) noexcept;
    [[nodiscard]] static std::span<const ElementDefinition> all() noexcept;
};

}  // namespace snt::game::chemistry
