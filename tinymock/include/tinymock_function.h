#ifndef TINYMOCK_FUNCTION_H
#define TINYMOCK_FUNCTION_H

/*
 * TinyMock free-function bridge for CMeta FunctionDecl.
 *
 * Normal test translation units include this header for state/access macros.
 * A generated replacement translation unit defines
 * TINYMOCK_GENERATE_FUNCTION_OVERRIDES before including this header, then
 * includes one or more production headers containing FunctionDecl(...)
 * declarations. The CMeta declaration extension hook replays those exact rows
 * into external replacement definitions.
 *
 * Phase-2a intentionally supports TinyMock's existing portable value carrier:
 * scalar values, strings and object pointers. Literal-void and value-return
 * functions are generated separately at preprocessing time. Arbitrary by-value
 * objects, variadics and function-pointer values remain outside this backend.
 */

#include "tinymock.h"

#ifdef __cplusplus
#error "tinymock_function.h is the strict-C11 free-function mock bridge"
#endif

#define TINYMOCk_FUNCTION_STATE_NAME_I(name) tinymock_function_##name
#define TINYMOCk_FUNCTION_STATE_NAME(name) TINYMOCk_FUNCTION_STATE_NAME_I(name)

#define TINYMOCk_FUNCTION_RESET_NAME_I(name) tinymock_function_##name##_reset
#define TINYMOCk_FUNCTION_RESET_NAME(name) TINYMOCk_FUNCTION_RESET_NAME_I(name)

#define TINYMOCk_FUNCTION_META_NAME_I(name) tinymock_function_##name##_meta
#define TINYMOCk_FUNCTION_META_NAME(name) TINYMOCk_FUNCTION_META_NAME_I(name)

#define TINYMOCk_FUNCTION_HISTORY_NAME_I(name) tinymock_function_##name##_history
#define TINYMOCk_FUNCTION_HISTORY_NAME(name) TINYMOCk_FUNCTION_HISTORY_NAME_I(name)

#define TINYMOCk_FUNCTION_DESTROY_NAME_I(name) tinymock_function_##name##_destroy
#define TINYMOCk_FUNCTION_DESTROY_NAME(name) TINYMOCk_FUNCTION_DESTROY_NAME_I(name)

#define TINYMOCk_FUNCTION_ACTIONS_NAME_I(name) tinymock_function_##name##_actions
#define TINYMOCk_FUNCTION_ACTIONS_NAME(name) TINYMOCk_FUNCTION_ACTIONS_NAME_I(name)

#define TINYMOCk_FUNCTION_DECLARE(name) \
  extern tinymock_mock_t TINYMOCk_FUNCTION_STATE_NAME(name); \
  extern struct tinymock_cmeta_history TINYMOCk_FUNCTION_HISTORY_NAME(name); \
  extern struct tinymock_cmeta_actions TINYMOCk_FUNCTION_ACTIONS_NAME(name); \
  void TINYMOCk_FUNCTION_RESET_NAME(name)(void); \
  void TINYMOCk_FUNCTION_DESTROY_NAME(name)(void); \
  const struct cmeta_function_desc *TINYMOCk_FUNCTION_META_NAME(name)(void)

#define TINYMOCk_FUNCTION(name) (&TINYMOCk_FUNCTION_STATE_NAME(name))
#define TINYMOCk_FUNCTION_HISTORY(name) (&TINYMOCk_FUNCTION_HISTORY_NAME(name))
#define TINYMOCk_FUNCTION_ACTIONS(name) (&TINYMOCk_FUNCTION_ACTIONS_NAME(name))
#define TINYMOCk_FUNCTION_RESET(name) TINYMOCk_FUNCTION_RESET_NAME(name)()
#define TINYMOCk_FUNCTION_DESTROY(name) TINYMOCk_FUNCTION_DESTROY_NAME(name)()
#define TINYMOCk_FUNCTION_META(name) TINYMOCk_FUNCTION_META_NAME(name)()

#define TINYMOCk_FUNCTION_ARG_EQUAL(name, call_index, param_name, expected_lvalue) \
  tinymock_cmeta_history_arg_equal_name( \
      TINYMOCk_FUNCTION_HISTORY(name), (call_index), (param_name), \
      &(expected_lvalue), TINYMOCk_VALUE(expected_lvalue))

#define TINYMOCk_FUNCTION_COUNT_EQUAL(name, param_name, expected_lvalue) \
  tinymock_cmeta_history_count_equal_name( \
      TINYMOCk_FUNCTION_HISTORY(name), (param_name), \
      &(expected_lvalue), TINYMOCk_VALUE(expected_lvalue))

