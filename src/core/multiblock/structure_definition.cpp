#include "structure_definition.hpp"

namespace science_and_theology::multiblock {

// ============================================================
// StructureDefinition — cache implementation
// ============================================================

std::unordered_map<std::string, std::weak_ptr<StructureDefinition>>
    StructureDefinition::cache_;
std::mutex StructureDefinition::cache_mutex_;

std::shared_ptr<StructureDefinition> StructureDefinition::get_or_build(
    const std::string& key,
    std::function<std::shared_ptr<StructureDefinition>()> factory) {

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            auto locked = it->second.lock();
            if (locked) return locked;
            cache_.erase(it);
        }
    }

    // Build outside the cache mutex. Factories often call builder helpers that
    // create StructureDefinitions too; holding the lock here would deadlock on
    // recursive get_or_build() calls.
    auto def = factory();
    if (!def) return nullptr;

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            auto existing = it->second.lock();
            if (existing) return existing;
        }
        cache_[key] = def;
    }

    return def;
}

std::shared_ptr<StructureDefinition> StructureDefinition::from_template(
    const std::string& key, PieceTemplate tpl) {
    return get_or_build(key, [&]() {
        return std::shared_ptr<StructureDefinition>(
            new StructureDefinition(key, std::move(tpl)));
    });
}

std::shared_ptr<StructureDefinition> StructureDefinition::from_template(
    PieceTemplate tpl) {
    // Anonymous key (not cached). Used for ad-hoc definitions.
    return std::shared_ptr<StructureDefinition>(
        new StructureDefinition("", std::move(tpl)));
}

void StructureDefinition::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();
}

// ============================================================
// DeclarativePatternBuilder
// ============================================================

DeclarativePatternBuilder DeclarativePatternBuilder::start() {
    return DeclarativePatternBuilder{};
}

DeclarativePatternBuilder& DeclarativePatternBuilder::aisle(
    const std::vector<std::string>& rows) {
    compiler_.aisle(rows);
    return *this;
}

DeclarativePatternBuilder& DeclarativePatternBuilder::where(
    char symbol, std::shared_ptr<IStructureElement> element) {
    compiler_.where(symbol, std::move(element));
    return *this;
}

DeclarativePatternBuilder& DeclarativePatternBuilder::self(char symbol) {
    compiler_.where(symbol, Elements::self());
    // Also register '~' as self() so the aisle shorthand works.
    compiler_.where('~', Elements::self());
    return *this;
}

DeclarativePatternBuilder& DeclarativePatternBuilder::air(char symbol) {
    compiler_.where(symbol, Elements::air());
    // Also register '#' as air() so the aisle shorthand works.
    compiler_.where('#', Elements::air());
    return *this;
}

DeclarativePatternBuilder& DeclarativePatternBuilder::material(
    char symbol, TerrainMaterialId mat) {
    compiler_.where(symbol, Elements::material(mat));
    return *this;
}

DeclarativePatternBuilder& DeclarativePatternBuilder::hatch(
    char symbol, uint16_t type_mask) {
    compiler_.where(symbol, Elements::hatch(type_mask));
    return *this;
}

DeclarativePatternBuilder& DeclarativePatternBuilder::any(char symbol) {
    compiler_.where(symbol, Elements::any());
    return *this;
}

DeclarativePatternBuilder& DeclarativePatternBuilder::chain(
    char symbol, std::vector<std::shared_ptr<IStructureElement>> elements) {
    compiler_.where(symbol, Elements::chain(std::move(elements)));
    return *this;
}

PieceTemplate DeclarativePatternBuilder::build_template() {
    return compiler_.build();
}

std::shared_ptr<StructureDefinition>
DeclarativePatternBuilder::build_structure_definition(const std::string& key) {
    auto tpl = compiler_.build();
    if (!tpl.valid()) return nullptr;
    return StructureDefinition::from_template(key, std::move(tpl));
}

} // namespace science_and_theology::multiblock
