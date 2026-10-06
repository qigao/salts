#include "plugin_native_fixture.h"
#include "../../cmeta/tests/cmeta_native_targets.h"

static int bias = NATIVE_PLUGIN_BIAS;
static bool stopped;
static plugin_native_offer offer;
const plugin_native_offer *plugin_native_get_offer(void) { return &offer; }
static cmeta_plugin_status CMETA_PLUGIN_CALL native_start(void *self) {
    native_test_bound_capture capture = {self};
    cmeta_status status;
    stopped = false;
    offer.source = FunctionAbi(native_test_context);
    status = native_test_bound_bind(&capture, &offer.reference);
    if (status != CMETA_OK) return CMETA_PLUGIN_INVALID_ARGUMENT;
    status = cmeta_native_context_i32_admit(offer.source, FunctionAbi(native_test_bound),
        native_test_context, self, &offer.binding);
    return status == CMETA_OK ? CMETA_PLUGIN_OK : CMETA_PLUGIN_INVALID_ARGUMENT;
}
static cmeta_plugin_status CMETA_PLUGIN_CALL native_stop(void *self) {
    (void)self;
    stopped = true;
    return CMETA_PLUGIN_OK;
}
static bool CMETA_PLUGIN_CALL native_quiet(const void *self) { (void)self; return stopped; }
static void CMETA_PLUGIN_CALL native_destroy(void *self) {
    (void)self;
    const plugin_native_offer empty = {0};
    offer = empty;
}
#define NATIVE_EXPORTS(X) \
    X(function, plugin_native_get_offer, NATIVE_OFFER_EXPORT, NATIVE_OFFER_CONTRACT, NATIVE_OFFER_VERSION, 0)
CMETA_PLUGIN_DECLARE(native_fixture, "test.plugin.native", (1,0,0), NATIVE_EXPORTS,
    CMETA_PLUGIN_LIFECYCLE(&bias, native_start, native_stop, native_quiet, native_destroy));
