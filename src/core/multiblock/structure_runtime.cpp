#include "structure_runtime.hpp"

#include "structure_element.hpp"

#include "../world/world_data.hpp"
#include "../world/block_entity_registry.hpp"

namespace science_and_theology::multiblock {

// ============================================================
// StructureRuntime
// ============================================================

StructureRuntime::StructureRuntime(
    std::shared_ptr<StructureDefinition> def)
    : definition_(std::move(def)) {}

void StructureRuntime::set_definition(
    std::shared_ptr<StructureDefinition> def) {
    if (definition_.get() != def.get()) {
        definition_ = std::move(def);
        formed_ = false;
        formed_view_ = FormedStructureView{};
    }
}

StructureCheckResult StructureRuntime::check(
    const WorldData& world,
    const BlockEntityRegistry& registry,
    EntityId controller_id,
    const std::string& dimension_id,
    int32_t ctrl_x, int32_t ctrl_y, int32_t ctrl_z,
    uint8_t facing) {

    if (!definition_) {
        return StructureCheckResult::failure(0, 0, 0, "no structure definition");
    }

    const PieceTemplate& tpl = definition_->primary_template();
    if (!tpl.valid()) {
        return StructureCheckResult::failure(0, 0, 0, "invalid piece template");
    }

    // Trivial 1x1x1 pattern (controller only) — always formed.
    if (tpl.is_trivial()) {
        formed_ = true;
        formed_view_ = FormedStructureView{};
        return StructureCheckResult::success({}, {});
    }

    // Set up the evaluation context.
    MatchCollector collector;
    StructureEvaluationContext ctx;
    ctx.operation = StructureOperation::MATCH_WORLD;
    ctx.world = &world;
    ctx.registry = &registry;
    ctx.dimension_id = dimension_id;
    ctx.controller_id = controller_id;
    ctx.collector = &collector;

    // Iterate all pattern cells.
    std::vector<OwnedCell> claimed_cells;
    const int ctrl_ox = tpl.controller_offset_x();
    const int ctrl_oy = tpl.controller_offset_y();
    const int ctrl_oz = tpl.controller_offset_z();

    for (int z = 0; z < tpl.size_z(); ++z) {
        for (int y = 0; y < tpl.size_y(); ++y) {
            for (int x = 0; x < tpl.size_x(); ++x) {
                const IStructureElement& elem = tpl.at(x, y, z);

                // Compute pattern-local offset from controller.
                int px = x - ctrl_ox;
                int py = y - ctrl_oy;
                int pz = z - ctrl_oz;

                // Rotate to world offset.
                int dx, dy, dz;
                rotate_pattern_offset(px, py, pz, facing, dx, dy, dz);

                // Compute world position.
                int32_t wx = ctrl_x + dx;
                int32_t wy = ctrl_y + dy;
                int32_t wz = ctrl_z + dz;

                // Set up context for this cell.
                ctx.x = wx;
                ctx.y = wy;
                ctx.z = wz;

                // Check the element.
                MatchResult result = elem.check(ctx);
                if (result == MatchResult::REJECT) {
                    formed_ = false;
                    return StructureCheckResult::failure(
                        x, y, z,
                        "element '" + elem.describe() + "' rejected at " +
                        std::to_string(wx) + "," + std::to_string(wy) +
                        "," + std::to_string(wz));
                }

                // ACCEPT or ACCEPT_STOP — record the cell.
                // Skip the controller cell itself (it's not "claimed",
                // it IS the controller).
                if (!elem.is_center()) {
                    claimed_cells.push_back(OwnedCell{wx, wy, wz});
                }

                if (result == MatchResult::ACCEPT_STOP) {
                    goto check_done;
                }
            }
        }
    }

check_done:
    // All cells passed — formation successful.
    formed_ = true;
    formed_view_ = FormedStructureView(
        claimed_cells, collector.matched_hatches, collector.channel_values);

    return StructureCheckResult::success(
        std::move(claimed_cells),
        std::move(collector.matched_hatches));
}

void StructureRuntime::invalidate() {
    formed_ = false;
    formed_view_ = FormedStructureView{};
}

} // namespace science_and_theology::multiblock
