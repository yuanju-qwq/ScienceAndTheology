#include "register_types.h"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "hello_world/gd_hello_world.h"
#include "network/gd_power_network.h"
#include "crafting/gd_crafting.h"
#include "ae2/gd_ae2_autocrafting.h"
#include "ae2/gd_me_network.h"
#include "network/gd_fluid_network.h"
#include "network/gd_item_pipe_network.h"
#include "machine/gd_machine.h"
#include "world/gd_world_data.h"
#include "world/gd_terrain_generator.h"
#include "simulation/gd_tick_system.h"
#include "player/gd_player_inventory.h"
#include "player/gd_player_equipment.h"

#include "core/fluid/fluid_registry.hpp"
#include "core/machine/module.hpp"

using namespace godot;

namespace science_and_theology {

void initialize_snt_extension(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    gt::FluidRegistry::initialize();
    gt::ModuleRegistry::initialize();

    ClassDB::register_class<GDHelloWorld>();
    ClassDB::register_class<GDPowerNetwork>();
    ClassDB::register_class<GDCraftingGrid>();
    ClassDB::register_class<GDCraftingManager>();
    ClassDB::register_class<GDFluidNetwork>();
    ClassDB::register_class<GDItemPipeNetwork>();
    ClassDB::register_class<GDMachine>();
    ClassDB::register_class<GDWorldData>();
    ClassDB::register_class<GDTerrainGenerator>();
    ClassDB::register_class<GDAutocraftingCPU>();
    ClassDB::register_class<GDAutocraftingService>();
    ClassDB::register_class<GDMENetwork>();
    ClassDB::register_class<GDTickSystem>();
    ClassDB::register_class<GDPlayerInventory>();
    ClassDB::register_class<GDPlayerEquipment>();
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
