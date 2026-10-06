#ifndef CMETA_SCOPE_H
#define CMETA_SCOPE_H

#include <cmeta/lifecycle.h>
#include <cmeta/pp.h>
#include <cmeta/status.h>
#include <stdlib.h>

/* Ordinary (Type, name) rows select lowering from canonical declaration facts.
 * Static scopes have no cached ops or per-value live flags. Nested control flow
 * records the initialized prefix; only fallible rows need a status branch.
 * Checked scopes first admit each runtime DataDesc and then use its facts.
 * The body is one status-returning expression (normally a typed function call).
 * Native return stays in that function. Cross-scope jumps/longjmp are forbidden.
 * C++ body exceptions unwind the same canonical restores in exact LIFO order.
 * Providers must not throw from initialization or no-fail restore callbacks.
 * Setup/cleanup is O(N), with O(N) lexical nesting and no allocated state. */
#define cmeta_autos(...) (__VA_ARGS__)
#define cmeta_body(expression_) (expression_)

CMETA_INLINE cmeta_status cmeta_scope_construct_ops(
    const cmeta_data_desc *data, size_t size, size_t align,
    const cmeta_data_construct_ops **out) {
    return cmeta_lifecycle_bind(data, size, align, out);
}

#define CMETA_SCOPE_TRIVIAL_(flags_) \
    (((flags_) & (CMETA_LIFECYCLE_TRIVIAL_ZERO | CMETA_LIFECYCLE_TRIVIAL_CLEANUP)) == \
        (CMETA_LIFECYCLE_TRIVIAL_ZERO | CMETA_LIFECYCLE_TRIVIAL_CLEANUP))
#define CMETA_SCOPE_NOFAIL_(flags_) (((flags_) & CMETA_LIFECYCLE_INIT_NOFAIL) != 0)
#define CMETA_SCOPE_OPS_I_(scope_, name_) scope_##__cmeta_ops_##name_
#define CMETA_SCOPE_OPS_(scope_, name_) CMETA_SCOPE_OPS_I_(scope_, name_)

/* Explicit spellings remain assertions for qualification; they do not select
 * a second lifecycle implementation or strengthen missing canonical facts. */
