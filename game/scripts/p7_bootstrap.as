// P7.1 smoke content. P7.2 replaces this with real machine definitions.
void snt_register() {
    snt_register_machine("snt.p7.bootstrap", "P7 Bootstrap", 1, 0);
    snt_on("p7.bootstrap", "on_p7_bootstrap");
    snt_log("P7 gameplay script registered");
}

void on_p7_bootstrap() {
}
