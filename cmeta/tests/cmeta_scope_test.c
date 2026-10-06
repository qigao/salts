#include <cmeta/meta.h>
#include <cmeta/scope.h>
#include "tinytest.h"

#include <string.h>

typedef struct ScopeProbe {
    int active;
    int id;
} ScopeProbe;

enum { SCOPE_LOG_CAPACITY = 16 };
static int scope_destroy_log[SCOPE_LOG_CAPACITY];
static size_t scope_destroy_count;
static size_t scope_restore_count;
static size_t scope_init_count;
static size_t scope_fail_init_at;
static size_t scope_body_count;

static void ScopeProbe_restore_zero(void *object) {
    ScopeProbe *value = (ScopeProbe *)object;
    if (value == NULL) return;
    ++scope_restore_count;
    if (value->active != 0 && scope_destroy_count < SCOPE_LOG_CAPACITY)
        scope_destroy_log[scope_destroy_count++] = value->id;
    memset(value, 0, sizeof(*value));
}

static const cmeta_type_identity ScopeProbe_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.ScopeProbe");
static const cmeta_type_desc ScopeProbe_type = {
    .name = "ScopeProbe",
    .size = sizeof(ScopeProbe),
    .align = _Alignof(ScopeProbe),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &ScopeProbe_identity
};
static cmeta_status ScopeProbe_init_zero(void *object) {
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    ++scope_init_count;
    if (scope_fail_init_at != 0u && scope_init_count == scope_fail_init_at) {
        ((ScopeProbe *)object)->active = 1;
        ((ScopeProbe *)object)->id = 99;
        return CMETA_CALLBACK_ERROR;
    }
    memset(object, 0, sizeof(ScopeProbe));
    return CMETA_OK;
}

static void ScopeProbe_construct_restore_zero(void *object) {
    ScopeProbe_restore_zero(object);
}

static void ScopeProbe_move(void *destination, void *source) {
    ScopeProbe *dst = (ScopeProbe *)destination;
    ScopeProbe *src = (ScopeProbe *)source;
    if (dst == NULL || src == NULL) return;
    *dst = *src;
    memset(src, 0, sizeof(*src));
}

static const cmeta_data_construct_ops ScopeProbe_construct_ops = {
    .struct_size = sizeof(cmeta_data_construct_ops),
    .abi_version = CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION,
    .storage_type = &ScopeProbe_type,
    .init_zero = ScopeProbe_init_zero,
    .restore_zero = ScopeProbe_construct_restore_zero,
    .move = ScopeProbe_move
};
static const unsigned char ScopeProbe_shape = 0u;

static const cmeta_data_desc ScopeProbe_data_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ScopeProbe.data",
    .display_name = "ScopeProbe",
    .kind = CMETA_DATA_CUSTOM,
    .storage_type = &ScopeProbe_type,
    .shape = &ScopeProbe_shape,
    .construct_ops = &ScopeProbe_construct_ops
};

static const cmeta_data_desc *ScopeProbe_cmeta_data(void) {
    return &ScopeProbe_data_desc;
}

static void scope_reset(size_t fail_init_at) {
    scope_destroy_count = 0u;
    scope_restore_count = 0u;
    scope_init_count = 0u;
    scope_fail_init_at = fail_init_at;
    scope_body_count = 0u;
}

static cmeta_status scope_normal_body(ScopeProbe *first, ScopeProbe *second) {
    ++scope_body_count;
    first->active = 1;
    first->id = 1;
    second->active = 1;
    second->id = 2;
    return CMETA_OK;
}

static cmeta_status scope_normal(void) {
    cmeta_status status;
    scope_reset(0u);

    cmeta_scope(status,
        cmeta_autos((ScopeProbe, first), (ScopeProbe, second)),
        cmeta_body(scope_normal_body(&first, &second))
    );

    return status;
}

static cmeta_status scope_early_body(ScopeProbe *first, ScopeProbe *second) {
    ++scope_body_count;
    first->active = 1;
    first->id = 10;
    second->active = 1;
    second->id = 20;
    return CMETA_CALLBACK_ERROR;
}

static cmeta_status scope_early_exit(void) {
    cmeta_status status;
    scope_reset(0u);

    cmeta_scope(status,
        cmeta_autos((ScopeProbe, first), (ScopeProbe, second)),
        cmeta_body(scope_early_body(&first, &second))
    );

    return status;
}

static cmeta_status scope_move_body(
    ScopeProbe *source, ScopeProbe *destination) {
    source->active = 1;
    source->id = 31;
    return cmeta_move(ScopeProbe, destination, source);
}

