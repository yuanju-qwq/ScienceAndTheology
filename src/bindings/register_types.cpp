#include "register_types.h"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "network/gd_power_network.h"
#include "network/gd_signal_network.h"
#include "crafting/gd_crafting.h"
#include "ae2/gd_ae2_autocrafting.h"
#include "ae2/gd_me_network.h"
#include "network/gd_fluid_network.h"
#include "fluid/gd_fluid_registry.h"
#include "item/gd_item_registry.h"
#include "material/gd_material_registry.hpp"
#include "network/gd_item_pipe_network.h"
#include "machine/gd_recipe_database.h"
#include "machine/gd_machine_definition_registry.h"
#include "world/gd_world_data.h"
#include "world/gd_world_gen_config.h"
#include "world/gd_terrain_content_registry.h"
#include "world/gd_terrain_generator.h"
#include "world/gd_furnace_manager.h"
#include "world/gd_charcoal_pit.h"
#include "world/gd_pit_kiln.h"
#include "world/gd_bloomery.h"
#include "world/gd_anvil.h"
#include "world/gd_chunk_helper.h"
#include "world/gd_planet_shell_helper.h"
#include "world/gd_chunk_persistence_helper.h"
#include "world/gd_planet_lod.hpp"
#include "world/gd_planet_build_frame.h"
#include "world/gd_custom_block_entity_registry.h"
#include "simulation/gd_game_command_server.h"
#include "simulation/gd_tick_system.h"
#include "mobile_structure/gd_ship_command_bridge.h"
#include "player/gd_player_inventory.h"
#include "player/gd_player_equipment.h"
#include "player/gd_player_accessory.h"
#include "player/gd_player_helper.h"
#include "magic/gd_rune_registry.hpp"
#include "magic/gd_glyph_registry.hpp"
#include "magic/gd_glyph_conversion.hpp"
#include "magic/gd_source_law_weapon.hpp"
#include "magic/gd_spell_book.hpp"
#include "magic/gd_mana_pool.hpp"
#include "fuel/gd_fuel_registry.hpp"
#include "loot/gd_loot_table_registry.h"
#include "source_law/gd_player_source_law_data.hpp"
#include "player/gd_satiation_data.hpp"
#include "quest/gd_quest_system.hpp"
#include "multiblock/gd_multiblock_builder.h"
#include "multiblock/gd_multiblock_controller.h"
#include "multiplayer/gd_network_server.hpp"
#include "multiplayer/gd_network_client.hpp"
#include "multiplayer/gd_prediction_buffer.hpp"
#include "sfm/gd_sfm_manager.h"

#include "core/fluid/fluid_registry.hpp"
#include "core/fuel/fuel_registry.hpp"
#include "core/loot/loot_table_registry.hpp"
#include "core/machine/recipe.hpp"
#include "core/machine/machine_definition_registry.hpp"
#include "core/magic/rune_registry.hpp"
#include "core/magic/glyph_registry.hpp"
#include "core/magic/glyph_conversion.hpp"
#include "core/magic/ritual_recipe_registry.hpp"
// material.hpp and material_item.hpp no longer need direct includes;
// materials are now registered from GDScript via GDMaterialRegistry.
#include "core/crafting/crafting.hpp"
#include "core/source_law/elixir_registry.hpp"
#include "core/source_law/sublimation_path_registry.hpp"

using namespace godot;

