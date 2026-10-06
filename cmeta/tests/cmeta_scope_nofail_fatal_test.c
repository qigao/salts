#include <cmeta/scope.h>
#include <signal.h>
#include <stdlib.h>

typedef int BrokenNofail;
static cmeta_status broken_init(BrokenNofail *value) {
    *value = 1;
#ifdef __cplusplus
    throw 1;
#else
    return CMETA_CALLBACK_ERROR;
#endif
}
static void forbidden_restore(BrokenNofail *value) {
    (void)value;
    /* A nofail violation cannot silently select fallible rollback. */
    _Exit(EXIT_FAILURE);
}
static void broken_move(BrokenNofail *destination, BrokenNofail *source) {
    *destination = *source;
    *source = 0;
}
CMETA_DEFINE_LIFECYCLE(BrokenNofail, &cmeta_type_int, broken_init,
    forbidden_restore, broken_move, CMETA_LIFECYCLE_INIT_NOFAIL | CMETA_LIFECYCLE_MOVABLE)

static void expected_abort(int signal_number) {
    _Exit(signal_number == SIGABRT ? EXIT_SUCCESS : EXIT_FAILURE);
}
int main(void) {
    cmeta_status status;
    if (signal(SIGABRT, expected_abort) == SIG_ERR) return EXIT_FAILURE;
    cmeta_scope(status, cmeta_autos((BrokenNofail, value)), cmeta_body(CMETA_OK));
    (void)status;
    return EXIT_FAILURE;
}
