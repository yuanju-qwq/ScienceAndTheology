#pragma once

// ============================================================
// GT V3 Multiblock System — Bottom Layer: Core Matching Engine
// ============================================================
//
// Ported from GregTech CEu's V3 structure system (see
// ../../../../GregTech/docs/structure-system-v3-design.md),
// skipping all deprecated/legacy compatibility layers.
//
// This file defines:
//   - IStructureElement: the core interface for pattern cell matchers
//   - Elements: factory for built-in element types
//   - StructureEvaluationContext: typed context passed to each cell check
//   - StructureOperation / MatchResult / MatchCheckpoint: supporting types
//
// Architecture (bottom → top):
//   structure_element.hpp   (this file) — core matching engine
//   piece_template.hpp      — 3D element array + compiler
//   structure_definition.hpp — immutable definition + DeclarativePatternBuilder
//   structure_runtime.hpp   — per-controller runtime + check results
//   multiblock_controller.hpp — controller adapter

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../world/terrain_data.hpp"
#include "../world/entity_data.hpp"

namespace science_and_theology {

// Forward declarations to keep headers light.
class WorldData;
class BlockEntityRegistry;

namespace multiblock {

// Forward declaration of PieceTemplateCompiler (defined in piece_template.hpp).
class PieceTemplateCompiler;

// ------------------------------------------------------------
// Hatch type bits — used by Elements::hatch() to select which
// hatch kinds a slot accepts. Mirrors GT V3's MultiblockAbility.
// ------------------------------------------------------------
enum MultiblockHatchType : uint16_t {
    HATCH_ITEM_INPUT    = 1u << 0,
    HATCH_ITEM_OUTPUT   = 1u << 1,
    HATCH_FLUID_INPUT   = 1u << 2,
    HATCH_FLUID_OUTPUT  = 1u << 3,
    HATCH_ENERGY_INPUT  = 1u << 4,
    HATCH_ENERGY_OUTPUT = 1u << 5,
    HATCH_ANY           = 0xFFFFu,  // accepts any hatch type
};

// ------------------------------------------------------------
// Operation mode — what the structure system is currently doing.
// Mirrors GT V3's StructureEvaluationContext.Operation enum.
// ------------------------------------------------------------
enum class StructureOperation : uint8_t {
    MATCH_WORLD,     // Check against live world blocks
    MATCH_SNAPSHOT,  // Check against a cached snapshot (async)
    PREVIEW,         // Render preview ghosts
    HINT,            // Render structure hints
    CREATIVE_BUILD,  // Auto-build in creative mode
    SURVIVAL_BUILD,  // Auto-build in survival mode
    ITERATE,         // Iterate over all cells (for export/debug)
};

// ------------------------------------------------------------
// Match result — returned by IStructureElement::check().
// Mirrors GT V3's IStructureElement.PlaceResult (simplified).
// ------------------------------------------------------------
enum class MatchResult : uint8_t {
    REJECT,          // Cell doesn't match — abort formation
    ACCEPT,          // Cell matches — continue to next cell
    SKIP,            // Skip this cell (e.g. ANY in ITERATE mode)
    ACCEPT_STOP,     // Cell matches and this is the last needed cell
};

// ------------------------------------------------------------
// Checkpoint — snapshot of match collector state for backtracking.
// Used by chain() elements and repeat groups to try alternatives.
// ------------------------------------------------------------
struct MatchCheckpoint {
    size_t collector_parts_size = 0;
    size_t collector_hatches_size = 0;
};

// ------------------------------------------------------------
// MatchCollector — accumulates matched parts and hatches during
// a single check pass. Owned by StructureEvaluationContext.
// ------------------------------------------------------------
struct MatchCollector {
    std::vector<EntityId> matched_parts;       // non-controller claimed cells
    std::vector<EntityId> matched_hatches;     // hatch entities
    std::unordered_map<std::string, int> channel_values;  // repeat counts

    MatchCheckpoint checkpoint() const {
        return {matched_parts.size(), matched_hatches.size()};
    }

    void restore(const MatchCheckpoint& cp) {
        matched_parts.resize(cp.collector_parts_size);
        matched_hatches.resize(cp.collector_hatches_size);
        // channel_values are not rolled back (they're set by repeat groups)
    }

