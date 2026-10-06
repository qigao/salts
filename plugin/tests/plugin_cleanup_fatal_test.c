#include <salts/plugin_scope.h>
#include <signal.h>
#include <stdlib.h>

/* A dedicated CTest process must terminate at the violated release invariant;
 * returning normally or calling another cleanup would fail this test. */
static void expected_abort(int signal_number) {
    _Exit(signal_number == SIGABRT ? EXIT_SUCCESS : EXIT_FAILURE);
}

int main(void) {
    cmeta_plugin_registry registry = {0};
    cmeta_plugin_lease lease = {0};
    cmeta_plugin_cleanup_lease owner = {&registry, &lease};
    cmeta_cleanup obligation = CMETA_CLEANUP_INIT;
    if (signal(SIGABRT, expected_abort) == SIG_ERR) return EXIT_FAILURE;
    if (cmeta_plugin_cleanup_arm(&obligation, &owner) != CMETA_OK)
        return EXIT_FAILURE;
    /* Non-null storage with no authoritative registry violates discharge. */
    cmeta_cleanup_run(&obligation);
    return EXIT_FAILURE;
}
