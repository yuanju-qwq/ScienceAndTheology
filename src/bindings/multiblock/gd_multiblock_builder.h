#pragma once

// ============================================================
// GDExtension 绑定：多方块结构声明式构建器
// ============================================================
//
// 包装 C++ 的 DeclarativePatternBuilder，让 GDScript 能用
// .aisle() / .where_*() 声明式定义多方块结构。
//
// GDScript 用法：
//
//   var builder = GDMultiblockBuilder.new()
//   builder.aisle(PackedStringArray(["XXX", "XXX", "XXX"]))
//   builder.aisle(PackedStringArray(["XXX", "X#X", "XXX"]))
//   builder.aisle(PackedStringArray(["XXX", "X~X", "XXX"]))
//   builder.where_material("X", 5)      # 'X' = 材质 5
//   builder.where_air("#")              # '#' = 空气
//   builder.where_self("~")             # '~' = 控制器
//   builder.where_hatch("H")            # 'H' = 任意仓
//   builder.where_any(".")              # '.' = 任意
//   var ok = builder.build("furnace")   # 构建并注册，返回 bool
//
// build(machine_type) 会：
//   1. 编译 pattern 为 PieceTemplate
//   2. 缓存为 StructureDefinition
//   3. 注册到 MultiblockControllerBase
// 之后 GDMultiblockController.check_formation() 即可使用。

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/multiblock/structure_definition.hpp"

namespace science_and_theology {

class GDMultiblockBuilder : public godot::Resource {
    GDCLASS(GDMultiblockBuilder, godot::Resource)

public:
    GDMultiblockBuilder();
    ~GDMultiblockBuilder() override;

    // 添加一个 aisle（z 层切片）。
    // PackedStringArray 中每个 String 是一行（y 行，从下到上）。
    // 字符是列（x 列，从左到右）。
    void aisle(const godot::PackedStringArray& rows);

    // --- where 方法：注册符号 → 元素映射 ---

    // 符号 = 特定材质
    void where_material(const godot::String& symbol, int material_id);

    // 符号 = 空气（必须为空格）
    void where_air(const godot::String& symbol);

    // 符号 = 任意（通配符）
    void where_any(const godot::String& symbol);

    // 符号 = 控制器自身（标记中心点）
    void where_self(const godot::String& symbol);

    // 符号 = 仓（可放 hatch 的位置）
    // type_mask: HATCH_* 位掩码，默认 HATCH_ANY
    void where_hatch(const godot::String& symbol, int type_mask = 0xFFFF);

    // 构建并注册结构定义。
    // machine_type: 机器类型键（如 "furnace"）
    // 返回 true 表示构建成功。
    bool build(const godot::String& machine_type);

    // 便捷常量
    static int HATCH_ITEM_INPUT();
    static int HATCH_ITEM_OUTPUT();
    static int HATCH_FLUID_INPUT();
    static int HATCH_FLUID_OUTPUT();
    static int HATCH_ENERGY_INPUT();
    static int HATCH_ENERGY_OUTPUT();
    static int HATCH_ANY();

protected:
    static void _bind_methods();

private:
    multiblock::DeclarativePatternBuilder builder_;

    static char to_char(const godot::String& s);
};

} // namespace science_and_theology
