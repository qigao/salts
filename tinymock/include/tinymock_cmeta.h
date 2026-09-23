#ifndef TINYMOCK_CMETA_H
#define TINYMOCK_CMETA_H

/*
 * CMeta interface bridge for TinyMock.
 *
 * This header deliberately replays an existing CMETA_INTERFACE method schema.
 * It does not define a second interface/function reflection model and it does
 * not make TinyTest itself depend on CMeta.
 *
 * Phase-1 scope:
 *   - exact interface vtable generation from the CMeta X-list;
 *   - one TinyMock invocation ledger per method;
 *   - relaxed zero/default behavior until a method is explicitly stubbed;
 *   - call-count verification and boxed argument inspection through tinymock.h.
 *
 * Method arguments and return values currently use TinyMock's portable C11
 * scalar/string/pointer carrier. Generic CMeta value/trait-backed argument
 * storage is a later phase and must not be faked with a second type registry.
 */

#include "tinymock.h"
#include "tinymock_history.h"
#include "tinymock_actions.h"
#include "tinymock_return.h"
#include <cmeta/interface.h>

#ifdef __cplusplus
#error "tinymock_cmeta.h is the strict-C11 CMeta interface mock bridge"
#endif

#define TINYMOCk_INTERFACE_TYPE(I) TINYMOCk_CAT(tinymock_, I)
#define TINYMOCk_INTERFACE_METHOD_BASE(I, N) \
  TINYMOCk_CAT(TINYMOCk_CAT(TINYMOCk_INTERFACE_TYPE(I), _), N)
#define TINYMOCk_INTERFACE_CALLBACK(I, N) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_METHOD_BASE(I, N), _callback)
#define TINYMOCk_INTERFACE_VTABLE(I) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_TYPE(I), _vtable)
#define TINYMOCk_INTERFACE_INIT(I) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_TYPE(I), _init)
#define TINYMOCk_INTERFACE_AS(I) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_TYPE(I), _as_interface)

#define TINYMOCk_INTERFACE_DESTROY(I) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_TYPE(I), _destroy)
#define TINYMOCk_INTERFACE_HISTORY_FIELD(N) TINYMOCk_CAT(N, _history)
#define TINYMOCk_INTERFACE_ACTIONS_FIELD(N) TINYMOCk_CAT(N, _actions)
#define TINYMOCk_INTERFACE_RETURN_FIELD(N) TINYMOCk_CAT(N, _return)

#define TINYMOCk_INTERFACE_ANCHOR(I) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_TYPE(I), _interface_anchor_t)

#define TINYMOCk_INTERFACE_FIELD_LEGACY(I,R,N,...) \
  tinymock_mock_t N;
#define TINYMOCk_INTERFACE_FIELD_R0 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_R1 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_R2 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_R3 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_R4 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_V0 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_V1 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_V2 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_V3 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_V4 TINYMOCk_INTERFACE_FIELD_LEGACY
#define TINYMOCk_INTERFACE_FIELD_D0 TINYMOCk_INTERFACE_FIELD_LEGACY

#define TINYMOCk_INTERFACE_FIELD_REFLECTED(I,R,N,...) \
  tinymock_mock_t N; \
  tinymock_cmeta_history TINYMOCk_INTERFACE_HISTORY_FIELD(N); \
  tinymock_cmeta_actions TINYMOCk_INTERFACE_ACTIONS_FIELD(N); \
  tinymock_cmeta_return TINYMOCk_INTERFACE_RETURN_FIELD(N);
#define TINYMOCk_INTERFACE_FIELD_F0 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_F1 TINYMOCk_INTERFACE_FIELD_REFLECTED

#define TINYMOCk_INTERFACE_FIELD_ROW(I, K, R, N, ...) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_FIELD_, K)(I, R, N, __VA_ARGS__)

