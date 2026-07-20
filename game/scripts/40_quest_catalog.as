// Game-owned task-book catalog.
//
// Quest progress is stored independently by QuestRegistry. Reloading this
// module changes only definition values and leaves player progress intact.

void snt_register() {
    // BQ-style player task line. The chapter and node positions are content
    // data rather than UI code, allowing a server/client content fingerprint
    // to protect the task-book graph from mismatched packages.
    snt_register_quest_chapter("p7.primitive", "Primitive Industry",
                               "Build the first heat and metalworking chain.", "", 0);
    snt_register_quest("p7.primitive.stone", "p7.primitive", "Gather Stone",
                       "Mine stone to establish the first material supply.",
                       80.0f, 180.0f, "", false, false);
    snt_add_quest_objective("p7.primitive.stone", "mine.stone", "mine_block", "stone", 8);
    snt_add_quest_item_reward("p7.primitive.stone", "dust.wood", 8);

    snt_register_quest("p7.primitive.iron", "p7.primitive", "Smelt Iron",
                       "Run the furnace until it produces an iron ingot.",
                       330.0f, 180.0f, "", false, false);
    snt_add_quest_prerequisite("p7.primitive.iron", "p7.primitive.stone");
    snt_add_quest_objective("p7.primitive.iron", "craft.ingot.iron", "craft_item", "ingot.iron", 1);
    snt_add_quest_item_reward("p7.primitive.iron", "charcoal", 4);

    snt_register_quest("p7.primitive.charcoal", "p7.primitive", "Make Charcoal",
                       "Complete a charcoal-pit run for the bloomery fuel chain.",
                       580.0f, 180.0f, "", false, false);
    snt_add_quest_prerequisite("p7.primitive.charcoal", "p7.primitive.iron");
    snt_add_quest_objective("p7.primitive.charcoal", "craft.charcoal", "craft_item", "charcoal", 8);
    snt_add_quest_item_reward("p7.primitive.charcoal", "crushed.iron", 5);

    snt_register_quest("p7.primitive.bloom", "p7.primitive", "Raise a Bloom",
                       "Finish a bloomery cycle and collect the iron bloom.",
                       830.0f, 180.0f, "", false, false);
    snt_add_quest_prerequisite("p7.primitive.bloom", "p7.primitive.charcoal");
    snt_add_quest_objective("p7.primitive.bloom", "craft.iron_bloom", "craft_item", "iron_bloom", 1);
    snt_add_quest_item_reward("p7.primitive.bloom", "ingot.wrought_iron", 1);
}
