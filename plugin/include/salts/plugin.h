#ifndef SALTS_PLUGIN_H
#define SALTS_PLUGIN_H

#include <cmeta/cmeta.h>
#include <cmeta/function.h>
#include <cmeta/interface.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Salts::Plugin supports exactly one current ABI.
 *
 * The version is an exact admission epoch, not a compatibility range. Hosts do
 * not negotiate or retry older layouts. A plugin built for another epoch must
 * be rebuilt.
 */
#define SALTS_PLUGIN_ABI_VERSION 2u
#define SALTS_PLUGIN_QUERY_SYMBOL "salts_plugin_query"

#define SALTS_PLUGIN_MAX_EXPORTS 256u
#define SALTS_PLUGIN_ID_MAX 255u
#define SALTS_PLUGIN_EXPORT_ID_MAX 255u
#define SALTS_PLUGIN_CONTRACT_ID_MAX 255u
#define SALTS_PLUGIN_PATH_MAX 4095u
#define SALTS_PLUGIN_MAX_LEASES_PER_PLUGIN 64u

#if defined(__cplusplus)
#  define SALTS_PLUGIN_EXTERN_C extern "C"
#else
#  define SALTS_PLUGIN_EXTERN_C
#endif

#if defined(_WIN32)
#  define SALTS_PLUGIN_ENTRY __declspec(dllexport)
#  define SALTS_PLUGIN_CALL __cdecl
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define SALTS_PLUGIN_ENTRY __attribute__((visibility("default")))
#  define SALTS_PLUGIN_CALL
#else
#  define SALTS_PLUGIN_ENTRY
#  define SALTS_PLUGIN_CALL
#endif

#define SALTS_PLUGIN_QUERY_EXPORT SALTS_PLUGIN_EXTERN_C SALTS_PLUGIN_ENTRY

typedef enum salts_plugin_status {
    SALTS_PLUGIN_OK = 0,
    SALTS_PLUGIN_INVALID_ARGUMENT,
    SALTS_PLUGIN_INVALID_MANIFEST,
    SALTS_PLUGIN_UNSUPPORTED_ABI,
    SALTS_PLUGIN_DUPLICATE_PLUGIN_ID,
    SALTS_PLUGIN_DUPLICATE_EXPORT,
    SALTS_PLUGIN_UNKNOWN_EXPORT,
    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT,
    SALTS_PLUGIN_CAPACITY_EXCEEDED,
    SALTS_PLUGIN_ALLOCATION_FAILED,
    SALTS_PLUGIN_LOAD_FAILED,
    SALTS_PLUGIN_QUERY_MISSING,
    SALTS_PLUGIN_QUERY_REJECTED,
    SALTS_PLUGIN_UNKNOWN_PLUGIN,
    SALTS_PLUGIN_STALE,
    SALTS_PLUGIN_UNLOAD_FAILED,
    SALTS_PLUGIN_ALREADY,
    SALTS_PLUGIN_BUSY,
    SALTS_PLUGIN_INVALID_STATE
} salts_plugin_status;

typedef enum salts_plugin_export_kind {
    SALTS_PLUGIN_EXPORT_INTERFACE = 1,
    SALTS_PLUGIN_EXPORT_FUNCTION = 2
} salts_plugin_export_kind;

typedef struct salts_plugin_version {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} salts_plugin_version;

typedef salts_plugin_status (SALTS_PLUGIN_CALL *salts_plugin_start_fn)(void *self);
typedef salts_plugin_status (SALTS_PLUGIN_CALL *salts_plugin_request_stop_fn)(void *self);
typedef bool (SALTS_PLUGIN_CALL *salts_plugin_is_quiescent_fn)(const void *self);
typedef void (SALTS_PLUGIN_CALL *salts_plugin_destroy_fn)(void *self);

