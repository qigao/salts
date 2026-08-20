#ifndef CFLOW_META_H
#define CFLOW_META_H

#include <cflow/operators.h>
#include <cmeta/cmeta.h>
#include <cflow/operator_policy.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CFLOW_OP_ROW(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, intrinsic_effects) \
    typedef struct cflow_##method##_callable { cmeta_callable fn; } cflow_##method##_callable;
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW

#define CFLOW_ASSOC_ID(id, ignored) , CMETA_FN_TYPE(id): CMETA_MAKER(id)
#define CFLOW_WRAP_OP_TYPED(op, fn) \
    _Generic(&(fn), default: cmeta_unsupported_signature \
        CMETA_PP_FOR_EACH_A(CFLOW_ASSOC_ID, ~, CFLOW_OP_SIGNATURE_LIST(op)))(&(fn))

#define CFLOW_SIG_ASSOC_ID(id, ignored) , CMETA_FN_TYPE(id): CMETA_SIG_NAME(id)
#define CFLOW_SIG_OF_OP(op, expr) \
    _Generic((expr), default: CMETA_SIG_INVALID \
        CMETA_PP_FOR_EACH_A(CFLOW_SIG_ASSOC_ID, ~, CFLOW_OP_SIGNATURE_LIST(op)))

#define CFLOW_OP_CALLABLE_I(op) cflow_##op##_callable
#define CFLOW_OP_CALLABLE(op) CFLOW_OP_CALLABLE_I(op)

/* Canonical named callable.  The public identifier is a value, not a provider
 * function, so s->map(s, square) passes a true first-class callable. */
