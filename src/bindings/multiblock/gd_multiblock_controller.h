#pragma once

// ============================================================
// GDExtension 绑定：多方块控制器
// ============================================================
//
// 包装 C++ 的 MultiblockControllerBase，让 GDScript 能触发
// 成型检查和失效。
//
// GDScript 用法：
//
//   # 检查成型（放置机器后或方块变更时调用）
//   var result = GDMultiblockController.check_formation(world_data, entity_id)
//   if result["matched"]:
//       print("成型成功，claimed_cells=", result["claimed_cells"])
//   else:
//       print("成型失败：", result["mismatch_reason"])
//
//   # 失效（破坏 claimed cell 时调用）
//   GDMultiblockController.invalidate(world_data, entity_id)
//
// 注意：check_formation 会自动调用 BlockEntityRegistry.set_machine_formation()
// 更新机器状态，无需 GDScript 侧手动同步。

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace science_and_theology {

class GDWorldData;

class GDMultiblockController : public godot::Resource {
    GDCLASS(GDMultiblockController, godot::Resource)

public:
    GDMultiblockController();
    ~GDMultiblockController() override;

    // 检查机器实体的多方块成型。
    // world_data: GDWorldData 实例（提供 WorldData + BlockEntityRegistry 访问）
    // entity_id:  机器实体的 EntityId（int64）
    //
    // 返回 Dictionary:
    //   "matched": bool — 是否成型成功
    //   "claimed_cells": PackedVector3Array — 成功时的 claimed 格子坐标
    //   "hatch_entities": PackedInt64Array — 成功时的 hatch EntityId 列表
    //   "mismatch_x/y/z": int — 失败时的 pattern 内坐标
    //   "mismatch_reason": String — 失败原因
    //
    // 成功时自动调用 registry.set_machine_formation(formed=true)。
    // 失败时自动调用 registry.set_machine_formation(formed=false)。
    godot::Dictionary check_formation(GDWorldData* world_data, int64_t entity_id);

    // 失效机器的成型状态。
    // 调用 registry.set_machine_formation(formed=false)。
    void invalidate(GDWorldData* world_data, int64_t entity_id);

    // 查询某机器类型是否已注册结构定义。
    static bool has_definition(const godot::String& machine_type);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
