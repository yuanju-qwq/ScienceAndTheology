#include "structure_element.hpp"
#include "piece_template.hpp"

#include <cmath>
#include <sstream>

#include "../world/world_data.hpp"
#include "../world/block_entity_registry.hpp"
#include "../world/chunk_data.hpp"

namespace science_and_theology::multiblock {

// ============================================================
// Internal helpers
// ============================================================

namespace {

// Query terrain material at world block coordinates.
// Returns 0 (air) for unloaded chunks or out-of-range cells.
TerrainMaterialId query_material_at(const WorldData& world,
                                     const std::string& dimension_id,
                                     int bx, int by, int bz) {
    constexpr int kChunkSize = ChunkData::kChunkSize;  // 32

    int cx = static_cast<int>(std::floor(static_cast<float>(bx) / kChunkSize));
    int cy = static_cast<int>(std::floor(static_cast<float>(by) / kChunkSize));
    int cz = static_cast<int>(std::floor(static_cast<float>(bz) / kChunkSize));

    const ChunkData* chunk = world.get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return 0;

    int lx = bx - cx * kChunkSize;
    int ly = by - cy * kChunkSize;
    int lz = bz - cz * kChunkSize;

    const TerrainData& terrain = chunk->terrain;
    if (!terrain.is_valid_cell(lx, ly, lz)) return 0;
    return terrain.cell_at(lx, ly, lz).material;
}

} // anonymous namespace

// ============================================================
// IStructureElement — default apply_to implementation
// ============================================================

void IStructureElement::apply_to(char symbol,
                                  PieceTemplateCompiler& compiler) const {
    // The element registers itself under the given symbol.
    // shared_from_this would be ideal, but IStructureElement doesn't
    // inherit from enable_shared_from_this. Instead, the caller
    // (DeclarativePatternBuilder) handles registration directly.
    (void)symbol;
    (void)compiler;
    // Base implementation is a no-op. Concrete elements don't need
    // to override this unless they register multiple symbols.
}

// ============================================================
// Built-in element types
// ============================================================

namespace {

// --- MaterialElement: match a specific terrain material ---
class MaterialElement final : public IStructureElement {
public:
    explicit MaterialElement(TerrainMaterialId mat) : mat_(mat) {}

    MatchResult check(StructureEvaluationContext& ctx) const override {
        if (ctx.world == nullptr) return MatchResult::REJECT;
        TerrainMaterialId m = query_material_at(
            *ctx.world, ctx.dimension_id, ctx.x, ctx.y, ctx.z);
        if (m != mat_) return MatchResult::REJECT;
        // The runtime records the world position as a claimed cell.
        return MatchResult::ACCEPT;
    }

    std::string describe() const override {
        std::ostringstream ss;
        ss << "material(" << static_cast<int>(mat_) << ")";
        return ss.str();
    }

private:
    TerrainMaterialId mat_;
};

// --- AirElement: match empty/non-solid cell ---
class AirElement final : public IStructureElement {
public:
    MatchResult check(StructureEvaluationContext& ctx) const override {
        if (ctx.world == nullptr) return MatchResult::REJECT;
        TerrainMaterialId m = query_material_at(
            *ctx.world, ctx.dimension_id, ctx.x, ctx.y, ctx.z);
        if (m != 0) return MatchResult::REJECT;
        return MatchResult::ACCEPT;
    }

    std::string describe() const override { return "air()"; }
};

// --- AnyElement: wildcard, matches anything ---
class AnyElement final : public IStructureElement {
public:
    MatchResult check(StructureEvaluationContext& /*ctx*/) const override {
        return MatchResult::ACCEPT;
    }

    std::string describe() const override { return "any()"; }
};

// --- SelfElement: the controller itself (marks center) ---
class SelfElement final : public IStructureElement {
public:
    MatchResult check(StructureEvaluationContext& ctx) const override {
        // The controller cell always matches (the controller exists
        // at its own position by definition). The runtime skips
        // recording this as a claimed cell (is_center() == true).
        (void)ctx;
        return MatchResult::ACCEPT;
    }

    bool is_center() const override { return true; }

    std::string describe() const override { return "self()"; }
};

// --- HatchElement: a hatch machine entity (not the controller) ---
class HatchElement final : public IStructureElement {
public:
    explicit HatchElement(uint16_t type_mask) : type_mask_(type_mask) {}

