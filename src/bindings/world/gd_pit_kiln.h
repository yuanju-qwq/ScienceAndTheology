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

class GDPitKilnData : public godot::Resource {
    GDCLASS(GDPitKilnData, godot::Resource)

public:
    int32_t get_input_count() const { return input_count_; }
    void set_input_count(int32_t value) { input_count_ = value; }

    int64_t get_input_item_id() const { return input_item_id_; }
    void set_input_item_id(int64_t value) { input_item_id_ = value; }

    bool get_covered() const { return covered_; }
    void set_covered(bool value) { covered_ = value; }

    bool get_lit() const { return lit_; }
    void set_lit(bool value) { lit_ = value; }

    double get_fire_progress() const { return fire_progress_; }
    void set_fire_progress(double value) { fire_progress_ = value; }

    double get_total_fire_time() const { return total_fire_time_; }
    void set_total_fire_time(double value) { total_fire_time_ = value; }

    int64_t get_output_item_id() const { return output_item_id_; }
    void set_output_item_id(int64_t value) { output_item_id_ = value; }

    bool is_burning() const;
    double get_progress_ratio() const;
    bool is_ready() const;
    godot::Dictionary to_dictionary() const;

protected:
    static void _bind_methods();

private:
    int32_t input_count_ = 0;
    int64_t input_item_id_ = 0;
    bool covered_ = false;
    bool lit_ = false;
    double fire_progress_ = 0.0;
    double total_fire_time_ = 120.0;
    int64_t output_item_id_ = 0;
};

class GDPitKilnManager : public godot::Node {
    GDCLASS(GDPitKilnManager, godot::Node)

public:
    void _ready() override;
    void _process(double delta) override;

    bool place_kiln(const godot::StringName& dimension, const godot::Vector3i& cell);
    bool remove_kiln(const godot::StringName& dimension, const godot::Vector3i& cell);
    godot::Ref<GDPitKilnData> get_kiln(
        const godot::StringName& dimension, const godot::Vector3i& cell) const;
    bool has_kiln(const godot::StringName& dimension, const godot::Vector3i& cell) const;

    bool insert_input(const godot::StringName& dimension, const godot::Vector3i& cell, int64_t item_id);
    bool cover(const godot::StringName& dimension, const godot::Vector3i& cell);
    bool light(const godot::StringName& dimension, const godot::Vector3i& cell);
    godot::Dictionary collect(const godot::StringName& dimension, const godot::Vector3i& cell);

    godot::Dictionary get_snapshot(
        const godot::StringName& dimension, const godot::Vector3i& cell) const;
    void tick_all(double delta);
    void clear();

protected:
    static void _bind_methods();

private:
    struct KilnKey {
        std::string dimension;
        int32_t x = 0; int32_t y = 0; int32_t z = 0;
        bool operator==(const KilnKey& o) const {
            return dimension == o.dimension && x == o.x && y == o.y && z == o.z;
        }
    };
    struct KilnKeyHash { size_t operator()(const KilnKey& k) const; };

    static KilnKey make_key(const godot::StringName& d, const godot::Vector3i& c);
    void mark_dirty(const KilnKey& k);
    bool tick_kiln(const KilnKey& k, GDPitKilnData* d, double delta);

    std::unordered_map<KilnKey, godot::Ref<GDPitKilnData>, KilnKeyHash> kilns_;
    std::unordered_set<KilnKey, KilnKeyHash> dirty_kilns_;
};

} // namespace science_and_theology