#define TINYMOCk_FUNCTION_CAPTURE(name, call_index, param_name, captor) \
  tinymock_cmeta_captor_capture_name( \
      (captor), TINYMOCk_FUNCTION_HISTORY(name), (call_index), (param_name))

#define TINYMOCk_FUNCTION_SET_OUT(name, param_name, value_lvalue) \
  tinymock_cmeta_actions_set_output_name( \
      TINYMOCk_FUNCTION_ACTIONS(name), TINYMOCk_FUNCTION_META(name), \
      (param_name), &(value_lvalue))

#define TINYMOCk_FUNCTION_CLEAR_OUT(name, param_name) \
  tinymock_cmeta_actions_clear_output_name( \
      TINYMOCk_FUNCTION_ACTIONS(name), (param_name))

#if defined(TINYMOCK_GENERATE_FUNCTION_OVERRIDES)

#define TINYMOCk_PP_SECOND_(a, b, ...) b
#define TINYMOCk_PP_PROBE_() ~, 1
#define TINYMOCk_PP_IS_PROBE_(...) TINYMOCk_PP_SECOND_(__VA_ARGS__, 0, 0)
#define TINYMOCk_RETURN_VOID_MARK_void TINYMOCk_PP_PROBE_()
#define TINYMOCk_RETURN_IS_VOID_(type) \
  TINYMOCk_PP_IS_PROBE_(TINYMOCk_CAT(TINYMOCk_RETURN_VOID_MARK_, type))
#define TINYMOCk_RETURN_SELECT_(prefix, type) \
  TINYMOCk_CAT(prefix, TINYMOCk_RETURN_IS_VOID_(type))

#define TINYMOCk_FUNCTION_SELECTED_MARK_(name) \
  TINYMOCk_CAT(TINYMOCK_SELECTED_FUNCTION_, name)
#define TINYMOCk_FUNCTION_IS_SELECTED_(name) \
  TINYMOCk_PP_IS_PROBE_(TINYMOCk_FUNCTION_SELECTED_MARK_(name))

#if defined(TINYMOCK_SELECTIVE_FUNCTION_OVERRIDES)
#define TINYMOCk_FUNCTION_SHOULD_GENERATE_(name) \
  TINYMOCk_FUNCTION_IS_SELECTED_(name)
#else
#define TINYMOCk_FUNCTION_SHOULD_GENERATE_(name) 1
#endif

#ifdef CMETA_FUNCTION_DECL_EXTENSION
#undef CMETA_FUNCTION_DECL_EXTENSION
#endif
#ifdef CMETA_FUNCTION0_DECL_EXTENSION
#undef CMETA_FUNCTION0_DECL_EXTENSION
#endif

#define TINYMOCk_FUNCTION_PARAM_NAME_3(type, name, flags) name
#define TINYMOCk_FUNCTION_PARAM_NAME_4(type, name, flags, descriptor) name
#define TINYMOCk_FUNCTION_PARAM_NAME_APPLY_I(...) \
  CMETA_PP_CAT(TINYMOCk_FUNCTION_PARAM_NAME_, CMETA_PP_NARG(__VA_ARGS__))(__VA_ARGS__)
#define TINYMOCk_FUNCTION_PARAM_NAME_APPLY(row) \
  TINYMOCk_FUNCTION_PARAM_NAME_APPLY_I row

#define TINYMOCk_FUNCTION_ACTUAL_ROW(index, row, ignored) \
  CMETA_PP_CAT(CMETA_FUNCTION_COMMA_, index) \
  TINYMOCk_VALUE(TINYMOCk_FUNCTION_PARAM_NAME_APPLY(row))

#define TINYMOCk_FUNCTION_TYPED_ARG_ROW(index, row, ignored) \
  CMETA_PP_CAT(CMETA_FUNCTION_COMMA_, index) \
  (const void *)&TINYMOCk_FUNCTION_PARAM_NAME_APPLY(row)

