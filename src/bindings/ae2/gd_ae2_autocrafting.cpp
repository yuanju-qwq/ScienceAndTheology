#include "gd_ae2_autocrafting.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>

#include <unordered_map>

#include "core/ae2/ae2_pattern_cache.hpp"
#include "core/crafting/crafting.hpp"
#include "core/machine/recipe.hpp"

namespace science_and_theology {

using namespace godot;
using namespace gt;

// Storage for provider patterns.
// Maps provider_id -> vector of SimplePatterns owned by the binding.
struct ProviderPatternStorage {
    std::unordered_map<uint64_t, std::vector<std::unique_ptr<SimplePattern>>> patterns;
    static ProviderPatternStorage& instance() {
        static ProviderPatternStorage s;
        return s;
    }
};

// ============================================================
// GDAutocraftingCPU
// ============================================================

GDAutocraftingCPU::GDAutocraftingCPU()
    : cpu_(std::make_unique<CraftingCPU>()) {}

GDAutocraftingCPU::~GDAutocraftingCPU() = default;

void GDAutocraftingCPU::configure(int64_t storage_bytes, int co_processors) {
    cpu_->configure(storage_bytes, co_processors);
}

bool GDAutocraftingCPU::is_busy() const { return cpu_->is_busy(); }
int64_t GDAutocraftingCPU::get_storage_bytes() const { return cpu_->storage_bytes(); }
int64_t GDAutocraftingCPU::get_available_storage() const { return cpu_->available_storage(); }
int GDAutocraftingCPU::get_co_processors() const { return cpu_->co_processors(); }
void GDAutocraftingCPU::cancel_job() { cpu_->cancel_job(); }

int64_t GDAutocraftingCPU::insert_item(int64_t item_id, int64_t amount) {
    return cpu_->insert_item(static_cast<ItemId>(item_id), amount);
}

void GDAutocraftingCPU::set_craft_executor(const godot::Callable& callback) {
    cpu_->set_craft_executor(
        [callback](CraftingCPU* cpu, const AEPattern* pattern, int64_t count) -> bool {
            ItemId output_id = pattern->get_primary_output().item_id();
            godot::Array args;
            godot::Dictionary data;
            data["item_id"] = static_cast<int64_t>(output_id);
            data["count"] = count;
            args.append(data);
            auto result = callback.callv(args);
            return result.operator bool();
        });
}

void GDAutocraftingCPU::set_process_executor(const godot::Callable& callback) {
    cpu_->set_process_executor(
        [callback](CraftingCPU* cpu, const AEPattern* pattern, int64_t count) -> bool {
            ItemId output_id = pattern->get_primary_output().item_id();
            godot::Array args;
            godot::Dictionary data;
            data["item_id"] = static_cast<int64_t>(output_id);
            data["count"] = count;
            args.append(data);
            auto result = callback.callv(args);
            return result.operator bool();
        });
}

void GDAutocraftingCPU::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "storage_bytes", "co_processors"),
                         &GDAutocraftingCPU::configure);
    ClassDB::bind_method(D_METHOD("is_busy"),
                         &GDAutocraftingCPU::is_busy);
    ClassDB::bind_method(D_METHOD("get_storage_bytes"),
                         &GDAutocraftingCPU::get_storage_bytes);
    ClassDB::bind_method(D_METHOD("get_available_storage"),
                         &GDAutocraftingCPU::get_available_storage);
    ClassDB::bind_method(D_METHOD("get_co_processors"),
                         &GDAutocraftingCPU::get_co_processors);
    ClassDB::bind_method(D_METHOD("cancel_job"),
                         &GDAutocraftingCPU::cancel_job);
    ClassDB::bind_method(D_METHOD("insert_item", "item_id", "amount"),
                         &GDAutocraftingCPU::insert_item);
    ClassDB::bind_method(D_METHOD("set_craft_executor", "callback"),
                         &GDAutocraftingCPU::set_craft_executor);
    ClassDB::bind_method(D_METHOD("set_process_executor", "callback"),
                         &GDAutocraftingCPU::set_process_executor);
}

// ============================================================
// GDAutocraftingService
// ============================================================

gt::CraftingService& GDAutocraftingService::service() {
    static CraftingService svc;
    return svc;
}

void GDAutocraftingService::initialize() {
    service().initialize();
}

void GDAutocraftingService::add_cpu(const godot::Ref<GDAutocraftingCPU>& cpu) {
    if (cpu.is_valid()) {
        service().add_cpu(cpu->get_cpu());
    }
}

void GDAutocraftingService::remove_cpu(const godot::Ref<GDAutocraftingCPU>& cpu) {
    if (cpu.is_valid()) {
        service().remove_cpu(cpu->get_cpu());
    }
}

