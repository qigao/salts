#ifndef CMETA_INVOKE_DECL_H
#define CMETA_INVOKE_DECL_H

#include <cmeta/function.h>

typedef bool (*cmeta_exact_invoke_fn)(void *return_storage,
    void *const *params, size_t param_count);

/* Opt-in exact native lowering. The same rows generate FunctionDesc, FunctionAbi
 * and the thunk; reflection is never interpreted as a calling convention.
 * Storage is borrowed, aligned, and contains the exact native objects. Pointer
 * parameters require a pointer object even when its stored value is NULL.
 * Use unqualified value carrier types: array/function typedefs and top-level
 * qualifiers undergo C parameter adjustment and are rejected by this frontend.
 * Count/storage rejection occurs before the native function is called. */
#define CMETA_INVOKE_PARAM_TYPE(index) CMETA_PP_CAT(cmeta_invoke_param_,index)
#define CMETA_INVOKE_PARAM(index,row,ignored) \
    typedef CMETA_PP_TUPLE_GET_0(row) CMETA_INVOKE_PARAM_TYPE(index); \
    CMETA_STATIC_ASSERT(CMETA_TYPE_IS_VALUE(CMETA_INVOKE_PARAM_TYPE(index)), \
        "CMeta exact invoke requires an unqualified value parameter carrier"); \
    CMETA_STATIC_ASSERT(CMETA_FUNCTION_PARAM_ABI_APPLY(row) != CMETA_ABI_UNSPECIFIED && \
        CMETA_FUNCTION_PARAM_ABI_APPLY(row) != CMETA_ABI_VOID, \
        "CMeta exact invoke requires explicit parameter ABI carriers"); \
    if (params[index] == NULL) return false;
#define CMETA_INVOKE_ARG(index,row,ignored) \
    CMETA_PP_SEP_COMMA(index) *CMETA_INVOKE_STORAGE(CMETA_INVOKE_PARAM_TYPE(index),params[index])

#ifdef __cplusplus
#define CMETA_INVOKE_STORAGE(type,storage) static_cast<type *>(storage)
#else
#define CMETA_INVOKE_STORAGE(type,storage) ((type *)(storage))
#endif

/* The return carrier is an admitted canonical token, not a type-spelling guess. */
#define CMETA_INVOKE_VOID_CMETA_ABI_VOID 1
#define CMETA_INVOKE_VOID_CMETA_ABI_SCALAR 0
#define CMETA_INVOKE_VOID_CMETA_ABI_OBJECT_POINTER 0
#define CMETA_INVOKE_VOID_CMETA_ABI_AGGREGATE 0
#define CMETA_INVOKE_VOID_CMETA_ABI_FUNCTION_POINTER 0
#define CMETA_INVOKE_VOID_CMETA_ABI_OPAQUE 0
#define CMETA_INVOKE_VOID_CMETA_ABI_ENUM 0
#define CMETA_INVOKE_RESULT_0(type,name,N,...) \
    if (return_storage == NULL) return false; \
    *CMETA_INVOKE_STORAGE(type,return_storage) = \
        name(CMETA_PP_MAP_I_N(N,CMETA_INVOKE_ARG,~,__VA_ARGS__));
#define CMETA_INVOKE_RESULT_1(type,name,N,...) \
    (void)return_storage; \
    name(CMETA_PP_MAP_I_N(N,CMETA_INVOKE_ARG,~,__VA_ARGS__));
#define CMETA_INVOKE_THUNK(type,carrier,name,N,...) \
    CMETA_STATIC_ASSERT(CMETA_TYPE_IS_VOID(type) == \
        CMETA_PP_CAT(CMETA_INVOKE_VOID_,carrier), "CMeta exact invoke result carrier mismatch"); \
    CMETA_INLINE bool name##__exact_invoke( \
        void *return_storage, void *const *params, size_t param_count) { \
        if (param_count != N || (N != 0 && params == NULL)) return false; \
        (void)params; \
        CMETA_PP_MAP_I_N(N,CMETA_INVOKE_PARAM,~,__VA_ARGS__) \
        CMETA_PP_CAT(CMETA_INVOKE_RESULT_,CMETA_PP_CAT(CMETA_INVOKE_VOID_,carrier))( \
            type,name,N,__VA_ARGS__) \
        return true; \
    } \
    typedef char name##__exact_invoke_complete[1]

#define FunctionInvoke(name) CMETA_PP_CAT(name,__exact_invoke)

#define FunctionInvokeDecl(contract,type,name,...) \
    FunctionDecl(contract,type,name,__VA_ARGS__); \
    CMETA_INVOKE_THUNK(type,CMETA_FUNCTION_RETURN_ABI(type),name, \
        CMETA_PP_NARG(__VA_ARGS__),__VA_ARGS__)
#define Function0InvokeDecl(contract,type,name) \
    Function0Decl(contract,type,name); \
    CMETA_INVOKE_THUNK(type,CMETA_FUNCTION_RETURN_ABI(type),name,0,)
#define FunctionInvokeDeclAsAbiResult(contract,type,descriptor,carrier,flags,name,...) \
    FunctionDeclAsAbiResult(contract,type,descriptor,carrier,flags,name,__VA_ARGS__); \
    CMETA_INVOKE_THUNK(type,carrier,name,CMETA_PP_NARG(__VA_ARGS__),__VA_ARGS__)
#define Function0InvokeDeclAsAbiResult(contract,type,descriptor,carrier,flags,name) \
    Function0DeclAsAbiResult(contract,type,descriptor,carrier,flags,name); \
    CMETA_INVOKE_THUNK(type,carrier,name,0,)
#define FunctionInvokeDeclAsAbi(contract,type,descriptor,carrier,name,...) \
    FunctionInvokeDeclAsAbiResult(contract,type,descriptor,carrier, \
        CMETA_RESULT_UNKNOWN,name,__VA_ARGS__)
#define Function0InvokeDeclAsAbi(contract,type,descriptor,carrier,name) \
    Function0InvokeDeclAsAbiResult(contract,type,descriptor,carrier,CMETA_RESULT_UNKNOWN,name)

#endif