    void clear() {
        matched_parts.clear();
        matched_hatches.clear();
        channel_values.clear();
    }
};

// ------------------------------------------------------------
// StructureEvaluationContext — passed to each element's check().
//
// Replaces GT V3's StructureEvaluationContext<T>. In our system
// the "controller" is a MachineBlockEntityState in the registry,
// identified by EntityId. The context carries:
//   - what operation is being performed
//   - world + registry access for querying blocks/entities
//   - the current cell's world coordinates
//   - the controller's identity (for self() matching)
//   - a match collector for accumulating results
// ------------------------------------------------------------
class StructureEvaluationContext {
public:
    StructureOperation operation = StructureOperation::MATCH_WORLD;

    // World access (non-owning). Null in ITERATE mode without world.
    const WorldData* world = nullptr;
    const BlockEntityRegistry* registry = nullptr;

    // Current cell being evaluated (world coordinates).
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    // Controller identity.
    std::string dimension_id;
    EntityId controller_id;  // EntityId of the controller machine

    // Match result accumulator. Must outlive the context.
    MatchCollector* collector = nullptr;

    // --- Checkpoint / restore for backtracking ---
    MatchCheckpoint checkpoint() const {
        return collector ? collector->checkpoint() : MatchCheckpoint{};
    }

    void restore(const MatchCheckpoint& cp) {
        if (collector) collector->restore(cp);
    }
};

// ------------------------------------------------------------
// IStructureElement — the core interface for pattern cell matchers.
//
// Each symbol in a pattern maps to an IStructureElement. During
// formation checking, the element's check() is called with a
// context positioned at the corresponding world cell.
//
// This is the C++ equivalent of GT V3's IStructureElement<T>.
// The template parameter T (controller type) is replaced by
// EntityId + registry lookup, since our controllers are data
// in MachineBlockEntityState rather than polymorphic objects.
// ------------------------------------------------------------
class IStructureElement {
public:
    virtual ~IStructureElement() = default;

    // Check if this element matches the cell at ctx.x/y/z.
    // Called during MATCH_WORLD / MATCH_SNAPSHOT operations.
    // Should update ctx.collector if the cell is a part or hatch.
    virtual MatchResult check(StructureEvaluationContext& ctx) const = 0;

    // Whether this element marks the controller center.
    // Used by PieceTemplate to compute the controller offset.
    // GT V3 uses isCenter() for the same purpose.
    virtual bool is_center() const { return false; }

    // Register this element under a symbol in the compiler.
    // Default implementation calls compiler.where(symbol, shared_from_this).
    // Elements may override to register additional symbols or casing slots.
    virtual void apply_to(char symbol, PieceTemplateCompiler& compiler) const;

    // Human-readable description for debug/hint rendering.
    virtual std::string describe() const = 0;
};

// ------------------------------------------------------------
// Elements — factory for built-in element types.
//
// Mirrors GT V3's gregtech.api.pattern.element.Elements class.
// Returns shared_ptr because elements are shared across patterns
// and may be referenced by multiple PieceTemplates.
// ------------------------------------------------------------
namespace Elements {

// Match a specific terrain material at the cell.
// GT V3 equivalent: Elements.block(IBlockState)
std::shared_ptr<IStructureElement> material(TerrainMaterialId mat);

// Match air (material == 0 / no solid block).
// GT V3 equivalent: Elements.air()
std::shared_ptr<IStructureElement> air();

// Match anything — always accepts.
// GT V3 equivalent: Elements.any()
std::shared_ptr<IStructureElement> any();

// Match the controller itself (the machine entity at the center).
// GT V3 equivalent: Elements.self(Class)
std::shared_ptr<IStructureElement> self();

// Match a hatch — a MACHINE block entity that is not the controller.
// type_mask: bitmask of MultiblockHatchType values (0xFFFF = any hatch).
// GT V3 equivalent: Elements.hatch(MultiblockAbility) / Elements.abilities(...)
std::shared_ptr<IStructureElement> hatch(uint16_t type_mask = 0xFFFF);

// Chain — try each element in order, accept the first that matches.
// Used for "this slot accepts material X OR a hatch of type Y".
// GT V3 equivalent: Elements.chain(IStructureElement...)
std::shared_ptr<IStructureElement> chain(
    std::vector<std::shared_ptr<IStructureElement>> elements);

// Lazy — defer element construction until first use.
// Useful for breaking circular dependencies (e.g. controller referencing
// its own structure definition).
// GT V3 equivalent: Elements.lazy(Supplier)
std::shared_ptr<IStructureElement> lazy(
    std::function<std::shared_ptr<IStructureElement>()> factory);

} // namespace Elements

} // namespace multiblock
} // namespace science_and_theology
