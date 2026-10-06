#ifndef CMETA_CAPABILITIES_FIXTURE_H
#define CMETA_CAPABILITIES_FIXTURE_H

#include <cmeta/flags.h>
#include <cmeta/variant.h>

typedef struct capability_owned {
    unsigned char *data;
    size_t size;
} capability_owned;
#ifdef __cplusplus
extern "C" {
#endif
extern const cmeta_data_desc capability_owned_data;
enum { CAPABILITY_PAYLOAD_LIMIT = 32, CAPABILITY_POISON_BYTE = 165 };
extern int owned_live, owned_frees, owned_moves;
extern bool fail_copy, fail_init;
const cmeta_data_desc *capability_variant_peer(void);
const cmeta_data_desc *capability_flags_peer(void);
#ifdef __cplusplus
}
#endif

cmeta_flags(CapabilityFlags, "test.CapabilityFlags",
    cmeta_flag(Read, UINT64_C(1), "read")
    cmeta_flag(High, UINT64_C(0x8000000000000000), "high")
);
cmeta_variant(CapabilityValue, "test.CapabilityValue",
    cmeta_case(Number, 11, int, &cmeta_data_int)
    cmeta_case(Owned, 23, capability_owned, &capability_owned_data)
    cmeta_case(Flags, 41, CapabilityFlags, &CapabilityFlags__data)
);
cmeta_variant(CapabilityNested, "test.CapabilityNested",
    cmeta_case(Value, 7, CapabilityValue, &CapabilityValue__data)
);

cmeta_variant(CapabilityBoundary, "test.CapabilityBoundary",
    cmeta_case(Min, INT_MIN, int, &cmeta_data_int)
    cmeta_case(A, 1, int, &cmeta_data_int)
    cmeta_case(B, 2, int, &cmeta_data_int)
    cmeta_case(C, 3, int, &cmeta_data_int)
    cmeta_case(D, 4, int, &cmeta_data_int)
    cmeta_case(E, 5, int, &cmeta_data_int)
    cmeta_case(F, 6, int, &cmeta_data_int)
    cmeta_case(G, 7, int, &cmeta_data_int)
    cmeta_case(H, 8, int, &cmeta_data_int)
    cmeta_case(I, 9, int, &cmeta_data_int)
    cmeta_case(J, 10, int, &cmeta_data_int)
    cmeta_case(K, 11, int, &cmeta_data_int)
    cmeta_case(L, 12, int, &cmeta_data_int)
    cmeta_case(M, 13, int, &cmeta_data_int)
    cmeta_case(N, 14, int, &cmeta_data_int)
    cmeta_case(Max, INT_MAX, int, &cmeta_data_int)
);
cmeta_require_trait(CapabilityValue, Copyable);
cmeta_require_trait(CapabilityValue, Movable);
cmeta_require_trait(CapabilityValue, Destructible);
cmeta_require_field(CapabilityValue, tag, int64_t);
cmeta_require_field(CapabilityFlags, bits, uint64_t);

#endif
