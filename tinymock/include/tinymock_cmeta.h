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

#define TINYMOCk_INTERFACE_HISTORY_NAME(N) TINYMOCk_CAT(N, _history)
#define TINYMOCk_INTERFACE_ACTIONS_NAME(N) TINYMOCk_CAT(N, _actions)
#define TINYMOCk_INTERFACE_RETURN_NAME(N) TINYMOCk_CAT(N, _return)

#define TINYMOCk_INTERFACE_ANCHOR(I) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_TYPE(I), _interface_anchor_t)

#define TINYMOCk_INTERFACE_FIELD_LEGACY(I, K, R, N, ...) \
  tinymock_mock_t N;
#define TINYMOCk_INTERFACE_FIELD_REFLECTED(I, K, R, N, ...) \
  tinymock_mock_t N; \
  tinymock_cmeta_history TINYMOCk_INTERFACE_HISTORY_NAME(N); \
  tinymock_cmeta_actions TINYMOCk_INTERFACE_ACTIONS_NAME(N); \
  tinymock_cmeta_return TINYMOCk_INTERFACE_RETURN_NAME(N);

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
#define TINYMOCk_INTERFACE_FIELD_F0 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_F1 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_F2 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_F3 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_F4 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_FV0 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_FV1 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_FV2 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_FV3 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_FV4 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_FD0 TINYMOCk_INTERFACE_FIELD_REFLECTED
#define TINYMOCk_INTERFACE_FIELD_ROW(I, K, R, N, ...) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_FIELD_, K)(I, K, R, N, __VA_ARGS__)

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


#define TINYMOCk_INTERFACE_PARAM_ABI_I(type,name,flags,descriptor,abi) abi
#define TINYMOCk_INTERFACE_PARAM_ABI(row) TINYMOCk_INTERFACE_PARAM_ABI_I row

#define TINYMOCk_INTERFACE_BOX_CMETA_ABI_SCALAR(value) TINYMOCk_VALUE(value)
#define TINYMOCk_INTERFACE_BOX_CMETA_ABI_OBJECT_POINTER(value) TINYMOCk_VALUE(value)
#define TINYMOCk_INTERFACE_BOX_CMETA_ABI_AGGREGATE(value) tinymock_value_zero()
#define TINYMOCk_INTERFACE_BOX_CMETA_ABI_FUNCTION_POINTER(value) tinymock_value_zero()
#define TINYMOCk_INTERFACE_BOX_CMETA_ABI_ENUM(value) tinymock_value_zero()
#define TINYMOCk_INTERFACE_BOX_CMETA_ABI_UNSPECIFIED(value) tinymock_value_zero()
#define TINYMOCk_INTERFACE_BOX_CMETA_ABI_OPAQUE(value) tinymock_value_zero()
#define TINYMOCk_INTERFACE_BOX_CMETA_ABI_VOID(value) tinymock_value_zero()
#define TINYMOCk_INTERFACE_BOX_I(abi, value) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_BOX_, abi)(value)
#define TINYMOCk_INTERFACE_BOX(row) \
  TINYMOCk_INTERFACE_BOX_I(TINYMOCk_INTERFACE_PARAM_ABI(row), \
                           CMETA_IFACE_PARAM_NAME(row))

#define TINYMOCk_INTERFACE_BOX_ROW(index, row, ignored) \
  CMETA_PP_CAT(CMETA_FUNCTION_COMMA_, index) TINYMOCk_INTERFACE_BOX(row)
#define TINYMOCk_INTERFACE_TYPED_ROW(index, row, ignored) \
  CMETA_PP_CAT(CMETA_FUNCTION_COMMA_, index) \
  (const void *)&CMETA_IFACE_PARAM_NAME(row)

#define TINYMOCk_INTERFACE_PARAM_ADMIT_I(type,name,flags,descriptor,abi) \
  _Static_assert( \
      (abi) == CMETA_ABI_SCALAR || \
      (abi) == CMETA_ABI_OBJECT_POINTER || \
      (abi) == CMETA_ABI_AGGREGATE || \
      (abi) == CMETA_ABI_FUNCTION_POINTER || \
      (abi) == CMETA_ABI_ENUM, \
      "TinyMock reflected interface parameter uses an unsupported ABI carrier")
