#ifndef CMETA_BIND_H
#define CMETA_BIND_H

#include <cmeta/invokable.h>
#include <cmeta/invoke_decl.h>

#ifdef __cplusplus
#define CMETA_BIND_EMPTY_CALLABLE_ {}
#else
#define CMETA_BIND_EMPTY_CALLABLE_ {0}
#endif

/* Explicit, finite parameter lowering. A row is (arg|value|borrow, FunctionRow).
 * Rows remain in native parameter order. Captures are named struct fields;
 * value accepts registered arithmetic scalars only, borrow accepts an explicit
 * BORROWED object pointer. Managed/consuming captures need an external owner.
 * No retained closure, heap allocation, or runtime argument interpretation. */
#define CMETA_BIND_ROW_(row) CMETA_PP_TUPLE_GET_1(row)
#define CMETA_BIND_MODE_(row) CMETA_PP_TUPLE_GET_0(row)
#define CMETA_BIND_SELECT_(prefix,row) CMETA_PP_CAT(prefix,CMETA_BIND_MODE_(row))
#define CMETA_BIND_BOUND_arg 0
#define CMETA_BIND_BOUND_value 1
#define CMETA_BIND_BOUND_borrow 1
#define CMETA_BIND_BOUND_(row) CMETA_BIND_SELECT_(CMETA_BIND_BOUND_,row)
#define CMETA_BIND_FIELD_arg(row)
#define CMETA_BIND_FIELD_value(row) CMETA_FUNCTION_PARAM_DECL_APPLY(row);
#define CMETA_BIND_FIELD_borrow(row) CMETA_BIND_FIELD_value(row)
#define CMETA_BIND_FIELD_(row,ctx) CMETA_BIND_SELECT_(CMETA_BIND_FIELD_,row)(CMETA_BIND_ROW_(row))
#define CMETA_BIND_META_arg(row) CMETA_FUNCTION_PARAM_META_APPLY(row)
#define CMETA_BIND_META_value(row)
#define CMETA_BIND_META_borrow(row)
#define CMETA_BIND_META_(row,ctx) CMETA_BIND_SELECT_(CMETA_BIND_META_,row)(CMETA_BIND_ROW_(row))
#define CMETA_BIND_ABI_arg(row) CMETA_FUNCTION_PARAM_CARRIER(row),
#define CMETA_BIND_ABI_value(row)
#define CMETA_BIND_ABI_borrow(row)
#define CMETA_BIND_ABI_(row,ctx) CMETA_BIND_SELECT_(CMETA_BIND_ABI_,row)(CMETA_BIND_ROW_(row))
#define CMETA_BIND_COUNT_(row,ctx) + !CMETA_BIND_BOUND_(row)
#define CMETA_BIND_MASK_(row,ctx) CMETA_BIND_BOUND_(row),
#define CMETA_BIND_SOURCE_META_(row,ctx) CMETA_FUNCTION_PARAM_META_APPLY(CMETA_BIND_ROW_(row))
#define CMETA_BIND_SOURCE_ABI_(row,ctx) CMETA_FUNCTION_PARAM_CARRIER(CMETA_BIND_ROW_(row)),
#define CMETA_BIND_NATIVE_(index,row,ctx) CMETA_FUNCTION_PARAM_DECL(index,CMETA_BIND_ROW_(row),~)
#define CMETA_BIND_SCALAR_(entry,type) \
    || (CMETA_TYPE_MATCHES((type *)0,CMETA_TYPE_CTYPE(entry) *) && \
       (CMETA_TYPE_KIND(entry) == CMETA_T_BOOL || CMETA_TYPE_KIND(entry) == CMETA_T_INTEGER || \
        CMETA_TYPE_KIND(entry) == CMETA_T_FLOAT))
#define CMETA_BIND_PROOF_arg(row)
#define CMETA_BIND_PROOF_value(row) \
    CMETA_STATIC_ASSERT((0 CMETA_PP_FOR_EACH_B(CMETA_BIND_SCALAR_, \
        CMETA_FUNCTION_PARAM_TYPE(row),CMETA_KNOWN_TYPE_LIST)), \
        "CMeta value capture requires a registered trivial scalar"); \
    CMETA_STATIC_ASSERT((CMETA_FUNCTION_PARAM_FLAGS(row) & \
        (CMETA_PARAM_OUT | CMETA_PARAM_OWNED | CMETA_PARAM_RECEIVER)) == 0, \
        "CMeta value capture cannot consume or write its snapshot");
#define CMETA_BIND_PROOF_borrow(row) \
    CMETA_STATIC_ASSERT(CMETA_FUNCTION_PARAM_CARRIER(row) == CMETA_ABI_OBJECT_POINTER && \
        (CMETA_FUNCTION_PARAM_FLAGS(row) & CMETA_PARAM_OWNERSHIP_MASK) == CMETA_PARAM_BORROWED, \
        "CMeta borrow capture requires an explicit borrowed object pointer");