namespace science_and_theology {

void initialize_snt_extension(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    gt::FluidRegistry::initialize();
    magic::RuneRegistry::initialize();
    magic::GlyphRegistry::initialize();
    magic::GlyphConversion::initialize();
    magic::RitualRecipeRegistry::initialize();
    // Materials & items are now registered from GDScript via GDMaterialRegistry.
    // FuelRegistry is re-initialized during MaterialRegistry::finalize()
    // so material-based fuels (coal, wood) can resolve item IDs.
    gt::FuelRegistry::initialize();
    gt::LootTableRegistry::initialize();
    gt::MachineDefinitionRegistry::initialize();
    gt::CraftingManager::initialize();
    gt::RecipeDatabase::initialize();
    source_law::ElixirRegistry::initialize();
    source_law::SublimationPathRegistry::initialize();

    ClassDB::register_class<GDPowerNetwork>();
    ClassDB::register_class<GDSignalNetwork>();
    ClassDB::register_class<GDCraftingManager>();
    ClassDB::register_class<GDFluidNetwork>();
    ClassDB::register_class<GDFluidRegistry>();
    ClassDB::register_class<GDItemRegistry>();
    ClassDB::register_class<GDMaterialRegistry>();
    ClassDB::register_class<GDItemPipeNetwork>();
    ClassDB::register_class<GDRecipeDatabase>();
    ClassDB::register_class<GDMachineDefinitionRegistry>();
    ClassDB::register_class<GDWorldData>();
    ClassDB::register_class<GDWorldGenConfig>();
    ClassDB::register_class<GDTerrainContentRegistry>();
    ClassDB::register_class<GDTerrainGenerator>();
    ClassDB::register_class<GDFurnaceData>();
    ClassDB::register_class<GDFurnaceManager>();
    ClassDB::register_class<GDCharcoalPitData>();
    ClassDB::register_class<GDCharcoalPitManager>();
    ClassDB::register_class<GDPitKilnData>();
    ClassDB::register_class<GDPitKilnManager>();
    ClassDB::register_class<GDBloomeryData>();
    ClassDB::register_class<GDBloomeryManager>();
    ClassDB::register_class<GDAnvilManager>();
    ClassDB::register_class<GDChunkHelper>();
    ClassDB::register_class<GDPlanetShellHelper>();
    ClassDB::register_class<GDChunkPersistenceHelper>();
    ClassDB::register_class<GDPlanetLod>();
    ClassDB::register_class<GDPlanetBuildFrame>();
    ClassDB::register_class<GDCustomBlockEntityRegistry>();
    ClassDB::register_class<GDGameCommandServer>();
    ClassDB::register_class<GDShipCommandBridge>();
    ClassDB::register_class<GDAutocraftingCPU>();
    ClassDB::register_class<GDAutocraftingService>();
    ClassDB::register_class<GDMENetwork>();
    ClassDB::register_class<GDTickSystem>();
    ClassDB::register_class<GDPlayerInventory>();
    ClassDB::register_class<GDPlayerEquipment>();
    ClassDB::register_class<GDPlayerAccessory>();
    ClassDB::register_class<GDPlayerHelper>();
    ClassDB::register_class<GDRuneRegistry>();
    ClassDB::register_class<GDGlyphRegistry>();
    ClassDB::register_class<GDGlyphConversion>();
    ClassDB::register_class<GDSourceLawWeapon>();
    ClassDB::register_class<GDSpellBook>();
    ClassDB::register_class<GDManaPool>();
    ClassDB::register_class<GDFuelRegistry>();
    ClassDB::register_class<GDLootTableRegistry>();
    ClassDB::register_class<GDPlayerSourceLawData>();
    ClassDB::register_class<GDSatiationData>();
    ClassDB::register_class<GDQuestSystem>();
    ClassDB::register_class<GDMultiblockBuilder>();
    ClassDB::register_class<GDMultiblockController>();
    ClassDB::register_class<GDNetworkServer>();
    ClassDB::register_class<GDNetworkClient>();
    ClassDB::register_class<GDPredictionBuffer>();
    ClassDB::register_class<GDFlowManager>();
    GDAutocraftingService::initialize();
}

void uninitialize_snt_extension(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

} // namespace science_and_theology

extern "C" {

GDExtensionBool GDE_EXPORT science_and_theology_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {

    godot::GDExtensionBinding::InitObject init_obj(
        p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(science_and_theology::initialize_snt_extension);
    init_obj.register_terminator(science_and_theology::uninitialize_snt_extension);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}

} // extern "C"
