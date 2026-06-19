#pragma once

// ============================================================
// GT V3 Multiblock System — Declarative Layer
// ============================================================
//
// Ported from GregTech CEu's V3 StructureDefinition +
// DeclarativePatternBuilder.
//
// StructureDefinition:
//   - Immutable, cached by key (e.g. "furnace", "coke_oven")
//   - Holds a PieceTemplate (the compiled pattern)
//   - Shared across all controllers of the same machine type
//   - getOrBuild() provides a global cache with weak_ptr semantics
//
// DeclarativePatternBuilder:
//   - Fluent .aisle() / .where() API (the user-facing entry point)
//   - Wraps PieceTemplateCompiler
//   - Convenience methods: .self(), .air(), .material(), .hatch(), .any()
//
// Typical usage:
//
//   auto def = DeclarativePatternBuilder::start()
//       .aisle({"XXX", "XXX", "XXX"})
//       .aisle({"XXX", "X#X", "XXX"})
//       .aisle({"XXX", "X~X", "XXX"})
//       .self('Y')          // or use '~' in the aisle
//       .air('#')
//       .material('X', 5)   // casing material
//       .build_structure_definition("furnace");
//
//   // Later, check formation:
//   StructureRuntime runtime(def);
//   auto result = runtime.check(world, registry, controller_id, ...);

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "piece_template.hpp"
#include "structure_element.hpp"
#include "../world/terrain_data.hpp"

namespace science_and_theology::multiblock {

// ------------------------------------------------------------
// StructureDefinition — immutable, cached structure template.
//
// Each multiblock machine type has one StructureDefinition.
// The definition is shared across all instances of that machine
// type. It is immutable after construction.
//
// GT V3 equivalent: StructureDefinition<T>
// (Our version drops the generic controller type parameter T
//  since our controllers are data in MachineBlockEntityState,
//  not polymorphic objects.)
// ------------------------------------------------------------
class StructureDefinition {
public:
    // Get or build a cached definition by key.
    // If a definition with this key exists in the cache, return it.
    // Otherwise, call factory() to create a new one, cache it, and return.
    static std::shared_ptr<StructureDefinition> get_or_build(
        const std::string& key,
        std::function<std::shared_ptr<StructureDefinition>()> factory);

    // Build from a single piece template (the common case).
    static std::shared_ptr<StructureDefinition> from_template(
        const std::string& key, PieceTemplate tpl);

    // Build from a single piece template with a custom factory key.
    static std::shared_ptr<StructureDefinition> from_template(
        PieceTemplate tpl);

    const std::string& key() const { return key_; }
    const PieceTemplate& primary_template() const { return primary_template_; }

    // Whether this definition has a non-trivial structure to check.
    // Trivial (1x1x1 controller-only) definitions always pass formation.
    bool is_trivial() const { return primary_template_.is_trivial(); }

    // Clear the global definition cache (for testing / hot-reload).
    static void clear_cache();

private:
    StructureDefinition(std::string key, PieceTemplate tpl)
        : key_(std::move(key)), primary_template_(std::move(tpl)) {}

    std::string key_;
    PieceTemplate primary_template_;

    static std::unordered_map<std::string, std::weak_ptr<StructureDefinition>> cache_;
    static std::mutex cache_mutex_;
};

// ------------------------------------------------------------
// DeclarativePatternBuilder — fluent .aisle() API.
//
// Mirrors GT V3's DeclarativePatternBuilder. This is the primary
// user-facing API for defining multiblock structures.
//
// Usage:
//   auto def = DeclarativePatternBuilder::start()
//       .aisle({"XXX", "XXX", "XXX"})
//       .aisle({"XXX", "X#X", "XXX"})
//       .aisle({"XXX", "X~X", "XXX"})
//       .where('X', Elements::material(5))
//       .where('#', Elements::air())
//       .build_structure_definition("my_machine");
// ------------------------------------------------------------
class DeclarativePatternBuilder {
public:
    static DeclarativePatternBuilder start();

    // Add an aisle (z-slice). Each string is a row (y, bottom-to-top).
    // This is the primary pattern definition method.
    DeclarativePatternBuilder& aisle(const std::vector<std::string>& rows);

    // Register a symbol → element mapping.
    DeclarativePatternBuilder& where(char symbol,
                                      std::shared_ptr<IStructureElement> element);

    // --- Convenience methods (sugar for common element types) ---

    // Register a symbol as the controller center.
    // Also marks '~' as self() in the symbol map.
    DeclarativePatternBuilder& self(char symbol);

    // Register a symbol as air (must be empty).
    // Also registers '#' as air() in the symbol map.
    DeclarativePatternBuilder& air(char symbol);

    // Register a symbol as a specific terrain material.
    DeclarativePatternBuilder& material(char symbol, TerrainMaterialId mat);

    // Register a symbol as a hatch slot.
    DeclarativePatternBuilder& hatch(char symbol, uint16_t type_mask = 0xFFFF);

    // Register a symbol as a wildcard (matches anything).
    DeclarativePatternBuilder& any(char symbol);

    // Register a symbol as a chain (try multiple alternatives).
    DeclarativePatternBuilder& chain(char symbol,
        std::vector<std::shared_ptr<IStructureElement>> elements);

    // --- Build methods ---

    // Build the compiled PieceTemplate.
    PieceTemplate build_template();

    // Build and cache a StructureDefinition with the given key.
    std::shared_ptr<StructureDefinition> build_structure_definition(
        const std::string& key);

private:
    PieceTemplateCompiler compiler_;
};

} // namespace science_and_theology::multiblock
