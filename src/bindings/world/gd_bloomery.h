#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace science_and_theology {

class GDBloomeryData : public godot::Resource {
    GDCLASS(GDBloomeryData, godot::Resource)

public:
    int32_t get_ore_count() const { return ore_count_; }
    void set_ore_count(int32_t v) { ore_count_ = v; }

    int32_t get_charcoal_count() const { return charcoal_count_; }
    void set_charcoal_count(int32_t v) { charcoal_count_ = v; }

    double get_temperature() const { return temperature_; }
    void set_temperature(double v) { temperature_ = v; }

    double get_process_progress() const { return process_progress_; }
    void set_process_progress(double v) { process_progress_ = v; }

    bool get_lit() const { return lit_; }
    void set_lit(bool v) { lit_ = v; }

    bool get_bellows_boosted() const { return bellows_boosted_; }
    void set_bellows_boosted(bool v) { bellows_boosted_ = v; }

    double get_bellows_timer() const { return bellows_timer_; }
    void set_bellows_timer(double v) { bellows_timer_ = v; }

    int32_t get_iron_bloom_yield() const { return iron_bloom_yield_; }
    void set_iron_bloom_yield(int32_t v) { iron_bloom_yield_ = v; }

    bool is_formed() const;
    double get_progress_ratio() const;
    bool is_ready() const;
    godot::Dictionary to_dictionary() const;

protected:
    static void _bind_methods();

private:
    int32_t ore_count_ = 0;
    int32_t charcoal_count_ = 0;
    double temperature_ = 0.0;
    double process_progress_ = 0.0;
    bool lit_ = false;
    bool bellows_boosted_ = false;
    double bellows_timer_ = 0.0;
    int32_t iron_bloom_yield_ = 0;
};

class GDBloomeryManager : public godot::Node {
    GDCLASS(GDBloomeryManager, godot::Node)

public:
    void _ready() override;
    void _process(double delta) override;

    bool place_bloomery(const godot::StringName& dim, const godot::Vector3i& cell);
    bool remove_bloomery(const godot::StringName& dim, const godot::Vector3i& cell);
    godot::Ref<GDBloomeryData> get_bloomery(const godot::StringName& dim, const godot::Vector3i& cell) const;
    bool has_bloomery(const godot::StringName& dim, const godot::Vector3i& cell) const;

    bool add_ore(const godot::StringName& dim, const godot::Vector3i& cell);
    bool add_charcoal(const godot::StringName& dim, const godot::Vector3i& cell);
    bool light_bloomery(const godot::StringName& dim, const godot::Vector3i& cell);
    bool use_bellows(const godot::StringName& dim, const godot::Vector3i& cell);
    godot::Dictionary break_bloomery(const godot::StringName& dim, const godot::Vector3i& cell);

    bool has_valid_structure(const godot::StringName& dim, const godot::Vector3i& cell) const;

    godot::Dictionary get_snapshot(const godot::StringName& dim, const godot::Vector3i& cell) const;
    void tick_all(double delta);
    void clear();

    // Inject the bloomery material id (resolved from runtime_ids by GDScript).
    void set_bloomery_material_id(int32_t id);

protected:
    static void _bind_methods();

private:
    struct BloomKey {
        std::string d; int32_t x = 0; int32_t y = 0; int32_t z = 0;
        bool operator==(const BloomKey& o) const {
            return d == o.d && x == o.x && y == o.y && z == o.z;
        }
    };
    struct BloomKeyHash { size_t operator()(const BloomKey& k) const; };

    static BloomKey mk(const godot::StringName& d, const godot::Vector3i& c);
    void mdirty(const BloomKey& k);
    bool tick_bloom(const BloomKey& k, GDBloomeryData* d, double delta);
    int32_t count_nearby_material(const godot::StringName& dim, const godot::Vector3i& center, int32_t mat_id) const;

    std::unordered_map<BloomKey, godot::Ref<GDBloomeryData>, BloomKeyHash> blooms_;
    std::unordered_set<BloomKey, BloomKeyHash> dirty_blooms_;
    godot::Node* wd_ = nullptr;
    int32_t bloomery_material_id_ = 0;
};

} // namespace science_and_theology
