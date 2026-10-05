#include <cmeta/meta.h>
#include "tinytest.h"

#include <string.h>

typedef struct ScopeProbe {
    int active;
    int id;
} ScopeProbe;

static int scope_destroy_log[16];
static size_t scope_destroy_count;

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
static const cmeta_data_desc ScopeProbe_data_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ScopeProbe.data",
    .display_name = "ScopeProbe",
    .kind = CMETA_DATA_CUSTOM,
    .storage_type = &ScopeProbe_type,
    .construct_ops = &ScopeProbe_construct_ops
};

static const cmeta_data_desc *ScopeProbe_cmeta_data(void) {
    return &ScopeProbe_data_desc;
}

static cmeta_status scope_normal(void) {
    cmeta_status status;
    scope_destroy_count = 0u;

    cmeta_scope(normal, status,
        cmeta_resources((ScopeProbe, first), (ScopeProbe, second)),
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

    cmeta_scope(early, status,
        cmeta_resources((ScopeProbe, first), (ScopeProbe, second)),
        cmeta_body(
            first.active = 1;
            first.id = 10;
            second.active = 1;
            second.id = 20;
            cmeta_scope_exit(early, status, CMETA_CALLBACK_ERROR);
        )
    );

    return status;
}

static cmeta_status scope_move_then_cleanup(void) {
    cmeta_status status;
    scope_destroy_count = 0u;

    cmeta_scope(moved, status,
        cmeta_resources((ScopeProbe, source), (ScopeProbe, destination)),
        cmeta_body(
            source.active = 1;
            source.id = 31;
            if (cmeta_move(ScopeProbe, &destination, &source) != CMETA_OK)
                cmeta_scope_exit(moved, status, CMETA_CALLBACK_ERROR);
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
        check_equal(scope_destroy_log[0], 31);
    }
}