#define TINYMOCk_IFACE_BOX_CMETA_ABI_SCALAR(value) TINYMOCk_VALUE(value)
#define TINYMOCk_IFACE_BOX_CMETA_ABI_OBJECT_POINTER(value) TINYMOCk_VALUE(value)
#define TINYMOCk_IFACE_BOX_CMETA_ABI_AGGREGATE(value) tinymock_value_zero()
#define TINYMOCk_IFACE_BOX_CMETA_ABI_FUNCTION_POINTER(value) tinymock_value_zero()
#define TINYMOCk_IFACE_BOX_CMETA_ABI_ENUM(value) tinymock_value_zero()
#define TINYMOCk_IFACE_BOX_CMETA_ABI_VOID(value) tinymock_value_zero()
#define TINYMOCk_IFACE_BOX_CMETA_ABI_UNSPECIFIED(value) tinymock_value_zero()
#define TINYMOCk_IFACE_BOX_CMETA_ABI_OPAQUE(value) tinymock_value_zero()
#define TINYMOCk_IFACE_BOX(carrier, value) \
  TINYMOCk_CAT(TINYMOCk_IFACE_BOX_, carrier)(value)

#define TINYMOCk_IFACE_PARAM_BOX_3(type,name,flags) \
  TINYMOCk_IFACE_BOX(CMETA_ABI_SCALAR, name)
#define TINYMOCk_IFACE_PARAM_BOX_4(type,name,flags,descriptor) \
  tinymock_value_zero()
#define TINYMOCk_IFACE_PARAM_BOX_5(type,name,flags,descriptor,carrier) \
  TINYMOCk_IFACE_BOX(carrier, name)
#define TINYMOCk_IFACE_PARAM_BOX_APPLY_I(...) \
  TINYMOCk_CAT(TINYMOCk_IFACE_PARAM_BOX_, CMETA_PP_NARG(__VA_ARGS__))(__VA_ARGS__)
#define TINYMOCk_IFACE_PARAM_BOX_APPLY(row) \
  TINYMOCk_IFACE_PARAM_BOX_APPLY_I row

#define TINYMOCk_IFACE_RETURN_SCALAR(mock,I,N,R,result) \
  do { \
    R typed_result__; \
    if (tinymock_cmeta_return_enabled( \
            &(mock)->TINYMOCk_INTERFACE_RETURN_FIELD(N))) { \
      bool ok__ = tinymock_cmeta_return_write( \
          &(mock)->TINYMOCk_INTERFACE_RETURN_FIELD(N), I##_##N##_function(), \
          &typed_result__); \
      TINYMOCk_ASSERT(ok__, "tinymock cannot materialize interface return %s.%s", \
                      #I, #N); \
      if (ok__) return typed_result__; \
    } \
    return TINYMOCk_VALUE_AS(R, result); \
  } while (0)
#define TINYMOCk_IFACE_RETURN_OBJECT_POINTER(mock,I,N,R,result) \
  TINYMOCk_IFACE_RETURN_SCALAR(mock,I,N,R,result)
#define TINYMOCk_IFACE_RETURN_AGGREGATE(mock,I,N,R,result) \
  do { \
    R typed_result__ = {0}; \
    bool ok__ = tinymock_cmeta_return_write( \
        &(mock)->TINYMOCk_INTERFACE_RETURN_FIELD(N), I##_##N##_function(), \
        &typed_result__); \
    TINYMOCk_ASSERT(ok__, "tinymock aggregate interface return %s.%s requires a typed return", \
                    #I, #N); \
    return typed_result__; \
  } while (0)
#define TINYMOCk_IFACE_RETURN_FUNCTION_POINTER(mock,I,N,R,result) \
  do { \
    R typed_result__ = (R)0; \
    bool ok__ = tinymock_cmeta_return_write( \
        &(mock)->TINYMOCk_INTERFACE_RETURN_FIELD(N), I##_##N##_function(), \
        &typed_result__); \
    TINYMOCk_ASSERT(ok__, "tinymock function-pointer interface return %s.%s requires a typed return", \
                    #I, #N); \
    return typed_result__; \
  } while (0)
#define TINYMOCk_IFACE_RETURN_ENUM(mock,I,N,R,result) \
  do { \
    R typed_result__ = (R)0; \
    bool ok__ = tinymock_cmeta_return_write( \
        &(mock)->TINYMOCk_INTERFACE_RETURN_FIELD(N), I##_##N##_function(), \
        &typed_result__); \
    TINYMOCk_ASSERT(ok__, "tinymock enum interface return %s.%s requires a typed return", \
                    #I, #N); \
    return typed_result__; \
  } while (0)
#define TINYMOCk_IFACE_RETURN(carrier,mock,I,N,R,result) \
  TINYMOCk_CAT(TINYMOCk_IFACE_RETURN_, carrier)(mock,I,N,R,result)

#define TINYMOCk_INTERFACE_CALLBACK_R0(I, R, N, _) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 0u, NULL); \
    return TINYMOCk_VALUE_AS(R, result); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_R1(I, R, N, T1, A1) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque, T1 A1) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { TINYMOCk_VALUE(A1) }; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 1u, args); \
    return TINYMOCk_VALUE_AS(R, result); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_R2(I, R, N, T1, A1, T2, A2) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque, T1 A1, T2 A2) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { TINYMOCk_VALUE(A1), TINYMOCk_VALUE(A2) }; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 2u, args); \
    return TINYMOCk_VALUE_AS(R, result); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_R3(I, R, N, T1, A1, T2, A2, T3, A3) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque, T1 A1, T2 A2, T3 A3) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(A1), TINYMOCk_VALUE(A2), TINYMOCk_VALUE(A3) \
    }; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 3u, args); \
    return TINYMOCk_VALUE_AS(R, result); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_R4(I, R, N, T1, A1, T2, A2, T3, A3, T4, A4) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque, T1 A1, T2 A2, T3 A3, T4 A4) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(A1), TINYMOCk_VALUE(A2), \
      TINYMOCk_VALUE(A3), TINYMOCk_VALUE(A4) \
    }; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 4u, args); \
    return TINYMOCk_VALUE_AS(R, result); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_V0(I, R, N, _) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    (void)tinymock_mock_dispatch(&mock->N, 0u, NULL); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_V1(I, R, N, T1, A1) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque, T1 A1) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { TINYMOCk_VALUE(A1) }; \
    (void)tinymock_mock_dispatch(&mock->N, 1u, args); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_V2(I, R, N, T1, A1, T2, A2) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque, T1 A1, T2 A2) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { TINYMOCk_VALUE(A1), TINYMOCk_VALUE(A2) }; \
    (void)tinymock_mock_dispatch(&mock->N, 2u, args); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_V3(I, R, N, T1, A1, T2, A2, T3, A3) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque, T1 A1, T2 A2, T3 A3) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(A1), TINYMOCk_VALUE(A2), TINYMOCk_VALUE(A3) \
    }; \
    (void)tinymock_mock_dispatch(&mock->N, 3u, args); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_V4(I, R, N, T1, A1, T2, A2, T3, A3, T4, A4) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque, T1 A1, T2 A2, T3 A3, T4 A4) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(A1), TINYMOCk_VALUE(A2), \
      TINYMOCk_VALUE(A3), TINYMOCk_VALUE(A4) \
    }; \
    (void)tinymock_mock_dispatch(&mock->N, 4u, args); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_D0(I, R, N, _) \
  TINYMOCk_INTERFACE_CALLBACK_V0(I, R, N, _)

