#include "structure_element.hpp"
#include "piece_template.hpp"

#include <algorithm>
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

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

uint16_t hatch_mask_for_machine_type(const std::string& machine_type) {
    const std::string key = lower_copy(machine_type);
    uint16_t mask = 0;

    if (key.find("input_bus") != std::string::npos ||
        key.find("item_input") != std::string::npos ||
        key.find("input_item") != std::string::npos) {
        mask |= HATCH_ITEM_INPUT;
    }
    if (key.find("output_bus") != std::string::npos ||
        key.find("item_output") != std::string::npos ||
        key.find("output_item") != std::string::npos) {
        mask |= HATCH_ITEM_OUTPUT;
    }
    if (key.find("input_hatch") != std::string::npos ||
        key.find("fluid_input") != std::string::npos ||
        key.find("input_fluid") != std::string::npos) {
        mask |= HATCH_FLUID_INPUT;
    }
    if (key.find("output_hatch") != std::string::npos ||
        key.find("fluid_output") != std::string::npos ||
        key.find("output_fluid") != std::string::npos) {
        mask |= HATCH_FLUID_OUTPUT;
    }
    if (key.find("energy_input") != std::string::npos ||
        key.find("energy_hatch") != std::string::npos ||
        key.find("power_input") != std::string::npos) {
        mask |= HATCH_ENERGY_INPUT;
    }
    if (key.find("energy_output") != std::string::npos ||
        key.find("power_output") != std::string::npos) {
        mask |= HATCH_ENERGY_OUTPUT;
    }

    return mask;
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
        EntityId root;
        if (ctx.registry != nullptr) {
            root = ctx.registry->find_machine_root_at(ctx.x, ctx.y, ctx.z);
        }
        if (root.is_valid() && root != ctx.controller_id) {
            return MatchResult::REJECT;
        }
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
        if (!owner.is_valid()) {
            owner = ctx.registry->find_machine_root_at(ctx.x, ctx.y, ctx.z);
        }
        if (!owner.is_valid() || owner == ctx.controller_id) {
            return MatchResult::REJECT;
        }
        if (ctx.registry->get_entity_type(owner) != BlockEntityType::MACHINE) {
            return MatchResult::REJECT;
        }

        const MachineBlockEntityState* hatch_state = ctx.registry->get_machine_state(owner);
        if (hatch_state == nullptr) return MatchResult::REJECT;

        const uint16_t actual_mask = hatch_mask_for_machine_type(hatch_state->machine_type);
        if (type_mask_ != HATCH_ANY && (actual_mask & type_mask_) == 0) {
            return MatchResult::REJECT;
        }

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
        MatchCheckpoint cp = ctx.checkpoint();
        for (const auto& elem : elems_) {
            ctx.restore(cp);
            MatchResult r = elem->check(ctx);
            if (r == MatchResult::ACCEPT || r == MatchResult::ACCEPT_STOP) {
                return r;
            }
        }
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
