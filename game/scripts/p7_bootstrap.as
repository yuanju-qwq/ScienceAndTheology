// P7.2 primitive thermal and forging content. MachineTickSystem copies these
// definitions at job start, so reloading this file safely changes only new
// machine work. Manual activation is the runtime boundary for future player
// commands to validate structure, cover, light, tools, and fuel.
void snt_register() {
    snt_register_machine("furnace", "Furnace", 1, 0, false);
    snt_register_recipe("snt.furnace.iron", "furnace", "iron_ore", 1,
                        "iron_ingot", 1, 200, 0, "smelting");

    snt_register_machine("pit_kiln", "Pit Kiln", 1, 0, true);
    snt_set_machine_activation_requirements("pit_kiln", true, true, false, "");
    snt_register_recipe("snt.pit_kiln.fire_unfired_bowl", "pit_kiln", "unfired_bowl", 1,
                        "fired_bowl", 1, 8000, 0, "primitive_thermal");
    snt_register_recipe("snt.pit_kiln.fire_unfired_jug", "pit_kiln", "unfired_jug", 1,
                        "fired_jug", 1, 8000, 0, "primitive_thermal");
    snt_register_recipe("snt.pit_kiln.fire_unfired_crucible", "pit_kiln", "unfired_crucible", 1,
                        "fired_crucible", 1, 12000, 0, "primitive_thermal");
    snt_register_recipe("snt.pit_kiln.fire_unfired_brick", "pit_kiln", "unfired_brick", 1,
                        "refractory_brick", 1, 6000, 0, "primitive_thermal");

    snt_register_machine("charcoal_pit", "Charcoal Pit", 1, 0, true);
    snt_set_machine_activation_requirements("charcoal_pit", true, true, false, "");
    snt_register_recipe("snt.charcoal_pit.burn_wood", "charcoal_pit", "wood_dust", 16,
                        "charcoal", 8, 24000, 0, "primitive_thermal");

    snt_register_machine("bloomery", "Bloomery", 1, 0, true);
    snt_set_machine_activation_requirements("bloomery", false, true, true, "");
    snt_register_recipe("snt.bloomery.iron", "bloomery", "iron_crushed", 5,
                        "iron_bloom", 1, 12000, 0, "primitive_thermal");
    snt_add_recipe_input("snt.bloomery.iron", "charcoal", 5);

    snt_register_machine("anvil", "Anvil", 1, 0, true);
    snt_set_machine_activation_requirements("anvil", false, false, false, "hammer");
    snt_register_recipe("snt.anvil.forge_wrought_iron", "anvil", "iron_bloom", 1,
                        "wrought_iron_ingot", 1, 1, 0, "primitive_forging");

    snt_log("P7.2.4 primitive machine gameplay content registered");
}
