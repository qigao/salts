#include <salts/plugin_decl.h>
#ifdef __cplusplus
#include "tinytest.hpp"
#else
#include "tinytest.h"
#endif

#ifdef PLUGIN_EMPTY_MANAGED
typedef struct empty_state {
    unsigned starts;
    unsigned stops;
    unsigned destroys;
} empty_state;
static empty_state state;

static salts_plugin_status SALTS_PLUGIN_CALL empty_start(void *self) {
    empty_state *value = CMETA_INVOKE_STORAGE(empty_state,self);
    ++value->starts;
    return SALTS_PLUGIN_OK;
}
static salts_plugin_status SALTS_PLUGIN_CALL empty_stop(void *self) {
    empty_state *value = CMETA_INVOKE_STORAGE(empty_state,self);
    ++value->stops;
    return SALTS_PLUGIN_OK;
}
static bool SALTS_PLUGIN_CALL empty_quiet(const void *self) {
    const empty_state *value = CMETA_INVOKE_STORAGE(const empty_state,self);
    return value->stops == value->starts;
}
static void SALTS_PLUGIN_CALL empty_destroy(void *self) {
    empty_state *value = CMETA_INVOKE_STORAGE(empty_state,self);
    ++value->destroys;
}
#define EMPTY_LIFECYCLE SALTS_PLUGIN_LIFECYCLE(&state,empty_start,empty_stop,empty_quiet,empty_destroy)
#else
#define EMPTY_LIFECYCLE SALTS_PLUGIN_PASSIVE()
#endif

#define EMPTY_NAME empty_plugin
SALTS_PLUGIN_DECLARE_EMPTY(EMPTY_NAME,"test.declarations.empty",(0u,UINT32_MAX,0u),EMPTY_LIFECYCLE);

suite("Plugin empty declarations") {
#ifdef PLUGIN_EMPTY_MANAGED
    before_each() {
        state.starts = state.stops = state.destroys = 0u;
    }
#endif
    it("publishes no exports and preserves the exact query and version contract") {
        const salts_plugin_manifest *manifest = salts_plugin_query(SALTS_PLUGIN_ABI_VERSION);
        check_not_null(manifest);
        check_true(manifest == &empty_plugin__manifest);
        check_equal(salts_plugin_manifest_validate(manifest),SALTS_PLUGIN_OK);
        check_equal(manifest->export_count,0u);
        check_null(manifest->exports);
        check_equal(manifest->version.major,0u);
        check_equal(manifest->version.minor,UINT32_MAX);
        check_equal(manifest->version.patch,0u);
        check_null(salts_plugin_query(SALTS_PLUGIN_ABI_VERSION - 1u));
        check_null(salts_plugin_query(SALTS_PLUGIN_ABI_VERSION + 1u));
        const salts_plugin_export *entry = NULL;
        check_equal(salts_plugin_manifest_find_export(manifest,"absent",&entry),
            SALTS_PLUGIN_UNKNOWN_EXPORT);
        check_null(entry);
    }
    it("preserves explicit lifecycle ownership without running callbacks on query") {
        const salts_plugin_manifest *manifest = salts_plugin_query(SALTS_PLUGIN_ABI_VERSION);
#ifdef PLUGIN_EMPTY_MANAGED
        check_true(manifest->self == &state);
        check_equal(state.starts,0u);
        check_equal(state.stops,0u);
        check_equal(state.destroys,0u);
        check_equal(manifest->start(manifest->self),SALTS_PLUGIN_OK);
        check_false(manifest->is_quiescent(manifest->self));
        check_equal(manifest->request_stop(manifest->self),SALTS_PLUGIN_OK);
        check_true(manifest->is_quiescent(manifest->self));
        manifest->destroy(manifest->self);
        check_equal(state.starts,1u);
        check_equal(state.stops,1u);
        check_equal(state.destroys,1u);
#else
        check_null(manifest->self);
        check_null(manifest->start);
        check_null(manifest->request_stop);
        check_null(manifest->is_quiescent);
        check_null(manifest->destroy);
#endif
    }
}