static cmeta_status scope_move_then_cleanup(void) {
    cmeta_status status;
    scope_reset(0u);

    cmeta_scope(status,
        cmeta_autos((ScopeProbe, source), (ScopeProbe, destination)),
        cmeta_body(scope_move_body(&source, &destination))
    );

    return status;
}


static cmeta_status scope_partial_init_failure(void) {
    cmeta_status status;
    scope_reset(2u);

    cmeta_scope(status,
        cmeta_autos((ScopeProbe, first), (ScopeProbe, second)),
        cmeta_body(scope_normal_body(&first, &second))
    );

    scope_fail_init_at = 0u;
    return status;
}

static cmeta_status scope_inner_body(ScopeProbe *inner_value) {
    inner_value->active = 1;
    inner_value->id = 52;
    return CMETA_CALLBACK_ERROR;
}

static cmeta_status scope_outer_body(ScopeProbe *outer_value) {
    cmeta_status inner_status;
    outer_value->active = 1;
    outer_value->id = 51;
    cmeta_scope(inner_status,
        cmeta_autos((ScopeProbe, inner_value)),
        cmeta_body(scope_inner_body(&inner_value))
    );
    /* Propagating the inner error cannot bypass either scope's cleanup. */
    return inner_status;
}

static cmeta_status scope_nested(void) {
    cmeta_status status;
    scope_reset(0u);

    cmeta_scope(status,
        cmeta_autos((ScopeProbe, outer_value)),
        cmeta_body(scope_outer_body(&outer_value))
    );

    return status;
}

enum ScopeBodyExit { SCOPE_BODY_RETURN, SCOPE_BODY_GOTO, SCOPE_BODY_BREAK };

static cmeta_status scope_native_exit_body(
    ScopeProbe *value, enum ScopeBodyExit mode) {
    value->active = 1;
    value->id = 61;
    for (;;) {
        if (mode == SCOPE_BODY_RETURN) return CMETA_CALLBACK_ERROR;
        if (mode == SCOPE_BODY_GOTO) goto body_done;
        break;
    }
body_done:
    return CMETA_CALLBACK_ERROR;
}

static cmeta_status scope_native_exit(enum ScopeBodyExit mode) {
    cmeta_status status;
    scope_reset(0u);
    cmeta_scope(status, cmeta_autos((ScopeProbe, value)),
        cmeta_body(scope_native_exit_body(&value, mode)));
    return status;
}

enum { SCOPE_MAX_RESOURCES = 16 };

static cmeta_status scope_max_body(ScopeProbe *const *values) {
    size_t i;
    ++scope_body_count;
    for (i = 0u; i < SCOPE_MAX_RESOURCES; ++i) {
        values[i]->active = 1;
        values[i]->id = (int)i;
    }
    return CMETA_OK;
}

static cmeta_status scope_max_resources(size_t fail_at) {
    cmeta_status status;
    scope_reset(fail_at);
    cmeta_scope(status,
        cmeta_autos(
            (ScopeProbe, a), (ScopeProbe, b), (ScopeProbe, c), (ScopeProbe, d),
            (ScopeProbe, e), (ScopeProbe, f), (ScopeProbe, g), (ScopeProbe, h),
            (ScopeProbe, i), (ScopeProbe, j), (ScopeProbe, k), (ScopeProbe, l),
            (ScopeProbe, m), (ScopeProbe, n), (ScopeProbe, o), (ScopeProbe, p)),
        cmeta_body(scope_max_body((ScopeProbe *const[]){
            &a, &b, &c, &d, &e, &f, &g, &h,
            &i, &j, &k, &l, &m, &n, &o, &p})));
    return status;
}

