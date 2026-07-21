// Game-owned machine catalog.
//
// This module owns machine definitions, placement mappings, and activation
// requirements. Recipes deliberately live in 30_recipe_catalog.as so a
// machine reload can include its dependent recipes without touching quests.

void snt_register() {
    snt_register_machine("furnace", "Furnace", 1, 0, false);
    snt_set_machine_offline_simulation("furnace", 1, 1200, true);
    snt_register_machine_placement("furnace", "furnace", "snt:runtime.machine.furnace");

    snt_register_machine("pit_kiln", "Pit Kiln", 1, 0, true);
    snt_register_machine_placement("pit_kiln", "pit_kiln", "snt:runtime.machine.pit_kiln");
    snt_set_machine_activation_requirements("pit_kiln", true, true, false, "");

    snt_register_machine("charcoal_pit", "Charcoal Pit", 1, 0, true);
    snt_register_machine_placement("charcoal_pit", "charcoal_pit", "snt:runtime.machine.charcoal_pit");
    snt_set_machine_activation_requirements("charcoal_pit", true, true, false, "");

    snt_register_machine("bloomery", "Bloomery", 1, 0, true);
    snt_register_machine_placement("bloomery", "bloomery", "snt:runtime.machine.bloomery");
    snt_set_machine_activation_requirements("bloomery", false, true, true, "");

    snt_register_machine("anvil", "Anvil", 1, 0, true);
    snt_register_machine_placement("anvil", "anvil", "snt:runtime.machine.anvil");
    snt_set_machine_activation_requirements("anvil", false, false, false, "hammer");
}
