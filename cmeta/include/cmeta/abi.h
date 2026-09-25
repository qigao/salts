#ifndef CMETA_ABI_H
#define CMETA_ABI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cmeta_type_desc;
typedef struct cmeta_type_desc cmeta_type_desc;

/* Reflection layout epoch, independent of package and callable-signature
 * versions. Freeze the function/parameter/interface/type descriptor layouts
 * (including array strides and reachable identity/trait layouts) within an
 * epoch; incompatible changes require a new epoch. Native target ABI, packing
 * and enum representation must also agree. This is not a callable ABI ID.
 *
 * A provider's bootstrap entry must compare the requested epoch with THIS
 * header constant before returning any descriptor pointers. Do not use a
 * host-resolved runtime query to advertise the provider's build epoch. */
#define CMETA_REFLECTION_ABI_VERSION UINT32_C(1)

/* Returns the linked CMeta library's reflection epoch, for checking it against
 * the caller's headers. Plugin bootstrap must separately check its own epoch. */
uint32_t cmeta_reflection_abi_version(void);

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
    CMETA_ABI_VOID = 1,
    CMETA_ABI_SCALAR = 2,
    CMETA_ABI_OBJECT_POINTER = 3,
    CMETA_ABI_AGGREGATE = 4,
    CMETA_ABI_FUNCTION_POINTER = 5,
    CMETA_ABI_OPAQUE = 6,
    CMETA_ABI_ENUM = 7
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