godot::Dictionary GDAutocraftingService::submit_job(int64_t item_id, int64_t amount) {
    godot::Dictionary result;

    auto submit_result = service().submit_job(
        static_cast<ItemId>(item_id), amount);

    result["success"] = submit_result.success;
    result["error_message"] = godot::String(submit_result.error_message);

    if (submit_result.success) {
        godot::Dictionary plan;
        plan["item_id"] = static_cast<int64_t>(
            submit_result.plan.final_output.item_id());
        plan["amount"] = submit_result.plan.final_output.amount;
        plan["bytes"] = submit_result.plan.bytes;
        plan["simulation"] = submit_result.plan.simulation;

        godot::Array used_arr;
        for (const auto& [id, amt] : submit_result.plan.used_items) {
            godot::Dictionary entry;
            entry["item_id"] = static_cast<int64_t>(id);
            entry["amount"] = amt;
            used_arr.append(entry);
        }
        plan["used_items"] = used_arr;

        godot::Array emitted_arr;
        for (const auto& [id, amt] : submit_result.plan.emitted_items) {
            godot::Dictionary entry;
            entry["item_id"] = static_cast<int64_t>(id);
            entry["amount"] = amt;
            emitted_arr.append(entry);
        }
        plan["emitted_items"] = emitted_arr;

        result["plan"] = plan;
    }

    return result;
}

void GDAutocraftingService::tick() {
    service().tick();
}

void GDAutocraftingService::set_network_check_callback(
        const godot::Callable& callback) {
    service().set_network_check_callback(
        [callback](ItemId item_id) -> int64_t {
            godot::Array args;
            args.append(static_cast<int64_t>(item_id));
            auto result = callback.callv(args);
            if (result.get_type() == godot::Variant::INT) {
                return static_cast<int64_t>(result);
            }
            return 0;
        });
}

void GDAutocraftingService::set_network_extract_callback(
        const godot::Callable& callback) {
    service().set_network_extract_callback(
        [callback](ItemId item_id, int64_t amount) -> int64_t {
            godot::Array args;
            args.append(static_cast<int64_t>(item_id));
            args.append(amount);
            auto result = callback.callv(args);
            if (result.get_type() == godot::Variant::INT) {
                return static_cast<int64_t>(result);
            }
            return 0;
        });
}

void GDAutocraftingService::set_network_insert_callback(
        const godot::Callable& callback) {
    service().set_network_insert_callback(
        [callback](ItemId item_id, int64_t amount) -> int64_t {
            godot::Array args;
            args.append(static_cast<int64_t>(item_id));
            args.append(amount);
            auto result = callback.callv(args);
            if (result.get_type() == godot::Variant::INT) {
                return static_cast<int64_t>(result);
            }
            return 0;
        });
}

void GDAutocraftingService::set_emitable(int64_t item_id, bool emitable) {
    service().set_emitable(static_cast<ItemId>(item_id), emitable);
}

bool GDAutocraftingService::is_item_craftable(int64_t item_id) {
    return PatternRegistry::is_craftable(static_cast<ItemId>(item_id));
}

void GDAutocraftingService::add_provider_pattern(
        int64_t provider_id, int64_t item_id,
        const godot::Array& inputs,
        const godot::Array& outputs,
        bool is_crafting) {
    std::vector<ResourceStack> ins;
    for (int i = 0; i < inputs.size(); ++i) {
        godot::Dictionary entry = inputs[i];
        ItemId id = static_cast<ItemId>(static_cast<int64_t>(entry["item_id"]));
        int64_t amount = static_cast<int64_t>(entry["amount"]);
        ins.push_back(ResourceStack::item(id, amount));
    }

    std::vector<ResourceStack> outs;
    for (int i = 0; i < outputs.size(); ++i) {
        godot::Dictionary entry = outputs[i];
        ItemId id = static_cast<ItemId>(static_cast<int64_t>(entry["item_id"]));
        int64_t amount = static_cast<int64_t>(entry["amount"]);
        outs.push_back(ResourceStack::item(id, amount));
    }

    // Build a name from the provider_id.
    std::string name = "provider_" + std::to_string(provider_id)
                     + "_" + std::to_string(item_id);

    auto pattern = std::make_unique<SimplePattern>(
        name.c_str(), std::move(ins), std::move(outs), is_crafting);

    const AEPattern* ptr = pattern.get();
    PatternRegistry::add_provider_pattern(ptr,
        static_cast<uint64_t>(provider_id));

    // Store ownership.
    auto& storage = ProviderPatternStorage::instance();
    storage.patterns[static_cast<uint64_t>(provider_id)].push_back(
        std::move(pattern));
}

void GDAutocraftingService::remove_provider_patterns(int64_t provider_id) {
    uint64_t pid = static_cast<uint64_t>(provider_id);
    PatternRegistry::remove_provider_patterns(pid);

    auto& storage = ProviderPatternStorage::instance();
    storage.patterns.erase(pid);
}

void GDAutocraftingService::sync_providers() {
    service().sync_providers();
}

