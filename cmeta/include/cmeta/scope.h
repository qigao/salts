#ifndef CMETA_SCOPE_H
#define CMETA_SCOPE_H

#include <cmeta/lifecycle.h>
#include <cmeta/pp.h>
#include <cmeta/status.h>

#include <stdbool.h>
#include <stdlib.h>

/*
 * Portable structured lifetime facade.
 *
 * (Type, name) rows in cmeta_autos are explicit finite resources. cmeta_scope
 * uses a local static lifecycle declaration; cmeta_scope_checked admits the
 * type's runtime DataDesc. Both use the same canonical concrete construct ops
 * to initialize semantic zero and restore it in reverse declaration order.
 *
 * The body is one ISO C expression returning cmeta_status, normally a call
 * to a typed body function borrowing the resources. Native returns stay in
 * that function; labels cannot cross the function boundary. Block bodies and
 * cross-scope exits are intentionally rejected rather than leaking resources.
 * C++ body exceptions restore live resources in the same reverse order before
 * rethrowing. Provider restore callbacks must not throw.
 */
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
#define CMETA_SCOPE_DECLARE_EXPAND_(...) CMETA_PP_OVERLOAD(CMETA_SCOPE_DECLARE_,__VA_ARGS__)(__VA_ARGS__)
#define CMETA_SCOPE_DECLARE_3(scope_,type_,name_) CMETA_SCOPE_DECLARE_managed(scope_,type_,name_)
#define CMETA_SCOPE_DECLARE_4(scope_,type_,name_,kind_) CMETA_PP_CAT(CMETA_SCOPE_DECLARE_,kind_)(scope_,type_,name_)
#define CMETA_SCOPE_DECLARE_trivial(scope_,type_,name_) \
    CMETA_STATIC_ASSERT((type_##_cmeta_lifecycle_flags & \
        (CMETA_LIFECYCLE_TRIVIAL_ZERO | CMETA_LIFECYCLE_TRIVIAL_CLEANUP)) == \
        (CMETA_LIFECYCLE_TRIVIAL_ZERO | CMETA_LIFECYCLE_TRIVIAL_CLEANUP), \
        "CMeta trivial scope requires canonical trivial lifecycle facts"); \
    type_ name_ = {0};
#define CMETA_SCOPE_DECLARE_managed(scope_, type_, name_) \
    type_ name_ = {0}; \
    bool CMETA_SCOPE_LIVE_(scope_, name_) = false; \
    const cmeta_data_construct_ops *CMETA_SCOPE_OPS_(scope_, name_) = NULL;

#define CMETA_SCOPE_BIND_STATIC_(scope_, status_, type_, name_) \
    CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES( \
        &CMETA_LIFECYCLE_ACCESSOR_(type_), \
        const cmeta_data_construct_ops *(*)(const type_ *)), \
        "CMeta static lifecycle native type mismatch"); \
    CMETA_SCOPE_OPS_(scope_, name_) = CMETA_LIFECYCLE_ACCESSOR_(type_)(&(name_));

#define CMETA_SCOPE_BIND_CHECKED_(scope_, status_, type_, name_) \
    (status_) = cmeta_scope_construct_ops( \
        CMETA_DATA_ACCESSOR_(type_)(), sizeof(type_), CMETA_ALIGNOF(type_), \
        &CMETA_SCOPE_OPS_(scope_, name_)); \
    if ((status_) != CMETA_OK) \
        goto CMETA_SCOPE_LABEL_(scope_);