/*
 * Exact generated execution bridge for a reflected native function.
 *
 * params[i] points at the exact C parameter object expected by the generated
 * adapter. return_storage points at an exact return object and may be NULL for
 * void returns. The adapter is generated from the same declaration/codegen
 * source as FunctionMeta/FunctionAbi; it must not reconstruct arbitrary C ABI
 * calls from runtime metadata.
 */
typedef bool (SALTS_PLUGIN_CALL *salts_plugin_function_invoke_fn)(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count);

typedef struct salts_plugin_interface_export {
    const cmeta_interface_desc *desc;
    void *value;
} salts_plugin_interface_export;

typedef struct salts_plugin_function_export {
    const cmeta_function_desc *desc;
    const cmeta_function_abi_desc *abi;
    void *context;
    salts_plugin_function_invoke_fn invoke;
} salts_plugin_function_export;

typedef union salts_plugin_export_value {
    salts_plugin_interface_export interface;
    salts_plugin_function_export function;
} salts_plugin_export_value;

/*
 * One immutable semantic export row.
 *
 * export_id is unique within one manifest. contract_id + contract_version are
 * authoritative domain contract identity. The row is one of exactly two
 * current representations:
 *
 * INTERFACE:
 *   value.interface = { cmeta_interface_desc, mutable {self,vtable} handle }.
 *
 * FUNCTION:
 *   value.function = { FunctionMeta, FunctionAbi, context, exact invoke }.
 *
 * The tagged union makes the two representations mutually exclusive by
 * construction. All pointed-to objects/code are borrowed from the loaded
 * plugin DSO and may only be used while the host holds a live plugin lease.
 */
typedef struct salts_plugin_export {
    uint32_t struct_size;
    salts_plugin_export_kind kind;
    uint32_t contract_version;
    uint64_t capabilities;
    const char *export_id;
    const char *contract_id;
    salts_plugin_export_value value;
} salts_plugin_export;

/*
 * Immutable plugin manifest returned by SALTS_PLUGIN_QUERY_SYMBOL.
 *
 * Salts::Plugin accepts only the exact current layout and ABI epoch. There is
 * no readable-prefix compatibility, tail compatibility, ABI negotiation or
 * fallback.
 */
typedef struct salts_plugin_manifest {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *plugin_id;
    salts_plugin_version version;
    const salts_plugin_export *exports;
    size_t export_count;

    /* Passive plugins set self and all callbacks to NULL. Managed plugins
     * provide self plus all four callbacks. */
    void *self;
    salts_plugin_start_fn start;
    salts_plugin_request_stop_fn request_stop;
    salts_plugin_is_quiescent_fn is_quiescent;
    salts_plugin_destroy_fn destroy;
} salts_plugin_manifest;

#define SALTS_PLUGIN_EXPORT_SIZE ((uint32_t)sizeof(salts_plugin_export))
#define SALTS_PLUGIN_MANIFEST_SIZE ((uint32_t)sizeof(salts_plugin_manifest))

typedef const salts_plugin_manifest *(SALTS_PLUGIN_CALL *salts_plugin_query_fn)(
    uint32_t host_abi);

/* Stable public registry reference. slot is 1-based; zero fields are invalid. */
typedef struct salts_plugin_ref {
    uint32_t slot;
    uint32_t generation;
} salts_plugin_ref;

typedef enum salts_plugin_lifecycle_state {
    SALTS_PLUGIN_LIFECYCLE_LOADED = 1,
    SALTS_PLUGIN_LIFECYCLE_STARTING,
    SALTS_PLUGIN_LIFECYCLE_STARTED,
    SALTS_PLUGIN_LIFECYCLE_STOPPING,
    SALTS_PLUGIN_LIFECYCLE_QUIESCENT
} salts_plugin_lifecycle_state;

typedef struct salts_plugin_lease {
    salts_plugin_ref plugin;
    uint32_t slot;
    uint32_t generation;
} salts_plugin_lease;

