#ifndef TINYMOCK_FUNCTION_H
#define TINYMOCK_FUNCTION_H

/*
 * CMeta-reflected free-function bridge for TinyMock.
 *
 * Normal test code includes this header after its production API header and
 * uses TINYMOCk_USE(name) / TINYMOCk_FUNCTION(name).
 *
 * Test-build replacement translation units define
 * TINYMOCk_FUNCTION_DEFINITIONS before including this header, then include the
 * production API header. The natural CMeta FunctionDecl* spellings are replayed
 * into exact-ABI mock definitions without parsing arbitrary C prototypes.
 */

#ifdef TINYMOCk_FUNCTION_DEFINITIONS
#ifndef TINYTEST_NO_MAIN
#define TINYTEST_NO_MAIN
#endif
#endif

#include "tinymock.h"
#include <cmeta/function.h>

#ifdef __cplusplus
#error "tinymock_function.h is the strict-C11 reflected function mock bridge"
#endif

typedef struct tinymock_function_mock {
  tinymock_mock_t mock;
  const cmeta_function_desc *function;
} tinymock_function_mock_t;

#define TINYMOCk_FUNCTION_STATE(name) TINYMOCk_CAT(tinymock_function_, name)
#define TINYMOCk_FUNCTION_RESET(name) TINYMOCk_MOCK_FN(name, _reset)
#define TINYMOCk_FUNCTION_META_FN(name) TINYMOCk_MOCK_FN(name, _function)

#define TINYMOCk_FUNCTION(name) (&TINYMOCk_FUNCTION_STATE(name).mock)
#define TINYMOCk_FUNCTION_META(name) TINYMOCk_FUNCTION_META_FN(name)()

#define TINYMOCk_USE(name) \
  extern tinymock_function_mock_t TINYMOCk_FUNCTION_STATE(name); \
  void TINYMOCk_FUNCTION_RESET(name)(void); \
  const cmeta_function_desc *TINYMOCk_FUNCTION_META_FN(name)(void); \
  typedef int TINYMOCk_CAT(tinymock_use_, TINYMOCk_CAT(name, _anchor_t))

#ifdef TINYMOCk_FUNCTION_DEFINITIONS

#define TINYMOCk_FUNCTION_PARAM_NAME_3(type, name, flags) name
#define TINYMOCk_FUNCTION_PARAM_NAME_4(type, name, flags, descriptor) name
#define TINYMOCk_FUNCTION_PARAM_NAME_I(...) \
  CMETA_PP_CAT(TINYMOCk_FUNCTION_PARAM_NAME_, CMETA_PP_NARG(__VA_ARGS__))(__VA_ARGS__)
#define TINYMOCk_FUNCTION_PARAM_NAME(row) TINYMOCk_FUNCTION_PARAM_NAME_I row

#define TINYMOCk_FUNCTION_ARG_ROW(index, row, ignored) \
  CMETA_PP_CAT(CMETA_FUNCTION_COMMA_, index) \
  TINYMOCk_VALUE(TINYMOCk_FUNCTION_PARAM_NAME(row))

#define TINYMOCk_FUNCTION_STATE_DEFINE(name) \
  tinymock_function_mock_t TINYMOCk_FUNCTION_STATE(name); \
  void TINYMOCk_FUNCTION_RESET(name)(void) { \
    TINYMOCk_FUNCTION_STATE(name).function = FunctionMeta(name); \
    TINYMOCk_ASSERT( \
        cmeta_function_desc_valid(TINYMOCk_FUNCTION_STATE(name).function), \
        "tinymock %s: invalid CMeta function descriptor", #name); \
    tinymock_mock_init(&TINYMOCk_FUNCTION_STATE(name).mock, #name); \
    tinymock_mock_set_default_return( \
        &TINYMOCk_FUNCTION_STATE(name).mock, tinymock_value_zero()); \
  } \
  const cmeta_function_desc *TINYMOCk_FUNCTION_META_FN(name)(void) { \
    return FunctionMeta(name); \
  }

#define TINYMOCk_FUNCTION_DECL_AS(contract, return_type, return_desc, name, ...) \
  CMETA_FUNCTION_DECL_AS(contract, return_type, return_desc, name, __VA_ARGS__); \
  _Static_assert(CMETA_PP_NARG(__VA_ARGS__) <= TINYMOCk_MAX_ARGS, \
                 "TinyMock reflected function exceeds TINYMOCk_MAX_ARGS"); \
  TINYMOCk_FUNCTION_STATE_DEFINE(name) \
  return_type name( \
      CMETA_PP_FOR_EACH_I(CMETA_FUNCTION_PARAM_DECL, ~, __VA_ARGS__)) { \
    tinymock_value_t tinymock_args__[] = { \
      CMETA_PP_FOR_EACH_I(TINYMOCk_FUNCTION_ARG_ROW, ~, __VA_ARGS__) \
    }; \
    tinymock_value_t tinymock_result__ = tinymock_mock_dispatch( \
        &TINYMOCk_FUNCTION_STATE(name).mock, CMETA_PP_NARG(__VA_ARGS__), \
        tinymock_args__); \
    return TINYMOCk_VALUE_AS(return_type, tinymock_result__); \
  } \
  typedef int TINYMOCk_CAT(tinymock_function_definition_, \
                           TINYMOCk_CAT(name, _anchor_t))

#define TINYMOCk_FUNCTION_DECL(contract, return_type, name, ...) \
  TINYMOCk_FUNCTION_DECL_AS(contract, return_type, \
                            CMETA_FUNCTION_RETURN_TYPEOF(return_type), \
                            name, __VA_ARGS__)

#define TINYMOCk_FUNCTION0_DECL_AS(contract, return_type, return_desc, name) \
  CMETA_FUNCTION0_DECL_AS(contract, return_type, return_desc, name); \
  TINYMOCk_FUNCTION_STATE_DEFINE(name) \
  return_type name(void) { \
    tinymock_value_t tinymock_result__ = tinymock_mock_dispatch( \
        &TINYMOCk_FUNCTION_STATE(name).mock, 0u, NULL); \
    return TINYMOCk_VALUE_AS(return_type, tinymock_result__); \
  } \
  typedef int TINYMOCk_CAT(tinymock_function_definition_, \
                           TINYMOCk_CAT(name, _anchor_t))

#define TINYMOCk_FUNCTION0_DECL(contract, return_type, name) \
  TINYMOCk_FUNCTION0_DECL_AS(contract, return_type, \
                             CMETA_FUNCTION_RETURN_TYPEOF(return_type), name)

/*
 * cmeta/function.h deliberately leaves the natural declaration spellings
 * overridable. Rebind only the natural names; the canonical CMETA_* macros stay
 * untouched and remain the semantic source.
 */
#undef FunctionDecl
#define FunctionDecl(...) TINYMOCk_FUNCTION_DECL(__VA_ARGS__)
#undef FunctionDeclAs
#define FunctionDeclAs(...) TINYMOCk_FUNCTION_DECL_AS(__VA_ARGS__)
#undef Function0Decl
#define Function0Decl(...) TINYMOCk_FUNCTION0_DECL(__VA_ARGS__)
#undef Function0DeclAs
#define Function0DeclAs(...) TINYMOCk_FUNCTION0_DECL_AS(__VA_ARGS__)

#endif /* TINYMOCk_FUNCTION_DEFINITIONS */

#endif /* TINYMOCK_FUNCTION_H */
