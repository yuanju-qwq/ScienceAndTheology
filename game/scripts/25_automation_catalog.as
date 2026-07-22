// Game-owned automation controller catalog.
//
// Processing machines register in 20_machine_catalog.as. Controllers own
// topology and flow state instead, so their stable block identity has a
// separate registration boundary.

const int kAutomationControllerSfmManager = 1;
const int kAutomationControllerAeController = 2;

void snt_register() {
    snt_register_automation_controller_placement(
        "sfm_manager", "automation.sfm_manager",
        kAutomationControllerSfmManager,
        "snt:runtime.automation.sfm_manager");
    snt_register_automation_controller_placement(
        "ae_controller", "automation.ae_controller",
        kAutomationControllerAeController,
        "snt:runtime.automation.ae_controller");
}
