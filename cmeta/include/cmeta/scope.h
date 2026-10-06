#ifndef CMETA_SCOPE_H
#define CMETA_SCOPE_H

#include <cmeta/data.h>
#include <cmeta/pp.h>
#include <cmeta/status.h>

#include <stdbool.h>

/*
 * Portable structured lifetime reference.
 *
 * Resources are explicit finite (Type, name) rows. Their canonical DataDesc
 * must expose concrete construct_ops. The same ops initialize semantic zero
 * and restore it in reverse declaration order, without Reflection queries.
 *
 * The body is one ISO C expression returning cmeta_status, normally a call
 * to a typed body function borrowing the resources. Native returns stay in
 * that function; labels cannot cross the function boundary. Block bodies and
 * cross-scope exits are intentionally rejected rather than leaking resources.
 */
#define cmeta_resources(...) (__VA_ARGS__)
#define cmeta_body(expression_) (expression_)

/* @internal Bind canonical concrete construction capability for one resource.
 * General runtime descriptors continue to use the checked data.h APIs. */
CMETA_INLINE cmeta_status cmeta_scope_construct_ops(
    const cmeta_data_desc *data, size_t size, size_t align,
    const cmeta_data_construct_ops **out) {
    const cmeta_data_construct_ops *ops;
    if (out == NULL) return CMETA_INVALID_ARGUMENT;
    *out = NULL;
    if (data == NULL ||
        data->struct_size < offsetof(cmeta_data_desc, construct_ops) +
                                sizeof(data->construct_ops) ||
        data->abi_version != CMETA_DATA_DESC_ABI_VERSION)
        return CMETA_INVALID_ARGUMENT;
    ops = data->construct_ops;
    if (ops == NULL) return CMETA_TRAIT_MISSING;
    if (ops->struct_size < sizeof(*ops) ||
        ops->abi_version != CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION ||
        ops->init_zero == NULL || ops->restore_zero == NULL ||
        data->storage_type == NULL || ops->storage_type == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (data->storage_type->size != size ||
        data->storage_type->align != align ||
        ops->storage_type->size != size || ops->storage_type->align != align)
        return CMETA_TYPE_MISMATCH;
    *out = ops;
    return CMETA_OK;
}

#define CMETA_SCOPE_LABEL_I_(scope_) scope_##__cmeta_cleanup
#define CMETA_SCOPE_LABEL_(scope_) CMETA_SCOPE_LABEL_I_(scope_)
#define CMETA_SCOPE_LIVE_I_(scope_, name_) scope_##__cmeta_live_##name_
#define CMETA_SCOPE_LIVE_(scope_, name_) CMETA_SCOPE_LIVE_I_(scope_, name_)
#define CMETA_SCOPE_OPS_I_(scope_, name_) scope_##__cmeta_ops_##name_
#define CMETA_SCOPE_OPS_(scope_, name_) CMETA_SCOPE_OPS_I_(scope_, name_)

#define CMETA_SCOPE_DECLARE_(row_, scope_) \
    CMETA_SCOPE_DECLARE_EXPAND_(scope_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_DECLARE_EXPAND_(...) CMETA_SCOPE_DECLARE_I_(__VA_ARGS__)
#define CMETA_SCOPE_DECLARE_I_(scope_, type_, name_) \
    type_ name_ = {0}; \
    bool CMETA_SCOPE_LIVE_(scope_, name_) = false; \
    const cmeta_data_construct_ops *CMETA_SCOPE_OPS_(scope_, name_) = NULL;

#define CMETA_SCOPE_INIT_(row_, ctx_) \
    CMETA_SCOPE_INIT_EXPAND_(CMETA_PP_UNPAREN ctx_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_INIT_EXPAND_(...) CMETA_SCOPE_INIT_I_(__VA_ARGS__)
#define CMETA_SCOPE_INIT_I_(scope_, status_, type_, name_)                    \
    do {                                                                       \
        (status_) = cmeta_scope_construct_ops(                                \
            CMETA_DATA_ACCESSOR_(type_)(), sizeof(type_), CMETA_ALIGNOF(type_), \
            &CMETA_SCOPE_OPS_(scope_, name_));                                \
        if ((status_) != CMETA_OK)                                             \
            goto CMETA_SCOPE_LABEL_(scope_);                                  \
        (status_) = CMETA_SCOPE_OPS_(scope_, name_)->init_zero(&(name_));      \
        if ((status_) != CMETA_OK) {                                          \
            CMETA_SCOPE_OPS_(scope_, name_)->restore_zero(&(name_));          \
            goto CMETA_SCOPE_LABEL_(scope_);                                  \
        }                                                                     \
        CMETA_SCOPE_LIVE_(scope_, name_) = true;                              \
    } while (0);

#define CMETA_SCOPE_DESTROY_(row_, scope_) \
    CMETA_SCOPE_DESTROY_EXPAND_(scope_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_DESTROY_EXPAND_(...) CMETA_SCOPE_DESTROY_I_(__VA_ARGS__)
#define CMETA_SCOPE_DESTROY_I_(scope_, type_, name_)                          \
    do {                                                                       \
        if (CMETA_SCOPE_LIVE_(scope_, name_)) {                               \
            CMETA_SCOPE_OPS_(scope_, name_)->restore_zero(&(name_));          \
            CMETA_SCOPE_LIVE_(scope_, name_) = false;                         \
        }                                                                      \
    } while (0);

#define CMETA_SCOPE_ROWS_(resources_) CMETA_SCOPE_ROWS_I_ resources_
#define CMETA_SCOPE_ROWS_I_(...) __VA_ARGS__

#define cmeta_scope_exit(scope_, status_, value_)                             \
    _Static_assert(0, "cmeta_scope_exit is removed; return from the body function")

#define cmeta_scope(scope_, status_, resources_, body_)                       \
    do {                                                                       \
        (status_) = CMETA_OK;                                                  \
        CMETA_PP_FOR_EACH(                                                     \
            CMETA_SCOPE_DECLARE_, scope_, CMETA_SCOPE_ROWS_(resources_))      \
        CMETA_PP_FOR_EACH(                                                     \
            CMETA_SCOPE_INIT_, (scope_, status_), CMETA_SCOPE_ROWS_(resources_)) \
        (status_) = (body_);                                                   \
        goto CMETA_SCOPE_LABEL_(scope_);                                      \
        CMETA_SCOPE_LABEL_(scope_):                                            \
        CMETA_PP_FOR_EACH_REVERSE(                                             \
            CMETA_SCOPE_DESTROY_, scope_, CMETA_SCOPE_ROWS_(resources_))      \
        ;                                                                      \
    } while (0)

#endif /* CMETA_SCOPE_H */
