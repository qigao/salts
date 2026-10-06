#ifndef CMETA_FINGERPRINT_H
#define CMETA_FINGERPRINT_H

#include <cmeta/manifest.h>
#include <cmeta/struct.h>
#include <cmeta/interface.h>

/* The projection version is separate from the FNV-1a algorithm version and
 * the Reflection ABI epoch. See STATIC_MANIFESTS.md for the exact byte stream. */
#define CMETA_CONTRACT_FINGERPRINT_VERSION UINT32_C(1)
#define CMETA_FINGERPRINT_DEPTH_LIMIT 64u
#define CMETA_FINGERPRINT_DEFAULT_DEPTH 32u
#define CMETA_FINGERPRINT_DEFAULT_NODES 4096u
#define CMETA_FINGERPRINT_DEFAULT_ROWS 1024u
#define CMETA_FINGERPRINT_DEFAULT_STRING_BYTES 65536u

typedef struct cmeta_fingerprint_limits {
    size_t max_depth;
    size_t max_nodes;
    size_t max_rows;
    size_t max_string_bytes;
} cmeta_fingerprint_limits;

#ifdef __cplusplus
extern "C" {
#endif

/** Hash canonical contract metadata on the control plane, without allocation,
 * callbacks, discovery or provider retention. All descriptors/arrays/strings
 * must be readable and live under the negotiated CMETA_REFLECTION_ABI_VERSION.
 * The caller owns the provider lease through the entire call.
 *
 * All limits are nonzero; max_depth must not exceed DEPTH_LIMIT. Nodes count
 * visited descriptors/identities per edge; rows and string bytes (including
 * validation-only display names and NUL terminators) are aggregate budgets.
 * Cycles exhaust depth/nodes. Failure leaves *out unchanged: INVALID_ARGUMENT
 * for malformed metadata, TYPE_MISMATCH for missing canonical identity,
 * dynamic struct layout, legacy interface rows or unspecified ABI carriers,
 * CAPACITY_EXCEEDED for exhausted budgets. No type names are used as identity.
 *
 * Fingerprints describe native layout/signature/trait shape, not provider/type
 * identity. Compare them with the projection version, algorithm version and a
 * separately agreed stable contract ID. Equal hashes are not proof of ABI
 * compatibility or authenticity; retain canonical compatibility validation.
 * Traversal/validation is O((nodes + string bytes) * max_depth + rows * string
 * bytes), including recursive validators and parameter-name comparisons; stack is
 * O(max_depth). See STATIC_MANIFESTS.md for the complete encoding and exclusions.
 */
cmeta_status cmeta_contract_fingerprint_type(const cmeta_type_desc *desc,
    const cmeta_fingerprint_limits *limits, uint64_t *out);
cmeta_status cmeta_contract_fingerprint_struct(const cmeta_struct_desc *desc,
    const cmeta_fingerprint_limits *limits, uint64_t *out);
cmeta_status cmeta_contract_fingerprint_enum(const cmeta_enum_domain *desc,
    const cmeta_fingerprint_limits *limits, uint64_t *out);
cmeta_status cmeta_contract_fingerprint_function(const cmeta_function_abi_desc *desc,
    const cmeta_fingerprint_limits *limits, uint64_t *out);
cmeta_status cmeta_contract_fingerprint_interface(const cmeta_interface_desc *desc,
    const cmeta_fingerprint_limits *limits, uint64_t *out);

#ifdef __cplusplus
}
#endif
#endif
