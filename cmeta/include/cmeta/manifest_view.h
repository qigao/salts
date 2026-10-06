#ifndef CMETA_MANIFEST_VIEW_H
#define CMETA_MANIFEST_VIEW_H

#include <cmeta/manifest.h>
#include <cmeta/plugin.h>
#include <cmeta/struct.h>
#include <cmeta/interface.h>
#include <cmeta/status.h>

/* Control-plane inspection limits, selected explicitly by the caller.
 * These bound work before calling canonical descriptor validators. */
#ifndef CMETA_MANIFEST_DEPTH_LIMIT
#define CMETA_MANIFEST_DEPTH_LIMIT 64u
#endif
#define CMETA_MANIFEST_DEFAULT_ITEMS 256u
#define CMETA_MANIFEST_DEFAULT_DEPTH 32u
#define CMETA_MANIFEST_DEFAULT_NODES 4096u

typedef struct cmeta_manifest_limits {
    size_t max_items;
    size_t max_identity_depth;
    size_t max_identity_nodes;
} cmeta_manifest_limits;

#ifdef __cplusplus
extern "C" {
#endif

#define cmeta_manifest_type_entry(name_, descriptor_) \
    cmeta_manifest_entry(name_, CMETA_MANIFEST_TYPE, \
        CMETA_MANIFEST_TYPED_POINTER_(cmeta_type_desc, descriptor_), UINT64_C(0), UINT32_C(0))
#define cmeta_manifest_struct_entry(name_, descriptor_) \
    cmeta_manifest_entry(name_, CMETA_MANIFEST_STRUCT, \
        CMETA_MANIFEST_TYPED_POINTER_(cmeta_struct_desc, descriptor_), UINT64_C(0), UINT32_C(0))
#define cmeta_manifest_function_entry(name_, descriptor_) \
    cmeta_manifest_entry(name_, CMETA_MANIFEST_FUNCTION, \
        CMETA_MANIFEST_TYPED_POINTER_(cmeta_function_abi_desc, descriptor_), UINT64_C(0), UINT32_C(0))
#define cmeta_manifest_interface_entry(name_, descriptor_) \
    cmeta_manifest_entry(name_, CMETA_MANIFEST_INTERFACE, \
        CMETA_MANIFEST_TYPED_POINTER_(cmeta_interface_desc, descriptor_), UINT64_C(0), UINT32_C(0))
#define cmeta_manifest_trace_entry(name_, descriptor_) \
    cmeta_manifest_entry(name_, CMETA_MANIFEST_TRACEPOINT, \
        CMETA_MANIFEST_TYPED_POINTER_(cmeta_struct_desc, descriptor_), UINT64_C(0), UINT32_C(0))
#define cmeta_manifest_capability_entry(name_, descriptor_) \
    cmeta_manifest_entry(name_, CMETA_MANIFEST_CAPABILITY, \
        CMETA_MANIFEST_TYPED_POINTER_(cmeta_interface_desc, descriptor_), UINT64_C(0), UINT32_C(0))
#define cmeta_manifest_enum_entry(name_, descriptor_) \
    cmeta_manifest_entry(name_, CMETA_MANIFEST_ENUM_DOMAIN, \
        CMETA_MANIFEST_TYPED_POINTER_(cmeta_enum_domain, descriptor_), UINT64_C(0), UINT32_C(0))
#define cmeta_manifest_plugin_entry(name_, descriptor_) \
    cmeta_manifest_entry(name_, CMETA_MANIFEST_PLUGIN, \
        CMETA_MANIFEST_TYPED_POINTER_(cmeta_plugin_desc, descriptor_), UINT64_C(0), UINT32_C(0))

/** Inspect one immutable manifest entry, with no allocation, registration,
 * object callbacks or provider retention. Inputs must be live, ABI-compatible
 * C descriptors with readable arrays and NUL-terminated strings. kind is a
 * provider contract, not a runtime proof of arbitrary void* storage. Negotiate
 * CMETA_REFLECTION_ABI_VERSION before using foreign metadata; Plugin owns leases.
 * Every getter checks manifest format/index, exact kind and canonical descriptor
 * validity. Limits bound row count and identity traversal; cycles exhaust the
 * depth/node budget. Failure leaves *out unchanged. INVALID_ARGUMENT covers
 * malformed input/metadata, TYPE_MISMATCH covers version/kind, CAPACITY_EXCEEDED
 * covers budgets. Returned const pointers borrow the original provider lifetime.
 * Function/interface validation is O(n^2) for parameter names; other traversal
 * is linear in bounded rows/identity nodes, with O(max_identity_depth) stack.
 */
cmeta_status cmeta_manifest_get_type(const cmeta_manifest *manifest, size_t index,
    const cmeta_manifest_limits *limits, const cmeta_type_desc **out);
cmeta_status cmeta_manifest_get_struct(const cmeta_manifest *manifest, size_t index,
    const cmeta_manifest_limits *limits, const cmeta_struct_desc **out);
cmeta_status cmeta_manifest_get_function(const cmeta_manifest *manifest, size_t index,
    const cmeta_manifest_limits *limits, const cmeta_function_abi_desc **out);
cmeta_status cmeta_manifest_get_interface(const cmeta_manifest *manifest, size_t index,
    const cmeta_manifest_limits *limits, const cmeta_interface_desc **out);
cmeta_status cmeta_manifest_get_trace(const cmeta_manifest *manifest, size_t index,
    const cmeta_manifest_limits *limits, const cmeta_struct_desc **out);
cmeta_status cmeta_manifest_get_capability(const cmeta_manifest *manifest, size_t index,
    const cmeta_manifest_limits *limits, const cmeta_interface_desc **out);
cmeta_status cmeta_manifest_get_enum(const cmeta_manifest *manifest, size_t index,
    const cmeta_manifest_limits *limits, const cmeta_enum_domain **out);
cmeta_status cmeta_manifest_get_plugin(const cmeta_manifest *manifest, size_t index,
    const cmeta_manifest_limits *limits, const cmeta_plugin_desc **out);
/** Validate the declaration and return one exact-role canonical interface.
 * Same budgets/errors/lifetime as manifest getters; no dependency resolution,
 * handle acquisition or retention. Failure leaves *out unchanged. */
cmeta_status cmeta_plugin_get_capability(const cmeta_plugin_desc *desc, size_t index,
    cmeta_plugin_role role, const cmeta_manifest_limits *limits,
    const cmeta_interface_desc **out);

#ifdef __cplusplus
}
#endif
#endif
