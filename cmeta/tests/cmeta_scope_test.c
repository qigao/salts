#include <cmeta/meta.h>
#include "tinytest.h"

#include <string.h>

typedef struct ScopeProbe {
    int active;
    int id;
} ScopeProbe;

static int scope_destroy_log[16];
static size_t scope_destroy_count;
static size_t scope_restore_count;
static size_t scope_init_count;
static size_t scope_fail_init_at;

static bool ScopeProbe_is_zero(const void *object) {
    const ScopeProbe *value = (const ScopeProbe *)object;
    return value != NULL && value->active == 0 && value->id == 0;
}

static cmeta_status ScopeProbe_copy(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return CMETA_INVALID_ARGUMENT;
    *(ScopeProbe *)destination = *(const ScopeProbe *)source;
    return CMETA_OK;
}

static void ScopeProbe_restore_zero(void *object) {
    ScopeProbe *value = (ScopeProbe *)object;
    if (value == NULL) return;
    ++scope_restore_count;
    if (value->active != 0 && scope_destroy_count < 16u)
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
    if (scope_fail_init_at != 0u && scope_init_count == scope_fail_init_at)
        return CMETA_CALLBACK_ERROR;
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

static cmeta_status scope_normal(void) {
    cmeta_status status;
    scope_destroy_count = 0u;
    scope_restore_count = 0u;
    scope_init_count = 0u;
    scope_fail_init_at = 0u;

    cmeta_scope(normal, status,
        cmeta_auto(ScopeProbe, first)\n        cmeta_auto(ScopeProbe, second),
        cmeta_body(
            first.active = 1;
            first.id = 1;
            second.active = 1;
            second.id = 2;
        )
    );

    return status;
}

static cmeta_status scope_early_exit(void) {
    cmeta_status status;
    scope_destroy_count = 0u;
    scope_restore_count = 0u;
    scope_init_count = 0u;
    scope_fail_init_at = 0u;

    cmeta_scope(early, status,
        cmeta_auto(ScopeProbe, first)\n        cmeta_auto(ScopeProbe, second),
        cmeta_body(
            first.active = 1;
            first.id = 10;
            second.active = 1;
            second.id = 20;
            cmeta_leave(early, status, CMETA_CALLBACK_ERROR);
        )
    );

    return status;
}

static cmeta_status scope_move_then_cleanup(void) {
    cmeta_status status;
    scope_destroy_count = 0u;
    scope_restore_count = 0u;
    scope_init_count = 0u;
    scope_fail_init_at = 0u;

    cmeta_scope(moved, status,
        cmeta_auto(ScopeProbe, source)\n        cmeta_auto(ScopeProbe, destination),
        cmeta_body(
            source.active = 1;
            source.id = 31;
            if (cmeta_move(ScopeProbe, &destination, &source) != CMETA_OK)
                cmeta_leave(moved, status, CMETA_CALLBACK_ERROR);
        )
    );

    return status;
}


static cmeta_status scope_partial_init_failure(void) {
    cmeta_status status;
    scope_destroy_count = 0u;
    scope_restore_count = 0u;
    scope_init_count = 0u;
    scope_fail_init_at = 2u;

    cmeta_scope(partial, status,
        cmeta_auto(ScopeProbe, first)\n        cmeta_auto(ScopeProbe, second),
        cmeta_body(
            first.active = 1;
            first.id = 41;
            second.active = 1;
            second.id = 42;
        )
    );

    scope_fail_init_at = 0u;
    return status;
}

static cmeta_status scope_nested(void) {
    cmeta_status status;
    cmeta_status inner_status;
    scope_destroy_count = 0u;
    scope_restore_count = 0u;
    scope_init_count = 0u;
    scope_fail_init_at = 0u;

    cmeta_scope(outer, status,
        cmeta_autos(
            cmeta_auto(ScopeProbe, outer_value)
        ),
        cmeta_body(
            outer_value.active = 1;
            outer_value.id = 51;

            cmeta_scope(inner, inner_status,
                cmeta_autos(
                    cmeta_auto(ScopeProbe, inner_value)
                ),
                cmeta_body(
                    inner_value.active = 1;
                    inner_value.id = 52;
                    cmeta_leave(
                        inner, inner_status, CMETA_CALLBACK_ERROR);
                )
            );

            if (inner_status != CMETA_CALLBACK_ERROR)
                cmeta_leave(outer, status, CMETA_CALLBACK_ERROR);
        )
    );

    return status;
}

spec("CMeta structured scope") {
    it("destroys managed values in LIFO order on fallthrough") {
        check_equal(scope_normal(), CMETA_OK);
        check_equal(scope_destroy_count, (size_t)2u);
        check_equal(scope_destroy_log[0], 2);
        check_equal(scope_destroy_log[1], 1);
    }

    it("routes a managed early exit through the same cleanup epilogue") {
        check_equal(scope_early_exit(), CMETA_CALLBACK_ERROR);
        check_equal(scope_destroy_count, (size_t)2u);
        check_equal(scope_destroy_log[0], 20);
        check_equal(scope_destroy_log[1], 10);
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
        check_equal(scope_destroy_count, (size_t)0u);
    }

    it("keeps nested cleanup ordering and propagates managed inner status") {
        check_equal(scope_nested(), CMETA_OK);
        check_equal(scope_restore_count, (size_t)2u);
        check_equal(scope_destroy_count, (size_t)2u);
        check_equal(scope_destroy_log[0], 52);
        check_equal(scope_destroy_log[1], 51);
    }
}
