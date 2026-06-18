#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace science_and_theology {

class GDFurnaceData : public godot::Resource {
    GDCLASS(GDFurnaceData, godot::Resource)

public:
    int64_t get_input_item_id() const { return input_item_id_; }
    void set_input_item_id(int64_t value) { input_item_id_ = value; }
    int32_t get_input_count() const { return input_count_; }
    void set_input_count(int32_t value) { input_count_ = value; }

    int64_t get_fuel_item_id() const { return fuel_item_id_; }
    void set_fuel_item_id(int64_t value) { fuel_item_id_ = value; }
    double get_fuel_burn_remaining() const { return fuel_burn_remaining_; }
    void set_fuel_burn_remaining(double value) { fuel_burn_remaining_ = value; }
    double get_fuel_burn_max() const { return fuel_burn_max_; }
    void set_fuel_burn_max(double value) { fuel_burn_max_ = value; }

    int64_t get_output_item_id() const { return output_item_id_; }
    void set_output_item_id(int64_t value) { output_item_id_ = value; }
    int32_t get_output_count() const { return output_count_; }
    void set_output_count(int32_t value) { output_count_ = value; }

    double get_smelt_progress() const { return smelt_progress_; }
    void set_smelt_progress(double value) { smelt_progress_ = value; }
    double get_smelt_target() const { return smelt_target_; }
    void set_smelt_target(double value) { smelt_target_ = value; }

    bool is_burning() const;
    double get_progress_ratio() const;
    double get_fuel_ratio() const;
    godot::Dictionary to_dictionary() const;

protected:
    static void _bind_methods();

private:
    int64_t input_item_id_ = 0;
    int32_t input_count_ = 0;
    int64_t fuel_item_id_ = 0;
    double fuel_burn_remaining_ = 0.0;
    double fuel_burn_max_ = 0.0;
    int64_t output_item_id_ = 0;
    int32_t output_count_ = 0;
    double smelt_progress_ = 0.0;
    double smelt_target_ = 5.0;
};

class GDFurnaceManager : public godot::Node {
    GDCLASS(GDFurnaceManager, godot::Node)

public:
    void _ready() override;
    void _process(double delta) override;

    bool place_furnace(const godot::StringName& dimension, const godot::Vector3i& cell);
    bool remove_furnace(const godot::StringName& dimension, const godot::Vector3i& cell);
    godot::Ref<GDFurnaceData> get_furnace(
        const godot::StringName& dimension, const godot::Vector3i& cell) const;
    bool has_furnace(const godot::StringName& dimension, const godot::Vector3i& cell) const;
    godot::Array get_all_furnaces() const;

    bool insert_input(const godot::StringName& dimension, const godot::Vector3i& cell,
                      int64_t item_id, int32_t count = 1);
    bool insert_fuel(const godot::StringName& dimension, const godot::Vector3i& cell,
                     int64_t item_id);
    bool take_output(const godot::StringName& dimension, const godot::Vector3i& cell,
                     int32_t count);

    double get_fuel_burn_time(int64_t item_id) const;
    godot::Dictionary get_recipe_for(int64_t item_id) const;
    godot::Dictionary get_furnace_snapshot(
        const godot::StringName& dimension, const godot::Vector3i& cell) const;

    void tick_all(double delta);
    godot::Array get_dirty_furnaces() const;
    void clear_dirty_furnaces();
    void clear();

protected:
    static void _bind_methods();

private:
    struct FurnaceKey {
        std::string dimension;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;

        bool operator==(const FurnaceKey& other) const {
            return dimension == other.dimension &&
                x == other.x &&
                y == other.y &&
                z == other.z;
        }
    };

    struct FurnaceKeyHash {
        size_t operator()(const FurnaceKey& key) const;
    };

    struct FurnaceRecipe {
        std::string name;
        int64_t output_id = 0;
        int32_t output_count = 0;
        double time = 0.0;
    };

    static FurnaceKey make_key(
        const godot::StringName& dimension, const godot::Vector3i& cell);
    static godot::String key_to_log_text(const FurnaceKey& key);
    static godot::Dictionary key_to_dictionary(const FurnaceKey& key);

    void ensure_recipes();
    bool tick_furnace(const FurnaceKey& key, GDFurnaceData* data, double delta);
    bool try_consume_fuel(const FurnaceKey& key, GDFurnaceData* data);
    void mark_dirty(const FurnaceKey& key, const char* reason, bool log_change);

    std::unordered_map<FurnaceKey, godot::Ref<GDFurnaceData>, FurnaceKeyHash> furnaces_;
    std::unordered_map<int64_t, FurnaceRecipe> recipes_;
    std::unordered_set<FurnaceKey, FurnaceKeyHash> dirty_furnaces_;
    bool recipes_initialized_ = false;
    bool missing_recipes_warned_ = false;
};

} // namespace science_and_theology