godot::Dictionary GDAutocraftingService::encode_crafting_pattern(
        const godot::String& recipe_name) {
    godot::Dictionary result;
    result["success"] = false;

    std::string name = recipe_name.utf8().get_data();
    auto all_recipes = CraftingManager::get_registry();
    const CraftingRecipe* found = nullptr;
    for (const auto& recipe : all_recipes) {
        if (recipe.name && name == recipe.name) {
            found = &recipe;
            break;
        }
    }

    if (!found) {
        result["error"] = godot::String("Recipe not found: ") + recipe_name;
        return result;
    }

    auto pattern = std::make_unique<AECraftingPattern>(found);
    std::vector<ResourceStack> ins = pattern->get_inputs();
    std::vector<ResourceStack> outs = pattern->get_outputs();

    ItemId id = PatternDataCache::register_pattern(ins, outs, true, found->name);
    result["success"] = true;
    result["item_id"] = static_cast<int64_t>(id);
    result["pattern_name"] = godot::String(PatternDataCache::get_pattern_name(id));
    return result;
}

godot::Dictionary GDAutocraftingService::encode_processing_pattern(
        const godot::String& machine_type, const godot::String& recipe_name) {
    godot::Dictionary result;
    result["success"] = false;

    std::string type = machine_type.utf8().get_data();
    std::string name = recipe_name.utf8().get_data();

    auto* map = RecipeDatabase::get_map(type.c_str());
    if (!map) {
        result["error"] = godot::String("Machine type not found: ") + machine_type;
        return result;
    }

    const Recipe* found = map->find_by_name(name.c_str());
    if (!found) {
        result["error"] = godot::String("Recipe not found: ") + recipe_name;
        return result;
    }

    auto pattern = std::make_unique<AEProcessingPattern>(found);
    std::vector<ResourceStack> ins = pattern->get_inputs();
    std::vector<ResourceStack> outs = pattern->get_outputs();

    ItemId id = PatternDataCache::register_pattern(ins, outs, false, found->name);
    result["success"] = true;
    result["item_id"] = static_cast<int64_t>(id);
    result["pattern_name"] = godot::String(PatternDataCache::get_pattern_name(id));
    return result;
}

void GDAutocraftingService::add_encoded_pattern(int64_t provider_id, int64_t encoded_item_id) {
    ItemId id = static_cast<ItemId>(encoded_item_id);
    auto* data = PatternDataCache::get_pattern_data(id);
    if (!data) return;

    godot::Array inputs;
    for (const auto& s : data->inputs) {
        godot::Dictionary entry;
        entry["item_id"] = static_cast<int64_t>(s.item_id());
        entry["amount"] = s.amount;
        inputs.append(entry);
    }

    godot::Array outputs;
    for (const auto& s : data->outputs) {
        godot::Dictionary entry;
        entry["item_id"] = static_cast<int64_t>(s.item_id());
        entry["amount"] = s.amount;
        outputs.append(entry);
    }

    // Use the first output's item_id as the primary key.
    int64_t item_id = data->outputs.empty() ? 0
        : static_cast<int64_t>(data->outputs[0].item_id());

    add_provider_pattern(provider_id, item_id, inputs, outputs, data->is_crafting);
}

void GDAutocraftingService::_bind_methods() {
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("initialize"),
        &GDAutocraftingService::initialize);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("add_cpu", "cpu"),
        &GDAutocraftingService::add_cpu);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("remove_cpu", "cpu"),
        &GDAutocraftingService::remove_cpu);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("submit_job", "item_id", "amount"),
        &GDAutocraftingService::submit_job);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("tick"),
        &GDAutocraftingService::tick);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("set_network_check_callback", "callback"),
        &GDAutocraftingService::set_network_check_callback);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("set_network_extract_callback", "callback"),
        &GDAutocraftingService::set_network_extract_callback);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("set_network_insert_callback", "callback"),
        &GDAutocraftingService::set_network_insert_callback);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("set_emitable", "item_id", "emitable"),
        &GDAutocraftingService::set_emitable);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("is_item_craftable", "item_id"),
        &GDAutocraftingService::is_item_craftable);

    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("add_provider_pattern", "provider_id", "item_id", "inputs", "outputs", "is_crafting"),
        &GDAutocraftingService::add_provider_pattern, DEFVAL(false));
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("remove_provider_patterns", "provider_id"),
        &GDAutocraftingService::remove_provider_patterns);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("sync_providers"),
        &GDAutocraftingService::sync_providers);

    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("encode_crafting_pattern", "recipe_name"),
        &GDAutocraftingService::encode_crafting_pattern);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("encode_processing_pattern", "machine_type", "recipe_name"),
        &GDAutocraftingService::encode_processing_pattern);
    ClassDB::bind_static_method("GDAutocraftingService",
        D_METHOD("add_encoded_pattern", "provider_id", "encoded_item_id"),
        &GDAutocraftingService::add_encoded_pattern);
}

} // namespace science_and_theology
