// P7.2 starter content. MachineTickSystem copies these definitions at job
// start, so reloading this file safely changes only new machine work.
void snt_register() {
    snt_register_machine("furnace", "Furnace", 1, 0);
    snt_register_recipe("snt.furnace.iron", "furnace", "iron_ore",
                        "iron_ingot", 1, 200, 0, "smelting");
    snt_log("P7.2 furnace gameplay content registered");
}
