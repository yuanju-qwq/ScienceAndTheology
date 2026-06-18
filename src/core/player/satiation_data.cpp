#include "player/satiation_data.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// Food helper functions
// ============================================================

FoodDef make_plant_food(float food_value, float total_essence) {
    FoodDef def;
    def.food_value = food_value;
    // Life essence distribution: EARTH 50%, WATER 35%, LIGHT 15%.
    def.elements.emplace_back(magic::RuneElement::EARTH, total_essence * 0.50f);
    def.elements.emplace_back(magic::RuneElement::WATER, total_essence * 0.35f);
    def.elements.emplace_back(magic::RuneElement::LIGHT, total_essence * 0.15f);
    return def;
}

FoodDef make_creature_food(float food_value, float total_essence,
                           magic::RuneElement creature_element) {
    FoodDef def;
    def.food_value = food_value;
    // Creature element dominant 70%, small life essence 30%.
    def.elements.emplace_back(creature_element, total_essence * 0.70f);
    def.elements.emplace_back(magic::RuneElement::EARTH, total_essence * 0.15f);
    def.elements.emplace_back(magic::RuneElement::WATER, total_essence * 0.10f);
    def.elements.emplace_back(magic::RuneElement::LIGHT, total_essence * 0.05f);
    return def;
}

// ============================================================
// SourceEssencePool
// ============================================================

float SourceEssencePool::get(magic::RuneElement element) const {
    int idx = static_cast<int>(element);
    if (idx < 0 || idx >= kElementCount) return 0.0f;
    return values_[idx];
}

float SourceEssencePool::add(magic::RuneElement element, float value) {
    int idx = static_cast<int>(element);
    if (idx < 0 || idx >= kElementCount) return 0.0f;
    if (value <= 0.0f) return 0.0f;

    float before = values_[idx];
    values_[idx] += value;
    clamp_to_cap();
    return values_[idx] - before;
}

float SourceEssencePool::total() const {
    float sum = 0.0f;
    for (int i = 0; i < kElementCount; ++i) {
        sum += values_[i];
    }
    return sum;
}

void SourceEssencePool::set_cap(float max_val) {
    cap_ = std::max(0.0f, max_val);
    clamp_to_cap();
}

void SourceEssencePool::set_decay_rate(float rate) {
    decay_rate_ = std::max(0.0f, rate);
}

void SourceEssencePool::decay() {
    if (decay_rate_ <= 0.0f) return;

    float t = total();
    if (t <= 0.0f) return;

    // Proportional decay: each element loses decay_rate_ * (its_share / total).
    // This preserves the element ratio while reducing total.
    for (int i = 0; i < kElementCount; ++i) {
        if (values_[i] <= 0.0f) continue;
        float share = values_[i] / t;
        float loss = decay_rate_ * share;
        // Also apply a flat per-element minimum decay.
        float flat_loss = decay_rate_ / static_cast<float>(kElementCount);
        float actual_loss = std::max(loss, flat_loss);
        values_[i] -= actual_loss;
        if (values_[i] < 0.0f) values_[i] = 0.0f;
    }
}

void SourceEssencePool::set(magic::RuneElement element, float value) {
    int idx = static_cast<int>(element);
    if (idx < 0 || idx >= kElementCount) return;
    values_[idx] = std::max(0.0f, value);
}

void SourceEssencePool::reset() {
    for (int i = 0; i < kElementCount; ++i) {
        values_[i] = 0.0f;
    }
    cap_ = kDefaultCap;
    decay_rate_ = kDefaultDecayRate;
}

void SourceEssencePool::clamp_to_cap() {
    float t = total();
    if (t > cap_ && t > 0.0f) {
        float scale = cap_ / t;
        for (int i = 0; i < kElementCount; ++i) {
            values_[i] *= scale;
        }
    }
}

// ============================================================
// Satiation value
// ============================================================

void SatiationData::set_satiation_max(float max_val) {
    satiation_max_ = std::max(1.0f, max_val);
    if (satiation_current_ > satiation_max_) {
        satiation_current_ = satiation_max_;
    }
}

void SatiationData::set_decay_rate(float rate) {
    decay_rate_ = std::max(0.0f, rate);
}

float SatiationData::eat(const FoodDef& food) {
    // Restore satiation.
    float before = satiation_current_;
    satiation_current_ = std::min(satiation_current_ + food.food_value, satiation_max_);

    // Add element essence.
    for (const auto& [element, value] : food.elements) {
        source_essence_.add(element, value);
    }

    return satiation_current_ - before;
}

float SatiationData::eat(float food_value) {
    float before = satiation_current_;
    satiation_current_ = std::min(satiation_current_ + food_value, satiation_max_);
    return satiation_current_ - before;
}

void SatiationData::set_satiation(float value) {
    satiation_current_ = std::clamp(value, 0.0f, satiation_max_);
}

// ============================================================
// Hunger level computation
// ============================================================

