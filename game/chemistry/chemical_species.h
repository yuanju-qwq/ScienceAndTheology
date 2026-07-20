// Extension boundary for non-periodic chemistry.
//
// Standard elements remain in ElementCatalog and use ElementId forever.
// This interface is intentionally separate for Mod-defined or fictional
// species, nuclear particles, and isotope/nuclide data. It declares the
// ownership boundary now without making mutable extension content part of the
// canonical periodic table.

#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/expected.h"
#include "game/chemistry/element_catalog.h"

namespace snt::game::chemistry {

// A runtime-local ID assigned by a future extension catalog. It is not an
// atomic number and must not be persisted without the extension key.
struct ExtensionSpeciesId {
    uint32_t value = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return is_valid();
    }

    constexpr auto operator<=>(const ExtensionSpeciesId&) const = default;
};

inline constexpr ExtensionSpeciesId kInvalidExtensionSpeciesId{};

enum class ChemicalSpeciesKind : uint8_t {
    // A non-canonical element such as `stargate:naquadah`. A proton count may
    // be supplied for game physics, but it never becomes an ElementId.
    kCustomElement = 0,
    // A free particle such as `core:neutron`; it is not a chemical element.
    kNuclearParticle,
    // An isotope/nuclide such as `core:hydrogen_2`.
    kNuclide,
};

struct ChemicalSpeciesDefinition {
    // Stable, namespaced semantic key, for example `stargate:naquadah`.
    std::string key;
    std::string title_key;
    ChemicalSpeciesKind kind = ChemicalSpeciesKind::kCustomElement;
    // Optional physical metadata. Zero means unmodelled/unknown, never the
    // invalid ElementId sentinel.
    uint16_t proton_count = 0;
    uint16_t neutron_count = 0;
    int16_t electric_charge = 0;
};

// Immutable query view exposed to simulation consumers after an extension
// catalog has completed its own script/Mod reload transaction.
class IChemicalSpeciesCatalog {
public:
    virtual ~IChemicalSpeciesCatalog() = default;

    [[nodiscard]] virtual std::optional<ExtensionSpeciesId> find_id(
        std::string_view key) const noexcept = 0;
    [[nodiscard]] virtual const ChemicalSpeciesDefinition* find(
        ExtensionSpeciesId id) const noexcept = 0;
};

// Mutable authoring boundary. A future game-owned registry will validate
// namespace ownership, normalize keys, and publish an IChemicalSpeciesCatalog
// snapshot only after registration succeeds.
class IChemicalSpeciesRegistrar {
public:
    virtual ~IChemicalSpeciesRegistrar() = default;

    [[nodiscard]] virtual snt::core::Expected<ExtensionSpeciesId> register_species(
        ChemicalSpeciesDefinition definition) = 0;
};

// Future material, recipe, and nuclear-simulation records can use this union
// once non-canonical species participate in authored compositions. Current
// GameMaterialElement intentionally stores ElementId directly while scripts
// only support the immutable standard periodic table.
enum class ChemicalSpeciesReferenceKind : uint8_t {
    kStandardElement = 0,
    kExtension,
};

struct ChemicalSpeciesReference {
    ChemicalSpeciesReferenceKind kind = ChemicalSpeciesReferenceKind::kStandardElement;
    ElementId element{};
    ExtensionSpeciesId extension{};

    [[nodiscard]] static constexpr ChemicalSpeciesReference from_element(
        ElementId value) noexcept {
        return {.kind = ChemicalSpeciesReferenceKind::kStandardElement, .element = value};
    }

    [[nodiscard]] static constexpr ChemicalSpeciesReference from_extension(
        ExtensionSpeciesId value) noexcept {
        return {.kind = ChemicalSpeciesReferenceKind::kExtension, .extension = value};
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return kind == ChemicalSpeciesReferenceKind::kStandardElement
            ? element.is_valid()
            : extension.is_valid();
    }
};

}  // namespace snt::game::chemistry