#define TINYMOCk_INTERFACE_PARAM_ADMIT(row) \
  TINYMOCk_INTERFACE_PARAM_ADMIT_I row
#define TINYMOCk_INTERFACE_PARAM_ADMIT_ROW(row, ignored) \
  TINYMOCk_INTERFACE_PARAM_ADMIT(row);

#define TINYMOCk_INTERFACE_VALUE_RETURN_ADMIT(abi) \
  _Static_assert( \
      (abi) == CMETA_ABI_SCALAR || \
      (abi) == CMETA_ABI_OBJECT_POINTER || \
      (abi) == CMETA_ABI_AGGREGATE || \
      (abi) == CMETA_ABI_FUNCTION_POINTER || \
      (abi) == CMETA_ABI_ENUM, \
      "TinyMock reflected interface return uses an unsupported ABI carrier")
#define TINYMOCk_INTERFACE_VOID_RETURN_ADMIT(abi) \
  _Static_assert((abi) == CMETA_ABI_VOID, \
      "TinyMock reflected void interface return must use CMETA_ABI_VOID")

#define TINYMOCk_INTERFACE_RECORD(I,N,argc,typed,boxed) \
  TINYMOCk_ASSERT( \
      tinymock_cmeta_history_record( \
          &mock->TINYMOCk_INTERFACE_HISTORY_NAME(N), I##_##N##_function(), \
          (argc), (typed), (boxed)), \
      "tinymock cannot snapshot reflected interface arguments for %s.%s", \
      #I, #N)

#define TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,argc,boxed) \
  TINYMOCk_ASSERT( \
      tinymock_cmeta_actions_apply( \
          &mock->TINYMOCk_INTERFACE_ACTIONS_NAME(N), I##_##N##_function(), \
          (argc), (boxed)), \
      "tinymock cannot apply reflected interface output actions for %s.%s", \
      #I, #N)

#define TINYMOCk_INTERFACE_RETURN_CMETA_ABI_SCALAR(I,N,R,result) \
  do { \
    R typed_result__; \
    tinymock_cmeta_return *return__ = \
        &mock->TINYMOCk_INTERFACE_RETURN_NAME(N); \
    if (tinymock_cmeta_return_enabled(return__)) { \
      bool ok__ = tinymock_cmeta_return_write( \
          return__, I##_##N##_function(), &typed_result__); \
      TINYMOCk_ASSERT(ok__, \
          "tinymock cannot materialize reflected interface return for %s.%s", \
          #I, #N); \
      if (ok__) return typed_result__; \
    } \
    return TINYMOCk_VALUE_AS(R, result); \
  } while (0)
#define TINYMOCk_INTERFACE_RETURN_CMETA_ABI_OBJECT_POINTER(I,N,R,result) \
  TINYMOCk_INTERFACE_RETURN_CMETA_ABI_SCALAR(I,N,R,result)
#define TINYMOCk_INTERFACE_RETURN_CMETA_ABI_AGGREGATE(I,N,R,result) \
  do { \
    R typed_result__ = {0}; \
    bool ok__ = tinymock_cmeta_return_write( \
        &mock->TINYMOCk_INTERFACE_RETURN_NAME(N), \
        I##_##N##_function(), &typed_result__); \
    (void)(result); \
    TINYMOCk_ASSERT(ok__, \
        "tinymock aggregate interface return for %s.%s requires a typed return", \
        #I, #N); \
    return typed_result__; \
  } while (0)
#define TINYMOCk_INTERFACE_RETURN_CMETA_ABI_FUNCTION_POINTER(I,N,R,result) \
  do { \
    R typed_result__ = (R)0; \
    bool ok__ = tinymock_cmeta_return_write( \
        &mock->TINYMOCk_INTERFACE_RETURN_NAME(N), \
        I##_##N##_function(), &typed_result__); \
    (void)(result); \
    TINYMOCk_ASSERT(ok__, \
        "tinymock function-pointer interface return for %s.%s requires a typed return", \
        #I, #N); \
    return typed_result__; \
  } while (0)
#define TINYMOCk_INTERFACE_RETURN_CMETA_ABI_ENUM(I,N,R,result) \
  do { \
    R typed_result__ = (R)0; \
    bool ok__ = tinymock_cmeta_return_write( \
        &mock->TINYMOCk_INTERFACE_RETURN_NAME(N), \
        I##_##N##_function(), &typed_result__); \
    (void)(result); \
    TINYMOCk_ASSERT(ok__, \
        "tinymock enum interface return for %s.%s requires a typed return", \
        #I, #N); \
    return typed_result__; \
  } while (0)
#define TINYMOCk_INTERFACE_RETURN_VALUE(abi,I,N,R,result) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_RETURN_, abi)(I,N,R,result)

#define TINYMOCk_INTERFACE_CALLBACK_F0(I,R,N,C,RD,RA) \
  TINYMOCk_INTERFACE_VALUE_RETURN_ADMIT(RA); \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    TINYMOCk_INTERFACE_RECORD(I,N,0u,NULL,NULL); \
    tinymock_value_t result__ = tinymock_mock_dispatch(&mock->N, 0u, NULL); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,0u,NULL); \
    TINYMOCk_INTERFACE_RETURN_VALUE(RA,I,N,R,result__); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_F1(I,R,N,C,RD,RA,P1) \
  TINYMOCk_INTERFACE_VALUE_RETURN_ADMIT(RA); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P1); \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t boxed__[] = { TINYMOCk_INTERFACE_BOX(P1) }; \
    const void *typed__[] = { (const void *)&CMETA_IFACE_PARAM_NAME(P1) }; \
    TINYMOCk_INTERFACE_RECORD(I,N,1u,typed__,boxed__); \
    tinymock_value_t result__ = tinymock_mock_dispatch(&mock->N, 1u, boxed__); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,1u,boxed__); \
    TINYMOCk_INTERFACE_RETURN_VALUE(RA,I,N,R,result__); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_F2(I,R,N,C,RD,RA,P1,P2) \
  TINYMOCk_INTERFACE_VALUE_RETURN_ADMIT(RA); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P1); TINYMOCk_INTERFACE_PARAM_ADMIT(P2); \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t boxed__[] = { \
      TINYMOCk_INTERFACE_BOX(P1), TINYMOCk_INTERFACE_BOX(P2) \
    }; \
    const void *typed__[] = { \
      (const void *)&CMETA_IFACE_PARAM_NAME(P1), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P2) \
    }; \
    TINYMOCk_INTERFACE_RECORD(I,N,2u,typed__,boxed__); \
    tinymock_value_t result__ = tinymock_mock_dispatch(&mock->N, 2u, boxed__); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,2u,boxed__); \
    TINYMOCk_INTERFACE_RETURN_VALUE(RA,I,N,R,result__); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_F3(I,R,N,C,RD,RA,P1,P2,P3) \
  TINYMOCk_INTERFACE_VALUE_RETURN_ADMIT(RA); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P1); TINYMOCk_INTERFACE_PARAM_ADMIT(P2); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P3); \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2), \
      CMETA_IFACE_PARAM_DECL(P3)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t boxed__[] = { \
      TINYMOCk_INTERFACE_BOX(P1), TINYMOCk_INTERFACE_BOX(P2), \
      TINYMOCk_INTERFACE_BOX(P3) \
    }; \
    const void *typed__[] = { \
      (const void *)&CMETA_IFACE_PARAM_NAME(P1), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P2), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P3) \
    }; \
    TINYMOCk_INTERFACE_RECORD(I,N,3u,typed__,boxed__); \
    tinymock_value_t result__ = tinymock_mock_dispatch(&mock->N, 3u, boxed__); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,3u,boxed__); \
    TINYMOCk_INTERFACE_RETURN_VALUE(RA,I,N,R,result__); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_F4(I,R,N,C,RD,RA,P1,P2,P3,P4) \
  TINYMOCk_INTERFACE_VALUE_RETURN_ADMIT(RA); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P1); TINYMOCk_INTERFACE_PARAM_ADMIT(P2); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P3); TINYMOCk_INTERFACE_PARAM_ADMIT(P4); \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2), \
      CMETA_IFACE_PARAM_DECL(P3), CMETA_IFACE_PARAM_DECL(P4)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t boxed__[] = { \
      TINYMOCk_INTERFACE_BOX(P1), TINYMOCk_INTERFACE_BOX(P2), \
      TINYMOCk_INTERFACE_BOX(P3), TINYMOCk_INTERFACE_BOX(P4) \
    }; \
    const void *typed__[] = { \
      (const void *)&CMETA_IFACE_PARAM_NAME(P1), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P2), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P3), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P4) \
    }; \
    TINYMOCk_INTERFACE_RECORD(I,N,4u,typed__,boxed__); \
    tinymock_value_t result__ = tinymock_mock_dispatch(&mock->N, 4u, boxed__); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,4u,boxed__); \
    TINYMOCk_INTERFACE_RETURN_VALUE(RA,I,N,R,result__); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_FV0(I,R,N,C,RD,RA) \
  TINYMOCk_INTERFACE_VOID_RETURN_ADMIT(RA); \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    TINYMOCk_INTERFACE_RECORD(I,N,0u,NULL,NULL); \
    (void)tinymock_mock_dispatch(&mock->N, 0u, NULL); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,0u,NULL); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_FV1(I,R,N,C,RD,RA,P1) \
  TINYMOCk_INTERFACE_VOID_RETURN_ADMIT(RA); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P1); \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t boxed__[] = { TINYMOCk_INTERFACE_BOX(P1) }; \
    const void *typed__[] = { (const void *)&CMETA_IFACE_PARAM_NAME(P1) }; \
    TINYMOCk_INTERFACE_RECORD(I,N,1u,typed__,boxed__); \
    (void)tinymock_mock_dispatch(&mock->N, 1u, boxed__); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,1u,boxed__); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_FV2(I,R,N,C,RD,RA,P1,P2) \
  TINYMOCk_INTERFACE_VOID_RETURN_ADMIT(RA); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P1); TINYMOCk_INTERFACE_PARAM_ADMIT(P2); \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t boxed__[] = { \
      TINYMOCk_INTERFACE_BOX(P1), TINYMOCk_INTERFACE_BOX(P2) \
    }; \
    const void *typed__[] = { \
      (const void *)&CMETA_IFACE_PARAM_NAME(P1), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P2) \
    }; \
    TINYMOCk_INTERFACE_RECORD(I,N,2u,typed__,boxed__); \
    (void)tinymock_mock_dispatch(&mock->N, 2u, boxed__); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,2u,boxed__); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_FV3(I,R,N,C,RD,RA,P1,P2,P3) \
  TINYMOCk_INTERFACE_VOID_RETURN_ADMIT(RA); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P1); TINYMOCk_INTERFACE_PARAM_ADMIT(P2); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P3); \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2), \
      CMETA_IFACE_PARAM_DECL(P3)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t boxed__[] = { \
      TINYMOCk_INTERFACE_BOX(P1), TINYMOCk_INTERFACE_BOX(P2), \
      TINYMOCk_INTERFACE_BOX(P3) \
    }; \
    const void *typed__[] = { \
      (const void *)&CMETA_IFACE_PARAM_NAME(P1), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P2), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P3) \
    }; \
    TINYMOCk_INTERFACE_RECORD(I,N,3u,typed__,boxed__); \
    (void)tinymock_mock_dispatch(&mock->N, 3u, boxed__); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,3u,boxed__); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_FV4(I,R,N,C,RD,RA,P1,P2,P3,P4) \
  TINYMOCk_INTERFACE_VOID_RETURN_ADMIT(RA); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P1); TINYMOCk_INTERFACE_PARAM_ADMIT(P2); \
  TINYMOCk_INTERFACE_PARAM_ADMIT(P3); TINYMOCk_INTERFACE_PARAM_ADMIT(P4); \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2), \
      CMETA_IFACE_PARAM_DECL(P3), CMETA_IFACE_PARAM_DECL(P4)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t boxed__[] = { \
      TINYMOCk_INTERFACE_BOX(P1), TINYMOCk_INTERFACE_BOX(P2), \
      TINYMOCk_INTERFACE_BOX(P3), TINYMOCk_INTERFACE_BOX(P4) \
    }; \
    const void *typed__[] = { \
      (const void *)&CMETA_IFACE_PARAM_NAME(P1), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P2), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P3), \
      (const void *)&CMETA_IFACE_PARAM_NAME(P4) \
    }; \
    TINYMOCk_INTERFACE_RECORD(I,N,4u,typed__,boxed__); \
    (void)tinymock_mock_dispatch(&mock->N, 4u, boxed__); \
    TINYMOCk_INTERFACE_APPLY_ACTIONS(I,N,4u,boxed__); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_FD0(I,R,N,C,RD,RA) \
  TINYMOCk_INTERFACE_CALLBACK_FV0(I,R,N,C,RD,RA)