#define CMETA_BIND_PROOF_(row,ctx) \
    CMETA_STATIC_ASSERT(CMETA_TYPE_IS_VALUE(CMETA_FUNCTION_PARAM_TYPE(CMETA_BIND_ROW_(row))), \
        "CMeta bind requires unqualified native value carriers"); \
    CMETA_BIND_SELECT_(CMETA_BIND_PROOF_,row)(CMETA_BIND_ROW_(row))
#define CMETA_BIND_LOCAL_(index) CMETA_PP_CAT(cmeta_bind_argument_,index)
#define CMETA_BIND_LOAD_arg(index,row) \
    CMETA_FUNCTION_PARAM_TYPE(row) CMETA_BIND_LOCAL_(index); \
    if (args == NULL || args[next] == NULL) return false; \
    memcpy(&CMETA_BIND_LOCAL_(index), args[next++], sizeof(CMETA_BIND_LOCAL_(index)));
#define CMETA_BIND_LOAD_value(index,row)
#define CMETA_BIND_LOAD_borrow(index,row)
#define CMETA_BIND_LOAD_(index,row,ctx) CMETA_BIND_SELECT_(CMETA_BIND_LOAD_,row)(index,CMETA_BIND_ROW_(row))
#define CMETA_BIND_ARG_arg(index,row) CMETA_BIND_LOCAL_(index)
#define CMETA_BIND_ARG_value(index,row) capture.CMETA_FUNCTION_PARAM_NAME(row)
#define CMETA_BIND_ARG_borrow(index,row) CMETA_BIND_ARG_value(index,row)
#define CMETA_BIND_ARG_(index,row,ctx) \
    CMETA_PP_SEP_COMMA(index) CMETA_BIND_SELECT_(CMETA_BIND_ARG_,row)(index,CMETA_BIND_ROW_(row))
#define CMETA_BIND_NONNULL_arg(row)
#define CMETA_BIND_NONNULL_value(row)
#define CMETA_BIND_NONNULL_borrow(row) \
    if ((CMETA_FUNCTION_PARAM_FLAGS(row) & CMETA_PARAM_NULLABLE) == 0u && \
        capture->CMETA_FUNCTION_PARAM_NAME(row) == NULL) return CMETA_INVALID_ARGUMENT;
