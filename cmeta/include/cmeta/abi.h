#ifndef CMETA_ABI_H
#define CMETA_ABI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cmeta_type_desc;
typedef struct cmeta_type_desc cmeta_type_desc;

/*
 * ABI carrier describes how a declaration crosses a C call boundary.
 *
 * It is intentionally distinct from cmeta_type_kind:
 * - type kind answers what a semantic type is;
 * - ABI carrier answers how this declaration passes/returns it.
 *
 * UNSPECIFIED preserves compatibility for existing explicit descriptors.
 * Consumers that require exact lowering (TinyMock/FFI/plugin bridges) may
 * reject UNSPECIFIED and request an explicit carrier.
 */
typedef enum cmeta_abi_carrier {
    CMETA_ABI_UNSPECIFIED = 0,
    CMETA_ABI_VOID,
    CMETA_ABI_SCALAR,
    CMETA_ABI_OBJECT_POINTER,
    CMETA_ABI_AGGREGATE,
    CMETA_ABI_FUNCTION_POINTER,
    CMETA_ABI_OPAQUE
} cmeta_abi_carrier;

bool cmeta_abi_carrier_valid(cmeta_abi_carrier carrier);
const char *cmeta_abi_carrier_name(cmeta_abi_carrier carrier);

/* Conservative semantic compatibility check.  FUNCTION_POINTER remains an
 * explicit declaration contract until CMeta grows a dedicated function type
 * kind; it is therefore accepted for non-void descriptors without guessing
 * from display names. */
bool cmeta_abi_carrier_matches_type(cmeta_abi_carrier carrier,
                                    const cmeta_type_desc *type);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_ABI_H */
