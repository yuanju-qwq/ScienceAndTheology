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

class GDCharcoalPitData : public godot::Resource {
    GDCLASS(GDCharcoalPitData, godot::Resource)

public:
    int32_t get_log_count() const { return log_count_; }
    void set_log_count(int32_t value) { log_count_ = value; }

    bool get_covered() const { return covered_; }
    void set_covered(bool value) { covered_ = value; }

    bool get_lit() const { return lit_; }
    void set_lit(bool value) { lit_ = value; }

    double get_burn_progress() const { return burn_progress_; }
    void set_burn_progress(double value) { burn_progress_ = value; }

    double get_total_burn_time() const { return total_burn_time_; }
    void set_total_burn_time(double value) { total_burn_time_ = value; }

    int32_t get_charcoal_yield() const { return charcoal_yield_; }
    void set_charcoal_yield(int32_t value) { charcoal_yield_ = value; }

    bool is_burning() const;
    double get_progress_ratio() const;
    bool is_ready() const;
    godot::Dictionary to_dictionary() const;

protected:
    static void _bind_methods();

private:
    int32_t log_count_ = 0;
    bool covered_ = false;
    bool lit_ = false;
    double burn_progress_ = 0.0;
    double total_burn_time_ = 0.0;
    int32_t charcoal_yield_ = 0;
};

class GDCharcoalPitManager : public godot::Node {
    GDCLASS(GDCharcoalPitManager, godot::Node)

public:
    void _ready() override;
    void _process(double delta) override;

    bool place_pit(const godot::StringName& dimension, const godot::Vector3i& cell);
    bool remove_pit(const godot::StringName& dimension, const godot::Vector3i& cell);
    godot::Ref<GDCharcoalPitData> get_pit(
        const godot::StringName& dimension, const godot::Vector3i& cell) const;
    bool has_pit(const godot::StringName& dimension, const godot::Vector3i& cell) const;

    bool add_log(const godot::StringName& dimension, const godot::Vector3i& cell);
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
    struct PitKey {
        std::string dimension;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;

        bool operator==(const PitKey& other) const {
            return dimension == other.dimension && x == other.x && y == other.y && z == other.z;
        }
    };

    struct PitKeyHash {
        size_t operator()(const PitKey& key) const;
    };

    static PitKey make_key(const godot::StringName& dimension, const godot::Vector3i& cell);
    static godot::String key_to_log_text(const PitKey& key);

    void mark_dirty(const PitKey& key);
    bool tick_pit(const PitKey& key, GDCharcoalPitData* data, double delta);

    std::unordered_map<PitKey, godot::Ref<GDCharcoalPitData>, PitKeyHash> pits_;
    std::unordered_set<PitKey, PitKeyHash> dirty_pits_;
};

} // namespace science_and_theology