#define CMETA_BIND_NONNULL_(row,ctx) CMETA_BIND_SELECT_(CMETA_BIND_NONNULL_,row)(CMETA_BIND_ROW_(row))
#define CMETA_BIND_COPY_arg(row,name)
#define CMETA_BIND_COPY_value(row,name) \
    memcpy(callable.capture.bytes + offsetof(name##_capture,CMETA_FUNCTION_PARAM_NAME(row)), \
        &capture->CMETA_FUNCTION_PARAM_NAME(row),sizeof(capture->CMETA_FUNCTION_PARAM_NAME(row)));
#define CMETA_BIND_COPY_borrow(row,name) CMETA_BIND_COPY_value(row,name)
#define CMETA_BIND_COPY_(row,name) CMETA_BIND_SELECT_(CMETA_BIND_COPY_,row)(CMETA_BIND_ROW_(row),name)
#define CMETA_BIND_RESULT_0(type,source,...) \
    type result; \
    if (out == NULL) return false; \
    result = source(CMETA_PP_FOR_EACH_I(CMETA_BIND_ARG_,~,__VA_ARGS__)); \
    memcpy(out,&result,sizeof(result));
#define CMETA_BIND_RESULT_1(type,source,...) \
    (void)out; source(CMETA_PP_FOR_EACH_I(CMETA_BIND_ARG_,~,__VA_ARGS__));

/** Generate name_capture, FunctionMeta/Abi(name), name_bind(capture,out).
 * source must have canonical FunctionMeta/Abi declarations. signature is an
 * existing cmeta_sig for the projected parameters; admission checks the match.
 * The same frontend binds a receiver using a borrow row with RECEIVER flags.
 * All descriptors/code/captured borrows must outlive every callable copy. */
#define FunctionBindDeclAsAbiResult(contract,type,descriptor,carrier,flags,name,source,signature,...) \
    CMETA_PP_FOR_EACH_A(CMETA_BIND_PROOF_,~,__VA_ARGS__) \
    typedef type (*name##__source_type)(CMETA_PP_FOR_EACH_I(CMETA_BIND_NATIVE_,~,__VA_ARGS__)); \
    CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&source,name##__source_type), \
        "CMeta bind source native signature mismatch"); \
    CMETA_STATIC_ASSERT(CMETA_TYPE_IS_VOID(type) == CMETA_PP_CAT(CMETA_INVOKE_VOID_,carrier), \
        "CMeta bind result carrier mismatch"); \
    typedef struct name##_capture { \
        CMETA_PP_FOR_EACH_A(CMETA_BIND_FIELD_,~,__VA_ARGS__) \
    } name##_capture; \
    CMETA_STATIC_ASSERT(sizeof(name##_capture) <= CMETA_CAPTURE_INLINE, \
        "CMeta bind capture exceeds inline capacity"); \
    enum { name##__arity = 0 CMETA_PP_FOR_EACH_A(CMETA_BIND_COUNT_,~,__VA_ARGS__) }; \
    CMETA_STATIC_ASSERT(name##__arity >= 1 && name##__arity <= 2 && \
        name##__arity < CMETA_PP_NARG(__VA_ARGS__), \
        "CMeta bind requires captures and a registered unary or binary projection"); \
    CMETA_LOCAL const cmeta_param_desc name##__params[] = { \
        {sizeof(cmeta_param_desc),NULL,NULL,0}, \
        CMETA_PP_FOR_EACH_A(CMETA_BIND_META_,~,__VA_ARGS__) }; \
    CMETA_LOCAL const cmeta_abi_carrier name##__carriers[] = { CMETA_ABI_UNSPECIFIED, \
        CMETA_PP_FOR_EACH_A(CMETA_BIND_ABI_,~,__VA_ARGS__) }; \
    CMETA_LOCAL const cmeta_function_desc name##__function_meta = { sizeof(cmeta_function_desc), \
        #name,descriptor,name##__params+1,name##__arity,CMETA_CONTRACT_EFFECTS(contract), \
        CMETA_CONTRACT_PROPERTIES(contract),flags }; \
    CMETA_LOCAL const cmeta_function_abi_desc name##__function_abi_meta = { \
        sizeof(cmeta_function_abi_desc),&name##__function_meta,carrier,name##__carriers+1,name##__arity }; \
    CMETA_INLINE const cmeta_function_desc *name##_function(void) { return &name##__function_meta; } \
    CMETA_INLINE const cmeta_function_abi_desc *name##_function_abi(void) { return &name##__function_abi_meta; } \
    CMETA_INLINE bool name##__invoke(const cmeta_callable *self,void *out,const void *const *args) { \
        name##_capture capture; size_t next = 0u; (void)next; (void)args; \
        if (self == NULL || self->capture_size != sizeof(capture)) return false; \
        memcpy(&capture,self->capture.bytes,sizeof(capture)); \
        CMETA_PP_FOR_EACH_I(CMETA_BIND_LOAD_,~,__VA_ARGS__) \
        CMETA_PP_CAT(CMETA_BIND_RESULT_,CMETA_PP_CAT(CMETA_INVOKE_VOID_,carrier))(type,source,__VA_ARGS__) \
        return true; \
    } \
    CMETA_INLINE cmeta_status name##_bind(const name##_capture *capture,cmeta_invokable *out) { \
        static const bool bound[] = { CMETA_PP_FOR_EACH_A(CMETA_BIND_MASK_,~,__VA_ARGS__) }; \
        static const cmeta_param_desc params[] = { CMETA_PP_FOR_EACH_A(CMETA_BIND_SOURCE_META_,~,__VA_ARGS__) }; \
        static const cmeta_abi_carrier carriers[] = { CMETA_PP_FOR_EACH_A(CMETA_BIND_SOURCE_ABI_,~,__VA_ARGS__) }; \
        const cmeta_function_desc source_desc = {sizeof(cmeta_function_desc),#source,descriptor,params, \
            CMETA_PP_NARG(__VA_ARGS__),CMETA_CONTRACT_EFFECTS(contract),CMETA_CONTRACT_PROPERTIES(contract),flags}; \
        const cmeta_function_abi_desc source_abi = {sizeof(cmeta_function_abi_desc),&source_desc,carrier, \
            carriers,CMETA_PP_NARG(__VA_ARGS__)}; \
        cmeta_callable callable = CMETA_BIND_EMPTY_CALLABLE_; \
        if (out == NULL) return CMETA_INVALID_ARGUMENT; \
        out->size = sizeof(*out); out->function = NULL; out->data = NULL; out->callable = callable; \
        if (capture == NULL) return CMETA_INVALID_ARGUMENT; \
        CMETA_PP_FOR_EACH_A(CMETA_BIND_NONNULL_,~,__VA_ARGS__) \
        if (!cmeta_function_abi_desc_equal(FunctionAbi(source),&source_abi) || \
            !cmeta_function_projection_valid(FunctionAbi(source),FunctionAbi(name),bound, \
                CMETA_PP_NARG(__VA_ARGS__))) return CMETA_TYPE_MISMATCH; \
        callable.meta.sig = signature; callable.meta.effects = CMETA_CONTRACT_EFFECTS(contract); \
        callable.meta.properties = CMETA_CONTRACT_PROPERTIES(contract); \
        callable.invoke = name##__invoke; callable.capture_size = sizeof(*capture); \
        memset(callable.capture.bytes,0,sizeof(callable.capture.bytes)); \
        CMETA_PP_FOR_EACH_A(CMETA_BIND_COPY_,name,__VA_ARGS__) \
        return cmeta_invokable_bind(FunctionMeta(name),callable,out); \
    } \
    typedef char name##__bind_complete[1]

#endif
