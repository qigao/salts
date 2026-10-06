#ifndef CMETA_SCOPE_H
#define CMETA_SCOPE_H

#include <cmeta/data.h>
#include <cmeta/pp.h>
#include <cmeta/status.h>

#include <stdbool.h>

/*
 * Portable structured lifetime reference.
 *
 * Resources are explicit finite (Type, name) rows. Each value is initialized
 * through its canonical DataDesc and destroyed in reverse declaration order.
 * Managed exits route to one generated cleanup epilogue.
 *
 * Native return/goto/break that escape the macro body are not managed exits;
 * callers must use cmeta_scope_exit for this reference backend.
 */
#define cmeta_resources(...) (__VA_ARGS__)
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

#define CMETA_SCOPE_ROWS_(resources_) CMETA_SCOPE_ROWS_I_ resources_
#define CMETA_SCOPE_ROWS_I_(...) __VA_ARGS__

#define cmeta_scope_exit(scope_, status_, value_)                             \
    do {                                                                       \
        (status_) = (value_);                                                  \
        goto CMETA_SCOPE_LABEL_(scope_);                                      \
    } while (0)

#define cmeta_scope(scope_, status_, resources_, body_)                       \
    do {                                                                       \
        (status_) = CMETA_OK;                                                  \
        CMETA_PP_FOR_EACH(                                                     \
            CMETA_SCOPE_DECLARE_, scope_, CMETA_SCOPE_ROWS_(resources_))      \
        CMETA_PP_FOR_EACH(                                                     \
            CMETA_SCOPE_INIT_, (scope_, status_), CMETA_SCOPE_ROWS_(resources_)) \
        CMETA_PP_UNPAREN body_                                                 \
        goto CMETA_SCOPE_LABEL_(scope_);                                      \
        CMETA_SCOPE_LABEL_(scope_):                                            \
        CMETA_PP_FOR_EACH_REVERSE(                                             \
            CMETA_SCOPE_DESTROY_, scope_, CMETA_SCOPE_ROWS_(resources_))      \
        ;                                                                      \
    } while (0)

#endif /* CMETA_SCOPE_H */
