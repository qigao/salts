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

#define TINYMOCk_INTERFACE_ANCHOR(I) \
  TINYMOCk_CAT(TINYMOCk_INTERFACE_TYPE(I), _interface_anchor_t)

#define TINYMOCk_INTERFACE_FIELD_ROW(I, K, R, N, ...) \
  tinymock_mock_t N;

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

#define TINYMOCk_INTERFACE_CALLBACK_F0(I,R,N,C,RD,RA) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 0u, NULL); \
    return TINYMOCk_VALUE_AS(R, result); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_F1(I,R,N,C,RD,RA,P1) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P1)) \
    }; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 1u, args); \
    return TINYMOCk_VALUE_AS(R, result); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_F2(I,R,N,C,RD,RA,P1,P2) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P1)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P2)) \
    }; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 2u, args); \
    return TINYMOCk_VALUE_AS(R, result); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_F3(I,R,N,C,RD,RA,P1,P2,P3) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2), \
      CMETA_IFACE_PARAM_DECL(P3)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P1)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P2)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P3)) \
    }; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 3u, args); \
    return TINYMOCk_VALUE_AS(R, result); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_F4(I,R,N,C,RD,RA,P1,P2,P3,P4) \
  static R TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2), \
      CMETA_IFACE_PARAM_DECL(P3), CMETA_IFACE_PARAM_DECL(P4)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P1)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P2)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P3)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P4)) \
    }; \
    tinymock_value_t result = tinymock_mock_dispatch(&mock->N, 4u, args); \
    return TINYMOCk_VALUE_AS(R, result); \
  }

#define TINYMOCk_INTERFACE_CALLBACK_FV0(I,R,N,C,RD,RA) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)(void *opaque) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    (void)tinymock_mock_dispatch(&mock->N, 0u, NULL); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_FV1(I,R,N,C,RD,RA,P1) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P1)) \
    }; \
    (void)tinymock_mock_dispatch(&mock->N, 1u, args); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_FV2(I,R,N,C,RD,RA,P1,P2) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P1)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P2)) \
    }; \
    (void)tinymock_mock_dispatch(&mock->N, 2u, args); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_FV3(I,R,N,C,RD,RA,P1,P2,P3) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2), \
      CMETA_IFACE_PARAM_DECL(P3)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P1)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P2)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P3)) \
    }; \
    (void)tinymock_mock_dispatch(&mock->N, 3u, args); \
  }
#define TINYMOCk_INTERFACE_CALLBACK_FV4(I,R,N,C,RD,RA,P1,P2,P3,P4) \
  static void TINYMOCk_INTERFACE_CALLBACK(I, N)( \
      void *opaque, CMETA_IFACE_PARAM_DECL(P1), CMETA_IFACE_PARAM_DECL(P2), \
      CMETA_IFACE_PARAM_DECL(P3), CMETA_IFACE_PARAM_DECL(P4)) { \
    TINYMOCk_INTERFACE_TYPE(I) *mock = (TINYMOCk_INTERFACE_TYPE(I) *)opaque; \
    tinymock_value_t args[] = { \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P1)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P2)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P3)), \
      TINYMOCk_VALUE(CMETA_IFACE_PARAM_NAME(P4)) \
    }; \
    (void)tinymock_mock_dispatch(&mock->N, 4u, args); \
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
  tinymock_mock_init(&mock->N, I##_##N##_function()->name); \
  tinymock_mock_set_default_return(&mock->N, tinymock_value_zero());

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
  static inline I TINYMOCk_INTERFACE_AS(I)(TINYMOCk_INTERFACE_TYPE(I) *mock) { \
    return I##_bind(mock, &TINYMOCk_INTERFACE_VTABLE(I)); \
  } \
  typedef int TINYMOCk_INTERFACE_ANCHOR(I)

#define TINYMOCk_INTERFACE(I, METHODS) \
  TINYMOCk_INTERFACE_WITH_CAPS(I, METHODS, 0u)

#define TINYMOCk_INTERFACE_METHOD(mock, method) (&((mock)->method))
#define TINYMOCk_INTERFACE_METHOD_FUNCTION(I, method) I##_##method##_function()
#define TINYMOCk_INTERFACE_METHOD_ABI(I, method) I##_##method##_function_abi()

#endif /* TINYMOCK_CMETA_H */