#define TINYMOCk_INTERFACE_CALLBACK_ROW(I, K, R, N, ...) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_CALLBACK_, K)(I, R, N, __VA_ARGS__)

#define TINYMOCk_INTERFACE_VTABLE_ROW(I, K, R, N, ...) \
  .N = TINYMOCk_INTERFACE_CALLBACK(I, N),

#define TINYMOCk_INTERFACE_INIT_LEGACY(I,N) \
  tinymock_mock_init(&mock->N, #I "." #N); \
  tinymock_mock_set_default_return(&mock->N, tinymock_value_zero());

#define TINYMOCk_INTERFACE_INIT_REFLECTED(I,N) \
  TINYMOCk_ASSERT(cmeta_function_desc_valid(I##_##N##_function()), \
                  "tinymock reflected interface metadata is invalid: %s.%s", \
                  #I, #N); \
  TINYMOCk_ASSERT(cmeta_function_abi_desc_valid(I##_##N##_function_abi()), \
                  "tinymock reflected interface ABI metadata is invalid: %s.%s", \
                  #I, #N); \
  tinymock_mock_init(&mock->N, I##_##N##_function()->name); \
  tinymock_mock_set_default_return(&mock->N, tinymock_value_zero()); \
  tinymock_cmeta_history_init( \
      &mock->TINYMOCk_INTERFACE_HISTORY_NAME(N), I##_##N##_function()); \
  tinymock_cmeta_actions_init( \
      &mock->TINYMOCk_INTERFACE_ACTIONS_NAME(N), I##_##N##_function()); \
  tinymock_cmeta_return_init( \
      &mock->TINYMOCk_INTERFACE_RETURN_NAME(N), I##_##N##_function());

#define TINYMOCk_INTERFACE_INIT_R0(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_R1(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_R2(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_R3(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_R4(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_V0(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_V1(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_V2(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_V3(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_V4(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_D0(I,R,N,...) TINYMOCk_INTERFACE_INIT_LEGACY(I,N)
#define TINYMOCk_INTERFACE_INIT_F0(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_F1(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_F2(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_F3(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_F4(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_FV0(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_FV1(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_FV2(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_FV3(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_FV4(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_FD0(I,R,N,...) TINYMOCk_INTERFACE_INIT_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_INIT_ROW(I, K, R, N, ...) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_INIT_, K)(I,R,N,__VA_ARGS__)

#define TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N) \
  tinymock_cmeta_history_destroy(&mock->TINYMOCk_INTERFACE_HISTORY_NAME(N)); \
  tinymock_cmeta_actions_destroy(&mock->TINYMOCk_INTERFACE_ACTIONS_NAME(N)); \
  tinymock_cmeta_return_destroy(&mock->TINYMOCk_INTERFACE_RETURN_NAME(N));

#define TINYMOCk_INTERFACE_DESTROY_R0(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_R1(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_R2(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_R3(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_R4(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_V0(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_V1(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_V2(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_V3(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_V4(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_D0(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_LEGACY(I,N)
#define TINYMOCk_INTERFACE_DESTROY_F0(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_F1(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_F2(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_F3(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_F4(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_FV0(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_FV1(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_FV2(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_FV3(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_FV4(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
#define TINYMOCk_INTERFACE_DESTROY_FD0(I,R,N,...) TINYMOCk_INTERFACE_DESTROY_REFLECTED(I,N)
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
    *mock = (TINYMOCk_INTERFACE_TYPE(I)){0}; \
    METHODS(TINYMOCk_INTERFACE_INIT_ROW, I) \
  } \
  static inline void TINYMOCk_INTERFACE_DESTROY(I)(TINYMOCk_INTERFACE_TYPE(I) *mock) { \
    if (!mock) return; \
    METHODS(TINYMOCk_INTERFACE_DESTROY_ROW, I) \
    *mock = (TINYMOCk_INTERFACE_TYPE(I)){0}; \
  } \
  static inline I TINYMOCk_INTERFACE_AS(I)(TINYMOCk_INTERFACE_TYPE(I) *mock) { \
    return I##_bind(mock, &TINYMOCk_INTERFACE_VTABLE(I)); \
  } \
  typedef int TINYMOCk_INTERFACE_ANCHOR(I)

#define TINYMOCk_INTERFACE(I, METHODS) \
  TINYMOCk_INTERFACE_WITH_CAPS(I, METHODS, 0u)

#define TINYMOCk_INTERFACE_METHOD(mock, method) (&((mock)->method))
#define TINYMOCk_INTERFACE_METHOD_FUNCTION(I, method) I##_##method##_function()
#define TINYMOCk_INTERFACE_METHOD_ABI(I, method) I##_##method##_function_abi()
#define TINYMOCk_INTERFACE_METHOD_HISTORY(mock, method) \
  (&((mock)->TINYMOCk_INTERFACE_HISTORY_NAME(method)))
#define TINYMOCk_INTERFACE_METHOD_ACTIONS(mock, method) \
  (&((mock)->TINYMOCk_INTERFACE_ACTIONS_NAME(method)))
#define TINYMOCk_INTERFACE_METHOD_RETURN(mock, method) \
  (&((mock)->TINYMOCk_INTERFACE_RETURN_NAME(method)))

#define TINYMOCk_INTERFACE_SET_RETURN(mock, method, value_lvalue) \
  tinymock_cmeta_return_set( \
      TINYMOCk_INTERFACE_METHOD_RETURN((mock), method), \
      TINYMOCk_INTERFACE_METHOD_RETURN((mock), method)->function, \
      &(value_lvalue))
#define TINYMOCk_INTERFACE_CLEAR_RETURN(mock, method) \
  tinymock_cmeta_return_clear(TINYMOCk_INTERFACE_METHOD_RETURN((mock), method))
#define TINYMOCk_INTERFACE_ARG_EQUAL_TYPED( \
    mock, method, call_index, param_name, expected_lvalue) \
  tinymock_cmeta_history_arg_equal_typed_name( \
      TINYMOCk_INTERFACE_METHOD_HISTORY((mock), method), (call_index), \
      (param_name), &(expected_lvalue))
#define TINYMOCk_INTERFACE_CAPTURE( \
    mock, method, call_index, param_name, captor) \
  tinymock_cmeta_captor_capture_name( \
      (captor), TINYMOCk_INTERFACE_METHOD_HISTORY((mock), method), \
      (call_index), (param_name))
#define TINYMOCk_INTERFACE_SET_OUT(mock, method, param_name, value_lvalue) \
  tinymock_cmeta_actions_set_output_name( \
      TINYMOCk_INTERFACE_METHOD_ACTIONS((mock), method), \
      TINYMOCk_INTERFACE_METHOD_ACTIONS((mock), method)->function, \
      (param_name), &(value_lvalue))
#define TINYMOCk_INTERFACE_CLEAR_OUT(mock, method, param_name) \
  tinymock_cmeta_actions_clear_output_name( \
      TINYMOCk_INTERFACE_METHOD_ACTIONS((mock), method), (param_name))

#endif /* TINYMOCK_CMETA_H */