spec("CMeta structured scope") {
    it("generates distinct cleanup labels for scopes on the same source line") {
        cmeta_status first;
        cmeta_status second;
        scope_reset(0u);
        cmeta_scope(first, cmeta_autos((ScopeProbe, value)), cmeta_body(scope_inner_body(&value))); cmeta_scope(second, cmeta_autos((ScopeProbe, value)), cmeta_body(scope_inner_body(&value)));
        check_equal(first, CMETA_CALLBACK_ERROR);
        check_equal(second, CMETA_CALLBACK_ERROR);
        check_equal(scope_init_count, (size_t)2u);
        check_equal(scope_restore_count, (size_t)2u);
        check_equal(scope_destroy_count, (size_t)2u);
    }

    it("replays the full finite resource list in exact reverse order") {
        size_t i;
        check_equal(scope_max_resources(0u), CMETA_OK);
        check_equal(scope_body_count, (size_t)1u);
        check_equal(scope_restore_count, (size_t)SCOPE_MAX_RESOURCES);
        check_equal(scope_destroy_count, (size_t)SCOPE_MAX_RESOURCES);
        for (i = 0u; i < SCOPE_MAX_RESOURCES; ++i)
            check_equal(scope_destroy_log[i], (int)(SCOPE_MAX_RESOURCES - i - 1u));
    }

    it("rolls back every possible partial construction prefix") {
        size_t fail_at;
        for (fail_at = 1u; fail_at <= SCOPE_MAX_RESOURCES; ++fail_at) {
            check_equal(scope_max_resources(fail_at), CMETA_CALLBACK_ERROR);
            check_equal(scope_init_count, fail_at);
            check_equal(scope_restore_count, fail_at);
            check_equal(scope_destroy_count, (size_t)1u);
            check_equal(scope_destroy_log[0], 99);
            check_equal(scope_body_count, (size_t)0u);
        }
    }

    it("keeps native return, goto and break inside the body function") {
        const enum ScopeBodyExit modes[] = {
            SCOPE_BODY_RETURN, SCOPE_BODY_GOTO, SCOPE_BODY_BREAK};
        size_t i;
        for (i = 0u; i < sizeof(modes) / sizeof(modes[0]); ++i) {
            check_equal(scope_native_exit(modes[i]), CMETA_CALLBACK_ERROR);
            check_equal(scope_restore_count, (size_t)1u);
            check_equal(scope_destroy_count, (size_t)1u);
            check_equal(scope_destroy_log[0], 61);
        }
    }

    it("rejects missing or mismatched concrete lifecycle before construction") {
        cmeta_data_desc data = ScopeProbe_data_desc;
        cmeta_data_construct_ops invalid = ScopeProbe_construct_ops;
        const cmeta_data_construct_ops *ops = NULL;
        scope_reset(0u);
        data.construct_ops = NULL;
        check_equal(cmeta_scope_construct_ops(
            &data, sizeof(ScopeProbe), _Alignof(ScopeProbe), &ops),
            CMETA_TRAIT_MISSING);
        check_null(ops);
        data.construct_ops = &invalid;
        invalid.storage_type = &cmeta_type_int;
        check_equal(cmeta_scope_construct_ops(
            &data, sizeof(ScopeProbe), _Alignof(ScopeProbe), &ops),
            CMETA_TYPE_MISMATCH);
        check_null(ops);
        check_equal(scope_init_count, (size_t)0u);
        invalid = ScopeProbe_construct_ops;
        invalid.move = NULL;
        check_equal(cmeta_scope_construct_ops(
            &data, sizeof(ScopeProbe), _Alignof(ScopeProbe), &ops), CMETA_OK);
        check_true(ops == &invalid);
    }

    it("destroys managed values in LIFO order on fallthrough") {
        check_equal(scope_normal(), CMETA_OK);
        check_equal(scope_destroy_count, (size_t)2u);
        check_equal(scope_destroy_log[0], 2);
        check_equal(scope_destroy_log[1], 1);
    }

    it("cleans all resources after a native early body return") {
        check_equal(scope_early_exit(), CMETA_CALLBACK_ERROR);
        check_equal(scope_destroy_count, (size_t)2u);
        check_equal(scope_destroy_log[0], 20);
        check_equal(scope_destroy_log[1], 10);
        check_equal(scope_restore_count, (size_t)2u);
        check_equal(scope_body_count, (size_t)1u);
    }

    it("safely cleans a moved-from source after destination") {
        check_equal(scope_move_then_cleanup(), CMETA_OK);
        check_equal(scope_destroy_count, (size_t)1u);
        check_equal(scope_restore_count, (size_t)2u);
        check_equal(scope_destroy_log[0], 31);
    }

    it("destroys only successfully initialized values after partial init failure") {
        check_equal(scope_partial_init_failure(), CMETA_CALLBACK_ERROR);
        check_equal(scope_init_count, (size_t)2u);
        /* construct init failure restores the failed slot once; scope cleanup
         * then restores only the earlier successfully initialized value. */
        check_equal(scope_restore_count, (size_t)2u);
        check_equal(scope_destroy_count, (size_t)1u);
        check_equal(scope_destroy_log[0], 99);
        check_equal(scope_body_count, (size_t)0u);
    }

    it("keeps nested cleanup ordering and propagates managed inner status") {
        check_equal(scope_nested(), CMETA_CALLBACK_ERROR);
        check_equal(scope_restore_count, (size_t)2u);
        check_equal(scope_destroy_count, (size_t)2u);
        check_equal(scope_destroy_log[0], 52);
        check_equal(scope_destroy_log[1], 51);
    }
}
