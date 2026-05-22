#include "register_types.h"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "hello_world/gd_hello_world.h"
#include "gt_bindings/gd_power_network.h"
#include "gt_bindings/gd_crafting.h"
#include "gt_bindings/gd_fluid_network.h"
#include "gt_bindings/gd_item_pipe_network.h"
#include "gt_bindings/gd_machine.h"
#include "world_bindings/gd_world_data.h"
#include "world_bindings/gd_terrain_generator.h"

#include "core/fluid/fluid_registry.hpp"
#include "core/gt/machine/module.hpp"

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