#define CMETA_SCOPE_ASSERT_auto(type_)
#define CMETA_SCOPE_ASSERT_managed(type_)
#define CMETA_SCOPE_ASSERT_trivial(type_) \
    CMETA_STATIC_ASSERT(CMETA_SCOPE_TRIVIAL_(type_##_cmeta_lifecycle_flags), \
        "CMeta trivial scope requires canonical trivial lifecycle facts");
#define CMETA_SCOPE_ASSERT_NOFAIL_(row_, ignored_) \
    CMETA_SCOPE_ASSERT_NOFAIL_I_ row_
#define CMETA_SCOPE_ASSERT_NOFAIL_I_(type_, name_) \
    CMETA_STATIC_ASSERT(CMETA_SCOPE_NOFAIL_(type_##_cmeta_lifecycle_flags), \
        "CMeta nofail scope requires canonical INIT_NOFAIL");
#define CMETA_SCOPE_DECLARE_(row_, ignored_) \
    CMETA_SCOPE_DECLARE_EXPAND_(CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_DECLARE_EXPAND_(...) CMETA_PP_OVERLOAD(CMETA_SCOPE_DECLARE_,__VA_ARGS__)(__VA_ARGS__)
#define CMETA_SCOPE_DECLARE_2(type_,name_) type_ name_ = {0};
#define CMETA_SCOPE_DECLARE_3(type_,name_,kind_) CMETA_SCOPE_DECLARE_2(type_,name_)

#ifdef __cplusplus
#define CMETA_SCOPE_TRY_ try {
#define CMETA_SCOPE_CATCH_(restore_) } catch (...) { restore_ throw; }
#define CMETA_SCOPE_INITIALIZE_(status_, ops_, flags_, value_) \
    try { \
        (status_) = (ops_)->init_zero(&(value_)); \
        if (CMETA_SCOPE_NOFAIL_(flags_) && (status_) != CMETA_OK) abort(); \
    } catch (...) { abort(); }
#else
#define CMETA_SCOPE_TRY_
#define CMETA_SCOPE_CATCH_(restore_)
#define CMETA_SCOPE_INITIALIZE_(status_, ops_, flags_, value_) \
    (status_) = (ops_)->init_zero(&(value_)); \
    if (CMETA_SCOPE_NOFAIL_(flags_) && (status_) != CMETA_OK) abort();
#endif

#define CMETA_SCOPE_STATIC_RESTORE_(type_, name_) \
    if (!CMETA_SCOPE_TRIVIAL_(type_##_cmeta_lifecycle_flags)) \
        CMETA_LIFECYCLE_ACCESSOR_(type_)(&(name_))->restore_zero(&(name_));
#define CMETA_SCOPE_OPEN_static(scope_, status_, type_, name_, kind_) \
    CMETA_PP_CAT(CMETA_SCOPE_ASSERT_,kind_)(type_) \
    CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&CMETA_LIFECYCLE_ACCESSOR_(type_), \
        const cmeta_data_construct_ops *(*)(const type_ *)), \
        "CMeta static lifecycle native type mismatch"); \
    if (!CMETA_SCOPE_TRIVIAL_(type_##_cmeta_lifecycle_flags)) { \
        CMETA_SCOPE_INITIALIZE_(status_, CMETA_LIFECYCLE_ACCESSOR_(type_)(&(name_)), \
            type_##_cmeta_lifecycle_flags, name_) \
    } \
    if (CMETA_SCOPE_NOFAIL_(type_##_cmeta_lifecycle_flags) || (status_) == CMETA_OK) { \
        CMETA_SCOPE_TRY_
#define CMETA_SCOPE_CLOSE_static(scope_, status_, type_, name_, kind_) \
        CMETA_SCOPE_CATCH_(CMETA_SCOPE_STATIC_RESTORE_(type_,name_)) \
    } \
    CMETA_SCOPE_STATIC_RESTORE_(type_,name_)

#define CMETA_SCOPE_CHECK_auto(status_, ops_)
#define CMETA_SCOPE_CHECK_managed(status_, ops_)
#define CMETA_SCOPE_CHECK_trivial(status_, ops_) \
    if (!CMETA_SCOPE_TRIVIAL_(cmeta_lifecycle_flags_of(ops_))) \
        (status_) = CMETA_TYPE_MISMATCH;
#define CMETA_SCOPE_CHECKED_RESTORE_(scope_, name_) \
    if (!CMETA_SCOPE_TRIVIAL_(cmeta_lifecycle_flags_of(CMETA_SCOPE_OPS_(scope_,name_)))) \
        CMETA_SCOPE_OPS_(scope_,name_)->restore_zero(&(name_));
#define CMETA_SCOPE_OPEN_checked(scope_, status_, type_, name_, kind_) \
    CMETA_PP_CAT(CMETA_SCOPE_ASSERT_,kind_)(type_) \
    const cmeta_data_construct_ops *CMETA_SCOPE_OPS_(scope_,name_) = NULL; \
    (status_) = cmeta_scope_construct_ops(CMETA_DATA_ACCESSOR_(type_)(), \
        sizeof(type_), CMETA_ALIGNOF(type_), &CMETA_SCOPE_OPS_(scope_,name_)); \
    if ((status_) == CMETA_OK) { \
        CMETA_PP_CAT(CMETA_SCOPE_CHECK_,kind_)(status_, CMETA_SCOPE_OPS_(scope_,name_)) \
    } \
    if ((status_) == CMETA_OK) { \
        if (!CMETA_SCOPE_TRIVIAL_(cmeta_lifecycle_flags_of(CMETA_SCOPE_OPS_(scope_,name_)))) { \
            CMETA_SCOPE_INITIALIZE_(status_, CMETA_SCOPE_OPS_(scope_,name_), \
                cmeta_lifecycle_flags_of(CMETA_SCOPE_OPS_(scope_,name_)), name_) \
        } \
        if ((status_) == CMETA_OK) { \
            CMETA_SCOPE_TRY_
#define CMETA_SCOPE_CLOSE_checked(scope_, status_, type_, name_, kind_) \
            CMETA_SCOPE_CATCH_(CMETA_SCOPE_CHECKED_RESTORE_(scope_,name_)) \
        } \
        CMETA_SCOPE_CHECKED_RESTORE_(scope_,name_) \
    }

#define CMETA_SCOPE_OPEN_(row_, ctx_) \
    CMETA_SCOPE_OPEN_EXPAND_(CMETA_PP_UNPAREN ctx_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_OPEN_EXPAND_(...) CMETA_PP_OVERLOAD(CMETA_SCOPE_OPEN_,__VA_ARGS__)(__VA_ARGS__)
#define CMETA_SCOPE_OPEN_5(mode_,scope_,status_,type_,name_) \
    CMETA_SCOPE_OPEN_6(mode_,scope_,status_,type_,name_,auto)
#define CMETA_SCOPE_OPEN_6(mode_,scope_,status_,type_,name_,kind_) \
    CMETA_PP_CAT(CMETA_SCOPE_OPEN_,mode_)(scope_,status_,type_,name_,kind_)
#define CMETA_SCOPE_CLOSE_(row_, ctx_) \
    CMETA_SCOPE_CLOSE_EXPAND_(CMETA_PP_UNPAREN ctx_, CMETA_PP_UNPAREN row_)
#define CMETA_SCOPE_CLOSE_EXPAND_(...) CMETA_PP_OVERLOAD(CMETA_SCOPE_CLOSE_,__VA_ARGS__)(__VA_ARGS__)
#define CMETA_SCOPE_CLOSE_5(mode_,scope_,status_,type_,name_) \
    CMETA_SCOPE_CLOSE_6(mode_,scope_,status_,type_,name_,auto)
#define CMETA_SCOPE_CLOSE_6(mode_,scope_,status_,type_,name_,kind_) \
    CMETA_PP_CAT(CMETA_SCOPE_CLOSE_,mode_)(scope_,status_,type_,name_,kind_)

#define cmeta_scope_exit(scope_, status_, value_) \
    CMETA_STATIC_ASSERT(0, "cmeta_scope_exit is removed; return from the body function")
#define cmeta_leave(scope_, status_, value_) \
    CMETA_STATIC_ASSERT(0, "cmeta_leave is removed; return from the body function")

#if CMETA_HAS_COUNTER
#define cmeta_scope(status_, autos_, body_) \
    CMETA_SCOPE_EXPAND_(static, CMETA_PP_UNIQUE(cmeta_scope_), status_, body_, CMETA_PP_UNPAREN autos_)
#define cmeta_scope_checked(status_, autos_, body_) \
    CMETA_SCOPE_EXPAND_(checked, CMETA_PP_UNIQUE(cmeta_scope_), status_, body_, CMETA_PP_UNPAREN autos_)
#define cmeta_scope_nofail(status_, autos_, body_) \
    do { \
        CMETA_SCOPE_ASSERT_NOFAIL_ALL_(CMETA_PP_UNPAREN autos_) \
        cmeta_scope(status_, autos_, body_); \
    } while (0)
#endif
#define CMETA_SCOPE_ASSERT_NOFAIL_ALL_(...) \
    CMETA_PP_FOR_EACH(CMETA_SCOPE_ASSERT_NOFAIL_, ~, __VA_ARGS__)
#define CMETA_SCOPE_EXPAND_(...) CMETA_SCOPE_I_(__VA_ARGS__)
#define CMETA_SCOPE_I_(mode_,scope_,status_,body_,...) \
    do { \
        (status_) = CMETA_OK; \
        CMETA_PP_FOR_EACH(CMETA_SCOPE_DECLARE_, ~, __VA_ARGS__) \
        CMETA_PP_FOR_EACH(CMETA_SCOPE_OPEN_, (mode_,scope_,status_), __VA_ARGS__) \
        (status_) = (body_); \
        CMETA_PP_FOR_EACH_REVERSE(CMETA_SCOPE_CLOSE_, (mode_,scope_,status_), __VA_ARGS__) \
    } while (0)

#endif /* CMETA_SCOPE_H */