HungerLevel SatiationData::compute_hunger_level() const {
    if (satiation_current_ <= starving_threshold_) {
        return HungerLevel::STARVING;
    }
    if (satiation_current_ <= hungry_threshold_) {
        return HungerLevel::HUNGRY;
    }
    if (satiation_current_ <= peckish_threshold_) {
        return HungerLevel::PECKISH;
    }
    return HungerLevel::SATIATED;
}

HungerLevel SatiationData::hunger_level() const {
    return compute_hunger_level();
}

bool SatiationData::is_starving() const {
    return compute_hunger_level() == HungerLevel::STARVING;
}

// ============================================================
// Effect modifiers
// ============================================================

float SatiationData::speed_modifier() const {
    switch (compute_hunger_level()) {
        case HungerLevel::PECKISH:  return 0.8f;
        case HungerLevel::HUNGRY:   return 0.6f;
        case HungerLevel::STARVING: return 0.4f;
        default:                    return 1.0f;
    }
}

float SatiationData::health_regen_modifier() const {
    switch (compute_hunger_level()) {
        case HungerLevel::PECKISH:  return 0.5f;
        case HungerLevel::HUNGRY:   return 0.0f;
        case HungerLevel::STARVING: return 0.0f;
        default:                    return 1.0f;
    }
}

float SatiationData::attack_modifier() const {
    switch (compute_hunger_level()) {
        case HungerLevel::HUNGRY:   return 0.8f;
        case HungerLevel::STARVING: return 0.6f;
        default:                    return 1.0f;
    }
}

float SatiationData::starvation_damage_per_tick() const {
    if (compute_hunger_level() == HungerLevel::STARVING) {
        return 1.0f;
    }
    return 0.0f;
}

// ============================================================
// Thresholds
// ============================================================

void SatiationData::set_peckish_threshold(float val) {
    peckish_threshold_ = std::clamp(val, 0.0f, satiation_max_);
}

void SatiationData::set_hungry_threshold(float val) {
    hungry_threshold_ = std::clamp(val, 0.0f, peckish_threshold_);
}

void SatiationData::set_starving_threshold(float val) {
    starving_threshold_ = std::clamp(val, 0.0f, hungry_threshold_);
}

// ============================================================
// Source essence convenience methods
// ============================================================

float SatiationData::get_source_essence(magic::RuneElement element) const {
    return source_essence_.get(element);
}

float SatiationData::get_total_source_essence() const {
    return source_essence_.total();
}

void SatiationData::set_source_essence_cap(float max_val) {
    source_essence_.set_cap(max_val);
}

// ============================================================
// Tick
// ============================================================

HungerLevel SatiationData::tick() {
    // Satiation decay
    if (decay_rate_ > 0.0f && satiation_current_ > 0.0f) {
        satiation_current_ -= decay_rate_;
        if (satiation_current_ < 0.0f) {
            satiation_current_ = 0.0f;
        }
    }

    // Source essence decay
    source_essence_.decay();

    return compute_hunger_level();
}

// ============================================================
// Reset
// ============================================================

void SatiationData::reset() {
    satiation_current_ = kDefaultSatiationMax;
    satiation_max_ = kDefaultSatiationMax;
    decay_rate_ = kDefaultDecayRate;
    peckish_threshold_ = kDefaultPeckishThreshold;
    hungry_threshold_ = kDefaultHungryThreshold;
    starving_threshold_ = kDefaultStarvingThreshold;
    source_essence_.reset();
}

// ============================================================
// Serialization
// ============================================================

SatiationData::SerializedData SatiationData::to_serialized() const {
    SerializedData data;
    data.satiation_current = satiation_current_;
    data.satiation_max = satiation_max_;
    data.decay_rate = decay_rate_;
    data.peckish_threshold = peckish_threshold_;
    data.hungry_threshold = hungry_threshold_;
    data.starving_threshold = starving_threshold_;
    data.source_essence_cap = source_essence_.cap();
    data.source_essence_decay_rate = source_essence_.decay_rate();
    auto& vals = source_essence_.values();
    for (int i = 0; i < static_cast<int>(magic::RuneElement::COUNT); ++i) {
        data.source_essence_values[i] = vals[i];
    }
    return data;
}

void SatiationData::from_serialized(const SerializedData& data) {
    satiation_current_ = data.satiation_current;
    satiation_max_ = data.satiation_max;
    decay_rate_ = data.decay_rate;
    peckish_threshold_ = data.peckish_threshold;
    hungry_threshold_ = data.hungry_threshold;
    starving_threshold_ = data.starving_threshold;
    source_essence_.set_cap(data.source_essence_cap);
    source_essence_.set_decay_rate(data.source_essence_decay_rate);
    for (int i = 0; i < static_cast<int>(magic::RuneElement::COUNT); ++i) {
        source_essence_.set(static_cast<magic::RuneElement>(i),
                            data.source_essence_values[i]);
    }
}

} // namespace science_and_theology
