// Game-owned machine recipe catalog.
//
// Every recipe depends on the item catalog and, through machine_id, on the
// machine catalog. The reload planner therefore reloads this module after a
// requested item or machine reload.

void snt_register() {
    snt_register_recipe("snt.furnace.iron", "furnace", "iron_ore", 1,
                        "ingot.iron", 1, 200, 0, "smelting");

    snt_register_recipe("snt.pit_kiln.fire_unfired_bowl", "pit_kiln", "unfired_bowl", 1,
                        "fired_bowl", 1, 8000, 0, "primitive_thermal");
    snt_register_recipe("snt.pit_kiln.fire_unfired_jug", "pit_kiln", "unfired_jug", 1,
                        "fired_jug", 1, 8000, 0, "primitive_thermal");
    snt_register_recipe("snt.pit_kiln.fire_unfired_crucible", "pit_kiln", "unfired_crucible", 1,
                        "fired_crucible", 1, 12000, 0, "primitive_thermal");
    snt_register_recipe("snt.pit_kiln.fire_unfired_brick", "pit_kiln", "unfired_brick", 1,
                        "refractory_brick", 1, 6000, 0, "primitive_thermal");

    snt_register_recipe("snt.charcoal_pit.burn_wood", "charcoal_pit", "dust.wood", 16,
                        "charcoal", 8, 24000, 0, "primitive_thermal");

    snt_register_recipe("snt.bloomery.iron", "bloomery", "crushed.iron", 5,
                        "iron_bloom", 1, 12000, 0, "primitive_thermal");
    snt_add_recipe_input("snt.bloomery.iron", "charcoal", 5);

    snt_register_recipe("snt.anvil.forge_wrought_iron", "anvil", "iron_bloom", 1,
                        "ingot.wrought_iron", 1, 1, 0, "primitive_forging");
}
