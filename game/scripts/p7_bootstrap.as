// P7.2 primitive thermal and forging content. MachineTickSystem copies these
// definitions at job start, so reloading this file safely changes only new
// machine work. Manual activation is the runtime boundary for future player
// commands to validate structure, cover, light, tools, and fuel.
void snt_register() {
    // Persistent saves and commands use these StringKeys. GameContentRegistry
    // normalizes and sorts the complete set before publishing RuntimeIds.
    snt_register_item("furnace", "item.furnace", 1);
    snt_register_item("pit_kiln", "item.pit_kiln", 1);
    snt_register_item("charcoal_pit", "item.charcoal_pit", 1);
    snt_register_item("bloomery", "item.bloomery", 1);
    snt_register_item("anvil", "item.anvil", 1);
    snt_register_item("iron_ore", "item.iron_ore", 64);
    snt_register_item("iron_ingot", "item.iron_ingot", 64);
    snt_register_item("unfired_bowl", "item.unfired_bowl", 16);
    snt_register_item("fired_bowl", "item.fired_bowl", 16);
    snt_register_item("unfired_jug", "item.unfired_jug", 16);
    snt_register_item("fired_jug", "item.fired_jug", 16);
    snt_register_item("unfired_crucible", "item.unfired_crucible", 16);
    snt_register_item("fired_crucible", "item.fired_crucible", 16);
    snt_register_item("unfired_brick", "item.unfired_brick", 64);
    snt_register_item("refractory_brick", "item.refractory_brick", 64);
    snt_register_item("wood_dust", "item.wood_dust", 64);
    snt_register_item("charcoal", "item.charcoal", 64);
    snt_register_item("iron_crushed", "item.iron_crushed", 64);
    snt_register_item("iron_bloom", "item.iron_bloom", 16);
    snt_register_item("wrought_iron_ingot", "item.wrought_iron_ingot", 64);

    snt_register_machine("furnace", "Furnace", 1, 0, false);
    snt_register_machine_placement("furnace", "furnace", 7);
    snt_register_recipe("snt.furnace.iron", "furnace", "iron_ore", 1,
                        "iron_ingot", 1, 200, 0, "smelting");

    snt_register_machine("pit_kiln", "Pit Kiln", 1, 0, true);
    snt_register_machine_placement("pit_kiln", "pit_kiln", 8);
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
    snt_register_machine_placement("charcoal_pit", "charcoal_pit", 9);
    snt_set_machine_activation_requirements("charcoal_pit", true, true, false, "");
    snt_register_recipe("snt.charcoal_pit.burn_wood", "charcoal_pit", "wood_dust", 16,
                        "charcoal", 8, 24000, 0, "primitive_thermal");

    snt_register_machine("bloomery", "Bloomery", 1, 0, true);
    snt_register_machine_placement("bloomery", "bloomery", 10);
    snt_set_machine_activation_requirements("bloomery", false, true, true, "");
    snt_register_recipe("snt.bloomery.iron", "bloomery", "iron_crushed", 5,
                        "iron_bloom", 1, 12000, 0, "primitive_thermal");
    snt_add_recipe_input("snt.bloomery.iron", "charcoal", 5);

    snt_register_machine("anvil", "Anvil", 1, 0, true);
    snt_register_machine_placement("anvil", "anvil", 11);
    snt_set_machine_activation_requirements("anvil", false, false, false, "hammer");
    snt_register_recipe("snt.anvil.forge_wrought_iron", "anvil", "iron_bloom", 1,
                        "wrought_iron_ingot", 1, 1, 0, "primitive_forging");

    // BQ-style player task line. The chapter and node positions are content
    // data rather than UI code, allowing a server/client content fingerprint
    // to protect the task-book graph from mismatched packages.
    snt_register_quest_chapter("p7.primitive", "Primitive Industry",
                               "Build the first heat and metalworking chain.", "", 0);
    snt_register_quest("p7.primitive.stone", "p7.primitive", "Gather Stone",
                       "Mine stone to establish the first material supply.",
                       80.0f, 180.0f, "", false, false);
    snt_add_quest_objective("p7.primitive.stone", "mine.stone", "mine_block", "stone", 8);
    snt_add_quest_item_reward("p7.primitive.stone", "wood_dust", 8);

    snt_register_quest("p7.primitive.iron", "p7.primitive", "Smelt Iron",
                       "Run the furnace until it produces an iron ingot.",
                       330.0f, 180.0f, "", false, false);
    snt_add_quest_prerequisite("p7.primitive.iron", "p7.primitive.stone");
    snt_add_quest_objective("p7.primitive.iron", "craft.iron_ingot", "craft_item", "iron_ingot", 1);
    snt_add_quest_item_reward("p7.primitive.iron", "charcoal", 4);

    snt_register_quest("p7.primitive.charcoal", "p7.primitive", "Make Charcoal",
                       "Complete a charcoal-pit run for the bloomery fuel chain.",
                       580.0f, 180.0f, "", false, false);
    snt_add_quest_prerequisite("p7.primitive.charcoal", "p7.primitive.iron");
    snt_add_quest_objective("p7.primitive.charcoal", "craft.charcoal", "craft_item", "charcoal", 8);
    snt_add_quest_item_reward("p7.primitive.charcoal", "iron_crushed", 5);

    snt_register_quest("p7.primitive.bloom", "p7.primitive", "Raise a Bloom",
                       "Finish a bloomery cycle and collect the iron bloom.",
                       830.0f, 180.0f, "", false, false);
    snt_add_quest_prerequisite("p7.primitive.bloom", "p7.primitive.charcoal");
    snt_add_quest_objective("p7.primitive.bloom", "craft.iron_bloom", "craft_item", "iron_bloom", 1);
    snt_add_quest_item_reward("p7.primitive.bloom", "wrought_iron_ingot", 1);

    snt_log("P7 primitive machine and task-book content registered");
}
