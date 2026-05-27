#pragma once

#include <memory>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/ae2/ae2_crafting_service.hpp"
#include "core/ae2/ae2_crafting_cpu.hpp"

namespace science_and_theology {

// GDExtension wrapper for a crafting CPU.
// Used by the game to create and configure crafting CPUs.
class GDAutocraftingCPU : public godot::Resource {
    GDCLASS(GDAutocraftingCPU, godot::Resource)

public:
    GDAutocraftingCPU();
    ~GDAutocraftingCPU() override;

    void configure(int64_t storage_bytes, int co_processors);

    bool is_busy() const;
    int64_t get_storage_bytes() const;
    int64_t get_available_storage() const;
    int get_co_processors() const;
    void cancel_job();

    // Insert an item result into the CPU.
    // Called by machines (Molecular Assembler, processors) when they finish.
    int64_t insert_item(int64_t item_id, int64_t amount);

    // Set callback for crafting pattern dispatch (Molecular Assembler).
    // Callback receives: Dictionary { item_id: int, count: int }
    // Returns bool: true if the craft was accepted by an assembler.
    void set_craft_executor(const godot::Callable& callback);

    // Set callback for processing pattern dispatch (external machines).
    // Callback receives: Dictionary { item_id: int, count: int }
    // Returns bool: true if the processing was accepted by a machine.
    void set_process_executor(const godot::Callable& callback);

    gt::CraftingCPU* get_cpu() { return cpu_.get(); }
    const gt::CraftingCPU* get_cpu() const { return cpu_.get(); }

protected:
    static void _bind_methods();

private:
    std::unique_ptr<gt::CraftingCPU> cpu_;
};

// GDExtension wrapper for the global autocrafting service.
// Singleton-like: all methods are static.
class GDAutocraftingService : public godot::Object {
    GDCLASS(GDAutocraftingService, godot::Object)

public:
    GDAutocraftingService() = default;
    ~GDAutocraftingService() override = default;

    // Initialize the service (patterns, resolvers).
    static void initialize();

    // Register a CPU with the service.
    static void add_cpu(const godot::Ref<GDAutocraftingCPU>& cpu);
    static void remove_cpu(const godot::Ref<GDAutocraftingCPU>& cpu);

    // Submit a crafting job.
    // Returns Dictionary with:
    //   "success": bool
    //   "error_message": String
    //   "plan": Dictionary (if success)
    static godot::Dictionary submit_job(int64_t item_id, int64_t amount);

    // Tick all active CPUs. Call each game tick.
    static void tick();

    // Set network callbacks.
    static void set_network_check_callback(
        const godot::Callable& callback);
    static void set_network_extract_callback(
        const godot::Callable& callback);
    static void set_network_insert_callback(
        const godot::Callable& callback);

    // Set emitable item.
    static void set_emitable(int64_t item_id, bool emitable);

    // Check if an item is craftable.
    static bool is_item_craftable(int64_t item_id);

    // --- Pattern provider API ---
    // For machines (e.g. ME Interface) to provide patterns dynamically.

    // Add a single pattern for an external provider.
    // provider_id: unique ID for the provider machine.
    // inputs: Array of Dictionary {item_id: int, amount: int}
    // outputs: Array of Dictionary {item_id: int, amount: int}
    // is_crafting: true for crafting table patterns, false for processing.
    static void add_provider_pattern(int64_t provider_id, int64_t item_id,
                                     const godot::Array& inputs,
                                     const godot::Array& outputs,
                                     bool is_crafting = false);

    // Remove all patterns for a given provider_id.
    static void remove_provider_patterns(int64_t provider_id);

    // Re-sync all registered providers (call after provider data changes).
    static void sync_providers();

    // --- Pattern encoding ---
    // Encode a recipe into an encoded pattern item.
    // Returns Dictionary: { item_id: int, success: bool, error: String }

    // Encode a manual crafting recipe by name.
    static godot::Dictionary encode_crafting_pattern(const godot::String& recipe_name);

    // Encode a machine processing recipe by name and machine type.
    static godot::Dictionary encode_processing_pattern(
        const godot::String& machine_type, const godot::String& recipe_name);

    // Add a pattern from an encoded pattern item.
    // Wraps add_provider_pattern using data from PatternDataCache.
    static void add_encoded_pattern(int64_t provider_id, int64_t encoded_item_id);

protected:
    static void _bind_methods();

private:
    static gt::CraftingService& service();
};

} // namespace science_and_theology
