#ifndef CMETA_SCOPE_H
#define CMETA_SCOPE_H

#include <cmeta/data.h>
#include <cmeta/pp.h>
#include <cmeta/status.h>

#include <stdbool.h>

/*
 * Portable structured lifetime facade.
 *
 * Managed resources stay statically visible to the preprocessor:
 *
 *   cmeta_scope(request, status,
 *       cmeta_auto(Buffer, buffer)
 *       cmeta_auto(Socket, socket),
 *       cmeta_body(
 *           ...
 *           if (failed)
 *               cmeta_leave(request, status, CMETA_CALLBACK_ERROR);
 *       )
 *   );
 *
 * cmeta_auto emits one finite (Type, name) row. cmeta_scope lowers the row
 * stream to ordinary-C declarations, DataDesc-backed initialization, one
 * generated cleanup epilogue, and reverse-order destruction.
 *
 * Native return/goto/break that escape the macro body are not managed exits.
 * Use cmeta_leave for exits that must run the generated cleanup epilogue.
 */
#define cmeta_auto(type_, name_) , (type_, name_)
#define cmeta_body(...) (__VA_ARGS__)

#define CMETA_SCOPE_LABEL_I_(scope_) scope_##__cmeta_cleanup
#define CMETA_SCOPE_LABEL_(scope_) CMETA_SCOPE_LABEL_I_(scope_)
#define CMETA_SCOPE_LIVE_I_(scope_, name_) scope_##__cmeta_live_##name_
#define CMETA_SCOPE_LIVE_(scope_, name_) CMETA_SCOPE_LIVE_I_(scope_, name_)

#define CMETA_SCOPE_DECLARE_(row_, scope_) \
    CMETA_SCOPE_DECLARE_EXPAND_(scope_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_DECLARE_EXPAND_(...) CMETA_SCOPE_DECLARE_I_(__VA_ARGS__)
#define CMETA_SCOPE_DECLARE_I_(scope_, type_, name_) \
    type_ name_ = {0}; \
    bool CMETA_SCOPE_LIVE_(scope_, name_) = false;

#define CMETA_SCOPE_INIT_(row_, ctx_) \
    CMETA_SCOPE_INIT_EXPAND_(CMETA_PP_UNPAREN ctx_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_INIT_EXPAND_(...) CMETA_SCOPE_INIT_I_(__VA_ARGS__)
#define CMETA_SCOPE_INIT_I_(scope_, status_, type_, name_)                    \
    do {                                                                       \
        (status_) = cmeta_data_value_init_zero(                               \
            CMETA_DATA_ACCESSOR_(type_)(), &(name_));                         \
        if ((status_) != CMETA_OK)                                             \
            goto CMETA_SCOPE_LABEL_(scope_);                                  \
        CMETA_SCOPE_LIVE_(scope_, name_) = true;                              \
    } while (0);

#define CMETA_SCOPE_DESTROY_(row_, scope_) \
    CMETA_SCOPE_DESTROY_EXPAND_(scope_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_DESTROY_EXPAND_(...) CMETA_SCOPE_DESTROY_I_(__VA_ARGS__)
#define CMETA_SCOPE_DESTROY_I_(scope_, type_, name_)                          \
    do {                                                                       \
        if (CMETA_SCOPE_LIVE_(scope_, name_)) {                               \
            cmeta_data_value_destroy(CMETA_DATA_ACCESSOR_(type_)(), &(name_));\
            CMETA_SCOPE_LIVE_(scope_, name_) = false;                         \
        }                                                                      \
    } while (0);

/* cmeta_auto deliberately emits a leading comma. Prefixing a sentinel turns
 * the field stream into ordinary variadic arguments; this trampoline drops the
 * sentinel and preserves the finite row list for forward/reverse replay. */
#define CMETA_SCOPE_ROWS_(autos_) \
    CMETA_SCOPE_ROWS_EXPAND_(cmeta_scope_auto_sentinel autos_)
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

#define cmeta_leave(scope_, status_, value_)                                  \
    do {                                                                       \
        (status_) = (value_);                                                  \
        goto CMETA_SCOPE_LABEL_(scope_);                                      \
    } while (0)

#define cmeta_scope(scope_, status_, autos_, body_)                           \
    do {                                                                       \
        (status_) = CMETA_OK;                                                  \
        CMETA_SCOPE_DECLARE_ALL_(scope_, autos_)                              \
        CMETA_SCOPE_INIT_ALL_(scope_, status_, autos_)                        \
        CMETA_PP_UNPAREN body_                                                 \
        goto CMETA_SCOPE_LABEL_(scope_);                                      \
        CMETA_SCOPE_LABEL_(scope_):                                            \
        CMETA_SCOPE_DESTROY_ALL_(scope_, autos_)                              \
        ;                                                                      \
    } while (0)

#endif /* CMETA_SCOPE_H */
