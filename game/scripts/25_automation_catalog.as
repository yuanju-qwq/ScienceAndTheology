// Game-owned automation block catalog.
//
// Processing machines register in 20_machine_catalog.as. Controllers and
// physical AE nodes own topology/flow state instead, so their stable block
// identity has a separate registration boundary.  Drive capacity remains
// authored here behind a stable node_key; sidecars retain only node_key and
// ResourceContentStack values, never content-runtime numeric IDs.

const int kAutomationControllerSfmManager = 1;
const int kAutomationControllerAeController = 2;

const int kAeNodeChannelProvider = 1;
const int kAeNodeDrive = 2;
const int kAeNodeStorageBus = 3;
const int kAeNodeInterface = 4;
const int kAeNodeTerminal = 5;
const int kAeNodeCable = 6;
const int kAeConnectionAll = 63;

void snt_register() {
    snt_register_automation_controller_placement(
        "sfm_manager", "automation.sfm_manager",
        kAutomationControllerSfmManager,
        "snt:runtime.automation.sfm_manager");
    snt_register_automation_controller_placement(
        "ae_controller", "automation.ae_controller",
        kAutomationControllerAeController,
        "snt:runtime.automation.ae_controller");

    snt_register_ae_network_node_placement(
        "ae_cable", "automation.ae_cable", kAeNodeCable,
        "snt:runtime.automation.ae_cable", true, 0, kAeConnectionAll,
        0, 0, 0, 0);
    snt_register_ae_network_node_placement(
        "ae_channel_provider", "automation.ae_channel_provider", kAeNodeChannelProvider,
        "snt:runtime.automation.ae_channel_provider", true, 32, kAeConnectionAll,
        0, 0, 0, 0);
    snt_register_ae_network_node_placement(
        "ae_drive_1k", "automation.ae_drive.1k", kAeNodeDrive,
        "snt:runtime.automation.ae_drive_1k", true, 0, kAeConnectionAll,
        1024, 63, 8, 1);
    snt_register_ae_network_node_placement(
        "ae_storage_bus", "automation.ae_storage_bus", kAeNodeStorageBus,
        "snt:runtime.automation.ae_storage_bus", true, 0, kAeConnectionAll,
        0, 0, 0, 0);
    snt_register_ae_network_node_placement(
        "ae_interface", "automation.ae_interface", kAeNodeInterface,
        "snt:runtime.automation.ae_interface", true, 0, kAeConnectionAll,
        0, 0, 0, 0);
    snt_register_ae_network_node_placement(
        "ae_terminal", "automation.ae_terminal", kAeNodeTerminal,
        "snt:runtime.automation.ae_terminal", true, 0, kAeConnectionAll,
        0, 0, 0, 0);
}
