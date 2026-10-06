#ifndef CMETA_FASTPATH_FIXTURE_H
#define CMETA_FASTPATH_FIXTURE_H

#include <cmeta/fastpath.h>

#ifdef __cplusplus
extern "C" {
extern cmeta_static_key_state fastpath_shared_key;
int fastpath_cpp_invoke(int value);
}
#else
typedef struct fastpath_pair { double value; int count; } fastpath_pair;
typedef int (*fastpath_callback)(int);
static const cmeta_type_desc fastpath_pair_type = {
    "fastpath_pair", sizeof(fastpath_pair), _Alignof(fastpath_pair),
    CMETA_T_OBJECT, NULL, NULL, NULL
};
static const cmeta_type_desc fastpath_callback_type = {
    "fastpath_callback", sizeof(fastpath_callback), _Alignof(fastpath_callback),
    CMETA_T_OBJECT, NULL, NULL, NULL
};
FunctionDecl(value, int, fastpath_add, (int, value, CMETA_PARAM_IN));
FunctionDecl(value, int, fastpath_other, (int, renamed, CMETA_PARAM_IN));
FunctionDecl(fallible, int, fastpath_effectful, (int, value, CMETA_PARAM_IN));
FunctionDecl(value, double, fastpath_float,
    (float, left, CMETA_PARAM_IN), (double, right, CMETA_PARAM_IN));
FunctionDeclAsAbi(value, fastpath_pair, &fastpath_pair_type, CMETA_ABI_AGGREGATE,
    fastpath_aggregate,
    (fastpath_pair, pair, CMETA_PARAM_IN, &fastpath_pair_type, CMETA_ABI_AGGREGATE),
    (double, value, CMETA_PARAM_IN));
FunctionDeclAsAbi(value, fastpath_callback, &fastpath_callback_type,
    CMETA_ABI_FUNCTION_POINTER, fastpath_choose,
    (fastpath_callback, callback, CMETA_PARAM_IN, &fastpath_callback_type,
     CMETA_ABI_FUNCTION_POINTER));
FunctionDecl(value, void, fastpath_void, (int, value, CMETA_PARAM_IN));
FunctionDeclAsAbiResult(value, char *, &cmeta_type_char_ptr, CMETA_ABI_OBJECT_POINTER,
    CMETA_RESULT_BORROWED, fastpath_borrow,
    (char *, input, CMETA_PARAM_IN | CMETA_PARAM_BORROWED,
     &cmeta_type_char_ptr, CMETA_ABI_OBJECT_POINTER));
Function0Decl(value, int, fastpath_zero);
Function0Decl(value, void, fastpath_void_zero);
Function0Decl(value, int, fastpath_unpublished);
Function0Decl(value, int, fastpath_published);
extern int fastpath_target_payload;
extern int fastpath_void_sink;
extern cmeta_static_key_state fastpath_shared_key;
const cmeta_function_abi_desc *fastpath_peer_abi(void);
int fastpath_cpp_invoke(int value);
#endif
#endif