#define TINYMOCk_FUNCTION_DEFINE_STATE(fn_name) \
  tinymock_mock_t TINYMOCk_FUNCTION_STATE_NAME(fn_name); \
  tinymock_cmeta_history TINYMOCk_FUNCTION_HISTORY_NAME(fn_name); \
  tinymock_cmeta_actions TINYMOCk_FUNCTION_ACTIONS_NAME(fn_name); \
  void TINYMOCk_FUNCTION_RESET_NAME(fn_name)(void) { \
    const cmeta_function_desc *meta__ = FunctionMeta(fn_name); \
    TINYMOCk_ASSERT(cmeta_function_desc_valid(meta__), \
                    "tinymock reflected function metadata is invalid: %s", #fn_name); \
    tinymock_mock_init(&TINYMOCk_FUNCTION_STATE_NAME(fn_name), meta__->name); \
    tinymock_mock_set_default_return(&TINYMOCk_FUNCTION_STATE_NAME(fn_name), \
                                     tinymock_value_zero()); \
    tinymock_cmeta_history_reset(&TINYMOCk_FUNCTION_HISTORY_NAME(fn_name), meta__); \
    tinymock_cmeta_actions_reset(&TINYMOCk_FUNCTION_ACTIONS_NAME(fn_name), meta__); \
  } \
  void TINYMOCk_FUNCTION_DESTROY_NAME(fn_name)(void) { \
    tinymock_cmeta_history_destroy(&TINYMOCk_FUNCTION_HISTORY_NAME(fn_name)); \
    tinymock_cmeta_actions_destroy(&TINYMOCk_FUNCTION_ACTIONS_NAME(fn_name)); \
  } \
  const cmeta_function_desc *TINYMOCk_FUNCTION_META_NAME(fn_name)(void) { \
    return FunctionMeta(fn_name); \
  }

#define TINYMOCk_FUNCTION_DECL_EXTENSION_0(contract, return_type, return_desc, name, ...) \
  TINYMOCk_FUNCTION_DEFINE_STATE(name) \
  return_type name( \
      CMETA_PP_FOR_EACH_I(CMETA_FUNCTION_PARAM_DECL, ~, __VA_ARGS__)) { \
    tinymock_value_t args__[] = { \
      CMETA_PP_FOR_EACH_I(TINYMOCk_FUNCTION_ACTUAL_ROW, ~, __VA_ARGS__) \
    }; \
    const void *typed_args__[] = { \
      CMETA_PP_FOR_EACH_I(TINYMOCk_FUNCTION_TYPED_ARG_ROW, ~, __VA_ARGS__) \
    }; \
    TINYMOCk_ASSERT( \
        tinymock_cmeta_history_record( \
            &TINYMOCk_FUNCTION_HISTORY_NAME(name), FunctionMeta(name), \
            CMETA_PP_NARG(__VA_ARGS__), typed_args__, args__), \
        "tinymock cannot snapshot reflected arguments for %s", #name); \
    tinymock_value_t result__ = tinymock_mock_dispatch( \
        &TINYMOCk_FUNCTION_STATE_NAME(name), CMETA_PP_NARG(__VA_ARGS__), args__); \
    TINYMOCk_ASSERT( \
        tinymock_cmeta_actions_apply( \
            &TINYMOCk_FUNCTION_ACTIONS_NAME(name), FunctionMeta(name), \
            CMETA_PP_NARG(__VA_ARGS__), args__), \
        "tinymock cannot apply reflected output actions for %s", #name); \
    return TINYMOCk_VALUE_AS(return_type, result__); \
  }

#define TINYMOCk_FUNCTION_DECL_EXTENSION_1(contract, return_type, return_desc, name, ...) \
  TINYMOCk_FUNCTION_DEFINE_STATE(name) \
  return_type name( \
      CMETA_PP_FOR_EACH_I(CMETA_FUNCTION_PARAM_DECL, ~, __VA_ARGS__)) { \
    tinymock_value_t args__[] = { \
      CMETA_PP_FOR_EACH_I(TINYMOCk_FUNCTION_ACTUAL_ROW, ~, __VA_ARGS__) \
    }; \
    const void *typed_args__[] = { \
      CMETA_PP_FOR_EACH_I(TINYMOCk_FUNCTION_TYPED_ARG_ROW, ~, __VA_ARGS__) \
    }; \
    TINYMOCk_ASSERT( \
        tinymock_cmeta_history_record( \
            &TINYMOCk_FUNCTION_HISTORY_NAME(name), FunctionMeta(name), \
            CMETA_PP_NARG(__VA_ARGS__), typed_args__, args__), \
        "tinymock cannot snapshot reflected arguments for %s", #name); \
    (void)tinymock_mock_dispatch( \
        &TINYMOCk_FUNCTION_STATE_NAME(name), CMETA_PP_NARG(__VA_ARGS__), args__); \
    TINYMOCk_ASSERT( \
        tinymock_cmeta_actions_apply( \
            &TINYMOCk_FUNCTION_ACTIONS_NAME(name), FunctionMeta(name), \
            CMETA_PP_NARG(__VA_ARGS__), args__), \
        "tinymock cannot apply reflected output actions for %s", #name); \
    return; \
  }

#define TINYMOCk_FUNCTION_DECL_SELECTED_0(contract, return_type, return_desc, name, ...)
#define TINYMOCk_FUNCTION_DECL_SELECTED_1(contract, return_type, return_desc, name, ...) \
  TINYMOCk_RETURN_SELECT_(TINYMOCk_FUNCTION_DECL_EXTENSION_, return_type)( \
      contract, return_type, return_desc, name, __VA_ARGS__)

#define TINYMOCk_FUNCTION_DECL_EXTENSION(contract, return_type, return_desc, name, ...) \
  TINYMOCk_CAT(TINYMOCk_FUNCTION_DECL_SELECTED_, \
               TINYMOCk_FUNCTION_SHOULD_GENERATE_(name))( \
      contract, return_type, return_desc, name, __VA_ARGS__)

#define TINYMOCk_FUNCTION0_DECL_EXTENSION_0(contract, return_type, return_desc, name) \
  TINYMOCk_FUNCTION_DEFINE_STATE(name) \
  return_type name(void) { \
    TINYMOCk_ASSERT( \
        tinymock_cmeta_history_record( \
            &TINYMOCk_FUNCTION_HISTORY_NAME(name), FunctionMeta(name), \
            0u, NULL, NULL), \
        "tinymock cannot snapshot reflected arguments for %s", #name); \
    tinymock_value_t result__ = tinymock_mock_dispatch( \
        &TINYMOCk_FUNCTION_STATE_NAME(name), 0u, NULL); \
    TINYMOCk_ASSERT( \
        tinymock_cmeta_actions_apply( \
            &TINYMOCk_FUNCTION_ACTIONS_NAME(name), FunctionMeta(name), \
            0u, NULL), \
        "tinymock cannot apply reflected output actions for %s", #name); \
    return TINYMOCk_VALUE_AS(return_type, result__); \
  }

#define TINYMOCk_FUNCTION0_DECL_EXTENSION_1(contract, return_type, return_desc, name) \
  TINYMOCk_FUNCTION_DEFINE_STATE(name) \
  return_type name(void) { \
    TINYMOCk_ASSERT( \
        tinymock_cmeta_history_record( \
            &TINYMOCk_FUNCTION_HISTORY_NAME(name), FunctionMeta(name), \
            0u, NULL, NULL), \
        "tinymock cannot snapshot reflected arguments for %s", #name); \
    (void)tinymock_mock_dispatch( \
        &TINYMOCk_FUNCTION_STATE_NAME(name), 0u, NULL); \
    TINYMOCk_ASSERT( \
        tinymock_cmeta_actions_apply( \
            &TINYMOCk_FUNCTION_ACTIONS_NAME(name), FunctionMeta(name), \
            0u, NULL), \
        "tinymock cannot apply reflected output actions for %s", #name); \
    return; \
  }

#define TINYMOCk_FUNCTION0_DECL_SELECTED_0(contract, return_type, return_desc, name)
#define TINYMOCk_FUNCTION0_DECL_SELECTED_1(contract, return_type, return_desc, name) \
  TINYMOCk_RETURN_SELECT_(TINYMOCk_FUNCTION0_DECL_EXTENSION_, return_type)( \
      contract, return_type, return_desc, name)

#define TINYMOCk_FUNCTION0_DECL_EXTENSION(contract, return_type, return_desc, name) \
  TINYMOCk_CAT(TINYMOCk_FUNCTION0_DECL_SELECTED_, \
               TINYMOCk_FUNCTION_SHOULD_GENERATE_(name))( \
      contract, return_type, return_desc, name)

#define CMETA_FUNCTION_DECL_EXTENSION(contract, return_type, return_desc, name, ...) \
  TINYMOCk_FUNCTION_DECL_EXTENSION(contract, return_type, return_desc, name, __VA_ARGS__)

#define CMETA_FUNCTION0_DECL_EXTENSION(contract, return_type, return_desc, name) \
  TINYMOCk_FUNCTION0_DECL_EXTENSION(contract, return_type, return_desc, name)

#endif /* TINYMOCK_GENERATE_FUNCTION_OVERRIDES */

#include <cmeta/function.h>
#include "tinymock_history.h"
#include "tinymock_actions.h"

#endif /* TINYMOCK_FUNCTION_H */