#define TINYMOCk_INTERFACE_CALLBACK_F0_0( \
    I,R,N,contract,return_desc,return_abi) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    TINYMOCk_ASSERT(tinymock_cmeta_history_record( \
        &mock->TINYMOCk_INTERFACE_HISTORY_FIELD(N), I##_##N##_function(), \
        0u, NULL, NULL), "tinymock cannot snapshot interface arguments %s.%s", \
        #I, #N); \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 0u, NULL); \
    TINYMOCk_ASSERT(tinymock_cmeta_actions_apply( \
        &mock->TINYMOCk_INTERFACE_ACTIONS_FIELD(N), I##_##N##_function(), \
        0u, NULL), "tinymock cannot apply interface actions %s.%s", #I, #N); \
    TINYMOCk_IFACE_RETURN(return_abi, mock, I, N, R, result); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_F0_1( \
    I,R,N,contract,return_desc,return_abi) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    TINYMOCk_ASSERT(tinymock_cmeta_history_record( \
        &mock->TINYMOCk_INTERFACE_HISTORY_FIELD(N), I##_##N##_function(), \
        0u, NULL, NULL), "tinymock cannot snapshot interface arguments %s.%s", \
        #I, #N); \
    (void)tinymock_mock_dispatch(&mock->N, 0u, NULL); \
    TINYMOCk_ASSERT(tinymock_cmeta_actions_apply( \
        &mock->TINYMOCk_INTERFACE_ACTIONS_FIELD(N), I##_##N##_function(), \
        0u, NULL), "tinymock cannot apply interface actions %s.%s", #I, #N); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_F0(I,R,N,...) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_CALLBACK_F0_, \
               CMETA_FUNCTION_RETURN_IS_VOID_(R))(I,R,N,__VA_ARGS__)

#define TINYMOCk_INTERFACE_CALLBACK_F1_0( \
    I,R,N,contract,return_desc,return_abi,P1) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_FUNCTION_PARAM_DECL_APPLY(P1)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { TINYMOCk_IFACE_PARAM_BOX_APPLY(P1) }; \
    const void *typed_args[] = { \
      (const void *)&CMETA_FUNCTION_PARAM_NAME_APPLY(P1) \
    }; \
    TINYMOCk_ASSERT(tinymock_cmeta_history_record( \
        &mock->TINYMOCk_INTERFACE_HISTORY_FIELD(N), I##_##N##_function(), \
        1u, typed_args, args), "tinymock cannot snapshot interface arguments %s.%s", \
        #I, #N); \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 1u, args); \
    TINYMOCk_ASSERT(tinymock_cmeta_actions_apply( \
        &mock->TINYMOCk_INTERFACE_ACTIONS_FIELD(N), I##_##N##_function(), \
        1u, args), "tinymock cannot apply interface actions %s.%s", #I, #N); \
    TINYMOCk_IFACE_RETURN(return_abi, mock, I, N, R, result); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_F1_1( \
    I,R,N,contract,return_desc,return_abi,P1) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_FUNCTION_PARAM_DECL_APPLY(P1)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { TINYMOCk_IFACE_PARAM_BOX_APPLY(P1) }; \
    const void *typed_args[] = { \
      (const void *)&CMETA_FUNCTION_PARAM_NAME_APPLY(P1) \
    }; \
    TINYMOCk_ASSERT(tinymock_cmeta_history_record( \
        &mock->TINYMOCk_INTERFACE_HISTORY_FIELD(N), I##_##N##_function(), \
        1u, typed_args, args), "tinymock cannot snapshot interface arguments %s.%s", \
        #I, #N); \
    (void)tinymock_mock_dispatch(&mock->N, 1u, args); \
    TINYMOCk_ASSERT(tinymock_cmeta_actions_apply( \
        &mock->TINYMOCk_INTERFACE_ACTIONS_FIELD(N), I##_##N##_function(), \
        1u, args), "tinymock cannot apply interface actions %s.%s", #I, #N); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_F1(I,R,N,...) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_CALLBACK_F1_, \
               CMETA_FUNCTION_RETURN_IS_VOID_(R))(I,R,N,__VA_ARGS__)

#define TINYMOCk_INTERFACE_CALLBACK_ROW(I, K, R, N, ...) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_CALLBACK_, K)(I, R, N, __VA_ARGS__)

#define TINYMOCk_INTERFACE_VTABLE_ROW(I, K, R, N, ...) \
  .N = TINYMOCk_INTERFACE_CALLBACK(I, N),

#define TINYMOCk_INTERFACE_INIT_LEGACY(I,R,N,...) \
  tinymock_mock_init(&mock->N, #I "." #N); \
  tinymock_mock_set_default_return(&mock->N, tinymock_value_zero());
#define TINYMOCk_INTERFACE_INIT_R0 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_R1 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_R2 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_R3 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_R4 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_V0 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_V1 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_V2 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_V3 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_V4 TINYMOCk_INTERFACE_INIT_LEGACY
#define TINYMOCk_INTERFACE_INIT_D0 TINYMOCk_INTERFACE_INIT_LEGACY

#define TINYMOCk_INTERFACE_INIT_REFLECTED(I,R,N,...) \
  tinymock_mock_init(&mock->N, #I "." #N); \
  tinymock_mock_set_default_return(&mock->N, tinymock_value_zero()); \
  TINYMOCk_ASSERT(cmeta_function_desc_valid(I##_##N##_function()), \
                  "invalid reflected interface function %s.%s", #I, #N); \
  TINYMOCk_ASSERT(cmeta_function_abi_desc_valid(I##_##N##_function_abi()), \
                  "invalid reflected interface ABI %s.%s", #I, #N); \
  tinymock_cmeta_history_reset( \
      &mock->TINYMOCk_INTERFACE_HISTORY_FIELD(N), I##_##N##_function()); \
  tinymock_cmeta_actions_reset( \
      &mock->TINYMOCk_INTERFACE_ACTIONS_FIELD(N), I##_##N##_function()); \
  tinymock_cmeta_return_reset( \
      &mock->TINYMOCk_INTERFACE_RETURN_FIELD(N), I##_##N##_function());
#define TINYMOCk_INTERFACE_INIT_F0 TINYMOCk_INTERFACE_INIT_REFLECTED
#define TINYMOCk_INTERFACE_INIT_F1 TINYMOCk_INTERFACE_INIT_REFLECTED

#define TINYMOCk_INTERFACE_INIT_ROW(I, K, R, N, ...) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_INIT_, K)(I, R, N, __VA_ARGS__)

#define TINYMOCk_INTERFACE_DESTROY_LEGACY(I,R,N,...)
#define TINYMOCk_INTERFACE_DESTROY_R0 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_R1 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_R2 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_R3 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_R4 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_V0 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_V1 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_V2 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_V3 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_V4 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_D0 TINYMOCk_INTERFACE_DESTROY_LEGACY
#define TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,R,N,...) \
  tinymock_cmeta_history_destroy( \
      &mock->TINYMOCk_INTERFACE_HISTORY_FIELD(N)); \
  tinymock_cmeta_actions_destroy( \
      &mock->TINYMOCk_INTERFACE_ACTIONS_FIELD(N)); \
  tinymock_cmeta_return_destroy( \
      &mock->TINYMOCk_INTERFACE_RETURN_FIELD(N));
#define TINYMOCk_INTERFACE_DESTROY_F0 TINYMOCk_INTERFACE_DESTROY_REFLECTED
#define TINYMOCk_INTERFACE_DESTROY_F1 TINYMOCk_INTERFACE_DESTROY_REFLECTED
#define TINYMOCk_INTERFACE_DESTROY_ROW(I,K,R,N,...) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_DESTROY_, K)(I,R,N,__VA_ARGS__)

#define TINYMOCk_INTERFACE_WITH_CAPS(I, METHODS, CAPS) \
  typedef struct TINYMOCk_INTERFACE_TYPE(I) { \
    METHODS(TINYMOCk_INTERFACE_FIELD_ROW, I) \
  } TINYMOCk_INTERFACE_TYPE(I); \
  METHODS(TINYMOCk_INTERFACE_CALLBACK_ROW, I) \
  static const I##_vtable TINYMOCk_INTERFACE_VTABLE(I) = { \
    .implementation = "tinymock:" #I, \
    .capabilities = (uint64_t)(CAPS), \
    METHODS(TINYMOCk_INTERFACE_VTABLE_ROW, I) \
  }; \
  static inline void TINYMOCk_INTERFACE_INIT(I)(TINYMOCk_INTERFACE_TYPE(I) *mock) { \
    if (!mock) return; \
    METHODS(TINYMOCk_INTERFACE_INIT_ROW, I) \
  } \
  static inline void TINYMOCk_INTERFACE_DESTROY(I)(TINYMOCk_INTERFACE_TYPE(I) *mock) { \
    if (!mock) return; \
    METHODS(TINYMOCk_INTERFACE_DESTROY_ROW, I) \
  } \
  static inline I TINYMOCk_INTERFACE_AS(I)(TINYMOCk_INTERFACE_TYPE(I) *mock) { \
    return I##_bind(mock, &TINYMOCk_INTERFACE_VTABLE(I)); \
  } \
  typedef int TINYMOCk_INTERFACE_ANCHOR(I)

#define TINYMOCk_INTERFACE(I, METHODS) \
  TINYMOCk_INTERFACE_WITH_CAPS(I, METHODS, 0u)

#define TINYMOCk_INTERFACE_METHOD(mock, method) (&((mock)->method))

#define TINYMOCk_INTERFACE_METHOD_HISTORY(mock, method) \
  (&((mock)->TINYMOCk_INTERFACE_HISTORY_FIELD(method)))
#define TINYMOCk_INTERFACE_METHOD_ACTIONS(mock, method) \
  (&((mock)->TINYMOCk_INTERFACE_ACTIONS_FIELD(method)))
#define TINYMOCk_INTERFACE_METHOD_RETURN(mock, method) \
  (&((mock)->TINYMOCk_INTERFACE_RETURN_FIELD(method)))

#endif /* TINYMOCK_CMETA_H */