    MatchResult check(StructureEvaluationContext& ctx) const override {
        if (ctx.registry == nullptr) return MatchResult::REJECT;
        EntityId owner = ctx.registry->find_owner_at(ctx.x, ctx.y, ctx.z);
        if (!owner.is_valid() || owner == ctx.controller_id) {
            return MatchResult::REJECT;
        }
        // Must be a MACHINE entity to count as a hatch.
        if (ctx.registry->get_entity_type(owner) != BlockEntityType::MACHINE) {
            return MatchResult::REJECT;
        }
        // TODO: check hatch type_mask against the hatch's actual type
        // once hatch subtypes are implemented. For now, any MACHINE
        // entity at a non-controller cell counts as a hatch.
        (void)type_mask_;
        if (ctx.collector) {
            ctx.collector->matched_hatches.push_back(owner);
        }
        return MatchResult::ACCEPT;
    }

    std::string describe() const override {
        std::ostringstream ss;
        ss << "hatch(mask=0x" << std::hex << type_mask_ << ")";
        return ss.str();
    }

private:
    uint16_t type_mask_;
};

// --- ChainElement: try each element in order, accept first match ---
class ChainElement final : public IStructureElement {
public:
    explicit ChainElement(std::vector<std::shared_ptr<IStructureElement>> elems)
        : elems_(std::move(elems)) {}

    MatchResult check(StructureEvaluationContext& ctx) const override {
        // Save collector state for backtracking.
        MatchCheckpoint cp = ctx.checkpoint();
        for (const auto& elem : elems_) {
            // Restore collector state before trying next alternative.
            ctx.restore(cp);
            MatchResult r = elem->check(ctx);
            if (r == MatchResult::ACCEPT || r == MatchResult::ACCEPT_STOP) {
                return r;
            }
        }
        // All alternatives rejected; restore to pre-chain state.
        ctx.restore(cp);
        return MatchResult::REJECT;
    }

    bool is_center() const override {
        for (const auto& elem : elems_) {
            if (elem->is_center()) return true;
        }
        return false;
    }

    std::string describe() const override {
        std::ostringstream ss;
        ss << "chain(";
        for (size_t i = 0; i < elems_.size(); ++i) {
            if (i > 0) ss << " | ";
            ss << elems_[i]->describe();
        }
        ss << ")";
        return ss.str();
    }

private:
    std::vector<std::shared_ptr<IStructureElement>> elems_;
};

// --- LazyElement: defer construction until first use ---
class LazyElement final : public IStructureElement {
public:
    explicit LazyElement(std::function<std::shared_ptr<IStructureElement>()> factory)
        : factory_(std::move(factory)) {}

    MatchResult check(StructureEvaluationContext& ctx) const override {
        ensure_resolved();
        if (resolved_) {
            return resolved_->check(ctx);
        }
        return MatchResult::REJECT;
    }

    bool is_center() const override {
        ensure_resolved();
        return resolved_ ? resolved_->is_center() : false;
    }

    std::string describe() const override {
        ensure_resolved();
        return resolved_ ? resolved_->describe() : "lazy(unresolved)";
    }

private:
    void ensure_resolved() const {
        if (!resolved_) {
            resolved_ = factory_();
        }
    }

    std::function<std::shared_ptr<IStructureElement>()> factory_;
    mutable std::shared_ptr<IStructureElement> resolved_;
};

} // anonymous namespace

// ============================================================
// Elements factory
// ============================================================

namespace Elements {

std::shared_ptr<IStructureElement> material(TerrainMaterialId mat) {
    return std::make_shared<MaterialElement>(mat);
}

std::shared_ptr<IStructureElement> air() {
    // Cache: air() is used frequently, return the same instance.
    static auto instance = std::make_shared<AirElement>();
    return instance;
}

std::shared_ptr<IStructureElement> any() {
    static auto instance = std::make_shared<AnyElement>();
    return instance;
}

std::shared_ptr<IStructureElement> self() {
    static auto instance = std::make_shared<SelfElement>();
    return instance;
}

std::shared_ptr<IStructureElement> hatch(uint16_t type_mask) {
    return std::make_shared<HatchElement>(type_mask);
}

std::shared_ptr<IStructureElement> chain(
    std::vector<std::shared_ptr<IStructureElement>> elements) {
    return std::make_shared<ChainElement>(std::move(elements));
}

std::shared_ptr<IStructureElement> lazy(
    std::function<std::shared_ptr<IStructureElement>()> factory) {
    return std::make_shared<LazyElement>(std::move(factory));
}

} // namespace Elements

} // namespace science_and_theology::multiblock
