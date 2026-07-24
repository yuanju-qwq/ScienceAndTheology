#pragma once

#include <godot_cpp/classes/object.hpp>

namespace science_and_theology {

// 热重载入口：统一调用所有 registry 的 reset()。
// 调用后所有 C++ registry 回到未注册状态，GD 端可重新注册内容。
class GDRegistryBank : public godot::Object {
    GDCLASS(GDRegistryBank, godot::Object)

public:
    GDRegistryBank() = default;
    ~GDRegistryBank() override = default;

    // 重置所有 registry 到未注册状态。
    // 调用后需要重新执行 ContentDatabase.load_content() 并重新 initialize。
    static void reset_all();

    // 重置单个 registry（按名称）。
    // 支持的名称：material, item, fluid, fuel, rune, glyph,
    // ritual_recipe, elixir, sublimation_path, dropped_organ
    static void reset_one(const godot::String& registry_name);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
