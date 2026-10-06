#ifndef CMETA_SCOPE_H
#define CMETA_SCOPE_H

#include <cmeta/lifecycle.h>
#include <cmeta/pp.h>
#include <cmeta/status.h>

#include <stdbool.h>

/*
 * Portable structured lifetime facade.
 *
 * cmeta_auto rows in cmeta_autos are explicit finite resources. Their DataDesc
 * must expose concrete construct_ops. The same ops initialize semantic zero
 * and restore it in reverse declaration order, without Reflection queries.
 *
 * The body is one ISO C expression returning cmeta_status, normally a call
 * to a typed body function borrowing the resources. Native returns stay in
 * that function; labels cannot cross the function boundary. Block bodies and
 * cross-scope exits are intentionally rejected rather than leaking resources.
 */
#define cmeta_auto(type_, name_) , (type_, name_)
#define cmeta_autos(...) (__VA_ARGS__)
#define cmeta_body(expression_) (expression_)

/* @internal Bind canonical concrete construction capability for one resource.
 * General runtime descriptors continue to use the checked data.h APIs. */
CMETA_INLINE cmeta_status cmeta_scope_construct_ops(
    const cmeta_data_desc *data, size_t size, size_t align,
    const cmeta_data_construct_ops **out) {
    return cmeta_lifecycle_bind(data, size, align, out);
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

/* cmeta_auto deliberately emits a leading comma. Prefixing a sentinel turns
 * the field stream into ordinary variadic arguments; this trampoline drops the
 * sentinel and preserves the finite row list for forward/reverse replay. */
#define CMETA_SCOPE_ROWS_(autos_) \
    CMETA_SCOPE_ROWS_EXPAND_(cmeta_scope_auto_sentinel CMETA_PP_UNPAREN autos_)
#define CMETA_SCOPE_ROWS_EXPAND_(...) CMETA_SCOPE_ROWS_DROP_(__VA_ARGS__)
#define CMETA_SCOPE_ROWS_DROP_(sentinel_, ...) __VA_ARGS__

#define CMETA_SCOPE_DECLARE_ALL_(scope_, autos_) \
    CMETA_SCOPE_DECLARE_ALL_EXPAND_(scope_, CMETA_SCOPE_ROWS_(autos_))
#define CMETA_SCOPE_DECLARE_ALL_EXPAND_(scope_, ...) \
    CMETA_PP_FOR_EACH(CMETA_SCOPE_DECLARE_, scope_, __VA_ARGS__)

#define CMETA_SCOPE_INIT_ALL_(scope_, status_, autos_) \
    CMETA_SCOPE_INIT_ALL_EXPAND_(scope_, status_, CMETA_SCOPE_ROWS_(autos_))
#define CMETA_SCOPE_INIT_ALL_EXPAND_(scope_, status_, ...) \
    CMETA_PP_FOR_EACH(CMETA_SCOPE_INIT_, (scope_, status_), __VA_ARGS__)

#define CMETA_SCOPE_DESTROY_ALL_(scope_, autos_) \
    CMETA_SCOPE_DESTROY_ALL_EXPAND_(scope_, CMETA_SCOPE_ROWS_(autos_))
#define CMETA_SCOPE_DESTROY_ALL_EXPAND_(scope_, ...) \
    CMETA_PP_FOR_EACH_REVERSE(CMETA_SCOPE_DESTROY_, scope_, __VA_ARGS__)

#define cmeta_scope_exit(scope_, status_, value_)                             \
    _Static_assert(0, "cmeta_scope_exit is removed; return from the body function")

#define cmeta_leave(scope_, status_, value_) \
    _Static_assert(0, "cmeta_leave is removed; return from the body function")

#define cmeta_scope(scope_, status_, autos_, body_)                           \
    do {                                                                       \
        (status_) = CMETA_OK;                                                  \
        CMETA_SCOPE_DECLARE_ALL_(scope_, autos_)                              \
        CMETA_SCOPE_INIT_ALL_(scope_, status_, autos_)                        \
        (status_) = (body_);                                                   \
        goto CMETA_SCOPE_LABEL_(scope_);                                      \
        CMETA_SCOPE_LABEL_(scope_):                                            \
        CMETA_SCOPE_DESTROY_ALL_(scope_, autos_)                              \
        ;                                                                      \
    } while (0)

#endif /* CMETA_SCOPE_H */