#define CMETA_SCOPE_INIT_(row_, ctx_) \
    CMETA_SCOPE_INIT_EXPAND_(CMETA_PP_UNPAREN ctx_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_INIT_EXPAND_(...) CMETA_PP_OVERLOAD(CMETA_SCOPE_INIT_,__VA_ARGS__)(__VA_ARGS__)
#define CMETA_SCOPE_INIT_5(bind_,scope_,status_,type_,name_) CMETA_SCOPE_INIT_managed(bind_,scope_,status_,type_,name_)
#define CMETA_SCOPE_INIT_6(bind_,scope_,status_,type_,name_,kind_) CMETA_PP_CAT(CMETA_SCOPE_INIT_,kind_)(bind_,scope_,status_,type_,name_)
#define CMETA_SCOPE_INIT_trivial(bind_,scope_,status_,type_,name_) \
    CMETA_PP_CAT(CMETA_SCOPE_TRIVIAL_,bind_)(scope_,status_,type_,name_)
#define CMETA_SCOPE_TRIVIAL_CMETA_SCOPE_BIND_STATIC_(scope_,status_,type_,name_)
#define CMETA_SCOPE_TRIVIAL_CMETA_SCOPE_BIND_CHECKED_(scope_,status_,type_,name_) \
    do { \
        const cmeta_data_construct_ops *ops = NULL; \
        (status_) = cmeta_lifecycle_bind(CMETA_DATA_ACCESSOR_(type_)(),sizeof(type_), \
            CMETA_ALIGNOF(type_),&ops); \
        if ((status_) != CMETA_OK) goto CMETA_SCOPE_LABEL_(scope_); \
        if ((cmeta_lifecycle_flags_of(ops) & \
            (CMETA_LIFECYCLE_TRIVIAL_ZERO | CMETA_LIFECYCLE_TRIVIAL_CLEANUP)) != \
            (CMETA_LIFECYCLE_TRIVIAL_ZERO | CMETA_LIFECYCLE_TRIVIAL_CLEANUP)) { \
            (status_) = CMETA_TYPE_MISMATCH; goto CMETA_SCOPE_LABEL_(scope_); \
        } \
    } while (0);
#define CMETA_SCOPE_INIT_managed(bind_, scope_, status_, type_, name_)             \
    do {                                                                       \
        bind_(scope_, status_, type_, name_)                                  \
        (status_) = CMETA_SCOPE_OPS_(scope_, name_)->init_zero(&(name_));      \
        if ((status_) != CMETA_OK) {                                          \
            CMETA_SCOPE_OPS_(scope_, name_)->restore_zero(&(name_));          \
            goto CMETA_SCOPE_LABEL_(scope_);                                  \
        }                                                                     \
        CMETA_SCOPE_LIVE_(scope_, name_) = true;                              \
    } while (0);

#define CMETA_SCOPE_DESTROY_(row_, scope_) \
    CMETA_SCOPE_DESTROY_EXPAND_(scope_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_DESTROY_EXPAND_(...) CMETA_PP_OVERLOAD(CMETA_SCOPE_DESTROY_,__VA_ARGS__)(__VA_ARGS__)
#define CMETA_SCOPE_DESTROY_3(scope_,type_,name_) CMETA_SCOPE_DESTROY_managed(scope_,type_,name_)
#define CMETA_SCOPE_DESTROY_4(scope_,type_,name_,kind_) CMETA_PP_CAT(CMETA_SCOPE_DESTROY_,kind_)(scope_,type_,name_)
#define CMETA_SCOPE_DESTROY_trivial(scope_,type_,name_)
#define CMETA_SCOPE_DESTROY_managed(scope_, type_, name_)                          \
    do {                                                                       \
        if (CMETA_SCOPE_LIVE_(scope_, name_)) {                               \
            CMETA_SCOPE_OPS_(scope_, name_)->restore_zero(&(name_));          \
            CMETA_SCOPE_LIVE_(scope_, name_) = false;                         \
        }                                                                      \
    } while (0);

#define CMETA_SCOPE_DECLARE_ALL_(scope_, autos_) \
    CMETA_SCOPE_DECLARE_ALL_EXPAND_(scope_, CMETA_PP_UNPAREN autos_)
#define CMETA_SCOPE_DECLARE_ALL_EXPAND_(scope_, ...) \
    CMETA_PP_FOR_EACH(CMETA_SCOPE_DECLARE_, scope_, __VA_ARGS__)

#define CMETA_SCOPE_INIT_ALL_(bind_, scope_, status_, autos_) \
    CMETA_SCOPE_INIT_ALL_EXPAND_(bind_, scope_, status_, CMETA_PP_UNPAREN autos_)
#define CMETA_SCOPE_INIT_ALL_EXPAND_(bind_, scope_, status_, ...) \
    CMETA_PP_FOR_EACH(CMETA_SCOPE_INIT_, (bind_, scope_, status_), __VA_ARGS__)

#define CMETA_SCOPE_DESTROY_ALL_(scope_, autos_) \
    CMETA_SCOPE_DESTROY_ALL_EXPAND_(scope_, CMETA_PP_UNPAREN autos_)
#define CMETA_SCOPE_DESTROY_ALL_EXPAND_(scope_, ...) \
    CMETA_PP_FOR_EACH_REVERSE(CMETA_SCOPE_DESTROY_, scope_, __VA_ARGS__)

#define cmeta_scope_exit(scope_, status_, value_)                             \
    _Static_assert(0, "cmeta_scope_exit is removed; return from the body function")

#define cmeta_leave(scope_, status_, value_) \
    _Static_assert(0, "cmeta_leave is removed; return from the body function")

/* Capture the compiler counter once before replaying the resource rows.
 * __LINE__ cannot distinguish adjacent expansions on the same source line. */
#if CMETA_HAS_COUNTER
#define cmeta_scope(status_, autos_, body_) \
    CMETA_SCOPE_WITH_ID_(CMETA_SCOPE_BIND_STATIC_, \
        CMETA_PP_UNIQUE(cmeta_scope_), status_, autos_, body_)
#define cmeta_scope_checked(status_, autos_, body_) \
    CMETA_SCOPE_WITH_ID_(CMETA_SCOPE_BIND_CHECKED_, \
        CMETA_PP_UNIQUE(cmeta_scope_), status_, autos_, body_)
#endif

#ifdef __cplusplus
#define CMETA_SCOPE_BODY_(scope_, status_, autos_, body_) \
    try { (status_) = (body_); } catch (...) { \
        CMETA_SCOPE_DESTROY_ALL_(scope_, autos_) \
        throw; \
    }
#else
#define CMETA_SCOPE_BODY_(scope_, status_, autos_, body_) (status_) = (body_);
#endif

#define CMETA_SCOPE_WITH_ID_(bind_, scope_, status_, autos_, body_)          \
    do {                                                                       \
        (status_) = CMETA_OK;                                                  \
        CMETA_SCOPE_DECLARE_ALL_(scope_, autos_)                              \
        CMETA_SCOPE_INIT_ALL_(bind_, scope_, status_, autos_)                 \
        CMETA_SCOPE_BODY_(scope_, status_, autos_, body_)                     \
        goto CMETA_SCOPE_LABEL_(scope_);                                      \
        CMETA_SCOPE_LABEL_(scope_):                                            \
        CMETA_SCOPE_DESTROY_ALL_(scope_, autos_)                              \
        ;                                                                      \
    } while (0)

/* All resources initialize successfully by declared provider contract. No
 * partial-construction state or cached ops pointers are needed. A provider
 * that violates INIT_NOFAIL is a broken local invariant, never a fallback to
 * the fallible lowering. The body has the same expression-only exit contract. */
#define CMETA_SCOPE_NOFAIL_DECLARE_(row_, ignored_) \
    CMETA_SCOPE_NOFAIL_DECLARE_I_ row_
#define CMETA_SCOPE_NOFAIL_DECLARE_I_(type_, name_) \
    CMETA_STATIC_ASSERT((type_##_cmeta_lifecycle_flags & CMETA_LIFECYCLE_INIT_NOFAIL) != 0, \
        "CMeta nofail scope requires canonical INIT_NOFAIL"); \
    CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&CMETA_LIFECYCLE_ACCESSOR_(type_), \
        const cmeta_data_construct_ops *(*)(const type_ *)), \
        "CMeta static lifecycle native type mismatch"); \
    type_ name_ = {0};
#define CMETA_SCOPE_NOFAIL_INIT_(row_, ignored_) CMETA_SCOPE_NOFAIL_INIT_I_ row_
#define CMETA_SCOPE_NOFAIL_INIT_I_(type_, name_) \
    if (CMETA_LIFECYCLE_ACCESSOR_(type_)(&(name_))->init_zero(&(name_)) != CMETA_OK) abort();
#define CMETA_SCOPE_NOFAIL_RESTORE_(row_, ignored_) CMETA_SCOPE_NOFAIL_RESTORE_I_ row_
#define CMETA_SCOPE_NOFAIL_RESTORE_I_(type_, name_) \
    CMETA_LIFECYCLE_ACCESSOR_(type_)(&(name_))->restore_zero(&(name_));
#define cmeta_scope_nofail(status_, autos_, body_) \
    CMETA_SCOPE_NOFAIL_EXPAND_(status_, body_, CMETA_PP_UNPAREN autos_)
#define CMETA_SCOPE_NOFAIL_EXPAND_(...) CMETA_SCOPE_NOFAIL_I_(__VA_ARGS__)
#ifdef __cplusplus
#define CMETA_SCOPE_NOFAIL_INITIALIZE_(...) \
    try { CMETA_PP_FOR_EACH(CMETA_SCOPE_NOFAIL_INIT_, ~, __VA_ARGS__) } catch (...) { abort(); }
#define CMETA_SCOPE_NOFAIL_BODY_(status_, body_, ...) \
    try { (status_) = (body_); } catch (...) { \
        CMETA_PP_FOR_EACH_REVERSE(CMETA_SCOPE_NOFAIL_RESTORE_, ~, __VA_ARGS__) \
        throw; \
    }
#else
#define CMETA_SCOPE_NOFAIL_INITIALIZE_(...) CMETA_PP_FOR_EACH(CMETA_SCOPE_NOFAIL_INIT_, ~, __VA_ARGS__)
#define CMETA_SCOPE_NOFAIL_BODY_(status_, body_, ...) (status_) = (body_);
#endif
#define CMETA_SCOPE_NOFAIL_I_(status_, body_, ...) \
    do { \
        (status_) = CMETA_OK; \
        CMETA_PP_FOR_EACH(CMETA_SCOPE_NOFAIL_DECLARE_, ~, __VA_ARGS__) \
        CMETA_SCOPE_NOFAIL_INITIALIZE_(__VA_ARGS__) \
        CMETA_SCOPE_NOFAIL_BODY_(status_, body_, __VA_ARGS__) \
        CMETA_PP_FOR_EACH_REVERSE(CMETA_SCOPE_NOFAIL_RESTORE_, ~, __VA_ARGS__) \
    } while (0)

#endif /* CMETA_SCOPE_H */