typedef struct salts_plugin_lifecycle_info {
    salts_plugin_lifecycle_state state;
    size_t active_leases;
    size_t callbacks_inflight;
    salts_plugin_status failure;
} salts_plugin_lifecycle_info;

typedef struct salts_plugin_registry_config {
    size_t capacity;
} salts_plugin_registry_config;

typedef struct salts_plugin_registry {
    void *impl;
} salts_plugin_registry;

static inline bool salts_plugin_ref_valid(salts_plugin_ref ref) {
    return ref.slot != 0u && ref.generation != 0u;
}

static inline bool salts_plugin_lease_valid(salts_plugin_lease lease) {
    return salts_plugin_ref_valid(lease.plugin) &&
           lease.slot != 0u && lease.generation != 0u;
}

const char *salts_plugin_status_string(salts_plugin_status status);

bool salts_plugin_export_has_capabilities(const salts_plugin_export *entry,
                                          uint64_t required);

salts_plugin_status salts_plugin_export_require_interface(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_interface_desc *expected_interface);

salts_plugin_status salts_plugin_export_require_function(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities);

salts_plugin_status salts_plugin_manifest_validate(
    const salts_plugin_manifest *manifest);

salts_plugin_status salts_plugin_manifest_find_export(
    const salts_plugin_manifest *manifest,
    const char *export_id,
    const salts_plugin_export **out_export);

/*
 * Bounded dynamic-plugin registry and lifecycle.
 *
 * load() admits only SALTS_PLUGIN_ABI_VERSION with exact current manifest/export
 * layouts. It never retries an older ABI.
 *
 * Plugin-owned manifest/export/FunctionMeta/FunctionAbi/type-trait/adapter/
 * interface pointers may be used only while holding a live lease acquired from
 * a STARTED plugin. request_stop() closes new lease admission before invoking
 * the plugin stop callback. unload() requires no active leases or lifecycle
 * callbacks and a LOADED-never-started or QUIESCENT state.
 *
 * A lease is a DSO/resource borrow, not necessarily one lease per function
 * call. When a reflected function uses plugin-owned native traits/descriptors,
 * the borrow must cover binding, invocation, output publication and cleanup.
 */
salts_plugin_status salts_plugin_registry_init(
    salts_plugin_registry *registry,
    const salts_plugin_registry_config *config);

salts_plugin_status salts_plugin_registry_load(
    salts_plugin_registry *registry,
    const char *path,
    salts_plugin_ref *out_ref);

salts_plugin_status salts_plugin_registry_find(
    const salts_plugin_registry *registry,
    const char *plugin_id,
    salts_plugin_ref *out_ref);

salts_plugin_status salts_plugin_registry_start(
    salts_plugin_registry *registry,
    salts_plugin_ref ref);

salts_plugin_status salts_plugin_registry_acquire(
    salts_plugin_registry *registry,
    salts_plugin_ref ref,
    salts_plugin_lease *out_lease,
    const salts_plugin_manifest **out_manifest);

salts_plugin_status salts_plugin_registry_release(
    salts_plugin_registry *registry,
    salts_plugin_lease *lease);

salts_plugin_status salts_plugin_registry_request_stop(
    salts_plugin_registry *registry,
    salts_plugin_ref ref);

salts_plugin_status salts_plugin_registry_poll_quiescent(
    salts_plugin_registry *registry,
    salts_plugin_ref ref,
    bool *out_quiescent);

salts_plugin_status salts_plugin_registry_get_lifecycle(
    const salts_plugin_registry *registry,
    salts_plugin_ref ref,
    salts_plugin_lifecycle_info *out_info);

salts_plugin_status salts_plugin_registry_unload(
    salts_plugin_registry *registry,
    salts_plugin_ref ref);

size_t salts_plugin_registry_count(const salts_plugin_registry *registry);

salts_plugin_status salts_plugin_registry_destroy(
    salts_plugin_registry *registry);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLUGIN_H */