#define typed_raw(op, effect_set, property_set, ret, name, params) \
    static ret cmeta_typed_##name params; \
    static cmeta_fn cmeta_meta_##name(void) { \
        cmeta_fn x = CFLOW_WRAP_OP_TYPED(op, cmeta_typed_##name); \
        x.effects = (cmeta_effects)(effect_set); \
        x.properties = (cmeta_properties)(property_set); \
        return x; \
    } \
    static bool cmeta_invoke_##name(const cmeta_callable *self, void *out, const void *const *args) { \
        return self ? cmeta_fn_invoke(self->meta, out, args) : false; \
    } \
    static cmeta_gen_status cmeta_generate_##name(const cmeta_callable *self, const void *input, void *out, size_t *cursor) { \
        return self ? cmeta_fn_generate(self->meta, input, out, cursor) : CMETA_GEN_ERROR; \
    } \
    const CFLOW_OP_CALLABLE(op) name = { .fn = \
        CMETA_CALLABLE_INIT(effect_set, property_set, cmeta_meta_##name, \
                           cmeta_invoke_##name, cmeta_generate_##name, 0u) }; \
    static ret cmeta_typed_##name params

#define CFLOW_TYPED(op, contract, ret, name, params) \
    typed_raw(op, CMETA_CONTRACT_EFFECTS(contract), CMETA_CONTRACT_PROPERTIES(contract), ret, name, params)

/* CMeta owns the global typed(kind, ...) router. Uppercase registered generic
 * kinds are handled by CMeta/Turbo; lowercase CFlow operator names arrive here
 * through the fallback hook. */
#undef CMETA_TYPED_FALLBACK
#define CMETA_TYPED_FALLBACK(op, ...) CFLOW_TYPED(op, __VA_ARGS__)

#define typed_decl(op, name) extern const CFLOW_OP_CALLABLE(op) name
#define typed_call(name) cmeta_typed_##name

/* Capturing C-meta lambda with one logical callback argument and one by-value
 * capture object.  cap_type may itself be a struct to capture many fields. */
#define lambda1_raw(op, effect_set, property_set, ret, name, in_type, in_name, cap_type, cap_name) \
    _Static_assert(CFLOW_SIG_OF_OP(op, (ret (*)(in_type))0) != CMETA_SIG_INVALID, \
                   "lambda1 signature is not enabled for this operator"); \
    _Static_assert(sizeof(cap_type) <= CMETA_CAPTURE_INLINE, \
                   "lambda capture exceeds CMETA_CAPTURE_INLINE"); \
    static ret cmeta_lambda_impl_##name(cap_type cap_name, in_type in_name); \
    static cmeta_fn cmeta_lambda_meta_##name(void) { \
        cmeta_fn x = { CFLOW_SIG_OF_OP(op, (ret (*)(in_type))0), { 0 }, \
            (cmeta_effects)(effect_set), (cmeta_properties)(property_set) }; \
        return x; \
    } \
    static bool cmeta_lambda_invoke_##name(const cmeta_callable *self, void *out, const void *const *args) { \
        in_type in_name; cap_type cap_name; ret _r; \
        if (!self || !args || self->capture_size != sizeof(cap_type)) return false; \
        memcpy(&in_name, args[0], sizeof(in_name)); \
        memcpy(&cap_name, self->capture.bytes, sizeof(cap_name)); \
        _r = cmeta_lambda_impl_##name(cap_name, in_name); \
        if (out) memcpy(out, &_r, sizeof(_r)); \
        return true; \
    } \
    static cmeta_gen_status cmeta_lambda_generate_##name(const cmeta_callable *self, const void *input, void *out, size_t *cursor) { \
        (void)self; (void)input; (void)out; (void)cursor; return CMETA_GEN_ERROR; \
    } \
    CFLOW_OP_CALLABLE(op) name(cap_type cap_name) { \
        CFLOW_OP_CALLABLE(op) _x = { .fn = \
            CMETA_CALLABLE_INIT(effect_set, property_set, cmeta_lambda_meta_##name, \
                               cmeta_lambda_invoke_##name, cmeta_lambda_generate_##name, sizeof(cap_type)) }; \
        memcpy(_x.fn.capture.bytes, &cap_name, sizeof(cap_name)); \
        return _x; \
    } \
    static ret cmeta_lambda_impl_##name(cap_type cap_name, in_type in_name)

/* Two-argument capturing lambda, useful for reduce/zip-like callbacks. */
#define lambda2_raw(op, effect_set, property_set, ret, name, a_type, a_name, b_type, b_name, cap_type, cap_name) \
    _Static_assert(CFLOW_SIG_OF_OP(op, (ret (*)(a_type, b_type))0) != CMETA_SIG_INVALID, \
                   "lambda2 signature is not enabled for this operator"); \
    _Static_assert(sizeof(cap_type) <= CMETA_CAPTURE_INLINE, \
                   "lambda capture exceeds CMETA_CAPTURE_INLINE"); \
    static ret cmeta_lambda_impl_##name(cap_type cap_name, a_type a_name, b_type b_name); \
    static cmeta_fn cmeta_lambda_meta_##name(void) { \
        cmeta_fn x = { CFLOW_SIG_OF_OP(op, (ret (*)(a_type, b_type))0), { 0 }, \
            (cmeta_effects)(effect_set), (cmeta_properties)(property_set) }; \
        return x; \
    } \
    static bool cmeta_lambda_invoke_##name(const cmeta_callable *self, void *out, const void *const *args) { \
        a_type a_name; b_type b_name; cap_type cap_name; ret _r; \
        if (!self || !args || self->capture_size != sizeof(cap_type)) return false; \
        memcpy(&a_name, args[0], sizeof(a_name)); memcpy(&b_name, args[1], sizeof(b_name)); \
        memcpy(&cap_name, self->capture.bytes, sizeof(cap_name)); \
        _r = cmeta_lambda_impl_##name(cap_name, a_name, b_name); \
        if (out) memcpy(out, &_r, sizeof(_r)); \
        return true; \
    } \
    static cmeta_gen_status cmeta_lambda_generate_##name(const cmeta_callable *self, const void *input, void *out, size_t *cursor) { \
        (void)self; (void)input; (void)out; (void)cursor; return CMETA_GEN_ERROR; \
    } \
    CFLOW_OP_CALLABLE(op) name(cap_type cap_name) { \
        CFLOW_OP_CALLABLE(op) _x = { .fn = \
            CMETA_CALLABLE_INIT(effect_set, property_set, cmeta_lambda_meta_##name, \
                               cmeta_lambda_invoke_##name, cmeta_lambda_generate_##name, sizeof(cap_type)) }; \
        memcpy(_x.fn.capture.bytes, &cap_name, sizeof(cap_name)); \
        return _x; \
    } \
    static ret cmeta_lambda_impl_##name(cap_type cap_name, a_type a_name, b_type b_name)

/* Cursor-generator capturing lambda for flatMap. */
#define lambda_gen_raw(op, effect_set, property_set, name, in_type, in_name, out_type, out_name, cap_type, cap_name) \
    _Static_assert(CFLOW_SIG_OF_OP(op, (cmeta_gen_status (*)(in_type, out_type *, size_t *))0) != CMETA_SIG_INVALID, \
                   "lambda_gen signature is not enabled for this operator"); \
    _Static_assert(sizeof(cap_type) <= CMETA_CAPTURE_INLINE, \
                   "lambda capture exceeds CMETA_CAPTURE_INLINE"); \
    static cmeta_gen_status cmeta_lambda_impl_##name(cap_type cap_name, in_type in_name, out_type *out_name, size_t *cursor); \
    static cmeta_fn cmeta_lambda_meta_##name(void) { \
        cmeta_fn x = { CFLOW_SIG_OF_OP(op, (cmeta_gen_status (*)(in_type, out_type *, size_t *))0), { 0 }, \
            (cmeta_effects)(effect_set), (cmeta_properties)(property_set) }; \
        return x; \
    } \
    static bool cmeta_lambda_invoke_##name(const cmeta_callable *self, void *out, const void *const *args) { \
        (void)self; (void)out; (void)args; return false; \
    } \
    static cmeta_gen_status cmeta_lambda_generate_##name(const cmeta_callable *self, const void *input, void *out, size_t *cursor) { \
        in_type in_name; cap_type cap_name; \
        if (!self || !input || !out || !cursor || self->capture_size != sizeof(cap_type)) return CMETA_GEN_ERROR; \
        memcpy(&in_name, input, sizeof(in_name)); memcpy(&cap_name, self->capture.bytes, sizeof(cap_name)); \
        return cmeta_lambda_impl_##name(cap_name, in_name, (out_type *)out, cursor); \
    } \
    CFLOW_OP_CALLABLE(op) name(cap_type cap_name) { \
        CFLOW_OP_CALLABLE(op) _x = { .fn = \
            CMETA_CALLABLE_INIT(effect_set, property_set, cmeta_lambda_meta_##name, \
                               cmeta_lambda_invoke_##name, cmeta_lambda_generate_##name, sizeof(cap_type)) }; \
        memcpy(_x.fn.capture.bytes, &cap_name, sizeof(cap_name)); \
        return _x; \
    } \
    static cmeta_gen_status cmeta_lambda_impl_##name(cap_type cap_name, in_type in_name, out_type *out_name, size_t *cursor)

/* Contract-level closure macros. Raw effect/property forms remain internal escape hatches. */
#define lambda1(op, contract, ret, name, in_type, in_name, cap_type, cap_name) \
    lambda1_raw(op, CMETA_CONTRACT_EFFECTS(contract), CMETA_CONTRACT_PROPERTIES(contract), \
                ret, name, in_type, in_name, cap_type, cap_name)
#define lambda2(op, contract, ret, name, a_type, a_name, b_type, b_name, cap_type, cap_name) \
    lambda2_raw(op, CMETA_CONTRACT_EFFECTS(contract), CMETA_CONTRACT_PROPERTIES(contract), \
                ret, name, a_type, a_name, b_type, b_name, cap_type, cap_name)
#define lambda_gen(op, contract, name, in_type, in_name, out_type, out_name, cap_type, cap_name) \
    lambda_gen_raw(op, CMETA_CONTRACT_EFFECTS(contract), CMETA_CONTRACT_PROPERTIES(contract), \
                   name, in_type, in_name, out_type, out_name, cap_type, cap_name)
#define lambda(...) lambda1(__VA_ARGS__)

#define lambda_decl(op, name, cap_type) CFLOW_OP_CALLABLE(op) name(cap_type)

/* Callable algebra -----------------------------------------------------------
 *
 * cmeta_bindable declares a binary C function whose LAST argument may be
 * partially applied.  The resulting object is the same operator-specific
 * callable value used by typed()/lambda(), so Graph/optimizer/plan need no
 * special bind semantic.
 *
 * Example:
 *   cmeta_bindable(map, value, long, multiply,
 *                  int, x, int, factor) { return (long)x * factor; }
 *   s->map(s, cmeta_bind(multiply, 10));
 *
 * A bare `bind` alias is deliberately not installed by default because POSIX
 * already owns the global identifier bind().  Define CMETA_ENABLE_SHORT_NAMES
 * before including cmeta.h if the DSL-friendly aliases are desired.
 */
#define cmeta_bindable_raw(op, effect_set, property_set, ret, name, in_type, in_name, bound_type, bound_name) \
    _Static_assert(CFLOW_SIG_OF_OP(op, (ret (*)(in_type))0) != CMETA_SIG_INVALID, \
                   "bound result signature is not enabled for this operator"); \
    _Static_assert(sizeof(bound_type) <= CMETA_CAPTURE_INLINE, \
                   "bound argument exceeds CMETA_CAPTURE_INLINE"); \
    static ret cmeta_bind_impl_##name(in_type in_name, bound_type bound_name); \
    static cmeta_fn cmeta_bind_meta_##name(void) { \
        cmeta_fn x = { CFLOW_SIG_OF_OP(op, (ret (*)(in_type))0), { 0 }, \
            (cmeta_effects)(effect_set), (cmeta_properties)(property_set) }; \
        return x; \
    } \
    static bool cmeta_bind_invoke_##name(const cmeta_callable *self, void *out, const void *const *args) { \
        in_type in_name; bound_type bound_name; ret _r; \
        if (!self || !args || !args[0] || self->capture_size != sizeof(bound_type)) return false; \
        memcpy(&in_name, args[0], sizeof(in_name)); \
        memcpy(&bound_name, self->capture.bytes, sizeof(bound_name)); \
        _r = cmeta_bind_impl_##name(in_name, bound_name); \
        if (out) memcpy(out, &_r, sizeof(_r)); \
        return true; \
    } \
    static cmeta_gen_status cmeta_bind_generate_##name(const cmeta_callable *self, const void *input, void *out, size_t *cursor) { \
        (void)self; (void)input; (void)out; (void)cursor; return CMETA_GEN_ERROR; \
    } \
    CFLOW_OP_CALLABLE(op) cmeta_bind_##name(bound_type bound_name) { \
        CFLOW_OP_CALLABLE(op) _x = { .fn = \
            CMETA_CALLABLE_INIT(effect_set, property_set, cmeta_bind_meta_##name, \
                               cmeta_bind_invoke_##name, cmeta_bind_generate_##name, sizeof(bound_type)) }; \
        memcpy(_x.fn.capture.bytes, &bound_name, sizeof(bound_name)); \
        return _x; \
    } \
    static ret cmeta_bind_impl_##name(in_type in_name, bound_type bound_name)

#define cmeta_bindable(op, contract, ret, name, in_type, in_name, bound_type, bound_name) \
    cmeta_bindable_raw(op, CMETA_CONTRACT_EFFECTS(contract), CMETA_CONTRACT_PROPERTIES(contract), \
                       ret, name, in_type, in_name, bound_type, bound_name)
#define cmeta_bind(name, value) cmeta_bind_##name((value))
#define cmeta_bindable_decl(op, name, bound_type) CFLOW_OP_CALLABLE(op) cmeta_bind_##name(bound_type)
#define cmeta_bindable_call(name) cmeta_bind_impl_##name

#ifdef CMETA_ENABLE_SHORT_NAMES
#define bindable(...) cmeta_bindable(__VA_ARGS__)
#define bind(name, value) cmeta_bind(name, value)
#endif


#ifdef __cplusplus
}
#endif
#endif
